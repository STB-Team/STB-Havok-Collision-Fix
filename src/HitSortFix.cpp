#include "HitSortFix.h"

namespace HitSortFix
{
	namespace
	{
		// hkpRootCdPoint, 0x40 bytes; the sort key is m_contact.m_separatingNormal.w = distance.
		struct RootCdPoint
		{
			std::byte pad00[0x1C];
			float     distance;  // 1C
			std::byte pad20[0x20];
		};
		static_assert(sizeof(RootCdPoint) == 0x40);

		// The 4th argument (r9b) is passed through untouched.
		using Sort_t = void(RootCdPoint*, std::int32_t, std::int32_t, std::uint64_t);

		constexpr REL::ID kSortPoints{ 42697 };

		struct Site
		{
			REL::ID       function;
			std::size_t   offset;
			std::uint8_t  opcode;  // E8 call / E9 tail jump
			const char*   name;
		};

		// Every caller of sort_points except its own recursion (which only sees already cleaned hits).
		constexpr Site kSites[]{
			{ REL::ID(59651), 0x19, 0xE9, "hkpAllCdPointCollector::sortHits" },
			{ REL::ID(77219), 0x19, 0xE9, "hkpAllCdPointTempCollector::sortHits" },
			{ REL::ID(59678), 0x69, 0xE9, "sortHits (collector at 0x140A5CDF0)" },
			{ REL::ID(42679), 0x1C0, 0xE8, "Explosion" },
		};

		Sort_t* sortPoints = nullptr;  // resolved once in Install(): called for every sorted hit list

		std::atomic<std::uint64_t> fixedHits{ 0 };
		std::atomic<std::uint64_t> fixedLists{ 0 };

		void SortPoints(RootCdPoint* a_points, std::int32_t a_left, std::int32_t a_right, std::uint64_t a_flag)
		{
			std::uint32_t fixed = 0;
			for (std::int32_t i = a_left; i <= a_right; ++i) {
				if (std::isnan(a_points[i].distance)) {
					a_points[i].distance = std::numeric_limits<float>::max();
					++fixed;
				}
			}
			if (fixed) {
				const auto hits = fixedHits += fixed;
				const auto lists = ++fixedLists;
				if (ShouldLog(lists)) {
					logger::warn("HitSort: {} hit(s) with a NaN distance moved to the end of the list (crash averted); "
								 "total {} hit(s) in {} list(s)",
						fixed, hits, lists);
				}
			}
			sortPoints(a_points, a_left, a_right, a_flag);
		}
	}

	void Install()
	{
		const auto target = kSortPoints.address();
		sortPoints = reinterpret_cast<Sort_t*>(target);
		auto&      trampoline = SKSE::GetTrampoline();
		int        installed = 0;
		for (const auto& site : kSites) {
			const auto at = site.function.address() + site.offset;
			if (Rel32Target(at, site.opcode) != target) {
				logger::warn("HitSort: {} (+0x{:X}) does not call sort_points -- another mod patched it, skipped",
					site.name, site.offset);
				continue;
			}
			if (site.opcode == 0xE9) {
				trampoline.write_branch<5>(at, SortPoints);
			} else {
				trampoline.write_call<5>(at, SortPoints);
			}
			++installed;
		}
		logger::info("HitSort: {}/{} call sites of sort_points guarded", installed, std::size(kSites));
	}
}
