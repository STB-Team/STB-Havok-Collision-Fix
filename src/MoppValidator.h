#pragma once

// MOPP bytecode validator. No game headers: the same file builds into the plugin and into tools/moppcheck.
//
// A MOPP (hkpMoppCode) is the bytecode of a collision BV-tree. The engine walks it with a small virtual machine
// (SE 1.5.97: REL::ID 66294 for casts, the ray VMs share the format). The VM has no checks at all:
//   - an unknown opcode hits `default: __debugbreak()` -> EXCEPTION_BREAKPOINT, the game crashes;
//   - a jump past the end of the buffer reads whatever memory follows -> crash or a phantom triangle;
//   - the absolute "chunk" jumps (0x0C, 0x70) can point backwards -> the VM loops forever, the game hangs.
// Relative jumps and split offsets are unsigned, so every other edge goes forward.
//
// Sanitize() walks every reachable instruction once, with the engine's own decoding rules, and turns each
// instruction that leads to one of the cases above into RETURN (0x00): the VM leaves that branch, the rest of the
// tree keeps working. The result is verified by another walk; if it is still not clean, the root becomes RETURN
// (the mesh loses its collision instead of crashing the game).

#include <cstdint>
#include <vector>

namespace Mopp
{
	struct Report
	{
		std::uint32_t invalidOpcodes = 0;  // unknown opcode (engine: int3)
		std::uint32_t outOfBounds = 0;     // jump or instruction past the end of the buffer
		std::uint32_t cycles = 0;          // a jump back onto the current path (engine: endless loop)
		std::uint32_t patched = 0;         // bytes rewritten to RETURN
		bool          rootCleared = false;  // could not repair locally, the whole tree was disabled

		bool Broken() const { return invalidOpcodes || outOfBounds || cycles; }
	};

	// Read-only check: what Sanitize() would find.
	Report Check(const std::uint8_t* a_code, std::uint32_t a_size);

	// Repairs the buffer in place. a_code must be writable.
	Report Sanitize(std::uint8_t* a_code, std::uint32_t a_size);
}
