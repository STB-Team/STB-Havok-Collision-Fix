#include "PendingQueueFix.h"

namespace PendingQueueFix
{
	namespace
	{
		// hkpWorld
		constexpr std::size_t kPendingOperations = 0xE8;       // hkpWorldOperationQueue*
		constexpr std::size_t kPendingOperationsCount = 0xF0;  // int32
		constexpr std::size_t kLockCountForPhantoms = 0xFC;    // int32

		using Execute_t = std::uint32_t(void* a_queue);

		constexpr REL::ID kExecutePending{ 60585 };  // hkpWorld::internal_executePendingOperations
		constexpr REL::ID kExecuteQueue{ 61313 };    // hkpWorldOperationQueue::executeAllPending
		constexpr std::size_t kRecursionOffset = 0x779;

		constexpr std::uint32_t kMaxDepth = 64;
		constexpr std::uintptr_t kMinStack = 64 * 1024;

		Execute_t* executeQueue = nullptr;

		std::atomic<std::uint64_t> postponed{ 0 };
		std::atomic<std::uint64_t> stopped{ 0 };

		thread_local std::uint32_t depth = 0;

		// Replaces 60585 as a whole (the original is these two lines without the check).
		std::uint32_t ExecutePending(std::byte* a_world)
		{
			if (Field<std::int32_t>(a_world, kLockCountForPhantoms) != 0) {
				const auto count = ++postponed;
				if (ShouldLog(count)) {
					logger::warn("PendingQueue: world locked for phantoms, queued physics changes postponed to the next "
								 "attempt (endless queue loop averted); total {}",
						count);
				}
				return 0;
			}
			*reinterpret_cast<std::int32_t*>(a_world + kPendingOperationsCount) = 0;
			return executeQueue(Field<void*>(a_world, kPendingOperations));
		}

		// The stack is one reserved region; its AllocationBase is the lowest address the stack can grow to.
		// Only asked on nested runs of the queue, which are rare.
		bool StackNearlyFull()
		{
			MEMORY_BASIC_INFORMATION info{};
			if (VirtualQuery(&info, &info, sizeof(info)) != sizeof(info)) {
				return false;
			}
			const auto here = reinterpret_cast<std::uintptr_t>(&info);
			return here - reinterpret_cast<std::uintptr_t>(info.AllocationBase) < kMinStack;
		}

		std::uint32_t ExecuteQueueNested(void* a_queue)
		{
			if (depth >= kMaxDepth || StackNearlyFull()) {
				const auto count = ++stopped;
				if (ShouldLog(count)) {
					logger::warn("PendingQueue: the physics queue kept re-running itself (depth {}), loop stopped "
								 "(stack overflow averted); total {}",
						depth, count);
				}
				return 0;
			}
			++depth;
			const auto result = executeQueue(a_queue);
			--depth;
			return result;
		}
	}

	void Install()
	{
		const auto queue = kExecuteQueue.address();
		executeQueue = reinterpret_cast<Execute_t*>(queue);
		auto& trampoline = SKSE::GetTrampoline();

		const auto pending = kExecutePending.address();
		// mov dword ptr [rcx+0F0h], 0; mov rcx, [rcx+0E8h]; jmp 61313
		if (CodeIs(pending, { 0xC7, 0x81, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x89, 0xE8,
								0x00, 0x00, 0x00 }) &&
			Rel32Target(pending + 17, 0xE9) == queue) {
			trampoline.write_branch<5>(pending, ExecutePending);
			logger::info("PendingQueue: queued physics changes wait while the world is locked for phantoms");
		} else {
			logger::warn("PendingQueue: internal_executePendingOperations is not the original code -- another mod "
						 "patched it, skipped");
		}

		const auto at = queue + kRecursionOffset;
		if (Rel32Target(at, 0xE8) == queue) {
			trampoline.write_call<5>(at, ExecuteQueueNested);
			logger::info("PendingQueue: queue recursion limited to depth {}", kMaxDepth);
		} else {
			logger::warn("PendingQueue: the queue recursion call is not the original -- another mod patched it, skipped");
		}
	}
}
