#pragma once

/// VFX trail-decal emitter — swept decal emission along a particle path.
///
/// Particle death decals place a single decal at the death position; trail
/// decals spawn a stream of decals along the particle's path — tire marks,
/// blood trails, magic grooves. Authors configure a cadence (distance- or
/// time-based) and the emitter tracks per-particle state so each segment of
/// movement contributes at most one decal regardless of frame rate.
///
/// # Architecture
///
/// `VfxTrailDecalEmitter` is a CPU-side accumulator. Each frame the
/// particle-sync pass calls `TryEmit(particleId, pos, now)` for every alive
/// particle; the emitter checks whether the particle has moved / waited far
/// enough since its last emission and — when true — returns a populated
/// `VfxTrailDecalEvent`. The caller forwards the event to the existing
/// `DecalManager` which handles the render-pass side.
///
/// # Determinism
///
/// The cadence comparison uses a pure `VfxShouldEmitTrailDecal` function
/// that takes per-particle state + the current sample. No internal time
/// source, no global state — the caller's clock drives everything, so
/// deterministic replay produces identical decal streams.
///
/// # Thread safety
/// Not thread-safe. Drive from the particle-sync thread.
///
/// @ref UE5 UNiagaraDecalRendererProperties trail mode
/// @ref docs/engine/25-vfx/30-trail-decal-streaks.md

#include "vector3.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Cadence modes
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxTrailDecalCadence : uint8_t {
    Distance = 0, ///< Emit every `cadenceValue` metres of travel.
    Time     = 1, ///< Emit every `cadenceValue` seconds.
};

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for one trail-decal stream. Attached to a VFX asset next
/// to a ParticleEmitter reference.
struct VfxTrailDecalConfig {
    /// Name of the source emitter whose particles drive the stream.
    std::string sourceEmitter;

    /// Path to the decal material asset.
    std::string decalMaterial;

    /// Cadence mode (distance- vs time-based).
    VfxTrailDecalCadence cadence = VfxTrailDecalCadence::Distance;

    /// Cadence value — metres (Distance) or seconds (Time). Must be > 0;
    /// non-positive values disable emission (logged as a warning at
    /// authoring time).
    float cadenceValue = 0.5f;

    /// World-space decal width (perpendicular to the path direction).
    float width = 0.5f;

    /// World-space decal length along the path direction.
    /// Typically matches `cadenceValue` so consecutive decals just touch;
    /// authors can increase for overlap or decrease for gaps.
    float length = 0.5f;

    /// Decal lifetime in seconds. Matches the existing `VfxDecal::lifetime`.
    float lifetime = 10.0f;

    /// When true, the emitter pre-seeds each particle's state at spawn so
    /// the very first sample does NOT emit a decal (typical for tire
    /// marks — authors want the first mark at the first cadence step, not
    /// at the spawn point itself).
    bool skipFirstSample = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-particle state
// ─────────────────────────────────────────────────────────────────────────────

/// Per-particle cadence state.
///
/// Initialised on first `TryEmit` with the particle's spawn pose. When
/// `skipFirstSample` is true the first call returns no event but primes the
/// state so the next call can measure travel from that point.
struct VfxTrailDecalParticleState {
    Vec3     lastEmitPos {0, 0, 0};
    float    lastEmitTime = 0.0f;
    float    accumulatedDistance = 0.0f; ///< Since lastEmitPos.
    Vec3     prevPos {0, 0, 0};           ///< For distance accumulation.
    bool     primed = false;              ///< True once state is valid.
};

// ─────────────────────────────────────────────────────────────────────────────
// Event emitted per decal
// ─────────────────────────────────────────────────────────────────────────────

/// One decal to spawn. Caller forwards this to the `DecalManager`.
struct VfxTrailDecalEvent {
    Vec3  centerPos {0, 0, 0};  ///< Midpoint between previous emit pos and current sample.
    Vec3  forward   {1, 0, 0};  ///< Unit tangent (previous → current).
    float width     = 0.0f;
    float length    = 0.0f;
    float lifetime  = 0.0f;
    std::string material;       ///< Copied from config.
};

// ─────────────────────────────────────────────────────────────────────────────
// Pure cadence predicate (tests use this directly)
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true when the cadence condition is met for the given sample.
/// `now` is the caller's clock in seconds; ignored in Distance mode.
[[nodiscard]] bool VfxShouldEmitTrailDecal(
    const VfxTrailDecalConfig& cfg,
    const VfxTrailDecalParticleState& state,
    float now) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Emitter
// ─────────────────────────────────────────────────────────────────────────────

/// Stateful per-stream emitter. One instance per active trail-decal stream.
class VfxTrailDecalEmitter {
public:
    explicit VfxTrailDecalEmitter(VfxTrailDecalConfig cfg)
        : config_(std::move(cfg)) {}

    /// Feeds one sample into the emitter for `particleId`. When the cadence
    /// condition is met, populates `outEvent` and returns true; otherwise
    /// returns false (no event). The caller decides whether to render.
    ///
    /// # Arguments
    /// - `particleId` — stable 64-bit id (typically `emitterId << 32 | localId`).
    /// - `pos`        — current particle world-space position.
    /// - `now`        — caller clock in seconds (monotonic).
    bool TryEmit(uint64_t particleId, const Vec3& pos, float now,
                 VfxTrailDecalEvent& outEvent);

    /// Removes a particle's state when it dies. Saves memory on long-running
    /// scenes with many short-lived particles.
    void ForgetParticle(uint64_t particleId) noexcept {
        states_.erase(particleId);
    }

    /// Clears every tracked particle (scene change).
    void Reset() noexcept { states_.clear(); }

    [[nodiscard]] const VfxTrailDecalConfig& Config() const noexcept { return config_; }
    [[nodiscard]] uint32_t TrackedParticleCount() const noexcept {
        return static_cast<uint32_t>(states_.size());
    }

private:
    VfxTrailDecalConfig config_;
    std::unordered_map<uint64_t, VfxTrailDecalParticleState> states_;
};

} // namespace uz
