#pragma once

// Havok's remove*Listener functions look the listener up in their array and assert when it is missing; the release
// build has no assert, so the "not found" index -1 is used as is: data[-1] = null (the 8 bytes before the array),
// or for phantoms the array shrinks by one and the shift starts at -1. A detach of something that was never
// attached (or already detached) silently corrupts memory and the game crashes now or much later.
// The fix makes "not found" return without touching anything (11 functions, a few bytes each).
namespace ListenerRemoveFix
{
	void Install();
}
