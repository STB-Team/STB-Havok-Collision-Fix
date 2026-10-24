#include "ListenerRemoveFix.h"

namespace ListenerRemoveFix
{
	namespace
	{
		// The search loop of every one of them ends in `or r9d/r8d, -1` (the "not found" index), which is also where
		// an empty array jumps; "found" jumps past it. These are leaf functions without a frame, so a `ret` there is
		// a clean exit.
		struct Site
		{
			REL::ID      function;
			std::size_t  offset;
			std::uint8_t reg;  // ModRM of `or reg, -1`: C9 = r9d, C8 = r8d
			const char*  name;
		};

		constexpr Site kSites[]{
			{ REL::ID(60531), 0x34, 0xC9, "hkpWorld constraint listener" },
			{ REL::ID(60533), 0x34, 0xC9, "hkpWorld entity listener" },
			{ REL::ID(60536), 0x34, 0xC9, "hkpWorld island activation listener" },
			{ REL::ID(60539), 0x34, 0xC9, "hkpWorld post-simulation listener" },
			{ REL::ID(60545), 0x34, 0xC9, "hkpWorld deletion listener" },
			{ REL::ID(60093), 0x34, 0xC8, "hkpEntity activation listener" },
			{ REL::ID(60095), 0x34, 0xC9, "hkpEntity contact listener" },
			{ REL::ID(60949), 0x34, 0xC9, "hkpPhantom phantom listener" },
			{ REL::ID(60951), 0x34, 0xC9, "hkpPhantom overlap listener" },
			{ REL::ID(60714), 0x2B, 0xC9, "hkpConstraintInstance listener" },
		};

		// hkpEntity::removeEntityListener (60091) has a frame and its "found" path jumps into the middle of the
		// "not found" code, so it is replaced as a whole. The original: allocate the extended listeners if there are
		// none, find the listener in extendedListeners->entityListeners, null that slot (no assert in release).
		constexpr REL::ID     kRemoveEntityListener{ 60091 };
		constexpr std::size_t kExtendedListeners = 0x2B8;  // hkpEntity::extendedListeners
		constexpr std::size_t kEntityListeners = 0x10;     // ExtendedListeners::entityListeners (hkSmallArray)

		void RemoveEntityListener(void* a_entity, void* a_listener)
		{
			auto* const extended = Field<std::byte*>(a_entity, kExtendedListeners);
			if (!extended) {
				return;  // nothing was ever attached, nothing to detach
			}
			auto* const    data = Field<void**>(extended, kEntityListeners);
			const auto     size = Field<std::uint16_t>(extended, kEntityListeners + 8);
			for (std::uint16_t i = 0; i < size; ++i) {
				if (data[i] == a_listener) {
					data[i] = nullptr;
					return;
				}
			}
		}
	}

	void Install()
	{
		int installed = 0;
		for (const auto& site : kSites) {
			const auto at = site.function.address() + site.offset;
			if (!PatchCode(at, { 0x41, 0x83, site.reg, 0xFF }, { 0xC3, 0x90, 0x90, 0x90 })) {  // ret
				logger::warn("ListenerRemove: {} ({}) is not the original code -- another mod patched it, skipped",
					site.name, site.function.id());
				continue;
			}
			++installed;
		}
		const auto entry = kRemoveEntityListener.address();
		// mov [rsp+8], rbx; mov [rsp+10h], rsi; push rdi; sub rsp, 20h; xor esi, esi
		if (CodeIs(entry, { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
							  0x33, 0xF6 })) {
			SKSE::GetTrampoline().write_branch<5>(entry, RemoveEntityListener);
			++installed;
		} else {
			logger::warn("ListenerRemove: hkpEntity entity listener ({}) is not the original code -- another mod "
						 "patched it, skipped",
				kRemoveEntityListener.id());
		}
		logger::info("ListenerRemove: {}/{} listener removals ignore a missing listener", installed,
			std::size(kSites) + 1);
	}
}
