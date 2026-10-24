#include "MoppLoadFix.h"

#include "MoppValidator.h"

namespace MoppLoadFix
{
	namespace
	{
		// hkpMoppCode: 0x10 m_info (offset xyz + scale), 0x20 hkArray<hkUint8> m_data { data, size, capacity }.
		constexpr std::size_t kCodeData = 0x20;
		constexpr std::size_t kCodeSize = 0x28;
		constexpr std::int32_t kMaxSize = 64 << 20;  // anything bigger is not a real MOPP

		using Load_t = std::uint64_t(RE::NiStream*, void**);
		REL::Relocation<Load_t> loadMoppCode;

		constexpr REL::ID kLoadBinary{ 77764 };
		constexpr std::size_t kCallOffset = 0x3A;
		constexpr REL::ID kLoadMoppCode{ 77439 };

		std::atomic<std::uint64_t> repaired{ 0 };
		std::mutex                 namesLock;
		std::unordered_set<std::string> names;  // each broken .nif is named once per session

		std::string NifName(const RE::NiStream* a_stream)
		{
			const char* path = a_stream->inputFilePath;
			return std::string(path, strnlen(path, sizeof(a_stream->inputFilePath)));
		}

		void Repair(RE::NiStream* a_stream, void* a_code)
		{
			auto* const data = Field<std::uint8_t*>(a_code, kCodeData);
			const auto  size = Field<std::int32_t>(a_code, kCodeSize);
			if (!data || size <= 0 || size > kMaxSize) {
				return;
			}
			Mopp::Report report;
			try {
				report = Mopp::Sanitize(data, static_cast<std::uint32_t>(size));
			} catch (const std::exception& e) {  // bad_alloc on a huge buffer: leave it as vanilla would
				logger::error("MoppAtLoad: validation failed ({}), size={} nif=\"{}\"", e.what(), size, NifName(a_stream));
				return;
			}
			if (!report.Broken()) {
				return;
			}
			const auto total = ++repaired;
			auto       name = NifName(a_stream);
			{
				std::lock_guard lock(namesLock);
				if (!names.insert(name).second) {
					return;
				}
			}
			logger::warn("MoppAtLoad: repaired broken collision bytecode -- {} unknown opcode(s) (crash), {} jump(s) "
						 "out of bounds (crash / phantom collision), {} loop(s) (freeze); {} byte(s) set to RETURN{} "
						 "size={} nif=\"{}\" (repairs so far: {})",
				report.invalidOpcodes, report.outOfBounds, report.cycles, report.patched,
				report.rootCleared ? ", the whole tree disabled (mesh has no collision now)" : "", size, name, total);
		}

		std::uint64_t LoadMoppCode(RE::NiStream* a_stream, void** a_out)
		{
			const auto result = loadMoppCode(a_stream, a_out);
			if (a_stream && a_out && *a_out) {
				Repair(a_stream, *a_out);
			}
			return result;
		}
	}

	void Install()
	{
		const auto at = kLoadBinary.address() + kCallOffset;
		if (Rel32Target(at, 0xE8) != kLoadMoppCode.address()) {
			logger::warn("MoppAtLoad: bhkMoppBvTreeShape::LoadBinary+0x{:X} does not call the MOPP reader -- another "
						 "mod patched it, skipped",
				kCallOffset);
			return;
		}
		loadMoppCode = SKSE::GetTrampoline().write_call<5>(at, LoadMoppCode);
		logger::info("MoppAtLoad: installed (MOPP bytecode is checked when a .nif loads)");
	}
}
