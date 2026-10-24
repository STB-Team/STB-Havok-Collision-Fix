#pragma once

// Data/SKSE/Plugins/STBHavokCollisionFix.ini, section [Fixes]. A missing file or key means "on".
struct Settings
{
	bool hitSort = true;            // HitSort: NaN distances in collision hit lists
	bool moppAtLoad = true;         // MoppAtLoad: repair MOPP bytecode when a .nif is loaded
	bool compressedMeshKeys = true;  // CompressedMeshKeys: bad triangle keys of compressed mesh collision
	bool solverBuffer = true;        // SolverBuffer: reserve for the island solver buffers
	bool islandUnlock = true;        // IslandUnlock: no read of a stale island when unlocking it
	bool listenerRemove = true;      // ListenerRemove: removing a listener that is not attached does nothing
	bool pendingQueue = true;        // PendingQueue: no endless re-run of the queue of physics changes
	bool worldLock = true;           // WorldLock: repair broken lock counters of the physics world
	bool batchCheck = true;          // BatchCheck: batch add/remove only with objects the world accepts
	bool footIkReserve = true;       // FootIkReserve: foot IK array never reallocated

	static Settings& Get()
	{
		static Settings instance;
		return instance;
	}

	void Load();
};
