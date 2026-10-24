#include "WorldLockFix.h"

namespace WorldLockFix
{
	namespace
	{
		// hkpWorld
		constexpr std::size_t kPendingOperationsCount = 0xF0;  // int32
		constexpr std::size_t kLockCount = 0xF8;               // int32 criticalOperationsLockCount
		constexpr std::size_t kLockCountForPhantoms = 0xFC;    // int32
		constexpr std::size_t kUserData = 0x430;               // back pointer to the bhkWorld

		constexpr std::size_t kWorldLock = 0xC598;  // bhkWorld::worldLock (BSReadWriteLock)
		constexpr std::size_t kHavokWorld = 0x10;   // bhkWorld -> hkpWorld

		constexpr REL::ID     kWorldUpdate{ 76018 };
		constexpr std::size_t kLockOffset = 0x11C;  // add rcx, 0C598h; call BSReadWriteLock::LockForWrite
		constexpr REL::ID     kLockForWrite{ 66977 };

		constexpr std::uint32_t kStuckSteps = 600;

		using Lock_t = void(void* a_lock);
		Lock_t* lockForWrite = nullptr;

		bool installed = false;

		// Only touched by the thread that holds the world's write lock.
		struct Stuck
		{
			const void*   world = nullptr;
			std::int32_t  value = 0;
			std::uint32_t steps = 0;
		};
		Stuck stuck[2];  // main / phantoms

		std::atomic<std::uint64_t> repaired{ 0 };

		bool Check(const void* a_world, std::int32_t& a_count, Stuck& a_stuck, bool a_pending)
		{
			if (a_stuck.world != a_world) {
				a_stuck = { a_world, 0, 0 };
			}
			if (a_count < 0) {
				a_count = 0;
				a_stuck.steps = 0;
				return true;
			}
			if (a_count == 0 || !a_pending || a_count != a_stuck.value) {
				a_stuck.value = a_count;
				a_stuck.steps = 0;
				return false;
			}
			if (++a_stuck.steps < kStuckSteps) {
				return false;
			}
			a_count = 0;
			a_stuck.value = 0;
			a_stuck.steps = 0;
			return true;
		}

		void LockForWrite(std::byte* a_lock)
		{
			lockForWrite(a_lock);
			auto* const owner = a_lock - kWorldLock;
			auto* const world = Field<std::byte*>(owner, kHavokWorld);
			if (!world || Field<std::byte*>(world, kUserData) != owner) {
				return;  // not the bhkWorld layout we know -- do nothing
			}
			auto&       main = *reinterpret_cast<std::int32_t*>(world + kLockCount);
			auto&       phantoms = *reinterpret_cast<std::int32_t*>(world + kLockCountForPhantoms);
			const bool  pending = Field<std::int32_t>(world, kPendingOperationsCount) > 0;
			const auto  oldMain = main;
			const auto  oldPhantoms = phantoms;
			const bool  fixedMain = Check(world, main, stuck[0], pending);
			const bool  fixedPhantoms = Check(world, phantoms, stuck[1], pending);
			if (fixedMain || fixedPhantoms) {
				const auto count = ++repaired;
				if (ShouldLog(count)) {
					logger::warn("WorldLock: physics world lock state was broken (lock {} / phantoms {}), repaired "
								 "to {} / {}; total {}",
						oldMain, oldPhantoms, main, phantoms, count);
				}
			}
		}
	}

	void Install()
	{
		const auto target = kLockForWrite.address();
		const auto at = kWorldUpdate.address() + kLockOffset;
		if (!CodeIs(at, { 0x48, 0x81, 0xC1, 0x98, 0xC5, 0x00, 0x00 }) || Rel32Target(at + 7, 0xE8) != target) {
			logger::warn("WorldLock: the world lock call of bhkWorld::Update is not the original -- another mod "
						 "patched it, skipped");
			return;
		}
		lockForWrite = reinterpret_cast<Lock_t*>(target);
		SKSE::GetTrampoline().write_call<5>(at + 7, LockForWrite);
		installed = true;
		logger::info("WorldLock: physics world lock state checked at every step");
	}

	void OnDataLoaded()
	{
		if (!installed) {
			return;
		}
		auto* const ini = RE::INISettingCollection::GetSingleton();
		auto* const setting = ini ? ini->GetSetting("bUseWorldLock:HAVOK"sv) : nullptr;
		if (!setting) {
			logger::warn("WorldLock: setting bUseWorldLock:HAVOK not found");
			return;
		}
		if (!setting->GetBool()) {
			setting->data.b = true;
			logger::warn("WorldLock: bUseWorldLock:HAVOK was off (an ini sets it), switched back on");
		}
	}
}
