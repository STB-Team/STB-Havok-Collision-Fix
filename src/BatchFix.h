#pragma once

// hkpWorld::addEntityBatch / removeEntityBatch / addPhantomBatch / removePhantomBatch (SE: REL::ID 60494, 60495,
// 60503, 60505) -- used for whole ragdolls and cells -- take the list as given: the checks that an object is not
// yet in a world (add) or is in this one (remove), and is listed once, are debug-only asserts in Havok.
// An object listed twice, added while already in a world or removed while not in this world is handled twice:
// its reference is dropped too often (freed while in use) and the broadphase / island data get mixed up -- the game
// crashes later at a random place.
// The fix lets each batch through with only the objects that pass those checks (first occurrence, world membership
// by hkpWorldObject::m_world); a clean list -- the normal case -- is passed on untouched.
// While the world is locked the batch is only queued; the check then runs when the queue executes it.
namespace BatchFix
{
	void Install();
}
