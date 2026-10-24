#pragma once

// hkpWorld keeps two "busy" counters, criticalOperationsLockCount and ...ForPhantoms: while one is above zero,
// changes to the world are queued instead of applied. An unbalanced unlock elsewhere leaves a counter negative
// (the world then counts as free in the middle of its own update), an unbalanced lock leaves it stuck above zero
// (every change queued forever). Both end in random crashes later.
// The fix checks the counters at the start of every physics step (SE: bhkWorld::Update, REL::ID 76018, right after
// it takes the world's write lock -- nobody else is inside the world then): a negative counter is set to 0 at once,
// a counter that stays at the same positive value for 600 steps (~10 s) while changes wait in the queue is reset.
// It also switches bUseWorldLock:HAVOK (default on) back on if an ini turned it off -- without that lock the step
// runs unprotected against the other threads, and this check is skipped.
namespace WorldLockFix
{
	void Install();
	void OnDataLoaded();
}
