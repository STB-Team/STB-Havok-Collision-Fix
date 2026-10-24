#pragma once

// Collision hit lists are sorted by Havok's quicksort sort_points (SE: REL::ID 42697). Its right-hand scan is
// `comiss pivot, [hit]; jb`: an unordered compare (NaN) counts as "less", so a hit with a NaN distance picked as
// the pivot sends the scan below the start of the array -- it reads memory until the game crashes.
// The fix replaces NaN distances with FLT_MAX (the hit sorts last) before every call of sort_points.
namespace HitSortFix
{
	void Install();
}
