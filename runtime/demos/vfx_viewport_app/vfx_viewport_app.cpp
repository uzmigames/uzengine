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
#include "engine/ecs/environment/vfx_emitter_component.h"
#include "engine/ecs/environment/vfx_system_component.h"
#include "engine/ecs/scene/scene_component.h"
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
    // Sprite texture (path cleared by default-constructed preset = procedural circle)
    c.spriteTexturePath  = p.spriteTexturePath;
    c.flipbookRows       = p.flipbookRows;
    c.flipbookCols       = p.flipbookCols;
    c.flipbookTotalFrames = p.flipbookTotalFrames;
    c.flipbookFPS        = p.flipbookFPS;
    c.spriteChannelMask  = p.spriteChannelMask;

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
    ApplyPreset_FireballProjectile,
    ApplyPreset_Fire, ApplyPreset_Smoke, ApplyPreset_Sparks,
    ApplyPreset_Fountain, ApplyPreset_MagicOrb, ApplyPreset_Burst,
    ApplyPreset_ExplosionBurst, ApplyPreset_ExplosionSmoke, ApplyPreset_ExplosionDebris,
    ApplyPreset_Fireball, ApplyPreset_RingOfFire, ApplyPreset_ConeJet,
    ApplyPreset_CylinderVolume, ApplyPreset_CurlSmoke, ApplyPreset_Tornado,
    ApplyPreset_Whirlpool, ApplyPreset_WindySmoke, ApplyPreset_LitSmoke,
    ApplyPreset_LitDebris, ApplyPreset_Orbital, ApplyPreset_AirResist,
};
static const char* const kPresetNames[] = {
    "FireballProj",
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
    cfg.width  = (m_overrideWidth  > 0u) ? m_overrideWidth  : 1280u;
    cfg.height = (m_overrideHeight > 0u) ? m_overrideHeight :  720u;

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

    // ── Fireball Compound: ONE entity with VfxSystemComponent ──────────
    // The VfxSystemComponent points to a `.uzvfxsys` asset that describes
    // the child emitters (head + sparks + smoke). The scene renderer
    // creates a VfxSystemInstance which owns those ParticleEmitters
    // internally and propagates the entity's world transform to them.
    // This replaces the legacy 3-entity hack (m_vfxEntity + m_vfxSparks
    // + m_vfxSmoke).
    {
        m_vfxEntity = world.CreateEntity();
        world.AddComponent<CName>(m_vfxEntity, CName{"VFX_Fireball"});
        auto& sc = world.AddComponent<SceneComponent>(m_vfxEntity);
        sc.SetPosition({0.0f, 0.0f, 0.0f});
        auto& sys = world.AddComponent<VfxSystemComponent>(m_vfxEntity);
        sys.systemAssetPath  = m_systemAssetPath;
        sys.emitting         = true;
    }

    // ── Standalone emitter for preset cycling (Tab/Left/Right) ─────────
    // The fireball compound is hard-coded above; for the remaining presets
    // we keep one VfxEmitterComponent entity whose parameters get rewritten
    // when the user cycles presets.
    auto createEmitter = [&](const char* name, const ParticleEmitter& p, bool useSprite) -> Entity {
        Entity ent = world.CreateEntity();
        world.AddComponent<CName>(ent, CName{name});
        auto& sc = world.AddComponent<SceneComponent>(ent);
        sc.SetPosition({0.0f, 0.0f, 0.0f});
        auto& em = world.AddComponent<VfxEmitterComponent>(ent);
        ApplyPresetToComponent(em, p);
        em.emitting = true;
        if (useSprite && !m_spriteTexturePath.empty()) {
            em.spriteTexturePath  = m_spriteTexturePath;
            em.flipbookRows       = m_flipbookRows;
            em.flipbookCols       = m_flipbookCols;
            em.flipbookTotalFrames = m_flipbookTotalFrames;
        }
        em.flipbookFPS       = m_flipbookFPS;
        em.spriteChannelMask = m_spriteChannelMask;
        m_emitterEntities.push_back(ent);
        return ent;
    };

    {
        // Default standalone preset = Fire (used when user cycles past the
        // built-in fireball compound). Emission is silenced by default so
        // the compound fireball is the only visible effect at startup.
        ParticleEmitter stdP;
        ApplyPreset_Fire(stdP);
        Entity stdEmitter = createEmitter("VFX_Standalone", stdP, true);
        if (world.HasComponent<VfxEmitterComponent>(stdEmitter)) {
            world.GetComponent<VfxEmitterComponent>(stdEmitter).emitting = false;
        }
    }

    UZ_LOG(App, Info, "VFXViewportApp: fireball compound created via VfxSystemComponent '{}'",
           m_systemAssetPath);

    UZ_LOG(App, Info, "VFXViewportApp: started (SUN + SKY + 1 emitter, preset: {})",
           kPresetNames[m_currentPreset]);

    // ── Configure VFX Editor Overlay ────────────────────────────────────
    if (auto* overlay = GetVfxEditorOverlay()) {
        std::vector<std::string> presetNames;
        for (int i = 0; i < kPresetCount; ++i) presetNames.push_back(kPresetNames[i]);
        overlay->SetPresetNames(presetNames);
        overlay->SetVisible(true); // Start with overlay visible
        overlay->SetNiagaraMode(true); // Phase 0 — default to Niagara-parity editor
        overlay->SetLayoutMode(VfxEditorLayoutMode::SplitHorizontal); // Phase 15.4 — 50/50 split default

        // Seed the emitter tab list for the Niagara-mode system
        // overview panel. Demo has one emitter slot; the label mirrors
        // the active preset so cycling Tab / Left / Right keeps the
        // Stack tab header in sync.
        overlay->SetEmitterNames({kPresetNames[m_currentPreset]}, 0);

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

        // Phase 12.10.0 — menu command dispatch. Each command from the
        // redesigned menu bar (12.7) routes to the appropriate app
        // action. Preset commands use the `Presets_Base + index`
        // convention.
        overlay->onMenuCommand = [this, overlay](VfxMenuCommand cmd) {
            const uint16_t code = static_cast<uint16_t>(cmd);
            const uint16_t presetBase = static_cast<uint16_t>(VfxMenuCommand::Presets_Base);
            if (code >= presetBase && code < presetBase + static_cast<uint16_t>(kPresetCount)) {
                const int idx = static_cast<int>(code - presetBase);
                m_currentPreset = idx;
                m_switchPresetPending = true;
                return;
            }
            switch (cmd) {
            case VfxMenuCommand::File_New:
            case VfxMenuCommand::File_Open:
            case VfxMenuCommand::File_Save:
            case VfxMenuCommand::File_SaveAs:
                // Demo has no file persistence — silently ignore.
                break;
            case VfxMenuCommand::File_Exit:
                Quit();
                break;
            case VfxMenuCommand::Edit_Undo: {
                auto& undo = overlay->UndoStack();
                if (undo.CanUndo()) undo.Undo();
                break;
            }
            case VfxMenuCommand::Edit_Redo: {
                auto& undo = overlay->UndoStack();
                if (undo.CanRedo()) undo.Redo();
                break;
            }
            case VfxMenuCommand::View_NiagaraMode:
                overlay->ToggleNiagaraMode();
                break;
            case VfxMenuCommand::View_ToggleStats:
                // Stats overlay toggling is handled by the Application
                // layer — no hook yet.
                break;
            case VfxMenuCommand::Help_Docs:
            case VfxMenuCommand::Help_About:
                // No demo-side implementation.
                break;
            default:
                break;
            }
        };

        // Phase 12.10.0 — Add Emitter from the menu tab bar. The demo
        // uses a single emitter today; this callback is stubbed so the
        // button renders but fires a warning instead of crashing.
        overlay->onAddEmitterRequested = [] {
            UZ_LOG(App, Info, "onAddEmitterRequested: demo only has one emitter slot");
        };
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — input handling and emitter animation
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportApp::OnUpdate(float dt) {
    m_totalTime += dt;

    // Phase 14.13 — drain the status-bar notification queue. Queue
    // auto-dismisses after 3 s per entry.
    // Phase 14.14 — feed smoothed FPS into the status bar. EMA with
    // 0.1 s time constant so a single-frame hitch does not slam the
    // colour threshold back and forth.
    if (auto* overlay = GetVfxEditorOverlay()) {
        overlay->TickStatus(dt);

        if (dt > 1.0e-4f) {
            const float instantFps = 1.0f / dt;
            const float alpha = dt / (dt + 0.1f);
            m_fpsSmoothed += (instantFps - m_fpsSmoothed) * alpha;
        }
        overlay->UpdateTelemetry(m_fpsSmoothed, 0u, 0.0f, 0.0f);
    }

    // ── Fireball projectile animation: move the single compound entity ──
    // The VfxSystemInstance automatically propagates this position to all
    // child emitters (head + sparks + smoke) via SetWorldTransform().
    if (m_currentPreset == 0) {
        World& w = GetWorld();
        m_projectilePhase += dt * 0.4f; // 2.5 seconds per cycle
        if (m_projectilePhase >= 1.0f) m_projectilePhase -= 1.0f;
        const float startX = -600.0f;
        const float endX   =  600.0f;
        const float x = startX + (endX - startX) * m_projectilePhase;
        const Vec3 pos{x, 0.0f, 0.0f};

        if (w.HasComponent<SceneComponent>(m_vfxEntity)) w.GetComponent<SceneComponent>(m_vfxEntity).SetPosition(pos);
    } else {
        m_projectilePhase = 0.0f;
    }

    // ── Gate fireball vs. standalone emitter based on selected preset ──
    // Preset 0 = FireballProj -> compound active, standalone silent.
    // Any other preset -> compound silent, standalone active.
    {
        World& w = GetWorld();
        const bool fireballActive = (m_currentPreset == 0);
        if (w.HasComponent<VfxSystemComponent>(m_vfxEntity)) {
            w.GetComponent<VfxSystemComponent>(m_vfxEntity).emitting = fireballActive;
        }
        if (!m_emitterEntities.empty() && w.IsValid(m_emitterEntities[0])) {
            if (w.HasComponent<VfxEmitterComponent>(m_emitterEntities[0])) {
                w.GetComponent<VfxEmitterComponent>(m_emitterEntities[0]).emitting = !fireballActive;
            }
        }
    }

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

    // ── F11: Toggle Niagara-parity composition (phase 11.14.0) ─────────
    // Requires the overlay to be visible — F10 gates F11 so pressing F11
    // while the overlay is hidden is a silent no-op.
    if (input.IsKeyJustPressed(Key::F11)) {
        if (auto* overlay = GetVfxEditorOverlay(); overlay && overlay->IsVisible()) {
            const bool enabled = overlay->ToggleNiagaraMode();
            UZ_LOG(App, Info, "VFX Editor Niagara mode: {}",
                   enabled ? "enabled" : "disabled");
        }
    }

    // ── Phase 12.10.0 — Key forwarding to the overlay's keybinding
    //    handler while Niagara mode is active. Translates the input
    //    system's `uz::Key` to `VfxEditorKeybindingHandler::Key` for
    //    the handful of keys the Niagara editor cares about.
    if (auto* overlay = GetVfxEditorOverlay();
        overlay != nullptr && overlay->IsVisible() && overlay->IsNiagaraMode()) {
        const bool ctrl  = input.IsKeyDown(Key::LeftCtrl)  || input.IsKeyDown(Key::RightCtrl);
        const bool shift = input.IsKeyDown(Key::LeftShift) || input.IsKeyDown(Key::RightShift);
        using Kb = VfxEditorKeybindingHandler::Key;
        const struct { Key in; Kb out; } kMap[] = {
            { Key::Z,      Kb::Z      },
            { Key::Y,      Kb::Y      },
            { Key::S,      Kb::S      },
            { Key::D,      Kb::D      },
            { Key::F,      Kb::F      },
            { Key::A,      Kb::A      },
            { Key::C,      Kb::C      },
            { Key::V,      Kb::V      },
            { Key::F2,     Kb::F2     },
            { Key::Escape, Kb::Escape },
            { Key::Tab,    Kb::Tab    },
            { Key::Delete, Kb::Delete },
            { Key::Enter,  Kb::Enter  },
            { Key::Space,  Kb::Space  },
            { Key::Home,   Kb::Home   },
            { Key::End,    Kb::End    },
            { Key::Left,   Kb::Left   },
            { Key::Right,  Kb::Right  },
            { Key::Up,     Kb::Up     },
            { Key::Down,   Kb::Down   },
        };
        for (const auto& m : kMap) {
            if (input.IsKeyJustPressed(m.in)) {
                overlay->InjectKey(m.out, ctrl, shift);
            }
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
        // Clear any leftover particles from the previously-active source so
        // switching presets doesn't leave phantom trails on screen.
        // `emitting = false` only stops new spawns — alive particles keep
        // simulating until they expire — so we need an explicit restart.
        if (world.HasComponent<VfxSystemComponent>(m_vfxEntity)) {
            auto& sysComp = world.GetComponent<VfxSystemComponent>(m_vfxEntity);
            sysComp.restartRequested = true;
        }
        if (!m_emitterEntities.empty() && world.IsValid(m_emitterEntities[0])) {
            if (world.HasComponent<VfxEmitterComponent>(m_emitterEntities[0])) {
                world.GetComponent<VfxEmitterComponent>(m_emitterEntities[0]).restartRequested = true;
            }
        }

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
                // Re-apply --sprite CLI override (presets don't set sprite texture)
                if (!m_spriteTexturePath.empty()) {
                    comp.spriteTexturePath  = m_spriteTexturePath;
                    comp.flipbookRows       = m_flipbookRows;
                    comp.flipbookCols       = m_flipbookCols;
                    comp.flipbookTotalFrames = m_flipbookTotalFrames;
                }
                comp.flipbookFPS       = m_flipbookFPS;
                comp.spriteChannelMask = m_spriteChannelMask;
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
                // Force rebuild of module list + retitle the emitter
                // slot so the Stack tab header + system overview show
                // the active preset instead of the startup name.
                if (auto* overlay = GetVfxEditorOverlay()) {
                    overlay->ForceRebuildModules();
                    overlay->SetEmitterNames(
                        {kPresetNames[m_currentPreset]}, 0);
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
