/// ParticleEmitter implementation -- GPU buffer allocation, shutdown, spawn counting.

#include "particle_emitter.h"

#include "engine/vfx/core/particle_budget.h"

#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// ParticleEmitter::Initialize
// ---------------------------------------------------------------------------

bool ParticleEmitter::Initialize(IRHIDevice* device, ParticleBudget& budget) {
    UZ_CHECK(device != nullptr);

    if (initialized) {
        UZ_LOG(VFX, Warning, "ParticleEmitter::Initialize called on already-initialized emitter");
        return true;
    }

    if (maxParticles == 0) {
        UZ_LOG(VFX, Error, "ParticleEmitter::Initialize: maxParticles is 0");
        return false;
    }

    // Request particles from the global budget.
    uint32_t granted = 0;
    if (!budget.Allocate(maxParticles, granted)) {
        UZ_LOG(VFX, Error,
               "ParticleEmitter::Initialize: budget exhausted, requested={}, available={}",
               maxParticles, budget.GetAvailable());
        return false;
    }

    if (granted < maxParticles) {
        UZ_LOG(VFX, Warning,
               "ParticleEmitter::Initialize: budget constrained, requested={}, granted={}",
               maxParticles, granted);
    }

    // Create GPU buffers (two SoA attribute sets + shared bookkeeping).
    if (!buffer.Create(device, granted)) {
        UZ_LOG(VFX, Error, "ParticleEmitter::Initialize: GPU buffer creation failed, capacity={}",
               granted);
        budget.Return(granted);
        return false;
    }

    // Opt-in persistent ID allocation (phase 7.4.0). Only allocated when the
    // emitter has explicitly requested event correlation or cross-emitter
    // reads — keeps memory overhead at zero for the majority of effects.
    if (emitsPersistentIDs) {
        if (!buffer.CreatePersistentIDBuffers(device)) {
            UZ_LOG(VFX, Error,
                   "ParticleEmitter::Initialize: persistent-ID buffer creation "
                   "failed, capacity={}", granted);
            // Non-fatal — emitter still runs without persistent IDs. Callers
            // that require them check `buffer.hasPersistentIDs` before using.
            emitsPersistentIDs = false;
        }
    }

    // Allocate mesh instance buffers if this emitter uses MeshRenderModule.
    // The render module's GetName() returns "mesh_render" for mesh-based emitters.
    if (moduleStack.renderModule &&
        std::strcmp(moduleStack.renderModule->GetName(), "mesh_render") == 0) {
        if (!buffer.CreateMeshBuffers(device)) {
            UZ_LOG(VFX, Error,
                   "ParticleEmitter::Initialize: mesh buffer creation failed, capacity={}",
                   granted);
            buffer.Destroy(device);
            budget.Return(granted);
            return false;
        }
    }

    allocatedFromBudget = granted;
    cpuAliveCount = 0;
    spawnAccumulator = 0.0f;
    initialized = true;

    UZ_LOG(VFX, Info, "ParticleEmitter initialized: capacity={}", granted);
    return true;
}

// ---------------------------------------------------------------------------
// ParticleEmitter::Shutdown
// ---------------------------------------------------------------------------

void ParticleEmitter::Seek(float targetTime) noexcept {
    // Negative targets are a no-op semantically (a "rewind to before the
    // emitter started"); collapse to 0 = full reset, no warmup.
    pendingSeekTime = (targetTime > 0.0f) ? targetTime : 0.0f;

    // Reset CPU-side accumulators that the sim path uses to compute spawn
    // counts. The next frame's inline sim consumes these and replays
    // pendingSeekTime via fixed-rate ticks before the regular tick — see
    // frame_vfx_inline.cpp `DispatchVFXSimulation` (warmup loop is wired in
    // the on-hardware follow-up `phase16_vfx-sort-pipeline-hardware-validation`).
    spawnAccumulator = 0.0f;
    cpuAliveCount    = 0;

    // Force GPU buffer reset on the next sim tick. `needsInitialization`
    // is the same flag scene_vfx_sync uses for its restart path, so we
    // reuse the existing reinit pipeline rather than inventing a parallel
    // one.
    buffer.needsInitialization = true;
}

void ParticleEmitter::Shutdown(IRHIDevice* device, ParticleBudget& budget) {
    if (!initialized) return;

    UZ_CHECK(device != nullptr);

    const uint32_t released = allocatedFromBudget;

    // Release any subsystem-private GPU buffers held by stacked modules.
    // Default `IVFXModule::ReleaseGPUResources` is a no-op, so simulation
    // modules that hold no GPU resources (gravity, drag, decay, etc.)
    // pay nothing here. The render module is the production case:
    // `MeshRenderModule` owns `instanceTransformBuffer_` +
    // `instanceColorBuffer_`, both of which used to leak past every
    // mesh-emitter shutdown until this hook was wired
    // (phase0 particle-mesh-bindless-rewrite-and-d3d12ma-leak section B).
    for (auto& mod : moduleStack.spawnModules) {
        if (mod) mod->ReleaseGPUResources(device);
    }
    for (auto& mod : moduleStack.updateModules) {
        if (mod) mod->ReleaseGPUResources(device);
    }
    if (moduleStack.renderModule) {
        moduleStack.renderModule->ReleaseGPUResources(device);
    }

    buffer.Destroy(device);
    budget.Return(released);

    allocatedFromBudget = 0;
    cpuAliveCount = 0;
    spawnAccumulator = 0.0f;
    initialized = false;

    UZ_LOG(VFX, Info, "ParticleEmitter shutdown: released {} particles to budget", released);
}

// ---------------------------------------------------------------------------
// ParticleEmitter::RegisterUserAttribute / FindUserAttribute / EnsureUserAttributeBuffers
// ---------------------------------------------------------------------------

uint32_t ParticleEmitter::RegisterUserAttribute(const UserAttributeDef& def) {
    // Re-declaration: keep the original registration (hot-reload safe).
    for (const auto& existing : userAttributes) {
        if (existing.name == def.name) {
            if (existing.type != def.type) {
                UZ_LOG(VFX, Error,
                       "RegisterUserAttribute: name '{}' already registered with a different type — keeping the original",
                       def.name);
                return UINT32_MAX;
            }
            return existing.slotIndex;
        }
    }

    // Assign a slot by counting how many entries of this type are already
    // registered.
    uint32_t usedFloat = 0;
    uint32_t usedVec4  = 0;
    for (const auto& existing : userAttributes) {
        if (existing.type == UserAttributeType::Float) ++usedFloat;
        else                                           ++usedVec4;
    }

    UserAttributeDef out = def;
    if (def.type == UserAttributeType::Float) {
        if (usedFloat >= kMaxUserFloatAttrs) {
            UZ_LOG(VFX, Error,
                   "RegisterUserAttribute: float quota ({}) exceeded — refusing '{}'",
                   kMaxUserFloatAttrs, def.name);
            return UINT32_MAX;
        }
        out.slotIndex = usedFloat;
    } else {
        if (usedVec4 >= kMaxUserVec4Attrs) {
            UZ_LOG(VFX, Error,
                   "RegisterUserAttribute: vec4 quota ({}) exceeded — refusing '{}'",
                   kMaxUserVec4Attrs, def.name);
            return UINT32_MAX;
        }
        out.slotIndex = usedVec4;
    }
    userAttributes.push_back(out);
    return out.slotIndex;
}

const UserAttributeDef* ParticleEmitter::FindUserAttribute(const std::string& name) const {
    for (const auto& a : userAttributes) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

bool ParticleEmitter::EnsureUserAttributeBuffers(IRHIDevice* device) {
    if (userAttributes.empty()) return true;
    if (!buffer.IsValid()) return false;
    UZ_CHECK(device != nullptr);

    bool allOk = true;
    for (const auto& a : userAttributes) {
        if (a.slotIndex == UINT32_MAX) continue;
        if (a.type == UserAttributeType::Float) {
            if ((buffer.userFloatMask & (1u << a.slotIndex)) == 0u) {
                if (!buffer.CreateUserFloatSlot(device, a.slotIndex)) {
                    allOk = false;
                }
            }
        } else {
            if ((buffer.userVec4Mask & (1u << a.slotIndex)) == 0u) {
                if (!buffer.CreateUserVec4Slot(device, a.slotIndex)) {
                    allOk = false;
                }
            }
        }
    }
    return allOk;
}

// ---------------------------------------------------------------------------
// ParticleEmitter::CalculateSpawnCount
// ---------------------------------------------------------------------------

uint32_t ParticleEmitter::CalculateSpawnCount(float dt) noexcept {
    if (!initialized || !isEmitting || dt <= 0.0f) {
        return 0;
    }

    // Phase 11.7: when the rich spawn-rate curve has keys, sample it at
    // the current absolute emitter time as particles/second. Otherwise
    // fall back to the constant `emissionRate`. The optional 4-point
    // multiplier (`spawnRateMultiplier`) applies on top of the chosen
    // base rate when enabled — it's a normalised-time ramp, sampled at
    // (timeSinceRegistration / lifetime).
    float baseRate = emissionRate;
    if (spawnRateCurve.GetKeyCount() > 0) {
        baseRate = spawnRateCurve.SampleFloat(timeSinceRegistration);
        // Safety clamp: negative rates are meaningless; upper bound is
        // (maxParticles / lifetime) so a runaway curve can't over-spawn
        // the buffer in a single frame.
        const float upperBound = (lifetime > 0.0f)
            ? (static_cast<float>(maxParticles) / lifetime)
            : static_cast<float>(maxParticles);
        baseRate = std::clamp(baseRate, 0.0f, upperBound);
    }

    if (spawnRateMultiplier.enabled && lifetime > 0.0f) {
        const float normalisedT = std::clamp(
            timeSinceRegistration / lifetime, 0.0f, 1.0f);
        baseRate *= spawnRateMultiplier.Evaluate(normalisedT);
    }

    // Phase 11.8: advance the burst cursor and add every burst whose
    // `time` has been crossed this frame. Runs regardless of `baseRate`
    // because bursts are independent of the continuous rate — a paused
    // rate (0/s) still lets scheduled bursts fire.
    uint32_t burstFired = 0u;
    if (!bursts.empty()) {
        VfxBurstList::AdvanceCursor(bursts, burstCursor,
                                     timeSinceRegistration,
                                     burstFired, burstCursor);
    }

    if (baseRate <= 0.0f && burstFired == 0u) {
        return 0;
    }

    // Accumulate fractional particles from this frame.
    spawnAccumulator += baseRate * dt;

    // Extract integer part as the spawn count.
    const float integerPart = std::floor(spawnAccumulator);
    uint32_t spawnCount = static_cast<uint32_t>(integerPart);
    spawnAccumulator -= integerPart;

    // Add any burst contributions.
    spawnCount += burstFired;

    // Godot pattern: CPU doesn't track alive count. Dispatch = capacity.
    // The GPU free-list handles slot allocation — spawn shader pops from
    // freeList and guards against overflow (freeListHead >= capacity → skip).
    // CPU just passes the spawn count; GPU decides if slots are available.
    cpuAliveCount = buffer.capacity;

    return spawnCount;
}

// ─────────────────────────────────────────────────────────────────────────────
// ParticleEmitter::UpdateFrameVelocity  (phase 7.20.0)
// ─────────────────────────────────────────────────────────────────────────────
//
// Derives world-space frame velocity from successive emitter positions.
// The first call after Initialize primes `lastEmitterPosition` but leaves
// `emitterFrameVelocity` at zero so we don't produce a spurious (0 -> real
// pos) velocity spike.

void ParticleEmitter::UpdateFrameVelocity(const Vec3& newPosition, float dt) noexcept {
    if (!(dt > 0.0f)) {
        // Zero / negative dt: refuse silently; keep previous velocity so a
        // paused editor frame doesn't wipe the inherited motion.
        return;
    }

    if (!emitterFrameVelocityPrimed) {
        lastEmitterPosition       = newPosition;
        emitterFrameVelocity      = {0.0f, 0.0f, 0.0f};
        emitterFrameVelocityPrimed = true;
        return;
    }

    const Vec3 delta = newPosition - lastEmitterPosition;
    const float invDt = 1.0f / dt;
    emitterFrameVelocity = Vec3{delta.x * invDt, delta.y * invDt, delta.z * invDt};
    lastEmitterPosition  = newPosition;
}

} // namespace uz
