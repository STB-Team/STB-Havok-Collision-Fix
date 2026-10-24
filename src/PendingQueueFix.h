#pragma once

// Changes to the physics world made while it is busy (adding or removing bodies, phantoms, constraints) are queued
// and run later by hkpWorld::internal_executePendingOperations (SE: REL::ID 60585) -> the queue executor (61313).
// The executor runs the operations one by one and, after each, runs anything queued meanwhile by calling itself.
// 1. The executor is also started while the world is locked for phantoms (criticalOperationsLockCountForPhantoms):
//    a queued phantom operation then cannot run and queues itself again, the executor calls itself for it, and so
//    on until the stack overflows. Havok only checks the main lock there. The fix leaves the queue for later: the
//    pending count stays set, so the next attempt -- at the latest the next physics step -- runs it.
// 2. Safety net for any other such loop: the executor's recursion (61313+0x779) stops at a depth of 64 or when the
//    thread's stack is nearly used up. The engine then drops what is still queued -- only in a loop that would
//    have crashed anyway.
namespace PendingQueueFix
{
	void Install();
}
