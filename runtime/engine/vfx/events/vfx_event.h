#pragma once

/// VFX event system for burst emission triggers.
///
/// Events allow spawning a burst of particles in a single frame,
/// overriding the normal emission rate. Used for explosions, impacts,
/// and other instantaneous effects.
///
/// # Usage
/// ```cpp
/// VFXEventQueue queue;
/// queue.Emit("explode", 500, 2.0f);
/// auto events = queue.Drain(); // returns 1 event, queue is now empty
/// ```

#include "engine/math/vector3.h"
#include "engine/math/vector4.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// VFXEventDef -- event blueprint from .uzvfx asset
// ---------------------------------------------------------------------------

/// Definition of a VFX event (authored in .uzvfx asset files).
///
/// Each definition describes a named burst trigger with default parameters.
/// Gameplay code fires the event by name; the emitter looks up the matching
/// def to determine emission count and velocity scale.
struct VFXEventDef {
    /// Event name (e.g., "burst", "explode").
    std::string name;

    /// Number of particles to spawn when event fires.
    uint32_t emissionCount = 100;

    /// Multiplier applied to spawn velocity during burst.
    float velocityScale = 1.0f;
};

// ---------------------------------------------------------------------------
// EventPayload -- parent particle state carried by inter-emitter events
// ---------------------------------------------------------------------------

/// Particle payload data carried by inter-emitter events.
///
/// When a particle triggers a sub-emitter event (e.g., on death or collision),
/// the parent particle's state is captured in this payload. The child emitter
/// uses this data to spawn particles at the parent's location with inherited
/// properties.
///
/// @ref GPU particle event dispatch pattern — parent particle data inheritance for inter-emitter events
struct EventPayload {
    /// World-space position of the triggering particle.
    Vec3 position = {0.0f, 0.0f, 0.0f};

    /// Velocity of the triggering particle at trigger time.
    Vec3 velocity = {0.0f, 0.0f, 0.0f};

    /// Color of the triggering particle (linear RGBA).
    Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};

    /// Age of the triggering particle in seconds at trigger time.
    float age = 0.0f;
};

// ---------------------------------------------------------------------------
// VFXPendingEvent -- queued event instance awaiting simulation
// ---------------------------------------------------------------------------

/// Pending event instance sitting in the queue.
///
/// Created when gameplay code calls `Emit()` or `EmitFromDef()`.
/// Consumed by the simulation pass during `Drain()`.
struct VFXPendingEvent {
    /// Event name (matches the triggering VFXEventDef::name).
    std::string name;

    /// Particles to spawn for this event.
    uint32_t emissionCount = 0;

    /// Velocity multiplier applied to spawned particles.
    float velocityScale = 1.0f;

    /// Optional payload from the triggering particle(s).
    ///
    /// Empty when the event is a simple burst trigger (no parent particle data).
    /// Non-empty when fired by a sub-emitter module on particle death or
    /// collision — each element corresponds to one triggering particle.
    std::vector<EventPayload> payloads;
};

// ---------------------------------------------------------------------------
// VFXEventQueue -- per-emitter event queue
// ---------------------------------------------------------------------------

/// Per-emitter queue of pending VFX events.
///
/// Events are queued from gameplay code (C++ or Lua) and drained
/// by the simulation pass each frame. After draining, the queue
/// is empty until new events are emitted.
///
/// # Thread Safety
/// Not thread-safe. Emit and Drain must be called from the same thread
/// (the main/game thread).
class VFXEventQueue {
public:
    /// Queues an event with the given parameters.
    ///
    /// # Parameters
    /// - `name`: Event identifier (matched against VFXEventDef::name).
    /// - `emissionCount`: Number of particles to spawn in one frame.
    /// - `velocityScale`: Multiplier on spawn velocity (default 1.0).
    void Emit(const std::string& name, uint32_t emissionCount,
              float velocityScale = 1.0f);

    /// Queues an event with per-particle payload data from a parent emitter.
    ///
    /// Used by sub-emitter modules when parent particles die or collide.
    /// Each payload element represents the state of one triggering particle.
    /// The `emissionCount` of the created event is set to `payloads.size()`.
    ///
    /// # Parameters
    /// - `name`: Event identifier (matched against VFXEventDef::name).
    /// - `payloads`: Per-particle state data from the triggering emitter.
    /// - `velocityScale`: Multiplier on spawn velocity (default 1.0).
    void EmitWithPayloads(const std::string& name,
                          std::vector<EventPayload>&& payloads,
                          float velocityScale = 1.0f);

    /// Queues an event using parameters from a VFXEventDef.
    ///
    /// Convenience overload that copies name, emissionCount, and
    /// velocityScale directly from the definition.
    ///
    /// # Parameters
    /// - `def`: Event definition to instantiate.
    void EmitFromDef(const VFXEventDef& def);

    /// Returns all pending events and clears the queue.
    ///
    /// The returned vector owns the events; the internal queue becomes
    /// empty. Calling Drain() twice without intermediate Emit() calls
    /// returns an empty vector on the second call.
    ///
    /// # Returns
    /// Vector of pending events (may be empty).
    [[nodiscard]] std::vector<VFXPendingEvent> Drain();

    /// Returns true if there are pending events.
    [[nodiscard]] bool HasPending() const noexcept;

    /// Returns the number of pending events.
    [[nodiscard]] size_t GetPendingCount() const noexcept;

    /// Clears all pending events without processing them.
    void Clear();

private:
    std::vector<VFXPendingEvent> pending_;
};

} // namespace uz
