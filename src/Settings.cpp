#include "Settings.h"

void Settings::Load()
{
	const auto path = std::filesystem::current_path() / "Data/SKSE/Plugins" / (std::string(Version::PROJECT) + ".ini");
	const auto file = path.string();
	auto read = [&](const char* a_key, bool a_default) {
		return GetPrivateProfileIntA("Fixes", a_key, a_default ? 1 : 0, file.c_str()) != 0;
	};
	hitSort = read("HitSort", true);
	moppAtLoad = read("MoppAtLoad", true);
	compressedMeshKeys = read("CompressedMeshKeys", true);
	solverBuffer = read("SolverBuffer", true);
	islandUnlock = read("IslandUnlock", true);
	listenerRemove = read("ListenerRemove", true);
	pendingQueue = read("PendingQueue", true);
	worldLock = read("WorldLock", true);
	batchCheck = read("BatchCheck", true);
	footIkReserve = read("FootIkReserve", true);
	logger::info("settings ({}): HitSort={} MoppAtLoad={} CompressedMeshKeys={} SolverBuffer={} IslandUnlock={} "
				 "ListenerRemove={} PendingQueue={} WorldLock={} BatchCheck={} FootIkReserve={}",
		std::filesystem::exists(path) ? file : "no ini, defaults"s, hitSort, moppAtLoad, compressedMeshKeys,
		solverBuffer, islandUnlock, listenerRemove, pendingQueue, worldLock, batchCheck, footIkReserve);
}
