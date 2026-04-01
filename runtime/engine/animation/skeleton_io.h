/// UzEngine Skeleton I/O — Binary .uzskel format for skeleton serialization.
///
/// Provides load/save functions for the `.uzskel` binary format, both to disk
/// and to/from in-memory byte buffers. The format stores bone hierarchy, names,
/// and bind-pose transforms in a compact binary layout.
///
/// # File Format (.uzskel v1)
/// ```
/// Header (16 bytes):
///   magic:     uint32  — 0x4B535A55 ('UZSK' little-endian)
///   version:   uint32  — format version (currently 1)
///   boneCount: uint32  — number of bones
///   reserved:  uint32  — reserved for future use (must be 0)
///
/// Per bone (variable size):
///   nameLen:     uint16   — byte length of bone name (max 65535)
///   name:        char[]   — bone name (NOT null-terminated)
///   parentIndex: int16    — parent bone index (-1 for root)
///   translation: float[3] — bind-pose translation (x, y, z)
///   rotation:    float[4] — bind-pose rotation quaternion (x, y, z, w)
///   scale:       float[3] — bind-pose scale (x, y, z)
///
/// Total per-bone overhead: 2 + nameLen + 2 + 12 + 16 + 12 = 44 + nameLen bytes
/// ```
///
/// # Endianness
/// All multi-byte values are little-endian (native x86/x64).
///
/// # Limits
/// * Max 256 bones (kMaxBones).
/// * Max bone name length: 65535 bytes (uint16 length prefix).
/// * Parent index stored as int16: supports up to 32767 bones (well above kMaxBones).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace uz {

class Skeleton;

// ─────────────────────────────────────────────────────────────────────────────
// Format Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Magic number for .uzskel files: 'UZSK' as little-endian uint32.
static constexpr uint32_t kUzSkelMagic = 0x4B535A55;

/// Current .uzskel format version.
static constexpr uint32_t kUzSkelVersion = 1;

/// Size of the .uzskel file header in bytes.
static constexpr uint32_t kUzSkelHeaderSize = 16;

// ─────────────────────────────────────────────────────────────────────────────
// Disk I/O
// ─────────────────────────────────────────────────────────────────────────────

/// Saves a skeleton to a .uzskel binary file.
///
/// The skeleton does not need to have inverse bind matrices computed — only
/// the bone hierarchy and bind-pose transforms are serialized. On load,
/// the caller should call `ComputeInverseBindMatrices()` to regenerate them.
///
/// # Arguments
/// * `skeleton` — Skeleton to serialize.
/// * `path` — Output file path (parent directory must exist).
///
/// # Returns
/// `true` on success, `false` on I/O error (logged via UZ_LOG).
[[nodiscard]] bool SaveSkeleton(const Skeleton& skeleton, const std::string& path);

/// Loads a skeleton from a .uzskel binary file.
///
/// On success, the returned skeleton has `BuildNameIndex()` and
/// `ComputeInverseBindMatrices()` already called — it is ready for use.
///
/// # Arguments
/// * `path` — Path to the .uzskel file.
///
/// # Returns
/// Loaded skeleton, or `std::nullopt` on error (logged via UZ_LOG).
[[nodiscard]] std::optional<Skeleton> LoadSkeleton(const std::string& path);

// ─────────────────────────────────────────────────────────────────────────────
// In-Memory I/O
// ─────────────────────────────────────────────────────────────────────────────

/// Serializes a skeleton to an in-memory byte buffer.
///
/// Produces the same binary format as `SaveSkeleton()` but returns bytes
/// instead of writing to disk. Useful for network transfer, embedding in
/// other formats, or testing without file I/O.
///
/// # Arguments
/// * `skeleton` — Skeleton to serialize.
///
/// # Returns
/// Byte buffer containing the .uzskel binary data.
[[nodiscard]] std::vector<uint8_t> WriteSkeleton(const Skeleton& skeleton);

/// Deserializes a skeleton from an in-memory byte buffer.
///
/// On success, the returned skeleton has `BuildNameIndex()` and
/// `ComputeInverseBindMatrices()` already called.
///
/// # Arguments
/// * `data` — Byte buffer containing .uzskel binary data.
///
/// # Returns
/// Deserialized skeleton, or `std::nullopt` on error (logged via UZ_LOG).
[[nodiscard]] std::optional<Skeleton> ReadSkeleton(const std::vector<uint8_t>& data);

} // namespace uz
