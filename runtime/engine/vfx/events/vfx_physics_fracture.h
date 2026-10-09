#pragma once

/// VFX physics-fracture event source.
///
/// Gameplay integration: when a Jolt rigid body fractures, the VFX
/// runtime spawns debris / dust particles inheriting the fracture's
/// position, velocity, normal, and per-piece mass. Niagara listens to
/// Chaos break events; UzEngine's equivalent subscribes to Jolt's
/// fracture callback since we chose Jolt over Chaos.
///
/// # Pipeline
///
/// ```
///  Jolt OnFracture(bodyId, pieces...)
///            │
///            ▼
///  VfxPhysicsFractureBridge::OnFracture(VfxFractureEventInput)
///            │
///            ▼
///  VfxTranslateFractureEvent → VFXPendingEvent (kind = "fracture")
///            │
///            ▼
///  VFXEventQueue (per-emitter) — the existing 7.3 event pipeline drains
///  it during the sim pass and spawns bursts.
/// ```
///
/// # Payload mapping
///
/// Each fracture piece becomes one `EventPayload`:
///
/// | EventPayload field | Source                                     |
/// |--------------------|--------------------------------------------|
/// | `position`         | `piece.position` (world space)             |
/// | `velocity`         | impact velocity + per-piece linearVelocity |
/// | `color.rgb`        | authored tint from fracture material        |
/// | `color.a`          | piece mass / totalMass (0..1 normalised)    |
/// | `age`              | 0 (fresh spawn)                            |
///
/// Authors typically use `color.a` (mass fraction) as a size or lifetime
/// multiplier in the child emitter so larger debris lives longer.
///
/// # Thread safety
/// The bridge is not thread-safe. Drive from the physics post-step on
/// the physics thread, then hand off to the sim queue which is drained
/// on the render thread.
///
/// @ref UE5 FNiagaraDataInterfaceChaosDestruction + FChaosBreakEvent
/// @ref docs/engine/25-vfx/41-physics-fracture-events.md

#include "engine/vfx/events/vfx_event.h"
#include "vector3.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Input from the physics layer
// ─────────────────────────────────────────────────────────────────────────────

/// One fracture piece produced by Jolt's break callback.
struct VfxFracturePiece {
    Vec3  position{0, 0, 0};      ///< World-space piece centroid.
    Vec3  linearVelocity{0, 0, 0}; ///< Piece linear velocity.
    float mass = 1.0f;             ///< Piece mass (kg).
};

/// Single fracture event produced by Jolt.
struct VfxFractureEventInput {
    /// The Jolt body id that fractured. Serves as a stable key for
    /// deduplication when the physics solver emits multiple callbacks
    /// per body in the same step.
    uint64_t bodyId = 0u;

    /// Impact normal at the fracture site (unit length, points from the
    /// colliding body outward). Defaults to world-up so authors get a
    /// sensible default when Jolt reports a degenerate contact.
    Vec3 impactNormal{0.0f, 0.0f, 1.0f};

    /// Impact velocity magnitude (m/s). Authors typically modulate debris
    /// lifetime / size by this value.
    float impactSpeed = 0.0f;

    /// Authored tint copied from the fracture material (linear RGB).
    /// Appears in `EventPayload::color.rgb`.
    Vec3 authoredTint{1.0f, 1.0f, 1.0f};

    /// Per-piece data. Must be non-empty for the translation to produce
    /// a payload; empty inputs are skipped by the bridge.
    std::vector<VfxFracturePiece> pieces;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pure translation
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a fracture event into a VFX pending event ready to enqueue.
///
/// One `EventPayload` per piece. The event `name` is the fracture event
/// name authored on the emitter (typically `"fracture"`). When the input
/// has no pieces, the returned event has empty `payloads` and
/// `emissionCount = 0`; callers should treat that as "nothing to spawn".
[[nodiscard]] VFXPendingEvent VfxTranslateFractureEvent(
    const VfxFractureEventInput& input,
    const std::string& eventName) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Bridge — subscribes to physics + pushes onto a VFX queue
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side bridge that receives Jolt fracture callbacks and forwards
/// them to a VFX event queue.
///
/// The physics-subscription registration is environment-specific (Jolt
/// listener, PhysicsWorld plugin, etc.) and lives in the caller. The
/// bridge itself is just the translation + enqueue glue so tests can
/// exercise the data path without Jolt.
class VfxPhysicsFractureBridge {
public:
    /// Binds the bridge to a target event queue.
    explicit VfxPhysicsFractureBridge(VFXEventQueue* target,
                                        std::string eventName = "fracture")
        : queue_(target), eventName_(std::move(eventName)) {}

    /// Called by the physics listener on every fracture. Translates the
    /// input and pushes onto the target queue. Returns `true` when the
    /// event produced at least one payload.
    bool OnFracture(const VfxFractureEventInput& input);

    /// Returns the number of events forwarded since construction. Used
    /// for diagnostics and testing.
    [[nodiscard]] uint64_t ForwardedCount() const noexcept { return forwarded_; }

    /// Returns the configured event name.
    [[nodiscard]] const std::string& EventName() const noexcept { return eventName_; }

    /// Rebinds to a different queue (e.g. on scene change). Null disables
    /// forwarding until a new queue is bound.
    void SetTarget(VFXEventQueue* queue) noexcept { queue_ = queue; }

private:
    VFXEventQueue* queue_ = nullptr;
    std::string    eventName_;
    uint64_t       forwarded_ = 0u;
};

} // namespace uz
