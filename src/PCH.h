#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#pragma warning(push)
#include <spdlog/sinks/basic_file_sink.h>
#pragma warning(pop)

#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

using namespace std::literals;

namespace logger = SKSE::log;

#include "Version.h"

// Read a field of an engine object by offset (Havok classes are not in CommonLib).
template <class T>
inline T Field(const void* a_object, std::size_t a_offset)
{
	return *reinterpret_cast<const T*>(static_cast<const std::byte*>(a_object) + a_offset);
}

// Target of the rel32 call (E8) / jmp (E9) at a_site, or 0 when the site holds something else (another mod's hook).
inline std::uintptr_t Rel32Target(std::uintptr_t a_site, std::uint8_t a_opcode)
{
	if (*reinterpret_cast<const std::uint8_t*>(a_site) != a_opcode) {
		return 0;
	}
	return a_site + 5 + *reinterpret_cast<const std::int32_t*>(a_site + 1);
}

// Logs the first few events in full, then every N-th, so a broken mesh touched every frame cannot flood the log.
inline bool ShouldLog(std::uint64_t a_count)
{
	return a_count <= 20 || a_count % 10000 == 0;
}
