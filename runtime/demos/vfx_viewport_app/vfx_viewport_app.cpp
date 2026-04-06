/// VFX Viewport Application — Engine Application Implementation
///
/// Creates a visible scene with directional light, sky atmosphere, and VFX
/// emitter entities via ECS. SceneRenderer handles all GPU particle simulation
/// and rendering automatically through SyncVfxEmitters().

#include "vfx_viewport_app.h"

#include "engine/app/application.h"
#include "engine/ecs/components.h"
#include "engine/ecs/core/world.h"
#include "engine/math/quaternion.h"
#include "engine/vfx/components/emitter_presets.h"
// VFX editor overlay — included directly (light header, no scene_renderer.h dependency)
#include "engine/vfx/editor/vfx_editor_overlay.h"
#include "engine/vfx/components/particle_emitter.h"
#include "input.h"
#include "camera.h"
#include "log.h"
#include "paths.h"

#include <DirectXMath.h>

#include <cmath>
#include <filesystem>

UZ_DECLARE_LOG_CATEGORY(App);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: copy preset from ParticleEmitter → VfxEmitterComponent
// ─────────────────────────────────────────────────────────────────────────────

static void ApplyPresetToComponent(VfxEmitterComponent& c, const ParticleEmitter& p) {
    c.maxParticles       = p.maxParticles;
    c.emissionRate       = p.emissionRate;
    c.lifetime           = p.lifetime;
    c.lifetimeVariation  = p.lifetimeVariation;
    c.spawnVelocity      = p.spawnVelocity;
    c.velocityVariation  = p.velocityVariation;
    c.spreadAngle        = p.spreadAngle;
    c.spawnShape         = p.spawnShape;
    c.spawnRadius        = p.spawnRadius;
    c.initialSize        = p.initialSize;
    c.initialColor       = p.initialColor;
    c.emissiveIntensity  = p.emissiveIntensity;
    c.softParticleFadeRange = p.softParticleFadeRange;
    c.facingMode         = p.facingMode;
    c.stretchFactor      = p.stretchFactor;
    c.blendMode          = p.blendMode;
    c.sizeOverLifeStart  = p.sizeOverLifeStart;
    c.sizeOverLifePeak   = p.sizeOverLifePeak;
    c.sizeOverLifeEnd    = p.sizeOverLifeEnd;
    c.sizeOverLifePeakTime = p.sizeOverLifePeakTime;
    c.colorLifeStartR    = p.colorLifeStartR;
    c.colorLifeStartG    = p.colorLifeStartG;
    c.colorLifeStartB    = p.colorLifeStartB;
    c.colorLifeStartA    = p.colorLifeStartA;
    c.colorLifeEndR      = p.colorLifeEndR;
    c.colorLifeEndG      = p.colorLifeEndG;
    c.colorLifeEndB      = p.colorLifeEndB;
    c.colorLifeEndA      = p.colorLifeEndA;
    c.gravity            = p.gravity;
    c.damping            = p.damping;
    c.curlStrength       = p.curlStrength;
    c.curlFrequency      = p.curlFrequency;
    c.curlSpeed          = p.curlSpeed;
    c.dragCoefficient    = p.dragCoefficient;
    c.windDirX           = p.windDirX;
    c.windDirY           = p.windDirY;
    c.windDirZ           = p.windDirZ;
    c.windStrength       = p.windStrength;
    c.orbitEnabled       = p.orbitEnabled;
    c.orbitRadius        = p.orbitRadius;
    c.orbitSpeed         = p.orbitSpeed;
    c.groundCollision    = p.groundCollision;
    c.restitution        = p.restitution;
    c.collisionFriction  = p.collisionFriction;
    c.startDelay         = p.startDelay;
    c.particleLightEnabled     = p.particleLightEnabled;
    c.particleLightRadius      = p.particleLightRadius;
    c.particleLightIntensity   = p.particleLightIntensity;
    c.particleLightFlicker     = p.particleLightFlicker;
    c.particleLightFlickerSpeed = p.particleLightFlickerSpeed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Preset Table — all available presets for cycling
// ─────────────────────────────────────────────────────────────────────────────

using PresetFn = void(*)(ParticleEmitter&);
static const PresetFn kPresetFns[] = {
    ApplyPreset_Fire, ApplyPreset_Smoke, ApplyPreset_Sparks,
    ApplyPreset_Fountain, ApplyPreset_MagicOrb, ApplyPreset_Burst,
    ApplyPreset_ExplosionBurst, ApplyPreset_ExplosionSmoke, ApplyPreset_ExplosionDebris,
    ApplyPreset_Fireball, ApplyPreset_RingOfFire, ApplyPreset_ConeJet,
    ApplyPreset_CylinderVolume, ApplyPreset_CurlSmoke, ApplyPreset_Tornado,
    ApplyPreset_Whirlpool, ApplyPreset_WindySmoke, ApplyPreset_LitSmoke,
    ApplyPreset_LitDebris, ApplyPreset_Orbital, ApplyPreset_AirResist,
};
static const char* const kPresetNames[] = {
    "Fire", "Smoke", "Sparks", "Fountain", "MagicOrb", "Burst",
    "ExpBurst", "ExpSmoke", "ExpDebris",
    "Fireball", "RingOfFire", "ConeJet", "CylinderVol",
    "CurlSmoke", "Tornado", "Whirlpool", "WindySmoke",
    "LitSmoke", "LitDebris", "Orbital", "AirResist",
};
static constexpr int kPresetCount = static_cast<int>(sizeof(kPresetFns) / sizeof(kPresetFns[0]));

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig VFXViewportApp::Configure() {
    ApplicationConfig cfg;
    cfg.title = "VFX Viewport";
    cfg.width = 1280;
    cfg.height = 720;

    // Camera defaults matching the legacy DemoApplication VFX viewport.
    // Position is in centimeters (engine convention), matching the old demo's
    // {0, -15, 8} meters = {0, -1500, 800} cm.
    cfg.cameraMoveSpeed = 500.f;
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = -1500.f;  // 15m back (matches standalone)
    cfg.cameraPositionZ = 800.f;    // 8m up (matches standalone)
    cfg.cameraYawDeg = 90.f;
    cfg.cameraPitchDeg = -20.f;
    cfg.enableValidation = true;

    // VFS root: use the demo's content directory if it exists next to the exe,
    // otherwise fall back to the default assets/ directory.
    const auto exeDir = GetExeDir();
    const auto contentDir = exeDir / "content";
    if (std::filesystem::exists(contentDir)) {
        cfg.vfsRoot = contentDir.string();
    }

    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart — create VFX emitter entities
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportApp::OnStart() {
    UZ_LOG(App, Info, "VFXViewportApp::OnStart — creating scene entities...");
    World& world = GetWorld();
    UZ_LOG(App, Info, "VFXViewportApp: World obtained, creating entities...");

    // DEBUG: Incremental entity creation — test each one for crash

    // ── Directional light (sun) — required for deferred lighting output ──────
    //
    // Without a directional light, the deferred lighting pass has no light
    // source and outputs black. The rotation aims the sun 50 degrees below
    // the horizontal (typical midday-ish angle) around the Y (Right) axis.
    {
        Entity sunEntity = world.CreateEntity();
        world.AddComponent<CName>(sunEntity, CName{"Sun"});
        auto& sun = world.AddComponent<DirectionalLightComponent>(sunEntity);
        sun.color = {1.0f, 0.95f, 0.85f};   // warm sunlight tint
        sun.intensity = 10.0f;               // 10 lux (default)
        sun.shadowCascades = 4;
        sun.SetRotation(Quat::FromAxisAngle(Vec3::Right(),
                                            -50.0f * DirectX::XM_PI / 180.0f));
    }

    // ── Sky atmosphere ────────────────────────────────────────────────────────
    {
        Entity skyEntity = world.CreateEntity();
        world.AddComponent<CName>(skyEntity, CName{"SkyAtmosphere"});
        world.AddComponent<SkyAtmosphereComponent>(skyEntity);
    }

    // ── Single VFX Emitter at center — cycle presets with Left/Right ────
    {
        ParticleEmitter preset;
        ApplyPreset_Fire(preset);

        Entity e = world.CreateEntity();
        world.AddComponent<CName>(e, CName{"VFX"});
        auto& sc = world.AddComponent<SceneComponent>(e);
        sc.SetPosition({0.0f, 0.0f, 0.0f});
        auto& em = world.AddComponent<VfxEmitterComponent>(e);
        ApplyPresetToComponent(em, preset);
        em.emitting = true;
        m_emitterEntities.push_back(e);
    }

    UZ_LOG(App, Info, "VFXViewportApp: started (SUN + SKY + 1 emitter, preset: {})",
           kPresetNames[m_currentPreset]);

    // ── Configure VFX Editor Overlay ────────────────────────────────────
    if (auto* overlay = GetVfxEditorOverlay()) {
        std::vector<std::string> presetNames;
        for (int i = 0; i < kPresetCount; ++i) presetNames.push_back(kPresetNames[i]);
        overlay->SetPresetNames(presetNames);
        overlay->SetVisible(true); // Start with overlay visible

        // Preset load: apply + restart
        overlay->onLoadPreset = [this](int presetIdx, ParticleEmitter*) {
            if (presetIdx < 0 || presetIdx >= kPresetCount) return;
            m_currentPreset = presetIdx;
            m_switchPresetPending = true;
        };

        // Restart
        overlay->onRestart = [this]() {
            m_restartPending = true;
        };
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — input handling and emitter animation
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportApp::OnUpdate(float dt) {
    m_totalTime += dt;

    InputSystem& input = GetInput();
    World& world = GetWorld();

    // ── Escape-hold-to-quit gesture (1 second hold) ──────────────────────
    if (input.IsKeyDown(Key::Escape)) {
        m_escapeHoldTime += dt;
        if (m_escapeHoldTime >= 1.0f) {
            Quit();
            return;
        }
    } else {
        m_escapeHoldTime = 0.0f;
    }

    // ── F10: Toggle VFX Editor Overlay ──────────────────────────────────
    if (input.IsKeyJustPressed(Key::F10)) {
        if (auto* overlay = GetVfxEditorOverlay()) {
            overlay->ToggleVisible();
            UZ_LOG(App, Info, "VFX Editor: {}", overlay->IsVisible() ? "visible" : "hidden");
        }
    }

    // ── Tab / Left/Right: Cycle presets ────────────────────────────────
    if (input.IsKeyJustPressed(Key::Tab) || input.IsKeyJustPressed(Key::Right)) {
        m_currentPreset = (m_currentPreset + 1) % kPresetCount;
        m_switchPresetPending = true;
    }
    if (input.IsKeyJustPressed(Key::Left)) {
        m_currentPreset = (m_currentPreset + kPresetCount - 1) % kPresetCount;
        m_switchPresetPending = true;
    }

    // ── Apply pending preset switch ─────────────────────────────────────
    if (m_switchPresetPending) {
        m_switchPresetPending = false;
        if (!m_emitterEntities.empty()) {
            Entity ent = m_emitterEntities[0];
            if (world.IsValid(ent)) {
                auto& comp = world.GetComponent<VfxEmitterComponent>(ent);
                // Reset to defaults first, then apply preset.
                // This ensures fields not set by the preset (facingMode, stretchFactor, etc.)
                // don't carry over from the previous preset.
                ParticleEmitter preset;  // default-constructed = all defaults
                kPresetFns[m_currentPreset](preset);
                ApplyPresetToComponent(comp, preset);
                // Also reset + apply to the runtime ParticleEmitter
                if (auto* rt = GetRuntimeEmitter(comp.runtimeEmitterIdx)) {
                    // Reset ALL config fields to defaults
                    ParticleEmitter defaults;
                    rt->emissionRate = defaults.emissionRate;
                    rt->lifetime = defaults.lifetime;
                    rt->lifetimeVariation = defaults.lifetimeVariation;
                    rt->spawnVelocity = defaults.spawnVelocity;
                    rt->velocityVariation = defaults.velocityVariation;
                    rt->spreadAngle = defaults.spreadAngle;
                    rt->spawnShape = defaults.spawnShape;
                    rt->spawnRadius = defaults.spawnRadius;
                    rt->initialSize = defaults.initialSize;
                    rt->initialColor = defaults.initialColor;
                    rt->emissiveIntensity = defaults.emissiveIntensity;
                    rt->litMode = defaults.litMode;
                    rt->softParticleFadeRange = defaults.softParticleFadeRange;
                    rt->facingMode = defaults.facingMode;
                    rt->stretchFactor = defaults.stretchFactor;
                    rt->blendMode = defaults.blendMode;
                    rt->cameraOffset = defaults.cameraOffset;
                    rt->pivotOffsetX = defaults.pivotOffsetX;
                    rt->pivotOffsetY = defaults.pivotOffsetY;
                    rt->sizeOverLifeStart = defaults.sizeOverLifeStart;
                    rt->sizeOverLifePeak = defaults.sizeOverLifePeak;
                    rt->sizeOverLifeEnd = defaults.sizeOverLifeEnd;
                    rt->sizeOverLifePeakTime = defaults.sizeOverLifePeakTime;
                    rt->colorLifeStartR = defaults.colorLifeStartR;
                    rt->colorLifeStartG = defaults.colorLifeStartG;
                    rt->colorLifeStartB = defaults.colorLifeStartB;
                    rt->colorLifeStartA = defaults.colorLifeStartA;
                    rt->colorLifeEndR = defaults.colorLifeEndR;
                    rt->colorLifeEndG = defaults.colorLifeEndG;
                    rt->colorLifeEndB = defaults.colorLifeEndB;
                    rt->colorLifeEndA = defaults.colorLifeEndA;
                    rt->sizeBySpeedEnabled = defaults.sizeBySpeedEnabled;
                    rt->colorBySpeedEnabled = defaults.colorBySpeedEnabled;
                    rt->screenSpaceSizeEnabled = defaults.screenSpaceSizeEnabled;
                    rt->gravity = defaults.gravity;
                    rt->damping = defaults.damping;
                    rt->curlStrength = defaults.curlStrength;
                    rt->curlFrequency = defaults.curlFrequency;
                    rt->curlSpeed = defaults.curlSpeed;
                    rt->dragCoefficient = defaults.dragCoefficient;
                    rt->windDirX = defaults.windDirX;
                    rt->windDirY = defaults.windDirY;
                    rt->windDirZ = defaults.windDirZ;
                    rt->windStrength = defaults.windStrength;
                    rt->accelX = defaults.accelX;
                    rt->accelY = defaults.accelY;
                    rt->accelZ = defaults.accelZ;
                    rt->vortexTangentialStrength = defaults.vortexTangentialStrength;
                    rt->vortexRadialStrength = defaults.vortexRadialStrength;
                    rt->vortexMaxRadius = defaults.vortexMaxRadius;
                    rt->orbitEnabled = defaults.orbitEnabled;
                    rt->orbitRadius = defaults.orbitRadius;
                    rt->orbitSpeed = defaults.orbitSpeed;
                    rt->groundCollision = defaults.groundCollision;
                    rt->restitution = defaults.restitution;
                    rt->collisionResponse = defaults.collisionResponse;
                    rt->collisionFriction = defaults.collisionFriction;
                    rt->rotationRate = defaults.rotationRate;
                    rt->rotationOverLifeStart = defaults.rotationOverLifeStart;
                    rt->rotationOverLifeEnd = defaults.rotationOverLifeEnd;
                    // Now apply preset on top of clean defaults
                    kPresetFns[m_currentPreset](*rt);
                }
                comp.restartRequested = true;
                // Force rebuild of module list in editor overlay
                if (auto* overlay = GetVfxEditorOverlay()) {
                    overlay->ForceRebuildModules();
                }
                UZ_LOG(App, Info, "[VFX] Preset: {} ({})", kPresetNames[m_currentPreset], m_currentPreset);
            }
        }
    }

    // ── R or Delete: Restart emitter ────────────────────────────────────
    if (input.IsKeyJustPressed(Key::R) || input.IsKeyJustPressed(Key::Delete)
        || input.IsKeyJustPressed(Key::Enter) || m_restartPending) {
        m_restartPending = false;
        for (const Entity& ent : m_emitterEntities) {
            if (!world.IsValid(ent)) continue;
            auto& comp = world.GetComponent<VfxEmitterComponent>(ent);
            comp.restartRequested = true;
        }
    }

    // ── Space: Toggle emission ──────────────────────────────────────────
    if (input.IsKeyJustPressed(Key::Space)) {
        for (const Entity& e : m_emitterEntities) {
            if (!world.IsValid(e)) continue;
            auto& em = world.GetComponent<VfxEmitterComponent>(e);
            em.emitting = !em.emitting;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStop — cleanup
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportApp::OnStop() {
    // Entities and their components are destroyed when World shuts down
    // (after OnStop returns). SceneRenderer's Shutdown() unregisters emitters
    // from VFXSystem before destroying the World.
    //
    // No manual cleanup needed — the engine handles the full teardown sequence:
    // 1. OnStop() (this function — app-level cleanup)
    // 2. SceneRenderer::Shutdown() (unregisters emitters, destroys GPU resources)
    // 3. World destruction (destroys all entities and components)

    UZ_LOG(App, Info,
           "VFXViewportApp: shutting down ({} emitter entities)",
           m_emitterEntities.size());

    m_emitterEntities.clear();
}

} // namespace uz
