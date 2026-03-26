/// UzEngine .uzsdf Binary Format — Sparse Signed Distance Field Sidecar
///
/// Stores a sparse signed distance field generated from a mesh at import time.
/// Created alongside the `.uzmesh` file, this sidecar contains brick data,
/// indirection tables, and MIP levels needed for GPU distance field shadow
/// cone tracing.
///
/// # Binary Layout (little-endian)
/// ```
/// Offset   Size                              Section
/// ──────── ───────────────────────────────── ─────────────────────────────────
/// 0x0000   64 bytes                          [Header] UzSdfHeader
///          mipLevels * 16 bytes              [MipDescs] UzSdfMipDesc array
///          padding to 64-byte boundary
///          --- For each MIP level (0..mipLevels-1) ---
///          indirectionCount * 4 bytes         [Indirection] uint32 array
///          brickCount * 512 bytes             [Bricks] SdfBrick data
/// ```
///
/// # Validation Rules
/// 1. File size >= 64 bytes.
/// 2. `magic == 0x46445A55` ("UZDF" in LE).
/// 3. `version == 1`.
/// 4. MIP 0 must have non-zero resolution and brick count.
///
/// # Endianness
/// All multi-byte fields are little-endian (native x86/ARM LE).

#pragma once

#include "../../../engine/renderer/shadows/sdf_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Magic number for .uzsdf files: ASCII "UZDF" stored as little-endian uint32.
/// Byte sequence on disk: `0x55 0x5A 0x44 0x46` → 'U' 'Z' 'D' 'F'.
static constexpr uint32_t kUzSdfMagic = 0x46445A55;

/// Current .uzsdf format version.
static constexpr uint32_t kUzSdfVersion = 1;

// ─────────────────────────────────────────────────────────────────────────────
// Binary Structs
// ─────────────────────────────────────────────────────────────────────────────

#pragma pack(push, 1)
/// .uzsdf file header — exactly 64 bytes, little-endian.
struct UzSdfHeader {
    uint32_t magic   = kUzSdfMagic;      ///< Must be kUzSdfMagic.
    uint32_t version = kUzSdfVersion;    ///< Format version (currently 1).
    uint32_t flags   = 0;               ///< Reserved, must be 0.

    /// Local-space AABB of the SDF volume (cm).
    float localBoundsMin[3] = {};
    float localBoundsMax[3] = {};

    float    voxelSize  = 0.0f;          ///< World-space voxel size (cm).
    uint32_t mipLevels  = 0;             ///< Total MIP levels (1 = base only, 4 = base + 3 MIPs).

    uint32_t reserved[5] = {};           ///< Reserved for future use, must be 0.
};
#pragma pack(pop)

static_assert(sizeof(UzSdfHeader) == 64, "UzSdfHeader must be exactly 64 bytes");

#pragma pack(push, 1)
/// Describes a single MIP level in the .uzsdf file.
///
/// Each MIP level has its own brick resolution and brick/indirection data
/// stored contiguously after the header + MIP descriptors.
struct UzSdfMipDesc {
    uint32_t resolutionX   = 0;   ///< Brick resolution X for this MIP.
    uint32_t resolutionY   = 0;   ///< Brick resolution Y for this MIP.
    uint32_t resolutionZ   = 0;   ///< Brick resolution Z for this MIP.
    uint32_t brickCount    = 0;   ///< Number of surface bricks stored for this MIP.
};
#pragma pack(pop)

static_assert(sizeof(UzSdfMipDesc) == 16, "UzSdfMipDesc must be exactly 16 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// Serialization
// ─────────────────────────────────────────────────────────────────────────────

/// Serializes a MeshSdfAsset into .uzsdf binary format.
///
/// # Arguments
/// * `asset` — The SDF asset to serialize. Must be valid.
///
/// # Returns
/// Complete .uzsdf file bytes on success, empty vector on failure.
[[nodiscard]] std::vector<uint8_t> WriteUzSdf(const MeshSdfAsset& asset);

/// Result of parsing a .uzsdf binary blob.
struct UzSdfParseResult {
    bool success = false;
    MeshSdfAsset asset;
    std::string error;
};

/// Parses a .uzsdf binary blob into a MeshSdfAsset.
///
/// Validates magic, version, and data sizes before extracting fields.
///
/// # Arguments
/// * `data` — Raw bytes of a .uzsdf file (minimum 64 bytes for header).
///
/// # Returns
/// `UzSdfParseResult` with `success == true` and populated `asset` on valid input.
[[nodiscard]] UzSdfParseResult ReadUzSdf(const std::vector<uint8_t>& data);

} // namespace uz
