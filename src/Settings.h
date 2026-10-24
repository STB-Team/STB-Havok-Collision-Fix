#pragma once

// Data/SKSE/Plugins/STBHavokCollisionFix.ini, section [Fixes]. A missing file or key means "on".
struct Settings
{
	bool hitSort = true;            // HitSort: NaN distances in collision hit lists
	bool moppAtLoad = true;         // MoppAtLoad: repair MOPP bytecode when a .nif is loaded
	bool compressedMeshKeys = true;  // CompressedMeshKeys: bad triangle keys of compressed mesh collision

	static Settings& Get()
	{
		static Settings instance;
		return instance;
	}

	void Load();
};
