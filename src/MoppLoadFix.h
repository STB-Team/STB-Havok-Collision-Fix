#pragma once

// bhkMoppBvTreeShape::LoadBinary (SE: REL::ID 77764) reads the MOPP bytecode of a .nif through 77439 and stores
// the new hkpMoppCode in the Havok shape right after. The call at 77764+0x3A is wrapped: once the bytecode is
// read, MoppValidator repairs it in place -- before physics can ever run it. Once per loaded mesh, on the loader
// thread; nothing runs on the physics path.
namespace MoppLoadFix
{
	void Install();
}
