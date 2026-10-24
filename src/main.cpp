// STB Havok Collision Fix -- the game no longer crashes or freezes on broken collision of modded meshes or on
// the physics engine's own bookkeeping errors:
//   HitSort            NaN hit distances sent Havok's quicksort out of the array (crash);
//   MoppAtLoad         broken MOPP bytecode in a .nif (unknown opcode = int3 crash, jump out of bounds, endless loop);
//   CompressedMeshKeys out-of-range triangle keys of compressed mesh collision (crash / phantom triangle);
//   SolverBuffer       island solver buffers sized from totals that drifted too low (crash in fights, ragdolls);
//   IslandUnlock       a pointless read of a stale island after solving it (crash);
//   ListenerRemove     removing a listener that is not attached wrote outside its array (memory corruption);
//   PendingQueue       the queue of physics changes re-running itself until the stack overflowed (crash);
//   WorldLock          broken lock counters of the physics world (random crashes later);
//   BatchCheck         batch add/remove of objects already in / not in the world or listed twice (crash later);
//   FootIkReserve      the foot IK array reallocated while in use (crash with many characters).
// Skyrim SE 1.5.97 only. Engine analysis: Skyrim-RE docs/HavokCollision_Guards_RE.md, docs/HavokSolver_Guards_RE.md.
// See README.md.
//
// Copyright (C) 2026 STB Team. GPL-3.0-or-later, see LICENSE.

#include "BatchFix.h"
#include "CompressedMeshFix.h"
#include "FootIkFix.h"
#include "HitSortFix.h"
#include "IslandUnlockFix.h"
#include "ListenerRemoveFix.h"
#include "MoppLoadFix.h"
#include "PendingQueueFix.h"
#include "Settings.h"
#include "SolverBufferFix.h"
#include "WorldLockFix.h"

SKSEPluginInfo(
	.Version = REL::Version{ Version::MAJOR, Version::MINOR, Version::PATCH, 0 },
	.Name = Version::PROJECT,
	.Author = "STB Team"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

static void InitLog()
{
	auto path = logger::log_directory();
	if (!path) {
		return;
	}
	*path /= Version::PROJECT;
	*path += ".log"sv;
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%H:%M:%S.%e] [%t] [%^%l%$] %v"s);
}

static void OnMessage(SKSE::MessagingInterface::Message* a_message)
{
	if (a_message->type == SKSE::MessagingInterface::kDataLoaded) {
		WorldLockFix::OnDataLoaded();
		FootIkFix::OnDataLoaded();
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitLog();
	SKSE::Init(a_skse);
	const auto game = REL::Module::get().version();
	logger::info("{} v{}, game {}", Version::PROJECT, Version::NAME, game.string());

	// Every address and offset here was checked on 1.5.97 only; on anything else stay out of the way.
	if (game != SKSE::RUNTIME_SSE_1_5_97) {
		logger::error("unsupported game version {} (needs 1.5.97) -- nothing installed", game.string());
		return true;
	}

	auto& settings = Settings::Get();
	settings.Load();

	SKSE::AllocTrampoline(512);
	if (settings.hitSort) {
		HitSortFix::Install();
	}
	if (settings.moppAtLoad) {
		MoppLoadFix::Install();
	}
	if (settings.compressedMeshKeys) {
		CompressedMeshFix::Install();
	}
	if (settings.solverBuffer) {
		SolverBufferFix::Install();
	}
	if (settings.islandUnlock) {
		IslandUnlockFix::Install();
	}
	if (settings.listenerRemove) {
		ListenerRemoveFix::Install();
	}
	if (settings.pendingQueue) {
		PendingQueueFix::Install();
	}
	if (settings.worldLock) {
		WorldLockFix::Install();
	}
	if (settings.batchCheck) {
		BatchFix::Install();
	}
	if (settings.footIkReserve) {
		FootIkFix::Install();
	}
	if (const auto messaging = SKSE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(OnMessage)) {
		logger::warn("no SKSE messaging -- WorldLock / FootIkReserve start-up checks skipped");
	}
	return true;
}
