#pragma once

/// VFX Sub-Emitter Module — triggers child particle emitters from parent events.
///
/// When a parent particle dies, collides, or receives a named event, this
/// module captures the parent particle's state (position, velocity, color,
/// age) and fires an inter-emitter event to spawn particles from a child
/// emitter at the parent particle's location.
///
/// The GPU-side HLSL snippet sets a per-particle flag in `SubEmitterFlags[]`
/// when the trigger condition is met. On the CPU side, `Execute()` reads the
/// readback data provided through `ExecuteContext::parameterData` (interpreted
/// as a `SubEmitterReadback`) and emits one `EventPayload` per flagged particle
/// into the target `VFXEventQueue`.
///
/// # Trigger Modes
/// | Trigger     | GPU detection                              | CPU action                      |
/// |-------------|--------------------------------------------|---------------------------------|
/// | OnDeath     | age >= lifespan                            | emit payload to child queue     |
/// | OnCollision | position.z < 0 (ground plane, see note)    | emit payload to child queue     |
/// | OnEvent     | none (flag not set by shader)              | CPU-only: relay received events |
///
/// # Note on OnCollision
/// The collision check in the shader uses a simple ground-plane test
/// (Positions[i].z < 0) as a placeholder until a dedicated CollisionModule
/// is available to write per-particle collision flags. When CollisionModule
/// is integrated, the check should read from its output buffer instead.
///
/// @ref UE5 Niagara Event Handler — parent-to-child particle event spawning
/// @ref UE5 NiagaraDataInterfaceEventWrite.cpp — OnDeath/OnCollision event types

#include "vfx_module.h"
#include "engine/vfx/events/vfx_event.h"

#include <cstdint>
#include <string>

namespace uz {

// ---------------------------------------------------------------------------
// SubEmitterTrigger — when to fire the child emitter
// ---------------------------------------------------------------------------

/// Trigger conditions for sub-emitter activation.
///
/// Determines what event on the parent particle causes the child emitter
/// to spawn new particles at the parent particle's location.
///
/// @ref UE5 Niagara — ENiagaraScriptUsage event-based spawning trigger types
enum class SubEmitterTrigger : uint8_t {
    OnDeath     = 0, ///< Fire when the parent particle reaches end of life.
    OnCollision = 1, ///< Fire when the parent particle collides with geometry.
    OnEvent     = 2, ///< Fire when a named event is received by the emitter.
};

// ---------------------------------------------------------------------------
// SubEmitterReadback — CPU-side readback data from GPU per-particle flags
// ---------------------------------------------------------------------------

/// Per-frame readback data provided to `SubEmitterModule::Execute()`.
///
/// The VFX system performs a GPU→CPU readback of the `SubEmitterFlags[]`
/// buffer and the particle state buffers after the simulation pass.
/// It packages that data as a `SubEmitterReadback` and passes a pointer
/// to it via `ExecuteContext::parameterData` before calling Execute().
///
/// Each element in `flaggedParticles` corresponds to one particle whose
/// `SubEmitterFlags[i]` was non-zero on the GPU. The payload contains the
/// particle's position, velocity, color, and age at the moment of flagging.
struct SubEmitterReadback {
    /// State of all flagged particles (those with SubEmitterFlags[i] != 0).
    ///
    /// May be empty if no particles triggered this frame. The VFXSystem
    /// only includes triggered particles, not the full particle array.
    std::vector<EventPayload> flaggedParticles;
};

// ---------------------------------------------------------------------------
// SubEmitterModule — child emitter spawn on particle events
// ---------------------------------------------------------------------------

/// Triggers a child particle emitter when parent particles die, collide,
/// or receive a named event.
///
/// GPU-side, the shader snippet marks each particle that meets the trigger
/// condition in a `SubEmitterFlags[]` UAV. CPU-side, `Execute()` reads the
/// readback provided by the VFX system, builds one `EventPayload` per flagged
/// particle (applying `inheritVelocity` and `inheritColor` blends), and emits
/// all payloads into the provided `childEventQueue` via `EmitWithPayloads()`.
///
/// # Parameters
/// | Name            | Type  | Default  | Description                                       |
/// |-----------------|-------|----------|---------------------------------------------------|
/// | child_graph     | Int   | 0        | Asset-ID of the child .uzvfx graph to trigger.   |
/// | trigger         | Int   | 0        | SubEmitterTrigger enum (OnDeath=0, etc.).         |
/// | inherit_velocity| Float | 0.5      | Blend factor: 0 = no inheritance, 1 = full.      |
/// | inherit_color   | Bool  | true     | Whether child spawn uses parent particle's color. |
///
/// # Wiring
/// Call `SetChildEventQueue()` once after construction to connect this module
/// to the child emitter's `VFXEventQueue`. Without a queue, `Execute()` is a
/// no-op (no crash — the queue pointer may be null during serialization).
///
/// @ref UE5 Niagara Event Handler — parent-to-child event spawning
class SubEmitterModule final : public IVFXModule {
public:
    SubEmitterModule() = default;
    ~SubEmitterModule() override = default;

    // -- IVFXModule interface ------------------------------------------------

    /// Returns "sub_emitter".
    [[nodiscard]] const char* GetName() const noexcept override;

    /// Returns the HLSL snippet that marks particles in SubEmitterFlags[]
    /// when the configured trigger condition is met.
    ///
    /// # Expected bindings (provided by the compositor)
    /// - `SubEmitterFlags[]` (RWBuffer<uint>) — per-particle flag UAV.
    /// - `cbSubEmitterTrigger` (uint) — SubEmitterTrigger enum value.
    /// - `Ages[]`, `Lifespans[]`, `Positions[]` — standard particle UAVs.
    [[nodiscard]] std::string_view GetShaderCode() const override;

    /// Returns four parameter definitions: child_graph, trigger,
    /// inherit_velocity, inherit_color.
    [[nodiscard]] std::vector<ParameterDef> GetParameters() const override;

    /// CPU-side: reads readback data from ctx.parameterData and fires events
    /// into the child emitter queue for each flagged particle.
    ///
    /// If `childEventQueue` is null or `ctx.parameterData` is null, this
    /// method returns immediately without performing any work.
    ///
    /// # Parameters
    /// - `ctx`: Dispatch context. `ctx.parameterData` must point to a valid
    ///   `SubEmitterReadback` or be null.
    void Execute(const ExecuteContext& ctx) override;

    // -- Wiring --------------------------------------------------------------

    /// Sets the event queue of the child emitter to fire events into.
    ///
    /// Must be called before `Execute()` for events to be delivered. The
    /// pointer is non-owning — the caller is responsible for the queue's
    /// lifetime. Set to null to disable event delivery without destroying
    /// the module.
    ///
    /// # Parameters
    /// - `queue`: Pointer to the child emitter's VFXEventQueue, or null.
    void SetChildEventQueue(VFXEventQueue* queue) noexcept;

    // -- Configuration (public for editor and serialization) -----------------

    /// Asset reference ID of the child .uzvfx graph to activate.
    ///
    /// A value of 0 means "no asset assigned". The VFX system uses this
    /// to look up the child emitter instance at runtime.
    int32_t childGraphId = 0;

    /// Condition under which the child emitter is activated.
    SubEmitterTrigger trigger = SubEmitterTrigger::OnDeath;

    /// Velocity inheritance blend factor applied to each spawned child particle.
    ///
    /// 0.0 means child particles spawn with no inherited velocity from the parent.
    /// 1.0 means child particles spawn with the parent's full velocity.
    /// Intermediate values linearly blend between zero and the parent velocity.
    /// Clamped to [0, 1] on construction and assignment.
    float inheritVelocity = 0.5f;

    /// When true, child particles inherit the triggering parent's color.
    ///
    /// The EventPayload carries the parent color (linear RGBA); the child
    /// emitter applies it as the initial color for spawned particles.
    bool inheritColor = true;

    /// Named event to listen for when trigger == SubEmitterTrigger::OnEvent.
    ///
    /// Unused when trigger is OnDeath or OnCollision. The module matches
    /// incoming events by comparing VFXPendingEvent::name against this field.
    std::string eventName;

private:
    /// Non-owning pointer to the child emitter's event queue.
    /// Null until SetChildEventQueue() is called.
    VFXEventQueue* m_childEventQueue = nullptr;
};

} // namespace uz
