// STB Havok Collision Fix -- the game no longer crashes or freezes on broken collision of modded meshes:
//   HitSort            NaN hit distances sent Havok's quicksort out of the array (crash);
//   MoppAtLoad         broken MOPP bytecode in a .nif (unknown opcode = int3 crash, jump out of bounds, endless loop);
//   CompressedMeshKeys out-of-range triangle keys of compressed mesh collision (crash / phantom triangle).
// Skyrim SE 1.5.97 only. Engine analysis: Skyrim-RE docs/HavokCollision_Guards_RE.md. See README.md.
//
// Copyright (C) 2026 STB Team. GPL-3.0-or-later, see LICENSE.

#include "CompressedMeshFix.h"
#include "HitSortFix.h"
#include "MoppLoadFix.h"
#include "Settings.h"

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

	SKSE::AllocTrampoline(128);
	if (settings.hitSort) {
		HitSortFix::Install();
	}
	if (settings.moppAtLoad) {
		MoppLoadFix::Install();
	}
	if (settings.compressedMeshKeys) {
		CompressedMeshFix::Install();
	}
	return true;
}
