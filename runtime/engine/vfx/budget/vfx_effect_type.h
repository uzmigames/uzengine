#pragma once

/// Effect-type scalability asset (Niagara-parity `UNiagaraEffectType`).
///
/// Groups VFX effects by role ("Ambient", "Weapon", "Spell", "Cinematic")
/// and applies platform-wide scalability rules — max concurrent instances,
/// cull distance multiplier, LOD bias — per group instead of per preset.
///
/// # Why groups?
///
/// Without groups, scalability tuning is per-preset busywork. Weapon
/// effects need different caps than ambient fog; cinematic shots need to
/// bypass gameplay caps entirely. A 1-dollar `.uzvfxtype` asset captures
/// these intents once and every emitter referencing the type inherits
/// them automatically.
///
/// # Asset format (`.uzvfxtype`)
///
/// ```json
/// {
///   "format": "uzvfxtype",
///   "version": 1,
///   "name": "Weapon",
///   "max_concurrent_instances": 16,
///   "cull_distance_multiplier": 0.75,
///   "lod_bias": 1,
///   "priority": 2,
///   "platform_overrides": {
///     "mobile":  { "max_concurrent_instances": 4, "lod_bias": 2 },
///     "console": { "max_concurrent_instances": 12 }
///   }
/// }
/// ```
///
/// Missing fields default to the documented defaults on `VfxEffectType`.
/// Unknown platform keys log a warning and are skipped.
///
/// # Runtime enforcement
///
/// `VfxEffectTypeInstanceTracker` (same header) maintains a per-type
/// active-instance count with LRU eviction. When a new instance would
/// exceed `maxConcurrentInstances`, the oldest lower-or-equal-priority
/// instance is evicted so the new spawn wins.
///
/// @ref UE5 UNiagaraEffectType
/// @ref UE5 FNiagaraEffectTypeScalability

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class IVFS;

// ─────────────────────────────────────────────────────────────────────────────
// VfxEffectType — the asset struct.
// ─────────────────────────────────────────────────────────────────────────────

/// Per-platform scalability overrides. All fields optional; zeros mean
/// "inherit the parent VfxEffectType value" at load time.
struct VfxEffectTypePlatformOverride {
    /// Negative sentinel means "unset" — the parent `maxConcurrentInstances`
    /// is used verbatim at query time.
    int32_t maxConcurrentInstances = -1;

    /// -1 = unset.
    float   cullDistanceMultiplier = -1.0f;

    /// INT32_MIN = unset.
    int32_t lodBias = -0x7FFFFFFF - 1;  // INT32_MIN
};

/// Loaded `.uzvfxtype` asset.
struct VfxEffectType {
    /// Stable identifier (lowercase kebab-case by convention).
    std::string name;

    /// Cap on concurrent live instances across the whole scene. 0 = no cap.
    uint32_t maxConcurrentInstances = 0u;

    /// Multiplier applied to each referencing emitter's cull distance.
    /// Values < 1 cull more aggressively; > 1 keep instances visible
    /// further out. Default 1.0.
    float cullDistanceMultiplier = 1.0f;

    /// Shifts every emitter's starting LOD level by this amount. Positive
    /// values bias toward cheaper LODs. Default 0.
    int32_t lodBias = 0;

    /// Relative priority. When the instance cap is exceeded the lowest-
    /// priority existing instance is evicted first (ties break on age).
    /// Higher numbers win. Cinematic effects usually carry priority 100+.
    int32_t priority = 0;

    /// Optional platform-specific overrides keyed by platform slug
    /// (e.g. `"mobile"`, `"console"`, `"pc_low"`).
    std::unordered_map<std::string, VfxEffectTypePlatformOverride> platformOverrides;

    /// Resolved scalability snapshot for a given platform slug. Empty
    /// `platform` returns the baseline values; unknown slugs fall back
    /// to the baseline as well.
    struct Resolved {
        uint32_t maxConcurrentInstances;
        float    cullDistanceMultiplier;
        int32_t  lodBias;
    };
    [[nodiscard]] Resolved Resolve(const std::string& platform = {}) const;

    /// Loads a `.uzvfxtype` JSON file.
    [[nodiscard]] static std::unique_ptr<VfxEffectType> Load(const std::string& path,
                                                              IVFS* vfs = nullptr);

    /// Parses a JSON string directly (tests + editor).
    [[nodiscard]] static std::unique_ptr<VfxEffectType> LoadFromString(const std::string& jsonStr);

    /// Writes the asset to disk as `.uzvfxtype` JSON.
    [[nodiscard]] bool SaveToFile(const std::string& path) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// VfxEffectTypeInstanceTracker — runtime cap + LRU eviction.
// ─────────────────────────────────────────────────────────────────────────────

/// Tracks active VFX instances per effect type and recommends whether a
/// new instance should be admitted, rejected, or admitted after evicting
/// an existing one.
///
/// Not thread-safe. The sync pass that registers/unregisters VFX
/// components drives it on the main thread.
class VfxEffectTypeInstanceTracker {
public:
    /// One live instance entry. `userHandle` is opaque to the tracker —
    /// callers pass it back when evicting (typically an entity or runtime
    /// instance index).
    struct InstanceEntry {
        uint64_t userHandle = 0;
        int32_t  priority   = 0;
        /// Monotonically increasing age counter assigned at `TryAcquire`.
        uint64_t ageTick    = 0;
    };

    /// Result of a `TryAcquire` call.
    struct AcquireResult {
        enum class Status : uint8_t {
            /// The new instance fits under the cap — admit without evicting.
            Admitted,

            /// Cap was full; the LRU-lowest-priority instance in `evicted`
            /// must be shut down. The new instance takes its slot.
            AdmittedWithEviction,

            /// Cap was full and no existing instance has priority <= the
            /// new one — caller should reject the spawn.
            Rejected,
        };
        Status        status = Status::Rejected;
        InstanceEntry evicted{};  ///< Valid only when AdmittedWithEviction.
    };

    /// Attempts to admit a new instance belonging to `type`. The cap
    /// comes from the resolved platform view of `type`. `userHandle`
    /// must be unique across active entries of this type (behaviour
    /// undefined on duplicates).
    AcquireResult TryAcquire(const VfxEffectType& type,
                             uint64_t userHandle,
                             int32_t instancePriority,
                             const std::string& platform = {});

    /// Releases an instance previously admitted via `TryAcquire`. No-op
    /// when the handle is unknown.
    void Release(const std::string& typeName, uint64_t userHandle);

    /// Number of live instances for the named type.
    [[nodiscard]] uint32_t GetActiveCount(const std::string& typeName) const;

private:
    struct TypeState {
        std::vector<InstanceEntry> entries;
    };
    std::unordered_map<std::string, TypeState> m_byType;
    uint64_t m_nextAge = 1u;
};

} // namespace uz
