#include "SolverBufferFix.h"

namespace SolverBufferFix
{
	namespace
	{
		// hkpSimulationIsland::m_constraintInfo (hkpConstraintInfo at +0x10).
		constexpr std::size_t kSizeOfSchemas = 0x14;
		constexpr std::size_t kNumSolverElemTemps = 0x1C;

		// Totals above this are not real islands (memory garbage) -- leave them to the engine as they are.
		constexpr std::uint32_t kSaneLimit = 64u << 20;

		// solveIsland(..., island, ...): eight arguments, all passed through.
		using Solve_t = std::uint64_t(void*, void*, void*, void*, void*, void*, void*, std::int32_t);

		constexpr REL::ID kContinuousIntegrate{ 60941 };
		constexpr std::size_t kCallOffset = 0x489;
		constexpr REL::ID kSolveIsland{ 61420 };

		Solve_t* solveIsland = nullptr;

		std::uint64_t SolveIsland(void* a1, void* a2, void* a3, void* a_island, void* a5, void* a6, void* a7,
			std::int32_t a8)
		{
			if (!a_island) {
				return solveIsland(a1, a2, a3, a_island, a5, a6, a7, a8);
			}
			auto* const base = static_cast<std::byte*>(a_island);
			auto&       schemas = *reinterpret_cast<std::uint32_t*>(base + kSizeOfSchemas);
			auto&       temps = *reinterpret_cast<std::uint32_t*>(base + kNumSolverElemTemps);
			if (schemas > kSaneLimit || temps > kSaneLimit) {
				return solveIsland(a1, a2, a3, a_island, a5, a6, a7, a8);
			}
			// Double plus a fixed minimum (16-byte aligned: schemas are written in 16-byte blocks). The buffers come
			// from Havok's per-thread LIFO allocator, so the reserve costs nothing but a little stack-like memory.
			const std::uint32_t schemasReserve = (schemas + 1024 + 15) & ~15u;
			const std::uint32_t tempsReserve = temps + 16;
			schemas += schemasReserve;
			temps += tempsReserve;
			const auto result = solveIsland(a1, a2, a3, a_island, a5, a6, a7, a8);
			// Subtract rather than restore: the totals may legitimately change during the call.
			schemas -= schemasReserve;
			temps -= tempsReserve;
			return result;
		}
	}

	void Install()
	{
		const auto target = kSolveIsland.address();
		const auto at = kContinuousIntegrate.address() + kCallOffset;
		if (Rel32Target(at, 0xE8) != target) {
			logger::warn("SolverBuffer: the island solver call is not the original -- another mod patched it, skipped");
			return;
		}
		solveIsland = reinterpret_cast<Solve_t*>(target);
		SKSE::GetTrampoline().write_call<5>(at, SolveIsland);
		logger::info("SolverBuffer: island solver buffers get a reserve");
	}
}
