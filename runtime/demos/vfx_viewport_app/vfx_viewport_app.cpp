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
    c.litMode            = p.litMode;
    c.softParticleFadeRange = p.softParticleFadeRange;
    c.facingMode         = p.facingMode;
    c.stretchFactor      = p.stretchFactor;
    c.blendMode          = p.blendMode;
    c.cameraOffset       = p.cameraOffset;
    c.pivotOffsetX       = p.pivotOffsetX;
    c.pivotOffsetY       = p.pivotOffsetY;
    c.sizeBySpeedEnabled = p.sizeBySpeedEnabled;
    c.sizeBySpeedMin     = p.sizeBySpeedMin;
    c.sizeBySpeedMax     = p.sizeBySpeedMax;
    c.speedRangeMax      = p.speedRangeMax;
    c.colorBySpeedEnabled = p.colorBySpeedEnabled;
    c.screenSpaceSizeEnabled = p.screenSpaceSizeEnabled;
    c.screenSpacePixelSize = p.screenSpacePixelSize;
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
    c.accelX             = p.accelX;
    c.accelY             = p.accelY;
    c.accelZ             = p.accelZ;
    c.vortexTangentialStrength = p.vortexTangentialStrength;
    c.vortexRadialStrength     = p.vortexRadialStrength;
    c.vortexMaxRadius          = p.vortexMaxRadius;
    c.orbitEnabled       = p.orbitEnabled;
    c.orbitRadius        = p.orbitRadius;
    c.orbitSpeed         = p.orbitSpeed;
    c.groundCollision    = p.groundCollision;
    c.restitution        = p.restitution;
    c.collisionResponse  = p.collisionResponse;
    c.collisionFriction  = p.collisionFriction;
    c.rotationRate       = p.rotationRate;
    c.rotationOverLifeStart = p.rotationOverLifeStart;
    c.rotationOverLifeEnd   = p.rotationOverLifeEnd;
    c.startDelay         = p.startDelay;
    c.emitterDuration    = p.emitterDuration;
    c.loopCount          = p.loopCount;
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
    // Shift camera +X so the emitter at world (0,0,0) appears centered in
    // the LEFT viewport (Parameters panel covers the right ~35%). Camera
    // right axis = +X (yaw=90 faces +Y), so offsetting +X moves the
    // subject leftward on screen by the same amount.
    cfg.cameraPositionX = -450.f;
    cfg.cameraPositionY = -1500.f;  // 15m back (matches standalone)
    cfg.cameraPositionZ = 500.f;    // lowered from 800 so emitter base sits mid-frame
    cfg.cameraYawDeg = 90.f;
    cfg.cameraPitchDeg = -10.f;     // flatter pitch so emitter rises into centre
    cfg.enableValidation = true;

    // VFS root: priority order is
    //   1. `--vfs-root <path>` CLI override (m_customVfsRoot) — used by the
    //      hardware-validation tasks to point at `sample/content/` so test
    //      assets (mesh_sort_shards.uzvfx, test_shard.uzmesh, test_glass.uzmat)
    //      resolve without bundling them into the demo build artifacts.
    //   2. `<exe_dir>/content` if it exists.
    //   3. Engine default (typically `<exe_dir>/assets/`).
    if (!m_customVfsRoot.empty()) {
        cfg.vfsRoot = m_customVfsRoot;
    } else {
        const auto exeDir = GetExeDir();
        const auto contentDir = exeDir / "content";
        if (std::filesystem::exists(contentDir)) {
            cfg.vfsRoot = contentDir.string();
        }
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

    // ── Asset-driven mode: skip fireball compound + preset cycling ─────────
    // When `--vfx-asset <path>` is set we are running a hardware-validation
    // scenario and want the scene to contain ONLY the asset under test, so
    // the inline-sort timing + visual A/B is not contaminated by the default
    // fireball + standalone Fire emitter the demo normally spawns. The sun
    // and sky atmosphere are still useful (the asset's mesh material is
    // DefaultLit), so they stay.
    if (!m_emitterAssetPath.empty()) {
        UZ_LOG(App, Info,
               "VFXViewportApp: asset-driven mode — loading single emitter from '{}'",
               m_emitterAssetPath);

        // Sun
        {
            Entity sunEntity = world.CreateEntity();
            world.AddComponent<CName>(sunEntity, CName{"Sun"});
            auto& sun = world.AddComponent<DirectionalLightComponent>(sunEntity);
            sun.color = {1.0f, 0.95f, 0.85f};
            sun.intensity = 10.0f;
            sun.shadowCascades = 4;
            sun.SetRotation(Quat::FromAxisAngle(Vec3::Right(),
                                                -50.0f * DirectX::XM_PI / 180.0f));
        }

        // Sky atmosphere
        {
            Entity skyEntity = world.CreateEntity();
            world.AddComponent<CName>(skyEntity, CName{"SkyAtmosphere"});
            world.AddComponent<SkyAtmosphereComponent>(skyEntity);
        }

        // Single emitter sourced from the .uzvfx asset. scene_vfx_sync.cpp
        // detects assetPath and routes through UzfxLoader::Load +
        // InstantiateEmitter — exactly the production code path the
        // hardware-validation suite is meant to exercise.
        {
            Entity ent = world.CreateEntity();
            world.AddComponent<CName>(ent, CName{"VFX_AssetDriven"});
            auto& sc = world.AddComponent<SceneComponent>(ent);
            sc.SetPosition({0.0f, 0.0f, 0.0f});
            auto& em = world.AddComponent<VfxEmitterComponent>(ent);
            em.assetPath = m_emitterAssetPath;
            em.emitting  = true;
            m_emitterEntities.push_back(ent);
            m_emitterPresets.push_back(0);  // not preset-driven, but list parity
        }
        m_activeEmitterIdx = 0;
        m_currentPreset    = -1;  // sentinel: preset cycling disabled

        // Asset mode used to hide the entire editor overlay
        // (`overlay->SetVisible(false)`) because the legacy preset cycler
        // was preset-centric. The Niagara-parity editor (phase 11) works
        // for any single emitter regardless of source, so the overlay is
        // shown in BOTH modes now. Preset cycling stays disabled (the
        // helper skips `SetPresetNames` + `onLoadPreset`) but every other
        // editor capability — parameter inspector, splitter widget, color
        // picker, module stack, multi-emitter add/duplicate/delete/move —
        // works against the asset-loaded emitter exactly the same.
        // @ref phase17_vfx-viewport-overlay-asset-mode
        ConfigureEditorOverlay(/*enablePresetCycling=*/false);
        return;
    }

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
        // Multi-emitter demo prioritises the author-composed slots.
        // Compound fireball stays dormant at startup and wakes up only
        // when a slot is cycled to preset 0 (FireballProj).
        sys.emitting         = false;
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
        // Default slot = Fire. Emission enabled so the demo greets the
        // user with a visible effect; the compound fireball
        // (VfxSystemComponent) starts dormant until a slot cycles to
        // preset 0.
        ParticleEmitter stdP;
        ApplyPreset_Fire(stdP);
        createEmitter("VFX_Standalone", stdP, true);
        m_emitterPresets.push_back(1);  // index 1 = Fire.
    }
    m_activeEmitterIdx = 0;
    m_currentPreset    = 1;

    UZ_LOG(App, Info, "VFXViewportApp: fireball compound created via VfxSystemComponent '{}'",
           m_systemAssetPath);

    UZ_LOG(App, Info, "VFXViewportApp: started (SUN + SKY + 1 emitter, preset: {})",
           kPresetNames[m_currentPreset]);

    // ── Configure VFX Editor Overlay ────────────────────────────────────
    ConfigureEditorOverlay(/*enablePresetCycling=*/true);

}

// ─────────────────────────────────────────────────────────────────────────────
// RefreshEmitterNames — rebuild the overview label list from preset slots.
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportApp::RefreshEmitterNames() {
    auto* overlay = GetVfxEditorOverlay();
    if (overlay == nullptr) return;

    std::vector<std::string> names;
    names.reserve(m_emitterPresets.size());
    for (size_t i = 0; i < m_emitterPresets.size(); ++i) {
        const int idx = m_emitterPresets[i];
        const char* preset =
            (idx >= 0 && idx < kPresetCount) ? kPresetNames[idx] : "?";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%zu: %s", i + 1u, preset);
        names.emplace_back(buf);
    }
    const int sel = std::clamp(
        m_activeEmitterIdx, 0,
        std::max(0, static_cast<int>(names.size()) - 1));
    overlay->SetEmitterNames(names, sel);
}

// ─────────────────────────────────────────────────────────────────────────────
// ConfigureEditorOverlay — Niagara-parity editor overlay setup.
//
// Shared between default mode (preset cycling enabled) and asset mode
// (preset cycling disabled — overlay still hosts parameter inspector,
// splitter, color picker, multi-emitter manipulation, etc.).
//
// @ref phase17_vfx-viewport-overlay-asset-mode — root cause of the bug
//      where the entire overlay was hidden in asset mode.
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportApp::ConfigureEditorOverlay(bool enablePresetCycling) {
    auto* overlay = GetVfxEditorOverlay();
    if (overlay == nullptr) return;

    overlay->SetVisible(true);
    overlay->SetNiagaraMode(true);                                  // Phase 0
    overlay->SetLayoutMode(VfxEditorLayoutMode::SplitHorizontal);   // Phase 15.4

    if (enablePresetCycling) {
        std::vector<std::string> presetNames;
        presetNames.reserve(static_cast<size_t>(kPresetCount));
        for (int i = 0; i < kPresetCount; ++i) {
            presetNames.push_back(kPresetNames[i]);
        }
        overlay->SetPresetNames(presetNames);
    }

    // Seed the emitter tab list for the Niagara-mode system overview
    // panel. `RefreshEmitterNames` builds the label list from
    // `m_emitterPresets` so adding / duplicating / deleting emitters
    // keeps the overview in sync automatically.
    RefreshEmitterNames();

    // Preset load callback — only wired when cycling is enabled. In asset
    // mode the user has no way to trigger this from the UI, but defensive
    // null-callback gating in the overlay handles the missing wire.
    if (enablePresetCycling) {
        overlay->onLoadPreset = [this](int presetIdx, ParticleEmitter*) {
            if (presetIdx < 0 || presetIdx >= kPresetCount) return;
            m_currentPreset = presetIdx;
            m_switchPresetPending = true;
        };
    }

    // Restart — works in both modes (asset mode restarts the asset emitter,
    // default mode restarts the standalone preset emitter).
    overlay->onRestart = [this]() {
        m_restartPending = true;
    };

    // Phase 12.10.0 — menu command dispatch. Each command from the
    // redesigned menu bar (12.7) routes to the appropriate app
    // action. Preset commands use the `Presets_Base + index`
    // convention; in asset mode the preset-base range is silently
    // ignored (cycling disabled).
    overlay->onMenuCommand = [this, overlay, enablePresetCycling](VfxMenuCommand cmd) {
        const uint16_t code = static_cast<uint16_t>(cmd);
        const uint16_t presetBase = static_cast<uint16_t>(VfxMenuCommand::Presets_Base);
        if (code >= presetBase && code < presetBase + static_cast<uint16_t>(kPresetCount)) {
            if (!enablePresetCycling) return;
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

    // Phase 15.12 — multi-emitter callbacks. Structural ECS ops
    // (create / destroy entities, reorder) are queued as pending
    // flags and drained in OnUpdate so the demo never mutates the
    // ECS list mid-overlay-tick. Wired in BOTH modes — asset mode
    // users can still add/duplicate/delete emitters next to the
    // asset-loaded one.
    overlay->onAddEmitterRequested = [this] {
        m_pendingAddEmitter = true;
    };
    overlay->onEmitterDuplicated = [this](size_t idx) {
        m_pendingDuplicateIdx = static_cast<int>(idx);
    };
    overlay->onEmitterDeleted = [this](size_t idx) {
        m_pendingDeleteIdx = static_cast<int>(idx);
    };
    overlay->onEmitterMoved = [this](size_t src, size_t dst) {
        m_pendingMoveSrc = static_cast<int>(src);
        m_pendingMoveDst = static_cast<int>(dst);
    };
    overlay->onEmitterEnableToggled = [this](size_t idx, bool enabled) {
        auto& w = GetWorld();
        if (idx >= m_emitterEntities.size()) return;
        Entity e = m_emitterEntities[idx];
        if (!w.IsValid(e)) return;
        if (!w.HasComponent<VfxEmitterComponent>(e)) return;
        w.GetComponent<VfxEmitterComponent>(e).emitting = enabled;
    };
    overlay->onSelectEmitter = [this, enablePresetCycling](int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_emitterEntities.size())) return;
        m_activeEmitterIdx = idx;
        if (enablePresetCycling && idx < static_cast<int>(m_emitterPresets.size())) {
            m_currentPreset = m_emitterPresets[idx];
        }
    };
}

void VFXViewportApp::ReassignRenderPriorities() {
    auto& w = GetWorld();
    for (size_t i = 0; i < m_emitterEntities.size(); ++i) {
        Entity e = m_emitterEntities[i];
        if (!w.IsValid(e) || !w.HasComponent<VfxEmitterComponent>(e)) continue;
        auto& comp = w.GetComponent<VfxEmitterComponent>(e);
        comp.renderPriority = static_cast<int32_t>(i);
        if (auto* rt = GetRuntimeEmitter(comp.runtimeEmitterIdx)) {
            rt->renderPriority = static_cast<int32_t>(i);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — input handling and emitter animation
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportApp::OnUpdate(float dt) {
    m_totalTime += dt;

    // Phase 15.12 — drain pending multi-emitter structural ops. Handled
    // before input routing so a click that adds / removes an emitter
    // this frame produces a consistent overview list before any other
    // consumer reads it.
    if (m_pendingAddEmitter) {
        m_pendingAddEmitter = false;
        auto& w = GetWorld();
        Entity ent = w.CreateEntity();
        w.AddComponent<CName>(ent, CName{"VFX_Emitter"});
        w.AddComponent<SceneComponent>(ent);
        auto& em = w.AddComponent<VfxEmitterComponent>(ent);
        ParticleEmitter seed;
        ApplyPreset_Fire(seed);
        ApplyPresetToComponent(em, seed);
        em.emitting = true;
        if (!m_spriteTexturePath.empty()) {
            em.spriteTexturePath   = m_spriteTexturePath;
            em.flipbookRows        = m_flipbookRows;
            em.flipbookCols        = m_flipbookCols;
            em.flipbookTotalFrames = m_flipbookTotalFrames;
        }
        em.flipbookFPS       = m_flipbookFPS;
        em.spriteChannelMask = m_spriteChannelMask;
        m_emitterEntities.push_back(ent);
        m_emitterPresets.push_back(1);  // "Fire" default preset index.
        m_activeEmitterIdx = static_cast<int>(m_emitterEntities.size()) - 1;
        RefreshEmitterNames();
        ReassignRenderPriorities();
    }
    if (m_pendingDuplicateIdx >= 0) {
        const int src = m_pendingDuplicateIdx;
        m_pendingDuplicateIdx = -1;
        if (src >= 0 && src < static_cast<int>(m_emitterEntities.size())) {
            auto& w = GetWorld();
            Entity srcE = m_emitterEntities[src];
            if (w.IsValid(srcE) && w.HasComponent<VfxEmitterComponent>(srcE)) {
                Entity ent = w.CreateEntity();
                w.AddComponent<CName>(ent, CName{"VFX_Clone"});
                w.AddComponent<SceneComponent>(ent);
                auto& em = w.AddComponent<VfxEmitterComponent>(ent);
                // Clone the component fields. The duplicated component
                // must NOT keep the source's `runtimeEmitterIdx` — the
                // sync pass would treat the clone as an alias for the
                // source's runtime and stomp it on the next
                // restartRequested.
                em = w.GetComponent<VfxEmitterComponent>(srcE);
                em.runtimeEmitterIdx = -1;  // force fresh runtime creation
                em.emitting = true;
                m_emitterEntities.insert(m_emitterEntities.begin() + src + 1, ent);
                const int srcPreset = (src < static_cast<int>(m_emitterPresets.size()))
                    ? m_emitterPresets[src] : 1;
                m_emitterPresets.insert(m_emitterPresets.begin() + src + 1, srcPreset);
                m_activeEmitterIdx = src + 1;
                RefreshEmitterNames();
                ReassignRenderPriorities();
            }
        }
    }
    if (m_pendingDeleteIdx >= 0) {
        const int idx = m_pendingDeleteIdx;
        m_pendingDeleteIdx = -1;
        if (idx >= 0 && idx < static_cast<int>(m_emitterEntities.size())
            && m_emitterEntities.size() > 1u) {
            auto& w = GetWorld();
            Entity e = m_emitterEntities[idx];
            if (w.IsValid(e)) w.DestroyEntity(e);
            m_emitterEntities.erase(m_emitterEntities.begin() + idx);
            if (idx < static_cast<int>(m_emitterPresets.size())) {
                m_emitterPresets.erase(m_emitterPresets.begin() + idx);
            }
            m_activeEmitterIdx = std::clamp(m_activeEmitterIdx, 0,
                static_cast<int>(m_emitterEntities.size()) - 1);
            RefreshEmitterNames();
            ReassignRenderPriorities();
        }
    }
    if (m_pendingMoveSrc >= 0 && m_pendingMoveDst >= 0) {
        const int src = m_pendingMoveSrc;
        const int dst = m_pendingMoveDst;
        m_pendingMoveSrc = -1;
        m_pendingMoveDst = -1;
        const int n = static_cast<int>(m_emitterEntities.size());
        if (src != dst && src >= 0 && src < n && dst >= 0 && dst < n) {
            Entity e = m_emitterEntities[src];
            int   p = (src < static_cast<int>(m_emitterPresets.size()))
                ? m_emitterPresets[src] : 1;
            m_emitterEntities.erase(m_emitterEntities.begin() + src);
            m_emitterPresets.erase(m_emitterPresets.begin() + src);
            const int insertAt = (dst > src) ? dst : dst;
            m_emitterEntities.insert(
                m_emitterEntities.begin() + insertAt, e);
            m_emitterPresets.insert(m_emitterPresets.begin() + insertAt, p);
            m_activeEmitterIdx = insertAt;
            RefreshEmitterNames();
            ReassignRenderPriorities();
        }
    }

    // Phase 14.13 — drain the status-bar notification queue. Queue
    // auto-dismisses after 3 s per entry.
    // Phase 14.14 — feed smoothed FPS into the status bar. EMA with
    // 0.1 s time constant so a single-frame hitch does not slam the
    // colour threshold back and forth.
    if (auto* overlay = GetVfxEditorOverlay()) {
        overlay->TickStatus(dt);

        // Phase 15.14 — publish the active emitter's runtime pointer
        // so the overlay binds Parameters to the correct standalone
        // (ignores compound fireball children that VFXSystem also
        // tracks in its global emitter list).
        ParticleEmitter* activeRt = nullptr;
        if (!m_emitterEntities.empty()) {
            const int idx = std::clamp(
                m_activeEmitterIdx, 0,
                static_cast<int>(m_emitterEntities.size()) - 1);
            Entity ent = m_emitterEntities[idx];
            auto& w = GetWorld();
            if (w.IsValid(ent) &&
                w.HasComponent<VfxEmitterComponent>(ent)) {
                const auto& comp = w.GetComponent<VfxEmitterComponent>(ent);
                activeRt = GetRuntimeEmitter(comp.runtimeEmitterIdx);
            }
        }
        overlay->SetActiveEmitterOverride(activeRt);

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
    // Skipped in asset-driven mode (no fireball compound was created).
    if (m_currentPreset == 0 && m_emitterAssetPath.empty()) {
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
    // Skipped in asset-driven mode — no VfxSystemComponent exists on
    // m_vfxEntity (we never created the fireball compound) so the
    // HasComponent guard would short-circuit anyway, but skipping the
    // whole block keeps the asset emitter's `emitting` flag authoritative.
    if (m_emitterAssetPath.empty()) {
        World& w = GetWorld();
        // Phase 15.12 — enable the compound fireball only when ANY
        // slot is configured to preset 0 (FireballProj). Standalone
        // emitters keep their per-slot `emitting` state untouched so
        // the Emitters-tab checkboxes remain authoritative across
        // frames — the previous global gate stomped every slot each
        // update and prevented mixed-preset setups (Fire + Sparks,
        // Smoke + Wind, etc.).
        bool anySlotFireball = false;
        for (int p : m_emitterPresets) {
            if (p == 0) { anySlotFireball = true; break; }
        }
        if (w.HasComponent<VfxSystemComponent>(m_vfxEntity)) {
            w.GetComponent<VfxSystemComponent>(m_vfxEntity).emitting =
                anySlotFireball;
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
    // Disabled in asset-driven mode — cycling would overwrite the
    // user-supplied .uzvfx asset with a built-in sprite preset and
    // defeat the purpose of `--vfx-asset`.
    if (m_emitterAssetPath.empty()) {
        if (input.IsKeyJustPressed(Key::Tab) || input.IsKeyJustPressed(Key::Right)) {
            m_currentPreset = (m_currentPreset + 1) % kPresetCount;
            m_switchPresetPending = true;
        }
        if (input.IsKeyJustPressed(Key::Left)) {
            m_currentPreset = (m_currentPreset + kPresetCount - 1) % kPresetCount;
            m_switchPresetPending = true;
        }
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
        // Preset switch targets the currently-active slot so users can
        // configure each emitter independently.
        const int activeIdx = std::clamp(
            m_activeEmitterIdx, 0,
            static_cast<int>(m_emitterEntities.size()) - 1);
        if (activeIdx >= 0 && activeIdx < static_cast<int>(m_emitterEntities.size())
            && world.IsValid(m_emitterEntities[activeIdx])) {
            if (world.HasComponent<VfxEmitterComponent>(m_emitterEntities[activeIdx])) {
                world.GetComponent<VfxEmitterComponent>(m_emitterEntities[activeIdx]).restartRequested = true;
            }
        }

        if (!m_emitterEntities.empty() && activeIdx >= 0) {
            Entity ent = m_emitterEntities[activeIdx];
            // Auto-enable emission on the slot the user just targeted
            // so a preset cycle always yields a visible effect — otherwise
            // slots started via OnStart with emitting=false stay silent.
            if (world.IsValid(ent) &&
                world.HasComponent<VfxEmitterComponent>(ent)) {
                world.GetComponent<VfxEmitterComponent>(ent).emitting = true;
            }
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
                // Track the preset the active slot is now showing so
                // the overview label list reflects per-slot presets.
                if (activeIdx >= 0 &&
                    activeIdx < static_cast<int>(m_emitterPresets.size())) {
                    m_emitterPresets[activeIdx] = m_currentPreset;
                }
                if (auto* overlay = GetVfxEditorOverlay()) {
                    overlay->ForceRebuildModules();
                    RefreshEmitterNames();
                }
                UZ_LOG(App, Info, "[VFX] Preset: {} ({}) @ slot {}",
                       kPresetNames[m_currentPreset], m_currentPreset,
                       activeIdx);
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
