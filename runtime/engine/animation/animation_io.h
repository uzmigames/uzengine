/// UzEngine Animation I/O — Binary .uzanim format for animation clip serialization.
///
/// Provides load/save functions for the `.uzanim` binary format. The format
/// stores an AnimationClip (metadata + per-bone channels + keyframe arrays)
/// in a compact little-endian layout.
///
/// # File Format (.uzanim v1)
/// ```
/// Header (32 bytes):
///   magic:        uint32  — 0x4D4E415A ('ZANM' little-endian)
///   version:      uint32  — format version (currently 1)
///   duration:     float   — clip duration in seconds
///   sampleRate:   float   — original DCC FPS (informational)
///   channelCount: uint32  — number of bone channels
///   flags:        uint32  — bit 0 = loop, bits 1-31 reserved
///   nameLen:      uint16  — length of clip name in bytes
///   skelNameLen:  uint16  — length of skeleton name in bytes
///   reserved:     uint32  — reserved (must be 0)
///
/// After header:
///   clipName:     char[nameLen]      — clip name, NOT null-terminated
///   skeletonName: char[skelNameLen]  — skeleton name, NOT null-terminated
///
/// Per channel (variable):
///   boneNameLen: uint16  — byte length of bone name
///   boneName:    char[]  — bone name, NOT null-terminated
///   posCount:    uint32  — number of position keyframes
///   rotCount:    uint32  — number of rotation keyframes
///   sclCount:    uint32  — number of scale keyframes
///   posKeys:     { float time; float value[3]; } × posCount  (16 B each)
///   rotKeys:     { float time; float value[4]; } × rotCount  (20 B each)
///   sclKeys:     { float time; float value[3]; } × sclCount  (16 B each)
/// ```
///
/// # Endianness
/// All multi-byte values are little-endian (native x86/x64).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace uz {

class AnimationClip;

// ─────────────────────────────────────────────────────────────────────────────
// Format Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Magic number for .uzanim files: 'ZANM' as little-endian uint32.
static constexpr uint32_t kUzAnimMagic = 0x4D4E415A;

/// Current .uzanim format version.
static constexpr uint32_t kUzAnimVersion = 1;

/// Size of the .uzanim file header in bytes.
static constexpr uint32_t kUzAnimHeaderSize = 32;

// ─────────────────────────────────────────────────────────────────────────────
// Disk I/O
// ─────────────────────────────────────────────────────────────────────────────

/// Saves an animation clip to a .uzanim binary file.
///
/// # Arguments
/// * `clip` — Clip to serialize.
/// * `path` — Output file path (parent directory must exist).
///
/// # Returns
/// `true` on success, `false` on I/O error (logged via UZ_LOG).
[[nodiscard]] bool SaveAnimation(const AnimationClip& clip, const std::string& path);

/// Loads an animation clip from a .uzanim binary file.
///
/// # Arguments
/// * `path` — Path to the .uzanim file.
///
/// # Returns
/// Loaded clip, or `std::nullopt` on error (logged via UZ_LOG).
[[nodiscard]] std::optional<AnimationClip> LoadAnimation(const std::string& path);

// ─────────────────────────────────────────────────────────────────────────────
// In-Memory I/O
// ─────────────────────────────────────────────────────────────────────────────

/// Serializes a clip to an in-memory byte buffer (same layout as SaveAnimation).
[[nodiscard]] std::vector<uint8_t> WriteAnimation(const AnimationClip& clip);

/// Deserializes a clip from an in-memory byte buffer.
[[nodiscard]] std::optional<AnimationClip> ReadAnimation(const std::vector<uint8_t>& data);

} // namespace uz
