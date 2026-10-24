// moppcheck -- tests for the MOPP validator, no game needed.
//
//   moppcheck              built-in synthetic cases (exit code 0 = all passed)
//   moppcheck <dump.bin>   checks real MOPP buffers dumped by tools/extract_mopp.py and prints the broken ones;
//                          also re-checks that Sanitize() leaves every buffer clean
//
// Dump format: repeated records { u32 codeSize; u32 nameSize; char name[nameSize]; u8 code[codeSize] }.

#include "MoppValidator.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	int failures = 0;

	void Expect(bool a_ok, const char* a_what)
	{
		std::printf("%s  %s\n", a_ok ? "ok  " : "FAIL", a_what);
		if (!a_ok) {
			++failures;
		}
	}

	struct Case
	{
		const char*               name;
		std::vector<std::uint8_t> code;
		std::uint32_t             invalid, oob, cycles;
		std::vector<std::uint8_t> repaired;  // expected bytes after Sanitize()
		bool                      rootCleared;
	};

	void RunCase(const Case& a_case)
	{
		auto code = a_case.code;
		const auto before = Mopp::Check(code.data(), static_cast<std::uint32_t>(code.size()));
		const auto fixed = Mopp::Sanitize(code.data(), static_cast<std::uint32_t>(code.size()));
		const auto after = Mopp::Check(code.data(), static_cast<std::uint32_t>(code.size()));
		const bool ok = before.invalidOpcodes == a_case.invalid && before.outOfBounds == a_case.oob &&
		                before.cycles == a_case.cycles && fixed.rootCleared == a_case.rootCleared &&
		                code == a_case.repaired && !after.Broken();
		Expect(ok, a_case.name);
	}

	int SelfTest()
	{
		const std::vector<Case> cases{
			// split 0x10: children pc+4 and pc+4+b3; 0x30/0x31 are terminals
			{ "valid split, two terminals",
				{ 0x10, 0x00, 0xFF, 0x01, 0x30, 0x31 }, 0, 0, 0,
				{ 0x10, 0x00, 0xFF, 0x01, 0x30, 0x31 }, false },
			{ "unknown opcode in a branch -> that byte becomes RETURN",
				{ 0x10, 0x00, 0xFF, 0x01, 0x30, 0x08 }, 1, 0, 0,
				{ 0x10, 0x00, 0xFF, 0x01, 0x30, 0x00 }, false },
			{ "jump past the end -> the jump becomes RETURN",
				{ 0x09, 0x01, 0x05, 0x10, 0x30 }, 0, 1, 0,
				{ 0x09, 0x01, 0x00, 0x10, 0x30 }, false },
			{ "terminal cut by the end of the buffer -> RETURN",
				{ 0x10, 0x00, 0xFF, 0x01, 0x30, 0x53, 0x01 }, 0, 1, 0,
				{ 0x10, 0x00, 0xFF, 0x01, 0x30, 0x00, 0x01 }, false },
			{ "chunk jump back to the root (endless loop) -> RETURN",
				{ 0x09, 0x01, 0x0C, 0x00, 0x00 }, 0, 0, 1,
				{ 0x09, 0x01, 0x00, 0x00, 0x00 }, false },
			{ "absolute jump onto a finished branch is fine (shared subtree)",
				{ 0x10, 0x00, 0xFF, 0x01, 0x30, 0x70, 0x00, 0x00, 0x00, 0x04 }, 0, 0, 0,
				{ 0x10, 0x00, 0xFF, 0x01, 0x30, 0x70, 0x00, 0x00, 0x00, 0x04 }, false },
			{ "16-bit split with both children in range",
				{ 0x23, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x30, 0x31 }, 0, 0, 0,
				{ 0x23, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x30, 0x31 }, false },
			{ "16-bit split, second child far away -> split becomes RETURN",
				{ 0x23, 0x00, 0xFF, 0x00, 0x00, 0x7F, 0x00, 0x30 }, 0, 1, 0,
				{ 0x00, 0x00, 0xFF, 0x00, 0x00, 0x7F, 0x00, 0x30 }, false },
			// 0x10 -> children 4 and 6; at 4 a 16-bit jump whose operand byte (6) is the second child, opcode 0x08
			{ "bad child inside another instruction's operand -> its parent becomes RETURN",
				{ 0x10, 0x00, 0xFF, 0x02, 0x06, 0x00, 0x08, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30 }, 1, 0, 0,
				{ 0x00, 0x00, 0xFF, 0x02, 0x06, 0x00, 0x08, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30 }, false },
			{ "property, rescale, bound check and terminal offsets are walked through",
				{ 0x60, 0x01, 0x01, 0x00, 0x00, 0x00, 0x26, 0x00, 0xFF, 0x09, 0x02, 0x50, 0x07 }, 0, 0, 0,
				{ 0x60, 0x01, 0x01, 0x00, 0x00, 0x00, 0x26, 0x00, 0xFF, 0x09, 0x02, 0x50, 0x07 }, false },
			{ "unknown root opcode -> the root becomes RETURN",
				{ 0xFF, 0x30 }, 1, 0, 0,
				{ 0x00, 0x30 }, false },
		};
		for (const auto& c : cases) {
			RunCase(c);
		}
		std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
		return failures ? 1 : 0;
	}

	int CheckDump(const char* a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			std::printf("cannot open %s\n", a_path);
			return 2;
		}
		std::size_t total = 0, broken = 0, stillBroken = 0, cleared = 0;
		std::uint64_t bytes = 0;
		for (;;) {
			std::uint32_t size = 0, nameSize = 0;
			if (!in.read(reinterpret_cast<char*>(&size), 4) || !in.read(reinterpret_cast<char*>(&nameSize), 4)) {
				break;
			}
			std::string name(nameSize, '\0');
			std::vector<std::uint8_t> code(size);
			in.read(name.data(), nameSize);
			in.read(reinterpret_cast<char*>(code.data()), size);
			if (!in) {
				std::printf("truncated dump\n");
				return 2;
			}
			++total;
			bytes += size;
			const auto r = Mopp::Sanitize(code.data(), size);
			if (r.Broken()) {
				++broken;
				cleared += r.rootCleared;
				std::printf("BROKEN  invalid=%u oob=%u cycles=%u patched=%u%s size=%u  %s\n", r.invalidOpcodes,
					r.outOfBounds, r.cycles, r.patched, r.rootCleared ? " ROOT-CLEARED" : "", size, name.c_str());
			}
			if (Mopp::Check(code.data(), size).Broken()) {
				++stillBroken;
				std::printf("STILL BROKEN after Sanitize  %s\n", name.c_str());
			}
		}
		std::printf("buffers=%zu bytes=%llu broken=%zu root-cleared=%zu still-broken=%zu\n", total,
			static_cast<unsigned long long>(bytes), broken, cleared, stillBroken);
		return stillBroken ? 1 : 0;
	}
}

int main(int argc, char** argv)
{
	return argc > 1 ? CheckDump(argv[1]) : SelfTest();
}
