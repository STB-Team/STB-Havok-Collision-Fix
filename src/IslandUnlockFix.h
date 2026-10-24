#pragma once

// After an island is solved, the simulation (SE: REL::ID 78487 twice, 79073 once) loads island->[0x130] only to
// pass it to hkpWorld::unlockIslandForConstraints (60526), which ignores that argument in the release build. When
// the island was merged or removed meanwhile, the pointer is stale and that pointless read crashes the game.
// The fix replaces the load with `xor edx, edx` -- the function gets the same unused argument as a null.
namespace IslandUnlockFix
{
	void Install();
}
