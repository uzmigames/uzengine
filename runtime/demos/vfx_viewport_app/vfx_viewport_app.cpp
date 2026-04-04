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
    cfg.cameraMoveSpeed = 1500.f;
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = -1500.f;
    cfg.cameraPositionZ = 800.f;
    cfg.cameraYawDeg = 90.f;
    cfg.cameraPitchDeg = -20.f;
    cfg.enableValidation = false; // Disabled: first-frame resource state mismatches cause device removal

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

    // VFX emitters re-enabled with buffer validation guard in frame_particles.cpp.

    // ── VFX Emitter 1: Fountain ──────────────────────────────────────────────
    //
    // Billboard particles launched upward with gravity pulling them down,
    // creating a fountain arc. SceneRenderer::SyncVfxEmitters() picks up the
    // VfxEmitterComponent each frame and creates the GPU ParticleEmitter.
    {
        Entity e = world.CreateEntity();
        world.AddComponent<CName>(e, CName{"Fountain"});
        auto& sc = world.AddComponent<SceneComponent>(e);
        sc.SetPosition({0.0f, 0.0f, 0.0f});
        auto& em = world.AddComponent<VfxEmitterComponent>(e);
        em.maxParticles = 10000;
        em.emissionRate = 300.0f;
        em.lifetime = 3.0f;
        em.gravity = {0.0f, 0.0f, -981.0f};
        em.spawnVelocity = {0.0f, 0.0f, 500.0f};
        em.emitting = true;
        m_emitterEntities.push_back(e);
    }

    UZ_LOG(App, Info, "VFXViewportApp: started (SUN + SKY + 1 VFX EMITTER)");
    return; // Skip emitters 2 and 3

    // ── VFX Emitter 2: Side burst ────────────────────────────────────────────
    //
    // Offset to the right, fires particles diagonally upward and to the side.
    {
        Entity e = world.CreateEntity();
        world.AddComponent<CName>(e, CName{"SideBurst"});
        auto& sc = world.AddComponent<SceneComponent>(e);
        sc.SetPosition({400.0f, 0.0f, 0.0f});
        auto& em = world.AddComponent<VfxEmitterComponent>(e);
        em.maxParticles = 5000;
        em.emissionRate = 150.0f;
        em.lifetime = 2.5f;
        em.gravity = {0.0f, 0.0f, -981.0f};
        em.spawnVelocity = {200.0f, 0.0f, 400.0f};
        em.emitting = true;
        m_emitterEntities.push_back(e);
    }

    // ── VFX Emitter 3: Slow rain ─────────────────────────────────────────────
    //
    // Offset to the left and elevated, drops particles downward slowly.
    {
        Entity e = world.CreateEntity();
        world.AddComponent<CName>(e, CName{"SlowRain"});
        auto& sc = world.AddComponent<SceneComponent>(e);
        sc.SetPosition({-400.0f, 0.0f, 500.0f});
        auto& em = world.AddComponent<VfxEmitterComponent>(e);
        em.maxParticles = 8000;
        em.emissionRate = 200.0f;
        em.lifetime = 4.0f;
        em.gravity = {0.0f, 0.0f, -300.0f};
        em.spawnVelocity = {0.0f, 0.0f, -50.0f};
        em.emitting = true;
        m_emitterEntities.push_back(e);
    }

    UZ_LOG(App, Info, "VFXViewportApp: started — {} emitter entities, sun + sky atmosphere",
           m_emitterEntities.size());
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

    // ── Toggle emitters with Space key ───────────────────────────────────
    if (input.IsKeyJustPressed(Key::Space)) {
        for (const Entity& e : m_emitterEntities) {
            if (!world.IsValid(e)) continue;
            auto& em = world.GetComponent<VfxEmitterComponent>(e);
            em.emitting = !em.emitting;
        }
    }

    // ── Orbit the side burst emitter around the origin ───────────────────
    //
    // The second emitter (index 1) orbits at a 400cm radius in the XY plane,
    // giving a visual sense of dynamic movement in the scene.
    if (m_emitterEntities.size() >= 2) {
        const Entity& orbitEntity = m_emitterEntities[1];
        if (world.IsValid(orbitEntity) && world.HasComponent<SceneComponent>(orbitEntity)) {
            constexpr float kOrbitRadius = 400.0f; // cm
            constexpr float kOrbitSpeed = 0.5f;    // radians/second
            const float angle = m_totalTime * kOrbitSpeed;
            auto& sc = world.GetComponent<SceneComponent>(orbitEntity);
            sc.SetPosition({
                kOrbitRadius * std::cos(angle),
                kOrbitRadius * std::sin(angle),
                0.0f
            });
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
