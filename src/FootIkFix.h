#pragma once

// Foot IK (feet placed on the ground) keeps its raycast entries in one array of BGSFootIkRaycastInterfaceDB, created
// for 50 entries (SE: constructor REL::ID 31978 -> SetSize 32017). With more characters around the array grows and
// is reallocated while other characters still use the old entries: crash in the foot IK code.
// The fix reserves room for 1024 entries when the array is created (about 57 KB), so it never moves in practice.
namespace FootIkFix
{
	void Install();
	void OnDataLoaded();
}
