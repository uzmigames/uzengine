#pragma once

/// Physics listener registry — backend-agnostic fan-out for contact /
/// fracture events (phase 10.10.0).
///
/// Gameplay + VFX subsystems register listeners for physics events through
/// this registry; a future PhysicsWorld (phase 4.2.0 Jolt integration)
/// forwards its single native callback into the registry which then
/// dispatches to every subscriber. This lets subsystems (VFX fracture
/// bridge phase 9.18, gameplay damage, audio impact sfx) listen without
/// touching the physics backend directly.
///
/// # Why backend-agnostic
///
/// Jolt, PhysX, or any other backend all share the same event shape:
/// "body A contacted body B at point P with normal N and impulse J".
/// This header defines that shape once so listener code doesn't care
/// which backend is active — the integration layer adapts native events
/// into `PhysicsContactEvent` / `VfxFractureEventInput`.
///
/// # Lifecycle
///
/// Subscribers register via `RegisterContactListener` / `RegisterFractureListener`
/// and receive a handle they use to unregister. The registry does **not**
/// own the listener — callers keep their IListener* alive until
/// unregister. This mirrors how UE5 / Unity event delegate lists work.
///
/// # Thread safety
///
/// Single-threaded. Dispatch happens on the physics step thread; register
/// / unregister must happen from the same thread or during a stop-the-
/// world moment (scene load, shutdown).
///
/// @ref UE5 FCollisionNotifyInfo + JPH::ContactListener
/// @ref docs/engine/physics/listener-registry.md

#include "engine/vfx/events/vfx_physics_fracture.h"
#include "vector3.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Contact event payload (backend-agnostic)
// ─────────────────────────────────────────────────────────────────────────────

/// Single contact point between two bodies.
struct PhysicsContactEvent {
    /// Stable 64-bit ids identifying the two bodies. Encoding is
    /// backend-specific (Jolt uses `JPH::BodyID::GetIndexAndSequenceNumber`).
    uint64_t bodyIdA = 0u;
    uint64_t bodyIdB = 0u;

    /// World-space contact point + surface normal (points from B into A).
    Vec3 position{0, 0, 0};
    Vec3 normal  {0, 0, 0};

    /// Magnitude of the contact impulse (N·s). Useful to gate listener
    /// work — sub-threshold touches skip per-event cost.
    float impulseMagnitude = 0.0f;

    /// Relative velocity along the normal at impact (m/s). Negative when
    /// bodies are separating.
    float relativeVelocity = 0.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Listener interfaces
// ─────────────────────────────────────────────────────────────────────────────

/// Subscriber interface for per-contact notifications.
class IPhysicsContactListener {
public:
    virtual ~IPhysicsContactListener() = default;

    /// Called on the frame a new contact manifold is created.
    virtual void OnContactAdded(const PhysicsContactEvent& /*event*/) {}

    /// Called on every frame an existing contact persists (optional —
    /// default impl is a no-op). Subscribers that only care about the
    /// initial hit override `OnContactAdded` only.
    virtual void OnContactPersisted(const PhysicsContactEvent& /*event*/) {}

    /// Called when a contact separates.
    virtual void OnContactRemoved(uint64_t /*bodyIdA*/, uint64_t /*bodyIdB*/) {}
};

/// Subscriber interface for fracture events (phase 8.24 / 9.18).
///
/// Returns `true` when the listener consumed the event (e.g., forwarded
/// to an emitter queue). The registry ignores the return value today —
/// reserved for a future "handled, skip lower-priority listeners" mode.
/// The bool return matches `VfxPhysicsFractureBridge` from phase 8.24
/// so bridges inherit directly.
class IPhysicsFractureListener {
public:
    virtual ~IPhysicsFractureListener() = default;

    /// Called when a composite rigid body fractures. The event carries
    /// the impact normal, speed, authored tint, and per-piece data.
    virtual bool OnFracture(const VfxFractureEventInput& event) = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Registry
// ─────────────────────────────────────────────────────────────────────────────

/// Stable subscriber handle. `0` means "invalid" (returned when the
/// listener pointer is null).
using PhysicsListenerHandle = uint32_t;
constexpr PhysicsListenerHandle kInvalidPhysicsListenerHandle = 0u;

/// Central fan-out for physics events. The PhysicsWorld (phase 4.2.0)
/// is expected to create one of these at init and forward its native
/// callbacks into the `Dispatch*` methods. Every other subsystem
/// registers against this registry rather than the native listener.
class PhysicsListenerRegistry {
public:
    PhysicsListenerRegistry()  = default;
    ~PhysicsListenerRegistry() = default;

    PhysicsListenerRegistry(const PhysicsListenerRegistry&)            = delete;
    PhysicsListenerRegistry& operator=(const PhysicsListenerRegistry&) = delete;

    // ── Contact listeners ───────────────────────────────────────────────

    /// Registers a contact listener. Returns a stable handle used to
    /// unregister; returns `kInvalidPhysicsListenerHandle` when `listener`
    /// is null.
    PhysicsListenerHandle RegisterContactListener(IPhysicsContactListener* listener);

    /// Unregisters by handle. No-op when the handle is invalid or already
    /// removed.
    void UnregisterContactListener(PhysicsListenerHandle handle) noexcept;

    /// Forwards an OnContactAdded event to every subscriber. Called by
    /// the physics integration layer.
    void DispatchContactAdded(const PhysicsContactEvent& event);

    /// Forwards an OnContactPersisted event to every subscriber.
    void DispatchContactPersisted(const PhysicsContactEvent& event);

    /// Forwards an OnContactRemoved event to every subscriber.
    void DispatchContactRemoved(uint64_t bodyIdA, uint64_t bodyIdB);

    // ── Fracture listeners ──────────────────────────────────────────────

    /// Registers a fracture listener. Same handle contract as
    /// `RegisterContactListener`.
    PhysicsListenerHandle RegisterFractureListener(IPhysicsFractureListener* listener);

    /// Unregisters a fracture listener.
    void UnregisterFractureListener(PhysicsListenerHandle handle) noexcept;

    /// Forwards an OnFracture event to every subscriber.
    void DispatchFracture(const VfxFractureEventInput& event);

    // ── Introspection ───────────────────────────────────────────────────

    [[nodiscard]] uint32_t ContactListenerCount() const noexcept;
    [[nodiscard]] uint32_t FractureListenerCount() const noexcept;

    /// Removes every subscriber. Used on scene unload / shutdown.
    void Clear() noexcept;

private:
    struct ContactSlot {
        PhysicsListenerHandle    handle;
        IPhysicsContactListener* listener;
    };
    struct FractureSlot {
        PhysicsListenerHandle     handle;
        IPhysicsFractureListener* listener;
    };

    std::vector<ContactSlot>  contactSlots_;
    std::vector<FractureSlot> fractureSlots_;
    PhysicsListenerHandle     nextHandle_ = 1u; // 0 reserved for invalid
};

} // namespace uz
