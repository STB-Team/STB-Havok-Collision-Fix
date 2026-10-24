#include "BatchFix.h"

namespace BatchFix
{
	namespace
	{
		// hkpWorld
		constexpr std::size_t kLockCount = 0xF8;             // int32 criticalOperationsLockCount
		constexpr std::size_t kLockCountForPhantoms = 0xFC;  // int32
		// hkpWorldObject (base of hkpEntity and hkpPhantom)
		constexpr std::size_t kObjectWorld = 0x10;

		// All four start with `test r8d, r8d; jle <ret>` (9 bytes); the hook jumps over it to +9 once the count is
		// known to be positive. The 4th argument (activation, add entities only) is passed through.
		using Batch_t = void(std::byte* a_world, std::byte** a_objects, std::int32_t a_count, std::int32_t a_arg);

		enum class Kind
		{
			kAdd,
			kRemove
		};

		struct Site
		{
			REL::ID     function;
			Kind        kind;
			bool        phantoms;  // phantom batches are also queued while the world is locked for phantoms
			const char* name;
			Batch_t*    original = nullptr;
		};

		Site sites[]{
			{ REL::ID(60494), Kind::kAdd, false, "addEntityBatch" },
			{ REL::ID(60495), Kind::kRemove, false, "removeEntityBatch" },
			{ REL::ID(60503), Kind::kAdd, true, "addPhantomBatch" },
			{ REL::ID(60505), Kind::kRemove, true, "removePhantomBatch" },
		};

		std::atomic<std::uint64_t> dropped{ 0 };

		bool Accepted(const std::byte* a_world, const std::byte* a_object, Kind a_kind)
		{
			if (!a_object) {
				return false;
			}
			const auto* const world = Field<const std::byte*>(a_object, kObjectWorld);
			return a_kind == Kind::kAdd ? world == nullptr : world == a_world;
		}

		bool Listed(std::byte* const* a_begin, std::byte* const* a_end, const std::byte* a_object)
		{
			return std::find(a_begin, a_end, a_object) != a_end;
		}

		template <std::size_t I>
		void Batch(std::byte* a_world, std::byte** a_objects, std::int32_t a_count, std::int32_t a_arg)
		{
			const auto& site = sites[I];
			if (a_count <= 0 || !a_objects) {
				return;
			}
			const bool locked = Field<std::int32_t>(a_world, kLockCount) != 0 ||
			                    (site.phantoms && Field<std::int32_t>(a_world, kLockCountForPhantoms) != 0);
			if (locked) {
				site.original(a_world, a_objects, a_count, a_arg);  // queued (checked when the queue runs it)
				return;
			}

			// Fast path: one pass, no allocation, for the usual clean list.
			const auto count = static_cast<std::size_t>(a_count);
			bool       clean = true;
			if (count <= 64) {
				for (std::size_t i = 0; i < count && clean; ++i) {
					clean = Accepted(a_world, a_objects[i], site.kind) && !Listed(a_objects, a_objects + i, a_objects[i]);
				}
			} else {
				std::unordered_set<const std::byte*> seen;
				seen.reserve(count);
				for (std::size_t i = 0; i < count && clean; ++i) {
					clean = Accepted(a_world, a_objects[i], site.kind) && seen.insert(a_objects[i]).second;
				}
			}
			if (clean) {
				site.original(a_world, a_objects, a_count, a_arg);
				return;
			}

			std::vector<std::byte*>              kept;
			std::unordered_set<const std::byte*> seen;
			kept.reserve(count);
			seen.reserve(count);
			for (std::size_t i = 0; i < count; ++i) {
				if (Accepted(a_world, a_objects[i], site.kind) && seen.insert(a_objects[i]).second) {
					kept.push_back(a_objects[i]);
				}
			}
			const auto total = dropped += count - kept.size();
			if (ShouldLog(total)) {
				logger::warn("BatchCheck: {} with {} object(s), {} already/not in the world or listed twice -- left out "
							 "(crash averted); total {}",
					site.name, count, count - kept.size(), total);
			}
			if (!kept.empty()) {
				site.original(a_world, kept.data(), static_cast<std::int32_t>(kept.size()), a_arg);
			}
		}

		template <std::size_t... I>
		void InstallAll(std::index_sequence<I...>)
		{
			(
				[] {
					auto&      site = sites[I];
					const auto entry = site.function.address();
					if (!CodeIs(entry, { 0x45, 0x85, 0xC0, 0x0F, 0x8E })) {  // test r8d, r8d; jle rel32
						logger::warn("BatchCheck: {} is not the original code -- another mod patched it, skipped", site.name);
						return;
					}
					site.original = reinterpret_cast<Batch_t*>(entry + 9);
					SKSE::GetTrampoline().write_branch<5>(entry, Batch<I>);
					logger::info("BatchCheck: {} guarded", site.name);
				}(),
				...);
		}
	}

	void Install()
	{
		InstallAll(std::make_index_sequence<std::size(sites)>{});
	}
}
