#pragma once

/// VFX emitter snapshot — capture full emitter state to disk for offline
/// inspection.
///
/// Debugging a misbehaving effect is painful without a reproducible
/// capture: the author sees the bug on-screen for a frame and cannot
/// replay the exact particle state. The snapshot tool captures every
/// SoA channel plus RNG + wall-clock metadata into a `.uzvfxsnap`
/// binary that an inspector reads offline.
///
/// # File format (`.uzvfxsnap`)
///
/// ```
/// ┌─────────────────────────────────────────┐
/// │ VfxSnapshotHeader  (128 B, fixed)       │
/// │   magic[8]       = "UZVFXSNP"           │
/// │   version        = uint32               │
/// │   headerSize     = uint32 (= 128)       │
/// │   frameIndex     = uint64               │
/// │   wallClockNs    = uint64               │
/// │   rngSeed        = uint64               │
/// │   capacity       = uint32               │
/// │   aliveCount     = uint32               │
/// │   channelCount   = uint32               │
/// │   flags          = uint32 (reserved)    │
/// │   emitterName    = char[64] (null-pad)  │
/// ├─────────────────────────────────────────┤
/// │ VfxSnapshotChannel[channelCount]        │
/// │   name[32]       = char (null-pad)      │
/// │   elementType    = uint32 (enum)        │
/// │   elementCount   = uint32 (capacity)    │
/// │   byteOffset     = uint64 (from file start) │
/// │   byteLength     = uint64                    │
/// ├─────────────────────────────────────────┤
/// │ channel data (packed, no alignment gaps)│
/// └─────────────────────────────────────────┘
/// ```
///
/// All integers are little-endian; floats are IEEE 754 binary32 /
/// binary16. Channel data follows the channel table in the same order.
///
/// # Thread safety
/// Not thread-safe. Capture from the render / sync thread; inspection
/// is single-threaded.
///
/// @ref UE5 FNiagaraSnapshotBuilder
/// @ref docs/engine/25-vfx/36-emitter-snapshot.md

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Element types
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxSnapshotElementType : uint32_t {
    Float   = 0,
    Float2  = 1,
    Float3  = 2,
    Float4  = 3,
    Uint32  = 4,
    Int32   = 5,
    Half    = 6,
    Bool    = 7,
};

/// Returns the byte size of one element of `type`. 0 for unknown types.
[[nodiscard]] uint32_t VfxSnapshotElementSize(VfxSnapshotElementType type) noexcept;

/// Returns a static string for `type`.
[[nodiscard]] const char* VfxSnapshotElementTypeName(VfxSnapshotElementType type) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Channel + header
// ─────────────────────────────────────────────────────────────────────────────

/// One SoA channel of snapshot data.
struct VfxSnapshotChannel {
    std::string name;                            ///< Channel name (<= 31 chars).
    VfxSnapshotElementType elementType = VfxSnapshotElementType::Float;
    uint32_t elementCount = 0u;                  ///< Number of elements (= capacity).
    std::vector<uint8_t> data;                   ///< Raw bytes; size = elementCount * elementSize.
};

/// Snapshot header metadata.
struct VfxSnapshotHeader {
    static constexpr const char kMagic[8] = {'U','Z','V','F','X','S','N','P'};
    static constexpr uint32_t kVersion    = 1u;

    uint64_t frameIndex   = 0u;
    uint64_t wallClockNs  = 0u;
    uint64_t rngSeed      = 0u;
    uint32_t capacity     = 0u;
    uint32_t aliveCount   = 0u;
    std::string emitterName;   ///< <= 63 chars; longer names are truncated at Save.
};

/// Full in-memory snapshot.
struct VfxSnapshot {
    VfxSnapshotHeader              header;
    std::vector<VfxSnapshotChannel> channels;

    /// Returns true when the snapshot's on-disk layout is valid:
    ///   - emitter name ≤ 63 chars;
    ///   - every channel name ≤ 31 chars, non-empty;
    ///   - every channel's data size = elementCount * elementSize(type);
    ///   - aliveCount ≤ capacity;
    ///   - no duplicate channel names.
    [[nodiscard]] bool IsValid() const noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// Save / Load
// ─────────────────────────────────────────────────────────────────────────────

/// Serialises `snapshot` to bytes in the `.uzvfxsnap` format.
///
/// Returns an empty vector when the snapshot is invalid (see IsValid).
[[nodiscard]] std::vector<uint8_t> VfxSnapshotSaveToBytes(const VfxSnapshot& snapshot);

/// Parses `bytes` into a `VfxSnapshot`.
///
/// Returns true on success. On failure `outSnapshot` is left in a
/// default-constructed state and `outError` is populated.
[[nodiscard]] bool VfxSnapshotLoadFromBytes(const std::vector<uint8_t>& bytes,
                                              VfxSnapshot& outSnapshot,
                                              std::string& outError);

/// Saves the snapshot to disk at `path`. Returns true on success.
[[nodiscard]] bool VfxSnapshotSaveToFile(const VfxSnapshot& snapshot,
                                           const std::string& path);

/// Loads a snapshot from disk at `path`. Returns true on success.
[[nodiscard]] bool VfxSnapshotLoadFromFile(const std::string& path,
                                             VfxSnapshot& outSnapshot,
                                             std::string& outError);

// ─────────────────────────────────────────────────────────────────────────────
// Stdout dump
// ─────────────────────────────────────────────────────────────────────────────

/// Produces a human-readable dump of the snapshot suitable for stdout /
/// log output. Format:
///
/// ```
/// VfxSnapshot: emitter="fireball_head" frame=12345 alive=3/1024 rngSeed=...
///   channel positions    Float3  1024 elems
///   channel velocities   Float3  1024 elems
///   channel ages         Float   1024 elems
/// ```
[[nodiscard]] std::string VfxSnapshotDump(const VfxSnapshot& snapshot);

} // namespace uz
