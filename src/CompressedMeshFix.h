#pragma once

// hkpCompressedMeshShape::getChildShape (SE: REL::ID 78721, slot 5 of the hkpShapeContainer vtable) turns a
// shape key from the MOPP tree into a triangle or a convex piece. It trusts the key completely: a key from a broken
// mesh indexes past the chunk / triangle / vertex arrays -- the game crashes or builds a "phantom" triangle from
// whatever memory is there. The vtable slot is replaced: an invalid key is swapped for the key of the first valid
// triangle of the same mesh (real geometry, so collision stays sane), then the engine function runs as usual.
namespace CompressedMeshFix
{
	void Install();
}
