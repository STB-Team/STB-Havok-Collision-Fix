#include "IslandUnlockFix.h"

namespace IslandUnlockFix
{
	namespace
	{
		constexpr REL::ID kUnlockIslandForConstraints{ 60526 };

		struct Site
		{
			REL::ID     function;
			std::size_t offset;  // of `mov rdx, [rdx+130h]`, followed by `call 60526`
		};

		constexpr Site kSites[]{
			{ REL::ID(78487), 0x541 },
			{ REL::ID(78487), 0x64C },
			{ REL::ID(79073), 0x569 },
		};
	}

	void Install()
	{
		const auto unlock = kUnlockIslandForConstraints.address();
		int        installed = 0;
		for (const auto& site : kSites) {
			const auto at = site.function.address() + site.offset;
			if (Rel32Target(at + 7, 0xE8) != unlock ||
				!PatchCode(at,
					{ 0x48, 0x8B, 0x92, 0x30, 0x01, 0x00, 0x00 },   // mov rdx, [rdx+130h]
					{ 0x31, 0xD2, 0x0F, 0x1F, 0x44, 0x00, 0x00 })) {  // xor edx, edx; nop
				logger::warn("IslandUnlock: {}+0x{:X} is not the original code -- another mod patched it, skipped",
					site.function.id(), site.offset);
				continue;
			}
			++installed;
		}
		logger::info("IslandUnlock: {}/{} island unlock reads removed", installed, std::size(kSites));
	}
}
