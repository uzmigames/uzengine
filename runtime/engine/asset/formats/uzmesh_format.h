/// UzEngine .uzmesh Binary Format
///
/// Runtime-ready mesh format containing LOD levels, submeshes, and interleaved
/// vertex/index data. All data regions are 64-byte aligned for direct GPU upload.
/// The format is designed for zero-copy GPU buffer creation — vertex and index
/// regions can be mapped directly into GPU memory without parsing or copying.
///
/// # Version History
/// - **v1** (initial): Header + LODs + Submeshes + vertex/index data.
/// - **v2**: Skeletal mesh support. `flags & kUzMeshFlag_Skinned` indicates
///   skinned vertex data (56 bytes/vertex). `reserved[0]` stores the byte
///   length of a null-terminated skeleton filepath string appended after
///   the index data region. Version 1 files remain fully loadable.
///
/// # Endianness
/// All multi-byte fields are little-endian (native x86/ARM LE). No byte-swap
/// is needed on Windows, Linux x86, or ARM in LE mode.
///
/// # Alignment and Padding
/// Vertex and index data regions are each padded to 64-byte boundaries. This
/// matches the GPU cache line size on most hardware and allows direct upload
/// via `ID3D12Resource::Map` or `vkMapMemory` without extra alignment work.
/// Padding bytes between sections are always zero.
///
/// # Binary Layout (little-endian)
/// ```
/// Offset   Size                         Section
/// ──────── ──────────────────────────── ──────────────────────────────────
/// 0x0000   64 bytes                     [Header] UzMeshHeader
/// 0x0040   lodCount * 16 bytes          [LODs] UzMeshLOD array
///          totalSubmeshCount * 16 bytes  [Submeshes] UzMeshSubmesh array
///          0..63 bytes                   [Padding] zeros to 64-byte boundary
///          vertexCount * vertexStride    [Vertex Data] interleaved vertices
///          0..63 bytes                   [Padding] zeros to 64-byte boundary
///          indexCount * indexStride      [Index Data] uint16 or uint32 indices
///          skeletonPathLength bytes      [v2 Skinned] skeleton filepath (null-terminated)
/// ```
///
/// # Validation Rules
/// Readers MUST validate in this order before accessing data:
/// 1. File size >= 64 bytes (header fits).
/// 2. `magic == 0x534D5A55` ("UZMS" in LE).
/// 3. `version <= kUzMeshVersion` (reject future versions).
/// 4. `flags` contains only known bits (`kUzMeshFlag_Skinned`).
/// 5. `indexStride` is 2 or 4.
/// 6. `vertexStride > 0`.
/// 7. `vertexDataOffset` and `indexDataOffset` are within file bounds.
/// 8. `vertexDataOffset + vertexCount * vertexStride <= indexDataOffset`.
/// 9. `indexDataOffset + indexCount * indexStride <= fileSize`.
/// 10. `totalSubmeshCount` equals the sum of all LOD `submeshCount` values.
///
/// # Example: Writing a simple mesh
/// ```cpp
/// uz::UzMeshHeader header;
/// header.lodCount = 1;
/// header.totalSubmeshCount = 1;
/// header.vertexCount = 3;
/// header.indexCount = 3;
/// header.vertexStride = sizeof(uz::VertexPosNormUV);  // 32
/// header.indexStride = 2;  // uint16 indices
///
/// uz::UzMeshLOD lod;
/// lod.lodLevel = 0;
/// lod.submeshOffset = 0;
/// lod.submeshCount = 1;
/// lod.screenHeightThreshold = 0.0f;
///
/// uz::UzMeshSubmesh submesh;
/// submesh.indexCount = 3;
///
/// auto blob = uz::WriteUzMesh(header, {lod}, {submesh}, vertexBytes, indexBytes);
/// ```
///
/// # Example: Reading and validating
/// ```cpp
/// auto result = uz::ReadUzMesh(fileBytes);
/// if (!result.success) { UZ_LOG(Asset, Error, "Bad mesh: {}", result.error); }
/// const auto& verts = result.file.vertexData;  // ready for GPU upload
/// ```

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Magic number for .uzmesh files: ASCII "UZMS" stored as little-endian uint32.
/// Byte sequence on disk: `0x55 0x5A 0x4D 0x53` → 'U' 'Z' 'M' 'S'.
static constexpr uint32_t kUzMeshMagic = 0x534D5A55;

/// Current .uzmesh format version. Increment when adding fields or changing
/// layout. Readers must reject files with `version > kUzMeshVersion`.
///
/// # Version History
/// - **v1**: Initial format — static meshes only.
/// - **v2**: Skeletal mesh support — adds `kUzMeshFlag_Skinned` flag. When set,
///   `reserved[0]` holds `skeletonPathLength` and a null-terminated skeleton
///   filepath string is appended after the index data region.
static constexpr uint32_t kUzMeshVersion = 2;

/// Mesh contains skinned vertex data (`SkinnedVertex`, 56 bytes/vertex) and
/// a skeleton filepath string appended after the index data region.
/// When this flag is set, `vertexStride` must be 56 and `reserved[0]` holds
/// the byte length of the skeleton path string (including null terminator).
static constexpr uint32_t kUzMeshFlag_Skinned = 0x1;

/// Alignment boundary (in bytes) for vertex and index data regions.
/// Matches GPU cache line size for zero-copy upload on most hardware.
static constexpr uint32_t kUzMeshDataAlignment = 64;

// ─────────────────────────────────────────────────────────────────────────────
// Binary Structs
// ─────────────────────────────────────────────────────────────────────────────

#pragma pack(push, 1)
/// .uzmesh file header — exactly 64 bytes, little-endian.
///
/// Immediately followed by `lodCount` LOD descriptors, then
/// `totalSubmeshCount` submesh descriptors, then aligned vertex and index data.
///
/// # Memory Layout (64 bytes)
/// ```
/// Offset  Size  Field
/// ──────  ────  ─────────────────────────────────────────
/// 0x00    4     magic              "UZMS" = 0x534D5A55
/// 0x04    4     version            1 or 2
/// 0x08    4     flags              bit 0 = kUzMeshFlag_Skinned
/// 0x0C    4     lodCount           [0, 256] typical
/// 0x10    4     totalSubmeshCount  sum of all LOD submeshCounts
/// 0x14    4     vertexCount        total vertices in buffer
/// 0x18    4     indexCount         total indices in buffer
/// 0x1C    4     vertexStride       bytes/vertex (must be > 0)
/// 0x20    4     indexStride        2 (uint16) or 4 (uint32)
/// 0x24    4     vertexDataOffset   64-byte aligned file offset
/// 0x28    4     indexDataOffset    64-byte aligned file offset
/// 0x2C    4     reserved[0]        v2: skeletonPathLength (incl. null)
/// 0x30    16    reserved[1..4]     must be zeros
/// ```
struct UzMeshHeader {
    uint32_t magic = kUzMeshMagic;          ///< Must be `kUzMeshMagic` (0x534D5A55, "UZMS"). First validation check.
    uint32_t version = kUzMeshVersion;      ///< Format version (currently 2). Reject if > kUzMeshVersion.
    uint32_t flags = 0;                     ///< Bit field: bit 0 = `kUzMeshFlag_Skinned`. Reject unknown bits.
    uint32_t lodCount = 0;                  ///< Number of LOD levels (0 is valid for empty mesh).
    uint32_t totalSubmeshCount = 0;         ///< Total submesh count across all LODs. Must equal sum of all LOD submeshCounts.
    uint32_t vertexCount = 0;              ///< Total vertex count in the vertex data region.
    uint32_t indexCount = 0;               ///< Total index count in the index data region.
    uint32_t vertexStride = 0;             ///< Bytes per vertex (e.g., 32 for VertexPosNormUV). Must be > 0 if vertexCount > 0.
    uint32_t indexStride = 0;              ///< Bytes per index: 2 (uint16) or 4 (uint32). Must be 2 or 4 if indexCount > 0.
    uint32_t vertexDataOffset = 0;         ///< Byte offset from file start to vertex data. Always 64-byte aligned.
    uint32_t indexDataOffset = 0;          ///< Byte offset from file start to index data. Always 64-byte aligned.
    uint32_t reserved[5] = {};             ///< `reserved[0]`: v2 skeleton path length (bytes, incl. null). `reserved[1..4]`: must be zero.
};
#pragma pack(pop)

static_assert(sizeof(UzMeshHeader) == 64, "UzMeshHeader must be exactly 64 bytes");

#pragma pack(push, 1)
/// Describes a single LOD level within the mesh.
///
/// Each LOD references a contiguous range of submeshes via offset and count
/// into the global submesh array that follows the LOD array in the file.
///
/// # Memory Layout (16 bytes)
/// ```
/// Offset  Size  Field
/// ──────  ────  ──────────────────────────────────────────
/// 0x00    4     lodLevel               0 = highest detail
/// 0x04    4     submeshOffset          index into submesh array
/// 0x08    4     submeshCount           submeshes in this LOD
/// 0x0C    4     screenHeightThreshold  float [0.0, 1.0]
/// ```
///
/// # Validation Rules
/// - `lodLevel` must equal the LOD's position in the LOD array (LODs[0].lodLevel == 0).
/// - `submeshOffset + submeshCount <= header.totalSubmeshCount`.
/// - LOD ranges must not overlap (each submesh belongs to exactly one LOD).
/// - `screenHeightThreshold` should decrease with increasing lodLevel
///   (LOD 0 = 0.0, LOD 1 = e.g. 0.5 meaning "use when object < 50% screen height").
struct UzMeshLOD {
    uint32_t lodLevel = 0;                ///< LOD index (0 = highest detail, N = coarsest).
    uint32_t submeshOffset = 0;           ///< Index of first submesh in the global submesh array.
    uint32_t submeshCount = 0;            ///< Number of submeshes belonging to this LOD.
    float screenHeightThreshold = 0.0f;   ///< Screen-space height ratio threshold. Use this LOD when
                                          ///< the object's screen height falls below this fraction.
                                          ///< LOD 0 should be 0.0 (always valid). Range: [0.0, 1.0].
};
#pragma pack(pop)

static_assert(sizeof(UzMeshLOD) == 16, "UzMeshLOD must be exactly 16 bytes");

#pragma pack(push, 1)
/// Describes a single submesh (draw call) within a LOD level.
///
/// Vertex and index offsets are element indices (not byte offsets) relative
/// to the start of the vertex/index data regions, not the file start.
/// Each submesh maps to exactly one `DrawIndexedInstanced` call.
///
/// # Memory Layout (16 bytes)
/// ```
/// Offset  Size  Field
/// ──────  ────  ─────────────────────────────────────
/// 0x00    4     vertexOffset    base vertex index
/// 0x04    4     indexOffset     first index in IB
/// 0x08    4     indexCount      indices to draw
/// 0x0C    4     materialIndex   material slot
/// ```
///
/// # Validation Rules
/// - `vertexOffset < header.vertexCount` (or 0 if vertexCount == 0).
/// - `indexOffset + indexCount <= header.indexCount`.
/// - `materialIndex` is an opaque slot — the renderer maps it to a material at load time.
struct UzMeshSubmesh {
    uint32_t vertexOffset = 0;            ///< Base vertex index added to each index value (DrawIndexedInstanced BaseVertexLocation).
    uint32_t indexOffset = 0;             ///< First index in the index buffer for this submesh.
    uint32_t indexCount = 0;              ///< Number of indices to draw for this submesh.
    uint32_t materialIndex = 0;           ///< Material slot index. Mapped to a runtime material by the renderer.
};
#pragma pack(pop)

static_assert(sizeof(UzMeshSubmesh) == 16, "UzMeshSubmesh must be exactly 16 bytes");

#pragma pack(push, 1)
/// Standard interleaved vertex layout: position + normal + UV.
///
/// 32 bytes per vertex, tightly packed with no internal padding.
/// Suitable for most opaque geometry. The stride (32) is a multiple of 4,
/// ensuring natural float alignment for GPU reads.
///
/// # Memory Layout (32 bytes)
/// ```
/// Offset  Size  Field
/// ──────  ────  ───────────────────────────────────
/// 0x00    12    position[3]   float x, y, z
/// 0x0C    12    normal[3]     float x, y, z
/// 0x18    8     uv[2]         float u, v
/// ```
///
/// # Notes
/// - Position is in object-space (model transform applied by vertex shader).
/// - Normal must be unit-length. The importer normalizes during conversion.
/// - UV origin is top-left (DirectX convention): u=0,v=0 is top-left corner.
struct VertexPosNormUV {
    float position[3];                    ///< Object-space position (x, y, z) in float32.
    float normal[3];                      ///< Unit normal vector (x, y, z). Must be normalized.
    float uv[2];                          ///< Texture coordinates (u, v). Top-left origin.
};
#pragma pack(pop)

static_assert(sizeof(VertexPosNormUV) == 32, "VertexPosNormUV must be exactly 32 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Serialization
// ─────────────────────────────────────────────────────────────────────────────

/// Serializes mesh data into .uzmesh binary format.
///
/// Produces a contiguous byte buffer with 64-byte aligned vertex and index
/// data regions, ready for direct GPU upload without additional copies.
///
/// # Arguments
/// * `header` - Pre-filled header (magic/version set automatically).
/// * `lods` - LOD level descriptors.
/// * `submeshes` - Submesh descriptors referenced by LODs.
/// * `vertexData` - Raw interleaved vertex bytes.
/// * `indexData` - Raw index bytes (uint16 or uint32).
/// * `skeletonPath` - Skeleton filepath for skinned meshes. When non-empty,
///   the writer sets `kUzMeshFlag_Skinned` in `header.flags` and stores the
///   null-terminated path after the index data region. Pass empty for static meshes.
///
/// # Returns
/// Complete .uzmesh file bytes. Vertex and index data regions are 64-byte
/// aligned. `header.vertexDataOffset` and `header.indexDataOffset` are
/// computed and written automatically.
[[nodiscard]] std::vector<uint8_t> WriteUzMesh(
    UzMeshHeader header,
    const std::vector<UzMeshLOD>& lods,
    const std::vector<UzMeshSubmesh>& submeshes,
    const std::vector<uint8_t>& vertexData,
    const std::vector<uint8_t>& indexData,
    const std::string& skeletonPath = {});

/// Deserialized contents of a .uzmesh file.
struct UzMeshFile {
    UzMeshHeader header;                  ///< Parsed 64-byte header
    std::vector<UzMeshLOD> lods;          ///< LOD level descriptors
    std::vector<UzMeshSubmesh> submeshes; ///< Submesh descriptors
    std::vector<uint8_t> vertexData;      ///< Raw vertex bytes
    std::vector<uint8_t> indexData;       ///< Raw index bytes
    std::string skeletonPath;             ///< Skeleton filepath (v2 skinned meshes). Empty for static meshes.
};

/// Result of parsing a .uzmesh binary blob.
struct UzMeshParseResult {
    bool success = false;                 ///< `true` if parsing completed without errors
    UzMeshFile file;                      ///< Parsed data (valid only if `success == true`)
    std::string error;                    ///< Human-readable error message (empty on success)
};

/// Parses a .uzmesh binary blob into its header, LODs, submeshes, and geometry.
///
/// Validates magic number, version, and data sizes before extracting fields.
///
/// # Arguments
/// * `data` - Raw bytes of a .uzmesh file (minimum 64 bytes for header).
///
/// # Returns
/// `UzMeshParseResult` with `success == true` and populated `file` on valid
/// input. `success == false` with `error` on invalid magic, wrong version,
/// or truncated data.
[[nodiscard]] UzMeshParseResult ReadUzMesh(const std::vector<uint8_t>& data);

} // namespace uz
