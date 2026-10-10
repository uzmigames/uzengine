/// VFX System implementation -- emitter registration, tick, and lifecycle.

#include "vfx_system.h"

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/modules/register_builtin_modules.h"

#include "engine/assertions/engine_assert.h"
#include "engine/core/cvar.h"
#include "engine/logging/log.h"

#include <algorithm>
#include <cmath>

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
    device_              = device;

    // ── Register VFX scalability CVars ───────────────────────────────────
    // The VFX system samples these once per `Tick()` and caches the values
    // on the system (see `Tick()`). External tooling, the editor console,
    // and Lua may freely change them between frames; the next Tick picks
    // up the new values. Defaults preserve legacy "no CVar" behaviour:
    // cap = construction value, all features on, no time-scale changes.
    auto& cvars = CVarRegistry::Get();
    cvars.Register(
        "r.VFX.MaxTotalParticles",
        static_cast<float>(maxTotalParticles),
        "Global ceiling on alive particle count across all emitters. "
        "Hard cap fed to the budget allocator each frame; lowering it "
        "starves new spawns until usage drops below the new limit.",
        1024.f, 10'000'000.f);
    cvars.Register(
        "r.VFX.AllowGPUSim", 1.0f,
        "Allow GPU compute simulation. When 0 the system warns once per "
        "emitter and skips the GPU sim dispatch (no fallback CPU sim "
        "yet — the emitter goes silent).",
        0.f, 1.f);
    cvars.Register(
        "r.VFX.QualityLevel", 2.0f,
        "Quality preset: 0=low, 1=medium, 2=high (default), 3=cinematic. "
        "Drives spawn-rate and LOD-distance multipliers.",
        0.f, 3.f);
    cvars.Register(
        "r.VFX.SimulatePaused", 0.0f,
        "Pause VFX simulation while still rendering particles. Used by "
        "screenshot capture / editor pause.",
        0.f, 1.f);
    cvars.Register(
        "r.VFX.TimeScale", 1.0f,
        "Global VFX time-scale multiplier (slow-motion / fast-forward). "
        "Multiplied into the frame dt seen by every emitter sim before "
        "per-emitter timeScale is applied.",
        0.f, 100.f);
    cvars.Register(
        "r.VFX.Seek.MaxTicksPerFrame", 256.0f,
        "Maximum spawn/update CS dispatches per frame consumed by "
        "ParticleEmitter::Seek(t) warmup loops. Long scrubs split across "
        "frames when this cap is hit so a single Seek() does not stall "
        "the GPU pipeline. Default 256 = 4.27 s of sim work at 60 Hz; "
        "raise for faster scrub-completion at the cost of frame spikes.",
        1.f, 100'000.f);

    // ── DDGI emissive injection (phase16 lumen-gi-contribution) ──────
    cvars.Register(
        "r.VFX.DDGIInjection", 1.0f,
        "Master switch for VFX emissive injection into the DDGI probe "
        "grid. When 1, emissive emitters contribute bounce light to GI; "
        "when 0, the per-frame gather is a no-op.",
        0.f, 1.f);
    cvars.Register(
        "r.VFX.DDGIInjection.MaxEmitters", 32.0f,
        "Hard cap on the number of emitters considered for DDGI "
        "emissive injection per frame. Clamped to "
        "VfxDDGIEmissiveFeed::kMaxSamples (32) by the gather path.",
        0.f, 256.f);

    // Seed the cache so the very first frame (before `Tick()` runs) sees
    // sensible values.
    allowGPUSim_      = true;
    qualityLevel_     = 2u;
    simulationPaused_ = false;
    timeScale_        = 1.0f;

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
    initialized_         = false;
    device_              = nullptr;

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

    // ── Sample scalability CVars and propagate the budget cap ───────────
    // CVars are read once per Tick, cached on the VFXSystem so per-frame
    // inline code (frame_vfx_inline.cpp) does not re-hash the registry.
    {
        const auto& cvars = CVarRegistry::Get();
        const auto budgetMax = static_cast<uint32_t>(std::lround(
            cvars.GetFloat("r.VFX.MaxTotalParticles", static_cast<float>(budget_.GetMax()))));
        if (budgetMax != budget_.GetMax()) {
            budget_.SetMax(budgetMax);
        }

        allowGPUSim_      = cvars.GetBool("r.VFX.AllowGPUSim", true);
        qualityLevel_     = static_cast<uint32_t>(
            std::clamp(cvars.GetInt("r.VFX.QualityLevel", 2), 0, 3));
        simulationPaused_ = cvars.GetBool("r.VFX.SimulatePaused", false);
        timeScale_        = std::max(0.0f, cvars.GetFloat("r.VFX.TimeScale", 1.0f));
    }

    uint32_t totalAlive = 0;

    for (auto* emitter : emitters_) {
        UZ_CHECK(emitter != nullptr);

        // Track time since registration for composite delay support.
        emitter->timeSinceRegistration += dt;

        // Spawn count is calculated in frame_particles.cpp::RegisterParticlePasses
        // to avoid double-accumulation. The start delay check is also done there.

        // Poll the GPU alive-count readback ring so editor/HUD can read
        // the authoritative live-particle count from `buffer.gpuAliveCount`.
        // Safe every frame — no-op until the first Kick has committed.
        if (device_ != nullptr) {
            emitter->buffer.PollAliveReadback(device_);
        }

        // Accumulate alive count for stats. Prefer the GPU-read value;
        // fall back to the CPU capacity mirror when the readback ring
        // has not yet produced a sample.
        totalAlive += (emitter->buffer.readbackHasKickedOnce)
            ? emitter->buffer.gpuAliveCount
            : emitter->cpuAliveCount;
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

// ---------------------------------------------------------------------------
// Quality-preset multipliers
// ---------------------------------------------------------------------------
//
// The table is intentionally small — four discrete levels mirroring the
// presets exposed by `r.VFX.QualityLevel`. Authors who need finer control
// override individual emitter `emissionRate` / LOD distances directly.

float VFXSystem::GetQualitySpawnRateMultiplier() const noexcept {
    switch (qualityLevel_) {
    case 0u: return 0.25f; // low
    case 1u: return 0.50f; // medium
    case 3u: return 1.50f; // cinematic
    case 2u: // high (default)
    default: return 1.00f;
    }
}

float VFXSystem::GetQualityLodDistanceMultiplier() const noexcept {
    switch (qualityLevel_) {
    case 0u: return 0.50f;
    case 1u: return 0.75f;
    case 3u: return 1.50f;
    case 2u:
    default: return 1.00f;
    }
}

// ---------------------------------------------------------------------------
// FindEmitterByName
// ---------------------------------------------------------------------------

ParticleEmitter* VFXSystem::FindEmitterByName(const std::string& name) const noexcept {
    if (name.empty()) return nullptr;
    for (auto* e : emitters_) {
        if (e != nullptr && e->name == name) {
            return e;
        }
    }
    return nullptr;
}

} // namespace uz
