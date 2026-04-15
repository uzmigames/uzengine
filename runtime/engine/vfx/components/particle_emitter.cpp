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

void ParticleEmitter::Shutdown(IRHIDevice* device, ParticleBudget& budget) {
    if (!initialized) return;

    UZ_CHECK(device != nullptr);

    const uint32_t released = allocatedFromBudget;

    buffer.Destroy(device);
    budget.Return(released);

    allocatedFromBudget = 0;
    cpuAliveCount = 0;
    spawnAccumulator = 0.0f;
    initialized = false;

    UZ_LOG(VFX, Info, "ParticleEmitter shutdown: released {} particles to budget", released);
}

// ---------------------------------------------------------------------------
// ParticleEmitter::CalculateSpawnCount
// ---------------------------------------------------------------------------

uint32_t ParticleEmitter::CalculateSpawnCount(float dt) noexcept {
    if (!initialized || !isEmitting || emissionRate <= 0.0f || dt <= 0.0f) {
        return 0;
    }

    // Accumulate fractional particles from this frame.
    spawnAccumulator += emissionRate * dt;

    // Extract integer part as the spawn count.
    const float integerPart = std::floor(spawnAccumulator);
    const auto spawnCount = static_cast<uint32_t>(integerPart);
    spawnAccumulator -= integerPart;

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
