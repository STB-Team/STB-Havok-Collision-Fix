#include "FootIkFix.h"

namespace FootIkFix
{
	namespace
	{
		constexpr REL::ID     kConstructor{ 31978 };  // BGSFootIkRaycastInterfaceDB::BGSFootIkRaycastInterfaceDB
		constexpr std::size_t kCallOffset = 0x110;    // SetSize(entries, 50)
		constexpr REL::ID     kSetSize{ 32017 };      // BSTArray<entry, 0x38>::SetSize: grows, shrinks, (re)allocates

		constexpr std::uint32_t kReserve = 1024;
		constexpr std::size_t   kCapacity = 0x8;  // BSTArray: data 0, capacity 8, size 10

		using SetSize_t = std::uint64_t(void* a_array, std::uint32_t a_size);
		SetSize_t* setSize = nullptr;

		bool installed = false;
		bool reserved = false;

		std::uint64_t SetSize(void* a_array, std::uint32_t a_size)
		{
			// Grow to the reserve once (allocates and constructs), then shrink to the requested size: the engine keeps
			// the capacity, later growth up to the reserve stays in place.
			if (a_size < kReserve && setSize(a_array, kReserve)) {
				const auto result = setSize(a_array, a_size);
				reserved = Field<std::uint32_t>(a_array, kCapacity) >= kReserve;
				return result;
			}
			return setSize(a_array, a_size);
		}
	}

	void Install()
	{
		const auto target = kSetSize.address();
		const auto at = kConstructor.address() + kCallOffset;
		if (Rel32Target(at, 0xE8) != target) {
			logger::warn("FootIkReserve: the foot IK array set-up is not the original -- another mod patched it, skipped");
			return;
		}
		setSize = reinterpret_cast<SetSize_t*>(target);
		SKSE::GetTrampoline().write_call<5>(at, SetSize);
		installed = true;
	}

	void OnDataLoaded()
	{
		if (!installed) {
			return;
		}
		if (reserved) {
			logger::info("FootIkReserve: foot IK array reserved for {} entries", kReserve);
		} else {
			logger::warn("FootIkReserve: the foot IK array was not created through the hooked set-up -- nothing reserved");
		}
	}
}
