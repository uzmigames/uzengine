/// VFX Sub-Emitter Module implementation — child emitter spawn on particle events.
///
/// @ref UE5 Niagara Event Handler — parent-to-child particle event spawning
/// @ref UE5 NiagaraDataInterfaceEventWrite.cpp — OnDeath/OnCollision/OnEvent triggers

#include "sub_emitter_module.h"

#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Shader code — HLSL snippet for sub-emitter flag marking
// ---------------------------------------------------------------------------

// The snippet sets SubEmitterFlags[particleIdx] = 1u when the configured
// trigger condition fires. The VFX system reads this buffer back to the CPU
// after the simulation dispatch; Execute() then converts flagged particle
// data into EventPayloads and fires them into the child emitter's queue.
//
// OnEvent (cbSubEmitterTrigger == 2) has no GPU-side condition — it is
// driven entirely from the CPU event queue and never writes flags here.
//
// cbSubEmitterTrigger values match SubEmitterTrigger:
//   0 = OnDeath, 1 = OnCollision, 2 = OnEvent
static constexpr const char* kSubEmitterShaderCode = R"hlsl(
// --- SubEmitterModule ---
// Sub-emitter trigger detection is done GPU-side; actual spawning is CPU-side.
// This snippet marks the SubEmitterFlag in the output buffer when the trigger
// condition is met. The CPU reads back flagged particle data next frame.
{
    bool shouldTrigger = false;

    // OnDeath: particle age has reached or exceeded its lifespan.
    if (cbSubEmitterTrigger == 0u)
    {
        shouldTrigger = (Ages[particleIdx] >= Lifespans[particleIdx]);
    }
    // OnCollision: simple ground-plane check (z < 0).
    // A future CollisionModule will write per-particle collision flags to a
    // dedicated buffer; this snippet will be updated to read from that buffer
    // instead of performing a position test here.
    else if (cbSubEmitterTrigger == 1u)
    {
        shouldTrigger = (Positions[particleIdx].z < 0.0f);
    }
    // OnEvent (cbSubEmitterTrigger == 2): handled CPU-side via VFXEventQueue.
    // No GPU condition — the flag is never set here for this trigger type.

    if (shouldTrigger)
    {
        // Write 1 to the sub-emitter flag buffer for this particle.
        // The VFX system will include this particle in the readback payload.
        SubEmitterFlags[particleIdx] = 1u;
    }
}
)hlsl";

// ---------------------------------------------------------------------------
// IVFXModule interface
// ---------------------------------------------------------------------------

/// Returns the module's stable identifier used for registration and serialization.
const char* SubEmitterModule::GetName() const noexcept {
    return "sub_emitter";
}

/// Returns the HLSL snippet that marks per-particle sub-emitter trigger flags.
///
/// The snippet accesses the following resources (bound by the compositor):
/// - `SubEmitterFlags[]` (RWBuffer<uint>)  — per-particle flag output UAV.
/// - `cbSubEmitterTrigger` (uint)          — SubEmitterTrigger enum (0/1/2).
/// - `Ages[]` (RWBuffer<float>)            — per-particle age in seconds.
/// - `Lifespans[]` (Buffer<float>)         — per-particle total lifespan.
/// - `Positions[]` (RWBuffer<float3>)      — per-particle world-space position.
std::string_view SubEmitterModule::GetShaderCode() const {
    return kSubEmitterShaderCode;
}

/// Declares the four configurable parameters for the sub-emitter.
///
/// Parameter order defines the constant buffer slot assignment:
/// 0 = child_graph, 1 = trigger, 2 = inherit_velocity, 3 = inherit_color.
std::vector<ParameterDef> SubEmitterModule::GetParameters() const {
    std::vector<ParameterDef> params;
    params.reserve(4);

    {
        // Asset reference ID of the child .uzvfx graph to activate.
        // 0 means "no asset assigned".
        ParameterDef p;
        p.name       = "child_graph";
        p.type       = ParameterType::Int;
        p.defaultInt = childGraphId;
        params.push_back(p);
    }
    {
        // Trigger condition: 0=OnDeath, 1=OnCollision, 2=OnEvent.
        ParameterDef p;
        p.name       = "trigger";
        p.type       = ParameterType::Int;
        p.defaultInt = static_cast<int32_t>(trigger);
        params.push_back(p);
    }
    {
        // Velocity inheritance blend. 0 = none, 1 = full parent velocity.
        ParameterDef p;
        p.name         = "inherit_velocity";
        p.type         = ParameterType::Float;
        p.defaultFloat = inheritVelocity;
        params.push_back(p);
    }
    {
        // Color inheritance flag.
        ParameterDef p;
        p.name        = "inherit_color";
        p.type        = ParameterType::Bool;
        p.defaultBool = inheritColor;
        params.push_back(p);
    }

    return params;
}

// ---------------------------------------------------------------------------
// Execute — CPU-side event emission from GPU readback
// ---------------------------------------------------------------------------

/// Reads flagged particle data from the GPU readback and fires EventPayloads
/// into the child emitter's event queue.
///
/// Each element in `readback.flaggedParticles` represents one particle that
/// triggered the sub-emitter condition on the GPU. This method applies the
/// `inheritVelocity` scale and `inheritColor` policy to each payload before
/// routing them to the child queue via EmitWithPayloads().
///
/// For the `OnEvent` trigger type there is no GPU readback — the event
/// detection is handled entirely on the CPU: the parent emitter forwards
/// matching events from its own queue to the child queue. This path is handled
/// below with a separate early-return branch.
void SubEmitterModule::Execute(const ExecuteContext& ctx) {
    // Nothing to do if no child queue has been wired.
    if (m_childEventQueue == nullptr) {
        return;
    }

    // OnEvent trigger: relay matching named events from the parent's pending
    // queue. The parent emitter must call Execute() after draining its own
    // queue and providing the drained events via ctx.parameterData.
    // When parameterData is null for OnEvent, there are no events this frame.
    if (trigger == SubEmitterTrigger::OnEvent) {
        if (ctx.parameterData == nullptr) {
            return;
        }

        const auto* readback = static_cast<const SubEmitterReadback*>(ctx.parameterData);

        // The OnEvent path reuses the flaggedParticles vector: each entry
        // represents a particle from a parent that fired the matching named event.
        if (readback->flaggedParticles.empty()) {
            return;
        }

        std::vector<EventPayload> payloads;
        payloads.reserve(readback->flaggedParticles.size());

        for (const EventPayload& src : readback->flaggedParticles) {
            EventPayload dst = src;

            // Apply velocity inheritance.
            dst.velocity.x *= inheritVelocity;
            dst.velocity.y *= inheritVelocity;
            dst.velocity.z *= inheritVelocity;

            // Discard color if the child should not inherit parent's color.
            if (!inheritColor) {
                dst.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
            }

            payloads.push_back(dst);
        }

        // Capture count before the move — size() is zero after EmitWithPayloads.
        const size_t payloadCount = payloads.size();

        // Fire all payloads as a single named event into the child queue.
        // The child emitter's event handler will spawn one particle per payload.
        m_childEventQueue->EmitWithPayloads(eventName, std::move(payloads));

        UZ_LOG(VFX, Debug,
               "SubEmitterModule(OnEvent): fired %zu payloads for event '%s'",
               payloadCount,
               eventName.c_str());
        return;
    }

    // OnDeath / OnCollision path: read the GPU readback bundle.
    if (ctx.parameterData == nullptr) {
        // No readback data this frame — nothing triggered.
        return;
    }

    const auto* readback = static_cast<const SubEmitterReadback*>(ctx.parameterData);

    if (readback->flaggedParticles.empty()) {
        // No particles were flagged this frame.
        return;
    }

    // Build payloads with inheritance applied.
    std::vector<EventPayload> payloads;
    payloads.reserve(readback->flaggedParticles.size());

    for (const EventPayload& src : readback->flaggedParticles) {
        EventPayload dst = src;

        // Scale velocity by the inheritance factor: 0 = no inherited velocity,
        // 1 = full parent velocity passed through to child spawn.
        dst.velocity.x *= inheritVelocity;
        dst.velocity.y *= inheritVelocity;
        dst.velocity.z *= inheritVelocity;

        // Override color to white when inheritance is disabled, so the child
        // emitter's own color module drives the final appearance unimpeded.
        if (!inheritColor) {
            dst.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        }

        payloads.push_back(dst);
    }

    const char* triggerName = (trigger == SubEmitterTrigger::OnDeath)
                                  ? "OnDeath"
                                  : "OnCollision";

    UZ_LOG(VFX, Debug,
           "SubEmitterModule(%s): firing %zu child-spawn payloads (childGraphId=%d)",
           triggerName,
           payloads.size(),
           childGraphId);

    // Emit all payloads as a single inter-emitter event. The event name is
    // the trigger type string so child emitters can filter by source if needed.
    m_childEventQueue->EmitWithPayloads(triggerName, std::move(payloads));
}

// ---------------------------------------------------------------------------
// SetChildEventQueue — wire the child emitter's queue
// ---------------------------------------------------------------------------

/// Sets the non-owning pointer to the child emitter's VFXEventQueue.
///
/// The pointer must remain valid for the lifetime of this module, or until
/// SetChildEventQueue(nullptr) is called to detach it.
void SubEmitterModule::SetChildEventQueue(VFXEventQueue* queue) noexcept {
    m_childEventQueue = queue;
}

} // namespace uz
