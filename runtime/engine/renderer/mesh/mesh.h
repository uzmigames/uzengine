/// UzEngine Mesh Format (.uzmesh)
///
/// Binary mesh format for static geometry. Designed for fast GPU upload
/// with minimal parsing overhead. The file is a flat binary blob with
/// a fixed-size header followed by vertex and index data.
///
/// # File Layout
/// ```
/// [MeshHeader]        — 128 bytes, fixed
/// [vertex data]       — header.vertexCount * header.vertexStride bytes
/// [index data]        — header.indexCount * (2 or 4) bytes
/// ```
///
/// # Vertex Formats
/// - `Vertex` (48 bytes): position + normal + texcoord + tangent (PBR meshes)
/// - `SimpleVertex` (36 bytes): position + color + texcoord (debug/UI)
///
/// # Index Formats
/// - `uint16_t` for meshes with < 65536 vertices
/// - `uint32_t` for larger meshes
///
/// # Coordinate System
/// Left-handed, X-forward, Y-right, Z-up (matches UzEngine convention).

#pragma once

#include "vector3.h"
#include "vertex.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MeshHeader — Fixed-size file header (128 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Magic bytes identifying a .uzmesh file: "UZMH" (UzMesh Header).
static constexpr uint32_t kMeshMagic = 0x484D5A55; // "UZMH" little-endian

/// Current .uzmesh format version.
static constexpr uint32_t kMeshVersion = 2; ///< v2: multi-LOD support (lodCount + lods[3] in header)

/// Vertex format identifier stored in the header.
enum class MeshVertexFormat : uint32_t {
    Standard = 0,   ///< `Vertex` (48 bytes): position, normal, texcoord, tangent
    Simple   = 1,   ///< `SimpleVertex` (36 bytes): position, color, texcoord
};

/// Index format identifier stored in the header.
enum class MeshIndexFormat : uint32_t {
    UInt16 = 0,     ///< 16-bit indices (max 65535 vertices)
    UInt32 = 1,     ///< 32-bit indices (unlimited)
};

/// Fixed-size binary header at the start of every .uzmesh file.
///
/// Padded to 128 bytes for alignment. All multi-byte values are little-endian.
/// The header is followed immediately by vertex data, then index data.
struct MeshHeader {
    uint32_t         magic;            ///< Must be `kMeshMagic` ("UZMH")
    uint32_t         version;          ///< Format version (currently 1)
    MeshVertexFormat vertexFormat;     ///< Which vertex struct is used
    uint32_t         vertexStride;     ///< Bytes per vertex (e.g. 48 for Vertex)
    uint32_t         vertexCount;      ///< Number of vertices
    MeshIndexFormat  indexFormat;      ///< UInt16 or UInt32
    uint32_t         indexCount;       ///< Number of indices (0 = non-indexed)
    uint32_t         submeshCount;     ///< Number of submeshes (0 or 1 for now)

    // Bounding box (axis-aligned, object space)
    Vec3             boundsMin;        ///< AABB minimum corner
    Vec3             boundsMax;        ///< AABB maximum corner

    // Data offsets (relative to file start)
    uint64_t         vertexDataOffset; ///< Byte offset to vertex data
    uint64_t         indexDataOffset;  ///< Byte offset to index data (LOD0)

    // ── LOD data (v2+) ──
    // LOD0 uses indexDataOffset + indexCount above.
    // LOD1..3 use these fields. All LOD indices are concatenated after LOD0.
    // Unused LODs have indexCount = 0.
    uint32_t         lodCount;         ///< Number of LOD levels (1 = no extra LODs)
    struct {
        uint32_t indexOffset;          ///< Byte offset from indexDataOffset to this LOD's indices
        uint32_t indexCount;           ///< Number of indices for this LOD
    } lods[3];                         ///< LOD1, LOD2, LOD3 descriptors

    uint8_t          reserved[28];     ///< Reserved for future use, must be zero
};

static_assert(sizeof(MeshHeader) == 128, "MeshHeader must be exactly 128 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Mesh — Runtime mesh data (CPU side)
// ─────────────────────────────────────────────────────────────────────────────

/// Describes a contiguous range of indices within a mesh that share a material.
///
/// Each submesh maps to a single draw call (`DrawIndexedInstanced`).
/// The `startIndex` and `indexCount` refer to the global index buffer.
/// `materialIndex` is an opaque slot — the renderer maps it to a material
/// at load time.
struct SubMesh {
    uint32_t startIndex = 0;       ///< First index in the index buffer
    uint32_t indexCount = 0;       ///< Number of indices to draw
    uint32_t materialIndex = 0;    ///< Material slot index
};

/// CPU-side mesh data loaded from a .uzmesh file or created programmatically.
///
/// Contains vertex and index buffers as raw byte arrays, plus metadata
/// from the header. Use `MeshLoader` to populate from disk, or construct
/// manually for procedural geometry.
struct Mesh {
    MeshHeader             header;       ///< Parsed file header
    std::vector<uint8_t>   vertexData;   ///< Raw vertex bytes (48 B Vertex stride, bind-pose for skinned)
    std::vector<uint8_t>   indexData;    ///< Raw index bytes
    std::vector<SubMesh>   submeshes;    ///< Submesh ranges (empty = single draw call for entire mesh)

    // ── Skinning data (set only when the source asset is UZMS/skinned) ──────
    // `skinnedVertexData` contains the original 56 B SkinnedVertex[] stream,
    // preserved for GPU skinning (consumed by gpu_skinning_cs.hlsl). When
    // present, `skeletonPath` names the .uzskel asset that goes with this mesh.
    // The static `vertexData` stream above is still the stripped 48 B bind-pose
    // copy — used as the bind-pose fallback when the mesh is rendered without
    // an animation controller (e.g., in the editor before controllers run).
    std::vector<uint8_t>   skinnedVertexData; ///< 56 B SkinnedVertex[], empty if unskinned.
    std::string            skeletonPath;      ///< VFS path to .uzskel, empty if unskinned.

    /// Returns vertex count from header.
    [[nodiscard]] uint32_t GetVertexCount() const { return header.vertexCount; }

    /// Returns index count from header (0 = non-indexed).
    [[nodiscard]] uint32_t GetIndexCount() const { return header.indexCount; }

    /// Returns total vertex data size in bytes.
    [[nodiscard]] uint64_t GetVertexDataSize() const {
        return static_cast<uint64_t>(header.vertexCount) * header.vertexStride;
    }

    /// Returns total index data size in bytes (all LODs combined).
    [[nodiscard]] uint64_t GetIndexDataSize() const {
        uint32_t indexSize = (header.indexFormat == MeshIndexFormat::UInt32) ? 4 : 2;
        uint64_t total = static_cast<uint64_t>(header.indexCount) * indexSize;
        for (uint32_t i = 0; i < 3 && i + 1 < header.lodCount; ++i) {
            total += static_cast<uint64_t>(header.lods[i].indexCount) * indexSize;
        }
        return total;
    }

    /// Returns the number of LOD levels (1 = LOD0 only, up to 4).
    [[nodiscard]] uint32_t GetLODCount() const {
        return header.lodCount > 0 ? header.lodCount : 1;
    }

    /// Returns index count for a specific LOD level.
    [[nodiscard]] uint32_t GetLODIndexCount(uint32_t lod) const {
        if (lod == 0) return header.indexCount;
        if (lod <= 3 && lod < header.lodCount) return header.lods[lod - 1].indexCount;
        return 0;
    }

    /// Returns byte offset (relative to indexData start) for a specific LOD's indices.
    [[nodiscard]] uint32_t GetLODIndexOffset(uint32_t lod) const {
        if (lod == 0) return 0;
        if (lod <= 3 && lod < header.lodCount) return header.lods[lod - 1].indexOffset;
        return 0;
    }

    /// Returns true if the mesh has valid data.
    [[nodiscard]] bool IsValid() const {
        return header.magic == kMeshMagic
            && header.vertexCount > 0
            && !vertexData.empty();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// MeshLoader — Load .uzmesh files from disk
// ─────────────────────────────────────────────────────────────────────────────

/// Loads a .uzmesh binary file into a `Mesh` struct.
///
/// # Arguments
/// * `path` - Filesystem path to the .uzmesh file.
///
/// # Returns
/// Populated `Mesh` on success, empty `Mesh` (magic=0) on failure.
///
/// # Errors
/// Logs `Renderer::Error` on:
/// - File not found or unreadable
/// - Invalid magic bytes
/// - Unsupported version
/// - Truncated file (vertex/index data shorter than header claims)
Mesh LoadMesh(const std::string& path);

/// Loads a .uzmesh from an in-memory byte buffer.
///
/// Same as `LoadMesh(path)` but reads from already-loaded bytes (e.g., from VFS).
/// The `name` argument is used only for log messages.
Mesh LoadMeshFromMemory(const std::vector<uint8_t>& data, const std::string& name);

/// Saves a `Mesh` to a .uzmesh binary file.
///
/// # Arguments
/// * `path` - Filesystem path to write.
/// * `mesh` - Mesh data to save. Must have valid header and data.
///
/// # Returns
/// `true` on success, `false` on I/O error.
///
/// # Errors
/// Logs `Renderer::Error` on write failure.
bool SaveMesh(const std::string& path, const Mesh& mesh);

// ─────────────────────────────────────────────────────────────────────────────
// Procedural mesh generators — convenience free functions
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a UV sphere mesh with the given subdivision counts.
///
/// Vertices use the Standard format (position, normal, texcoord, tangent).
/// The sphere has radius 1.0 centered at origin.
/// Coordinate system: Z-up, X-forward, Y-right.
///
/// # Arguments
/// * `slices` — Number of longitude divisions. Default 32.
/// * `stacks` — Number of latitude divisions. Default 16.
///
/// # Returns
/// A valid `Mesh` with Standard vertices and UInt16 indices (for slices=32,
/// stacks=16 the result is 578 vertices and 3072 indices).
[[nodiscard]] Mesh GenerateSphereMesh(uint32_t sectorCount = 64, uint32_t stackCount = 32);

/// Generates a flat quad (plane) mesh lying in the XY plane (Z=0).
///
/// Uses the Standard vertex format. Normal points up (+Z). The quad spans
/// [-1, 1] in X and Y. UV coordinates range from [0, uvScale] to allow
/// tiled texturing without requiring a separate sampler or material param.
///
/// # Arguments
/// * `uvScale` — UV coordinate range. Default 1.0. Pass 8.0 for 8x8 tiling.
///
/// # Returns
/// A `Mesh` with 4 vertices and 6 indices (two CCW triangles).
[[nodiscard]] Mesh GeneratePlaneMesh(float uvScale = 1.0f);

/// Generates a cone mesh with apex at (0,0,height) and base circle at Z=0.
///
/// Uses the Standard vertex format. The cone side surface has smooth normals
/// computed from the cone half-angle. The base cap uses separate vertices with
/// normals pointing down (-Z). UVs are cylindrical for the sides and planar
/// for the cap.
///
/// # Arguments
/// * `radius` — Radius of the base circle. Default 50.0 (cm).
/// * `height` — Height from base to apex. Default 100.0 (cm).
/// * `segments` — Number of divisions around the circumference. Default 32.
///
/// # Returns
/// A valid `Mesh` with Standard vertices and UInt32 indices.
[[nodiscard]] Mesh GenerateConeMesh(float radius = 50.0f, float height = 100.0f,
                                     uint32_t segments = 32);

/// Generates a cylinder mesh centered at origin, extending from Z=-height/2
/// to Z=+height/2.
///
/// Uses the Standard vertex format. The side surface has smooth normals
/// pointing radially outward. Top and bottom caps have separate vertices with
/// flat normals (+Z and -Z respectively). UVs are cylindrical for sides and
/// planar for caps.
///
/// # Arguments
/// * `radius` — Radius of the cylinder. Default 50.0 (cm).
/// * `height` — Total height of the cylinder. Default 100.0 (cm).
/// * `segments` — Number of divisions around the circumference. Default 32.
///
/// # Returns
/// A valid `Mesh` with Standard vertices and UInt32 indices.
[[nodiscard]] Mesh GenerateCylinderMesh(float radius = 50.0f, float height = 100.0f,
                                         uint32_t segments = 32);

/// Generates a torus mesh centered at origin with the ring in the XY plane.
///
/// Uses the Standard vertex format. The torus is the surface of revolution
/// of a circle of `minorRadius` around a circle of `majorRadius` in the XY
/// plane. Normals are computed analytically from partial derivatives of the
/// parametric surface. UV u wraps around the major circle, v around the minor.
///
/// # Arguments
/// * `majorRadius` — Distance from the center of the torus to the center
///                   of the tube. Default 100.0 (cm).
/// * `minorRadius` — Radius of the tube. Default 30.0 (cm).
/// * `majorSegments` — Divisions around the major circle. Default 32.
/// * `minorSegments` — Divisions around the minor circle (tube). Default 16.
///
/// # Returns
/// A valid `Mesh` with Standard vertices and UInt32 indices.
[[nodiscard]] Mesh GenerateTorusMesh(float majorRadius = 100.0f, float minorRadius = 30.0f,
                                      uint32_t majorSegments = 32, uint32_t minorSegments = 16);

// ─────────────────────────────────────────────────────────────────────────────
// Tangent Generation — shared utility
// ─────────────────────────────────────────────────────────────────────────────

/// Generates tangent vectors for a triangle mesh using UV-space edge vectors.
///
/// For each triangle, computes the tangent and bitangent from the edge vectors
/// and UV deltas, then accumulates per-vertex tangents (weighted by triangle
/// contribution). After accumulation, tangents are orthogonalized against the
/// vertex normal via Gram-Schmidt and normalized. The bitangent sign is stored
/// in `tangent.w` (+1 or -1) so the shader can reconstruct the bitangent as
/// `cross(normal, tangent.xyz) * tangent.w`.
///
/// Handles degenerate triangles (zero-area or zero-UV-area) gracefully by
/// skipping them. Vertices that receive no tangent contribution (isolated or
/// all-degenerate) get a fallback tangent perpendicular to their normal.
///
/// # Arguments
/// * `vertices` - Vertex array with valid positions, normals, and texcoords.
///                Tangent fields are overwritten.
/// * `indices` - Triangle index buffer (size must be a multiple of 3).
void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

// ─────────────────────────────────────────────────────────────────────────────
// MeshBuilder — Programmatic mesh construction
// ─────────────────────────────────────────────────────────────────────────────

/// Helper for building meshes from vertex/index arrays.
///
/// Usage:
/// ```cpp
/// auto mesh = MeshBuilder::CreateCube(1.0f);
/// auto mesh = MeshBuilder::CreateFromVertices(vertices, indices);
/// ```
struct MeshBuilder {
    /// Creates a unit cube centered at origin with bottom at Z=0.
    ///
    /// Uses `Vertex` format with normals, UVs, and tangents.
    /// Each face is subdivided into a grid of quads for per-vertex
    /// lighting and displacement fidelity.
    ///
    /// # Arguments
    /// * `size` - Half-extent of the cube (default 1.0).
    /// * `subdivisions` - Quads per edge per face (default 1 = 4 verts/face,
    ///   8 = 64 quads/face = 81 verts/face). Clamped to [1, 256].
    static Mesh CreateCube(float size = 1.0f, uint32_t subdivisions = 1);

    /// Creates a UV sphere centered at origin with the given radius.
    ///
    /// Uses `Vertex` format with normals, UVs (spherical mapping), and tangents.
    /// CCW winding order (matches `frontCounterClockwise = true`, DX12 standard).
    ///
    /// # Arguments
    /// * `radius` - Sphere radius (default 1.0).
    /// * `stacks` - Number of latitude divisions (default 32).
    /// * `slices` - Number of longitude divisions (default 64).
    static Mesh CreateSphere(float radius = 1.0f, uint32_t stacks = 32, uint32_t slices = 64);

    /// Creates a mesh from explicit vertex and index arrays.
    ///
    /// # Arguments
    /// * `vertices` - Array of `Vertex` structs.
    /// * `indices` - Array of `uint32_t` indices. Empty = non-indexed.
    static Mesh CreateFromVertices(const std::vector<Vertex>& vertices,
                                   const std::vector<uint32_t>& indices);

    /// Creates a mesh from SimpleVertex arrays (debug/UI geometry).
    static Mesh CreateFromSimpleVertices(const std::vector<SimpleVertex>& vertices,
                                         const std::vector<uint32_t>& indices);
};

} // namespace uz
