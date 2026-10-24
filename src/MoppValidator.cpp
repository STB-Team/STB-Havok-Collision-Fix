#include "MoppValidator.h"

// Opcode table = the switch of the SE 1.5.97 cast VM (REL::ID 66294); bN is the N-th byte after the opcode.
//
//   00          RETURN                                   len 1, no successors
//   01..04      rescale                                  len 4
//   05          jump  +b1                                len 2, -> pc+2+b1
//   06          jump  +b1b2                              len 3, -> pc+3+u16
//   07          jump  +b1b2b3                            len 4, -> pc+4+u24
//   09 / 0A     add to the terminal offset (8 / 16 bit)  len 2 / 3
//   0B          set the terminal offset (32 bit)         len 5
//   0C          chunk jump, absolute (b1b2 << 9)         reads 3 bytes, -> code + offset
//   0D          (data, skipped)                          len 5
//   10..1C      split, 8-bit offset                      len 4, -> pc+4 and pc+4+b3
//   20..22      single split, 8-bit offset               len 3, -> pc+3 and pc+3+b2
//   23..25      split, two 16-bit offsets                len 7, -> pc+7+b3b4 and pc+7+b5b6
//   26..28      bound check                              len 3
//   29..2B      bound check, 24-bit                      len 7
//   30..4F      terminal (key in the opcode)             len 1, end of the branch
//   50..53      terminal, 8/16/24/32-bit key             len 2..5, end of the branch
//   60..63      property, 8 bit                          len 2
//   64..67      property, 16 bit                         len 3
//   68..6B      property, 32 bit                         len 5
//   70          chunk jump, absolute (b1b2b3b4)          reads 5 bytes, -> code + offset
//   anything else                                        __debugbreak()

namespace Mopp
{
	namespace
	{
		constexpr std::uint32_t kRoot = 0xFFFFFFFF;  // "the edge into the root"
		constexpr std::uint32_t kMaxPasses = 8;

		struct Insn
		{
			std::uint32_t len = 1;
			std::uint32_t count = 0;  // successors
			std::uint64_t next[2]{};  // 64-bit: an offset may point far past the buffer
			bool          invalid = false;
		};

		Insn Decode(const std::uint8_t* a_code, std::uint32_t a_size, std::uint32_t a_pc)
		{
			auto b = [&](std::uint32_t a_i) -> std::uint64_t {
				const std::uint64_t at = std::uint64_t(a_pc) + a_i;
				return at < a_size ? a_code[at] : 0;
			};
			const std::uint64_t pc = a_pc;
			Insn i;
			auto one = [&](std::uint32_t a_len) {
				i.len = a_len;
				i.count = 1;
				i.next[0] = pc + a_len;
			};
			auto two = [&](std::uint32_t a_len, std::uint64_t a_first, std::uint64_t a_second) {
				i.len = a_len;
				i.count = 2;
				i.next[0] = pc + a_len + a_first;
				i.next[1] = pc + a_len + a_second;
			};

			const std::uint8_t op = a_code[a_pc];
			switch (op) {
			case 0x00:
				break;
			case 0x01:
			case 0x02:
			case 0x03:
			case 0x04:
				one(4);
				break;
			case 0x05:
				i.len = 2;
				i.count = 1;
				i.next[0] = pc + 2 + b(1);
				break;
			case 0x06:
				i.len = 3;
				i.count = 1;
				i.next[0] = pc + 3 + (b(1) << 8 | b(2));
				break;
			case 0x07:
				i.len = 4;
				i.count = 1;
				i.next[0] = pc + 4 + (b(1) << 16 | b(2) << 8 | b(3));
				break;
			case 0x09:
				one(2);
				break;
			case 0x0A:
				one(3);
				break;
			case 0x0B:
			case 0x0D:
				one(5);
				break;
			case 0x0C:
				i.len = 3;
				i.count = 1;
				i.next[0] = (b(1) << 8 | b(2)) << 9;
				break;
			case 0x70:
				i.len = 5;
				i.count = 1;
				i.next[0] = b(1) << 24 | b(2) << 16 | b(3) << 8 | b(4);
				break;
			default:
				if (op >= 0x10 && op <= 0x1C) {
					two(4, 0, b(3));
				} else if (op >= 0x20 && op <= 0x22) {
					two(3, 0, b(2));
				} else if (op >= 0x23 && op <= 0x25) {
					two(7, b(3) << 8 | b(4), b(5) << 8 | b(6));
				} else if (op >= 0x26 && op <= 0x28) {
					one(3);
				} else if (op >= 0x29 && op <= 0x2B) {
					one(7);
				} else if (op >= 0x30 && op <= 0x4F) {
					i.len = 1;
				} else if (op >= 0x50 && op <= 0x53) {
					i.len = 2u + (op - 0x50u);
				} else if (op >= 0x60 && op <= 0x63) {
					one(2);
				} else if (op >= 0x64 && op <= 0x67) {
					one(3);
				} else if (op >= 0x68 && op <= 0x6B) {
					one(5);
				} else {
					i.invalid = true;
				}
				break;
			}
			return i;
		}

		enum class Kind : std::uint8_t
		{
			kInvalid,    // dst is an unknown opcode
			kTruncated,  // the instruction at dst runs past the end of the buffer
			kOutOfBounds,  // src jumps to dst >= size
			kCycle       // src jumps back onto the current path
		};

		struct Problem
		{
			Kind          kind;
			std::uint32_t src;  // instruction that leads there (kRoot for the root)
			std::uint32_t dst;
		};

		struct Walk
		{
			std::vector<Problem> problems;
			std::vector<std::uint8_t> operand;  // 1 = the byte is an operand of a valid instruction
		};

		// Depth-first walk over every reachable instruction. Colors: 0 = unseen, 1 = on the current path, 2 = done.
		Walk Run(const std::uint8_t* a_code, std::uint32_t a_size)
		{
			Walk w;
			w.operand.assign(a_size, 0);
			std::vector<std::uint8_t> color(a_size, 0);

			struct Frame
			{
				std::uint32_t pc;
				Insn          insn;
				std::uint32_t done;
			};
			std::vector<Frame> stack;

			auto enter = [&](std::uint32_t a_src, std::uint32_t a_pc) {
				const Insn insn = Decode(a_code, a_size, a_pc);
				if (insn.invalid) {
					w.problems.push_back({ Kind::kInvalid, a_src, a_pc });
					color[a_pc] = 2;
					return;
				}
				if (std::uint64_t(a_pc) + insn.len > a_size) {
					w.problems.push_back({ Kind::kTruncated, a_src, a_pc });
					color[a_pc] = 2;
					return;
				}
				for (std::uint32_t k = 1; k < insn.len; ++k) {
					w.operand[a_pc + k] = 1;
				}
				color[a_pc] = 1;
				stack.push_back({ a_pc, insn, 0 });
			};

			enter(kRoot, 0);
			while (!stack.empty()) {
				Frame& f = stack.back();
				if (f.done == f.insn.count) {
					color[f.pc] = 2;
					stack.pop_back();
					continue;
				}
				const std::uint64_t next = f.insn.next[f.done++];
				if (next >= a_size) {
					w.problems.push_back({ Kind::kOutOfBounds, f.pc, static_cast<std::uint32_t>(next > 0xFFFFFFFE ? 0xFFFFFFFE : next) });
					continue;
				}
				const auto to = static_cast<std::uint32_t>(next);
				if (color[to] == 1) {
					w.problems.push_back({ Kind::kCycle, f.pc, to });
					continue;
				}
				if (color[to] == 0) {
					enter(f.pc, to);  // may reallocate the stack: f is not used after this
				}
			}
			return w;
		}

		void Count(Report& a_report, const Walk& a_walk)
		{
			for (const auto& p : a_walk.problems) {
				switch (p.kind) {
				case Kind::kInvalid:
					++a_report.invalidOpcodes;
					break;
				case Kind::kTruncated:
				case Kind::kOutOfBounds:
					++a_report.outOfBounds;
					break;
				case Kind::kCycle:
					++a_report.cycles;
					break;
				}
			}
		}
	}

	Report Check(const std::uint8_t* a_code, std::uint32_t a_size)
	{
		Report report;
		if (a_code && a_size) {
			Count(report, Run(a_code, a_size));
		}
		return report;
	}

	Report Sanitize(std::uint8_t* a_code, std::uint32_t a_size)
	{
		Report report;
		if (!a_code || !a_size) {
			return report;
		}

		for (std::uint32_t pass = 0; pass < kMaxPasses; ++pass) {
			const Walk w = Run(a_code, a_size);
			if (w.problems.empty()) {
				return report;
			}
			if (pass == 0) {
				Count(report, w);
			}

			bool clearRoot = false;
			auto toReturn = [&](std::uint32_t a_pc) {
				// Rewriting a byte that another reachable instruction reads as an operand would change that
				// instruction (and open paths nobody checked). Such overlaps only exist in garbage: give up locally.
				if (a_pc == kRoot || w.operand[a_pc]) {
					clearRoot = true;
				} else if (a_code[a_pc] != 0x00) {
					a_code[a_pc] = 0x00;
					++report.patched;
				}
			};
			for (const auto& p : w.problems) {
				switch (p.kind) {
				case Kind::kInvalid:
				case Kind::kTruncated:
					// The bad instruction itself becomes RETURN when nothing else reads its byte, else its parent.
					toReturn(w.operand[p.dst] ? p.src : p.dst);
					break;
				case Kind::kOutOfBounds:
				case Kind::kCycle:
					toReturn(p.src);
					break;
				}
			}
			if (clearRoot) {
				break;
			}
		}

		if (!Check(a_code, a_size).Broken()) {
			return report;
		}
		a_code[0] = 0x00;
		++report.patched;
		report.rootCleared = true;
		return report;
	}
}
