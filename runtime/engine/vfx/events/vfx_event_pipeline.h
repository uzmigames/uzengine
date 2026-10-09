#pragma once

/// Full-pipeline VFX event generator + receiver (Niagara-parity).
///
/// The existing `VFXEventQueue` (same directory) supports burst-style
/// emit/receive used by the original sub-emitter module. Phase 7.3.0
/// ships a richer pipeline that carries position / velocity / normal /
/// color payloads + the persistent source particle ID (7.4.0), so
/// receivers can spawn at the exact event location and correlate the
/// spawn back to the emitting particle.
///
/// # Pipeline shape
///
/// ```
///   [Writer emitter update CS]
///          │  hit / death / spawn branch appends VfxEventPayload
///          ▼
///   [VfxEventBuffer append counter]
///          │  readback framework (phase 4.26.0) copies to CPU
///          ▼
///   [CPU VfxEventPipeline]
///          │  dispatches payloads to matching VfxEventReceivers
///          ▼
///   [Receiver emitter spawn burst next frame]
/// ```
///
/// The CPU pipeline shipped here owns steps 3-4. Steps 1-2 (GPU-side
/// append buffer + readback wiring) are deferred to
/// `phase7_7.3.1-vfx-event-pipeline-gpu-binding` — the payload layout +
/// receiver contract frozen in this header are what the GPU side must
/// produce.
///
/// # Event kinds
///
/// Niagara's `ENiagaraScriptEventType` covers spawn / death / collision /
/// custom. UzEngine uses the same split plus an explicit `Custom` bucket
/// so authors can emit gameplay-specific payloads without bolting onto a
/// pre-defined kind.
///
/// @ref UE5 FNiagaraEventScript + UNiagaraEventReceiverModule
/// @ref phase7_7.4.0-vfx-persistent-particle-ids

#include "vector3.h"
#include "vector4.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Payload + kinds
// ─────────────────────────────────────────────────────────────────────────────

/// Category of event. Receivers filter on this to pick the payloads they
/// care about.
enum class VfxEventKind : uint8_t {
    Spawn     = 0,   ///< Fired when a new particle spawns.
    Death     = 1,   ///< Fired when a particle's age reaches its lifespan.
    Collision = 2,   ///< Fired when the collision CS reports a hit.
    Custom    = 3,   ///< User-defined; driven by module scripts.
};

/// Fully specified event carrying everything a receiver typically needs
/// to spawn a secondary effect.
///
/// Size is intentionally small + POD-layout so the GPU side can append
/// structs byte-for-byte into an `RWStructuredBuffer<VfxEventPayload>`
/// without serialisation shimming.
struct VfxEventPayload {
    /// Emitter handle of the writer (producer) emitter.
    uint32_t sourceEmitterHandle = 0u;

    /// Persistent ID of the source particle (from phase 7.4.0). Zero
    /// when the producer emitter doesn't opt in to persistent IDs —
    /// receivers should treat that as "correlatable to the emitter as
    /// a whole, not to an individual particle".
    uint64_t sourceParticleID = 0u;

    /// Event kind. Receivers filter on this.
    VfxEventKind kind = VfxEventKind::Custom;
    uint8_t _pad0    = 0u;
    uint16_t _pad1   = 0u;

    /// World-space position at event time (collision point for Collision
    /// events, death location for Death events, spawn position for Spawn).
    Vec3  position {};
    float _padPos = 0.0f;

    /// Velocity at event time. For Collision this is pre-bounce velocity.
    Vec3  velocity {};
    float _padVel = 0.0f;

    /// Surface normal — only populated for Collision events; zero vector
    /// for other kinds.
    Vec3  normal {};
    float _padNrm = 0.0f;

    /// Color at event time (typically the source particle's current color).
    Vec4  color {};
};

// ─────────────────────────────────────────────────────────────────────────────
// Receiver config
// ─────────────────────────────────────────────────────────────────────────────

/// Action the receiver performs when it consumes a payload.
enum class VfxEventReceiverAction : uint8_t {
    /// Spawn `burstCount` particles at the payload's position using the
    /// receiver's own spawn parameters. Default.
    SpawnBurst = 0,

    /// Same as SpawnBurst but the newly spawned particles inherit a
    /// fraction of the source particle's velocity.
    InheritVelocity = 1,

    /// Spawn at the payload's position but ignore velocity (static-point
    /// burst — e.g. scorch decals or impact glows).
    SpawnAtPosition = 2,
};

/// Per-receiver configuration authored on the follower emitter.
struct VfxEventReceiverConfig {
    /// Writer emitter handle whose events this receiver consumes.
    uint32_t sourceEmitterHandle = 0u;

    /// Event kinds this receiver cares about. Bitmask of
    /// `(1u << static_cast<uint32_t>(VfxEventKind::X))`.
    uint32_t kindMask = 0xFFFFFFFFu;   // all kinds by default

    /// Action to take on each received payload.
    VfxEventReceiverAction action = VfxEventReceiverAction::SpawnBurst;

    /// Particles to spawn per event (SpawnBurst / InheritVelocity actions).
    uint32_t burstCount = 1u;

    /// Inheritance factor for InheritVelocity action. 0 = no inheritance,
    /// 1 = full copy.
    float velocityInheritance = 0.5f;

    /// Returns true when `kind` falls inside the receiver's filter mask.
    [[nodiscard]] bool Accepts(VfxEventKind kind) const noexcept {
        return (kindMask & (1u << static_cast<uint32_t>(kind))) != 0u;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Spawn request — output of the dispatcher
// ─────────────────────────────────────────────────────────────────────────────

/// Resolved spawn request emitted by the pipeline when a receiver accepts
/// a payload. The scene-vfx-sync layer turns these into concrete spawn-CS
/// dispatches on the receiver emitter next frame.
struct VfxEventSpawnRequest {
    uint32_t receiverEmitterHandle = 0u;
    uint32_t burstCount            = 1u;
    Vec3     spawnPosition         {};
    Vec3     inheritedVelocity     {};  // zero for non-inheriting actions
    Vec4     tintColor             {};  // copied from payload
    VfxEventKind triggeringKind    = VfxEventKind::Custom;
    uint64_t sourceParticleID      = 0u;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// CPU pipeline driving event → receiver dispatch.
///
/// Not thread-safe. One instance per `VfxSystemInstance` or per scene.
class VfxEventPipeline {
public:
    /// Adds a new receiver. Returns the receiver's internal index (for
    /// later removal by index if the caller wants to disable a receiver
    /// without dropping the whole pipeline).
    uint32_t AddReceiver(uint32_t receiverEmitterHandle,
                         const VfxEventReceiverConfig& config);

    /// Removes every receiver targeting `receiverEmitterHandle`.
    void RemoveReceiver(uint32_t receiverEmitterHandle);

    /// Drops every receiver.
    void Clear() noexcept {
        m_receivers.clear();
        m_pendingRequests.clear();
    }

    /// Enqueues a payload for later `Dispatch` call. Typically invoked by
    /// the readback framework's completion callback.
    void SubmitPayload(const VfxEventPayload& payload);

    /// Batch submission helper.
    void SubmitPayloads(const VfxEventPayload* payloads, size_t count);

    /// Matches queued payloads against receivers, produces spawn requests,
    /// and returns them in insertion order. Clears the payload queue.
    [[nodiscard]] std::vector<VfxEventSpawnRequest> Dispatch();

    /// Number of payloads waiting to be dispatched. Useful for debugging.
    [[nodiscard]] uint32_t PendingCount() const noexcept {
        return static_cast<uint32_t>(m_pendingPayloads.size());
    }

    /// Number of registered receivers (including duplicates on the same
    /// source emitter, since a follower can listen to multiple kinds of
    /// events from the same producer).
    [[nodiscard]] uint32_t ReceiverCount() const noexcept {
        return static_cast<uint32_t>(m_receivers.size());
    }

private:
    struct ReceiverEntry {
        uint32_t                 receiverEmitterHandle = 0u;
        VfxEventReceiverConfig   config{};
    };

    std::vector<ReceiverEntry>        m_receivers;
    std::vector<VfxEventPayload>      m_pendingPayloads;
    /// Scratch vector reused between Dispatch calls so we don't realloc.
    std::vector<VfxEventSpawnRequest> m_pendingRequests;
};

} // namespace uz
