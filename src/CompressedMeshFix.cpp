#include "CompressedMeshFix.h"

namespace CompressedMeshFix
{
	namespace
	{
		// hkpCompressedMeshShape as seen from its hkpShapeContainer base (`this` of getChildShape), SE 1.5.97.
		// Offsets are taken from the engine's getChildShape (78721), not from Havok headers.
		constexpr std::size_t kBitsPerWIndex = 0x14;  // key >> bitsPerWIndex = section
		constexpr std::size_t kWIndexMask = 0x18;     // key & wIndexMask = index (+ winding bit)
		constexpr std::size_t kIndexMask = 0x1C;      // key & indexMask = triangle index inside a chunk
		constexpr std::size_t kTransforms = 0x58;     // hkArray<hkQsTransform>
		constexpr std::size_t kBigVertices = 0x68;    // hkArray<hkVector4>
		constexpr std::size_t kBigTriangles = 0x78;   // hkArray<BigTriangle>, 0x10 each
		constexpr std::size_t kChunks = 0x88;         // hkArray<Chunk>, 0x60 each
		constexpr std::size_t kConvexPieces = 0x98;   // hkArray<ConvexPiece>, 0x50 each

		constexpr std::size_t kBigTriangleSize = 0x10;  // u16 a, b, c @0/2/4, material @C, transform @E
		constexpr std::size_t kChunkSize = 0x60;        // vertices (u16 xyz) @10, indices (u16) @20, ref @54, transform @56
		constexpr std::size_t kConvexPieceSize = 0x50;  // ref @40, transform @42

		constexpr std::uint16_t kNone = 0xFFFF;

		struct Array
		{
			const std::byte* data;
			std::uint32_t    size;
		};

		Array ArrayAt(const void* a_object, std::size_t a_offset)
		{
			const auto size = Field<std::int32_t>(a_object, a_offset + 8);
			return { Field<const std::byte*>(a_object, a_offset), size > 0 ? static_cast<std::uint32_t>(size) : 0u };
		}

		bool TransformValid(const void* a_shape, std::uint16_t a_index)
		{
			return a_index == kNone || a_index < ArrayAt(a_shape, kTransforms).size;
		}

		// Mirrors the three decode paths of getChildShape and checks every array access it is about to make.
		bool KeyValid(const void* a_shape, std::uint32_t a_key)
		{
			const auto bits = Field<std::int32_t>(a_shape, kBitsPerWIndex);
			if (bits <= 0 || bits >= 32) {
				return true;  // not a layout we understand: leave the engine alone
			}
			const std::uint32_t section = a_key >> bits;
			const std::uint32_t index = a_key & Field<std::uint32_t>(a_shape, kWIndexMask);

			if (section == (1u << (32 - bits)) - 1) {  // convex piece
				const auto pieces = ArrayAt(a_shape, kConvexPieces);
				if (index >= pieces.size) {
					return false;
				}
				const auto* piece = pieces.data + kConvexPieceSize * index;
				const auto  ref = Field<std::uint16_t>(piece, 0x40);
				return TransformValid(a_shape, Field<std::uint16_t>(piece, 0x42)) && (ref == kNone || ref < pieces.size);
			}

			if (section == 0) {  // big triangle
				const auto triangles = ArrayAt(a_shape, kBigTriangles);
				if (index >= triangles.size) {
					return false;
				}
				const auto* triangle = triangles.data + kBigTriangleSize * index;
				const auto  vertices = ArrayAt(a_shape, kBigVertices).size;
				return Field<std::uint16_t>(triangle, 0) < vertices && Field<std::uint16_t>(triangle, 2) < vertices &&
				       Field<std::uint16_t>(triangle, 4) < vertices && TransformValid(a_shape, Field<std::uint16_t>(triangle, 0xE));
			}

			const auto chunks = ArrayAt(a_shape, kChunks);  // chunk = section - 1
			if (section - 1 >= chunks.size) {
				return false;
			}
			const auto* chunk = chunks.data + kChunkSize * (section - 1);
			if (!TransformValid(a_shape, Field<std::uint16_t>(chunk, 0x56))) {
				return false;
			}
			if (const auto ref = Field<std::uint16_t>(chunk, 0x54); ref != kNone) {
				if (ref >= chunks.size) {
					return false;
				}
				chunk = chunks.data + kChunkSize * ref;
			}
			const std::uint64_t triangle = a_key & Field<std::uint32_t>(a_shape, kIndexMask);
			const auto          indices = ArrayAt(chunk, 0x20);
			if (triangle + 2 >= indices.size) {
				return false;
			}
			const auto  vertices = ArrayAt(chunk, 0x10).size;  // u16 components, 3 per vertex
			const auto* strip = reinterpret_cast<const std::uint16_t*>(indices.data);
			for (std::uint64_t k = 0; k < 3; ++k) {
				if (3ull * strip[triangle + k] + 2 >= vertices) {
					return false;
				}
			}
			return true;
		}

		// The first valid triangle of the mesh: chunk 0, big triangle 0 or convex piece 0.
		std::optional<std::uint32_t> SafeKey(const void* a_shape)
		{
			const auto bits = Field<std::int32_t>(a_shape, kBitsPerWIndex);
			const std::uint32_t candidates[]{ 1u << bits, 0u, ((1u << (32 - bits)) - 1) << bits };
			for (const auto key : candidates) {
				if (KeyValid(a_shape, key)) {
					return key;
				}
			}
			return std::nullopt;
		}

		using GetChildShape_t = const void*(const void*, std::uint32_t, void*);
		REL::Relocation<GetChildShape_t> getChildShape;

		constexpr REL::ID kGetChildShape{ 78721 };
		constexpr std::size_t kSlot = 5;
		// hkpShapeContainer vtables (second vtable of each class) that hold getChildShape at slot 5.
		constexpr std::pair<REL::ID, const char*> kVtables[]{
			{ REL::ID(290794), "hkpCompressedMeshShape" },
			{ REL::ID(290004), "hkpCompressedMeshShapeShared" },
		};

		std::atomic<std::uint64_t> redirected{ 0 };
		std::atomic<std::uint64_t> unrepairable{ 0 };

		const void* GetChildShape(const void* a_shape, std::uint32_t a_key, void* a_buffer)
		{
			if (!KeyValid(a_shape, a_key)) {
				if (const auto safe = SafeKey(a_shape)) {
					const auto count = ++redirected;
					if (ShouldLog(count)) {
						logger::warn("CompressedMeshKeys: invalid key 0x{:08X} on shape 0x{:X} -> 0x{:08X} (crash / phantom "
									 "triangle averted; total {})",
							a_key, reinterpret_cast<std::uintptr_t>(a_shape), *safe, count);
					}
					a_key = *safe;
				} else if (const auto count = ++unrepairable; ShouldLog(count)) {
					logger::error("CompressedMeshKeys: invalid key 0x{:08X} on shape 0x{:X} and no valid triangle to use "
								  "instead -- passed to the engine unchanged (total {})",
						a_key, reinterpret_cast<std::uintptr_t>(a_shape), count);
				}
			}
			return getChildShape(a_shape, a_key, a_buffer);
		}
	}

	void Install()
	{
		const auto vanilla = kGetChildShape.address();
		getChildShape = REL::Relocation<GetChildShape_t>{ kGetChildShape };
		int installed = 0;
		for (const auto& [id, name] : kVtables) {
			REL::Relocation<std::uintptr_t> vtable{ id };
			if (*reinterpret_cast<const std::uintptr_t*>(vtable.address() + kSlot * sizeof(void*)) != vanilla) {
				logger::warn("CompressedMeshKeys: {} vtable slot {} is not the vanilla getChildShape -- another mod "
							 "patched it, skipped",
					name, kSlot);
				continue;
			}
			vtable.write_vfunc(kSlot, GetChildShape);
			++installed;
		}
		logger::info("CompressedMeshKeys: {}/{} vtables guarded", installed, std::size(kVtables));
	}
}
