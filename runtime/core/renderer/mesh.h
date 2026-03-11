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

#include "engine_math.h"
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
static constexpr uint32_t kMeshVersion = 1;

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
    uint64_t         indexDataOffset;  ///< Byte offset to index data

    uint8_t          reserved[56];     ///< Reserved for future use, must be zero
};

static_assert(sizeof(MeshHeader) == 128, "MeshHeader must be exactly 128 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Mesh — Runtime mesh data (CPU side)
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side mesh data loaded from a .uzmesh file or created programmatically.
///
/// Contains vertex and index buffers as raw byte arrays, plus metadata
/// from the header. Use `MeshLoader` to populate from disk, or construct
/// manually for procedural geometry.
struct Mesh {
    MeshHeader             header;       ///< Parsed file header
    std::vector<uint8_t>   vertexData;   ///< Raw vertex bytes
    std::vector<uint8_t>   indexData;    ///< Raw index bytes

    /// Returns vertex count from header.
    [[nodiscard]] uint32_t GetVertexCount() const { return header.vertexCount; }

    /// Returns index count from header (0 = non-indexed).
    [[nodiscard]] uint32_t GetIndexCount() const { return header.indexCount; }

    /// Returns total vertex data size in bytes.
    [[nodiscard]] uint64_t GetVertexDataSize() const {
        return static_cast<uint64_t>(header.vertexCount) * header.vertexStride;
    }

    /// Returns total index data size in bytes.
    [[nodiscard]] uint64_t GetIndexDataSize() const {
        uint32_t indexSize = (header.indexFormat == MeshIndexFormat::UInt32) ? 4 : 2;
        return static_cast<uint64_t>(header.indexCount) * indexSize;
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
    /// Each face has 4 unique vertices (24 total) for correct normals.
    ///
    /// # Arguments
    /// * `size` - Half-extent of the cube (default 1.0).
    static Mesh CreateCube(float size = 1.0f);

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
