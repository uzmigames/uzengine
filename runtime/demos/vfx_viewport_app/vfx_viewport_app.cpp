/// VFX Viewport Application — Engine Application Implementation
///
/// Creates VFX emitter entities via ECS and lets SceneRenderer handle all
/// GPU particle simulation and rendering automatically.

#include "vfx_viewport_app.h"

#include "engine/app/application.h"
#include "engine/ecs/components.h"
#include "engine/ecs/core/world.h"
#include "input.h"
#include "camera.h"
#include "log.h"
#include "paths.h"

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
    World& world = GetWorld();

    // ── Primary emitter: upward fountain at the world origin ─────────────
    {
        Entity entity = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(entity);
        scene.SetPosition({0.f, 0.f, 0.f});

        auto& name = world.AddComponent<CName>(entity);
        name.name = "VFX_Fountain";

        auto& emitter = world.AddComponent<VfxEmitterComponent>(entity);
        emitter.maxParticles = 10000;
        emitter.emissionRate = 300.0f;
        emitter.lifetime     = 3.0f;
        emitter.gravity      = {0.0f, 0.0f, -981.0f}; // Z-down gravity in cm/s^2
        emitter.damping      = 0.98f;
        emitter.spawnVelocity = {0.0f, 0.0f, 500.0f};  // Upward Z in cm/s
        emitter.emitting     = true;

        m_emitterEntities.push_back(entity);
    }

    // ── Secondary emitter: orbiting sparkles ─────────────────────────────
    {
        Entity entity = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(entity);
        scene.SetPosition({300.f, 0.f, 200.f});

        auto& name = world.AddComponent<CName>(entity);
        name.name = "VFX_Orbiter";

        auto& emitter = world.AddComponent<VfxEmitterComponent>(entity);
        emitter.maxParticles = 5000;
        emitter.emissionRate = 150.0f;
        emitter.lifetime     = 2.0f;
        emitter.gravity      = {0.0f, 0.0f, -490.5f}; // Half gravity
        emitter.damping      = 0.95f;
        emitter.spawnVelocity = {100.0f, 100.0f, 300.0f};
        emitter.emitting     = true;

        m_emitterEntities.push_back(entity);
    }

    // ── Third emitter: ground-level radial burst ─────────────────────────
    {
        Entity entity = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(entity);
        scene.SetPosition({-300.f, 0.f, 50.f});

        auto& name = world.AddComponent<CName>(entity);
        name.name = "VFX_Radial";

        auto& emitter = world.AddComponent<VfxEmitterComponent>(entity);
        emitter.maxParticles = 8000;
        emitter.emissionRate = 200.0f;
        emitter.lifetime     = 2.5f;
        emitter.gravity      = {0.0f, 0.0f, -981.0f};
        emitter.damping      = 0.96f;
        emitter.spawnVelocity = {200.0f, 200.0f, 100.0f}; // Mostly horizontal spread
        emitter.emitting     = true;

        m_emitterEntities.push_back(entity);
    }

    UZ_LOG(App, Info,
           "VFXViewportApp: created {} emitter entities",
           m_emitterEntities.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — input handling and emitter animation
// ─────────────────────────────────────────────────────────────────────────────

void VFXViewportApp::OnUpdate(float dt) {
    m_totalTime += dt;

    InputSystem& input = GetInput();

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

    // ── Animate the orbiting emitter around the origin ───────────────────
    // The second emitter (index 1) orbits at radius 300 cm, period ~6 seconds.
    if (m_emitterEntities.size() >= 2) {
        World& world = GetWorld();
        if (world.HasComponent<SceneComponent>(m_emitterEntities[1])) {
            constexpr float kOrbitRadius = 300.0f; // cm
            constexpr float kOrbitSpeed  = 1.0f;   // radians/second
            const float angle = m_totalTime * kOrbitSpeed;

            auto& scene = world.GetComponent<SceneComponent>(m_emitterEntities[1]);
            scene.SetPosition({
                kOrbitRadius * std::cos(angle),
                kOrbitRadius * std::sin(angle),
                200.0f // Elevated above ground
            });
        }
    }

    // ── Toggle emitters with number keys 1-3 ─────────────────────────────
    for (uint32_t i = 0; i < m_emitterEntities.size() && i < 3; ++i) {
        // Key::Num1='1', Num2='2', Num3='3' — sequential char values
        const auto key = static_cast<Key>(
            static_cast<uint16_t>(Key::Num1) + i);

        if (input.IsKeyJustPressed(key)) {
            World& world = GetWorld();
            if (world.HasComponent<VfxEmitterComponent>(m_emitterEntities[i])) {
                auto& emitter = world.GetComponent<VfxEmitterComponent>(
                    m_emitterEntities[i]);
                emitter.emitting = !emitter.emitting;

                const char* state = emitter.emitting ? "ON" : "OFF";
                UZ_LOG(App, Info, "Emitter {} toggled {}", i, state);
            }
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
