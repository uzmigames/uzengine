/// VFX System implementation -- emitter registration, tick, and lifecycle.

#include "vfx_system.h"

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/modules/register_builtin_modules.h"

#include "engine/assertions/engine_assert.h"
#include "engine/logging/log.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// VFXSystem::Initialize
// ---------------------------------------------------------------------------

void VFXSystem::Initialize(IRHIDevice* device, uint32_t maxTotalParticles) {
    UZ_CHECK(device != nullptr);
    UZ_CHECK(!initialized_);

    budget_.SetMax(maxTotalParticles);
    totalAliveParticles_ = 0;

    // Create module registry and populate with built-in modules.
    moduleRegistry_ = std::make_unique<ModuleRegistry>();
    RegisterBuiltinModules(*moduleRegistry_);

    initialized_ = true;

    UZ_LOG(VFX, Info, "VFXSystem initialized: budget={} particles", maxTotalParticles);
}

// ---------------------------------------------------------------------------
// VFXSystem::Shutdown
// ---------------------------------------------------------------------------

void VFXSystem::Shutdown(IRHIDevice* device) {
    if (!initialized_) return;

    if (!emitters_.empty()) {
        UZ_LOG(VFX, Warning,
               "VFXSystem::Shutdown: {} emitters still registered, they should be unregistered first",
               emitters_.size());
    }

    moduleRegistry_.reset();
    emitters_.clear();
    totalAliveParticles_ = 0;
    initialized_ = false;

    UZ_LOG(VFX, Info, "VFXSystem shutdown");
}

// ---------------------------------------------------------------------------
// VFXSystem::RegisterEmitter
// ---------------------------------------------------------------------------

void VFXSystem::RegisterEmitter(ParticleEmitter* emitter) {
    UZ_CHECK(emitter != nullptr);
    UZ_CHECK(initialized_);

    if (!emitter->initialized) {
        UZ_LOG(VFX, Warning,
               "VFXSystem::RegisterEmitter: emitter is not initialized, skipping");
        return;
    }

    // Check for duplicate registration.
    const auto it = std::find(emitters_.begin(), emitters_.end(), emitter);
    if (it != emitters_.end()) {
        UZ_LOG(VFX, Warning, "VFXSystem::RegisterEmitter: emitter already registered");
        return;
    }

    emitters_.push_back(emitter);

    UZ_LOG(VFX, Debug, "VFXSystem: emitter registered, total={}", emitters_.size());
}

// ---------------------------------------------------------------------------
// VFXSystem::UnregisterEmitter
// ---------------------------------------------------------------------------

void VFXSystem::UnregisterEmitter(ParticleEmitter* emitter) {
    UZ_CHECK(emitter != nullptr);

    const auto it = std::find(emitters_.begin(), emitters_.end(), emitter);
    if (it == emitters_.end()) {
        UZ_LOG(VFX, Warning, "VFXSystem::UnregisterEmitter: emitter not found");
        return;
    }

    // Swap-and-pop for O(1) removal (order does not matter).
    *it = emitters_.back();
    emitters_.pop_back();

    UZ_LOG(VFX, Debug, "VFXSystem: emitter unregistered, total={}", emitters_.size());
}

// ---------------------------------------------------------------------------
// VFXSystem::Tick
// ---------------------------------------------------------------------------

void VFXSystem::Tick(float dt) {
    if (!initialized_) return;

    uint32_t totalAlive = 0;

    for (auto* emitter : emitters_) {
        UZ_CHECK(emitter != nullptr);

        // Track time since registration for composite delay support.
        emitter->timeSinceRegistration += dt;

        // Spawn count is calculated in frame_particles.cpp::RegisterParticlePasses
        // to avoid double-accumulation. The start delay check is also done there.

        // Accumulate alive count for stats.
        totalAlive += emitter->cpuAliveCount;
    }

    totalAliveParticles_ = totalAlive;
}

// ---------------------------------------------------------------------------
// VFXSystem::GetModuleRegistry
// ---------------------------------------------------------------------------

ModuleRegistry& VFXSystem::GetModuleRegistry() noexcept {
    UZ_CHECK(moduleRegistry_ != nullptr);
    return *moduleRegistry_;
}

const ModuleRegistry& VFXSystem::GetModuleRegistry() const noexcept {
    UZ_CHECK(moduleRegistry_ != nullptr);
    return *moduleRegistry_;
}

} // namespace uz
