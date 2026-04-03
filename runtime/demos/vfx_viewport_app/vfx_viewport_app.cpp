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
    // NOTE: VFX emitter entities are NOT created here because the SceneRenderer
    // does not yet have a VFX sync pass (SyncVfxEmitters). Creating ECS entities
    // with VfxEmitterComponent would have no visual effect and may cause crashes.
    //
    // TODO (Task 5.5.0): Once SceneRenderer has ParticleSimulationPass and
    // ParticleRenderPass integrated, uncomment the emitter entity creation below.
    //
    // For now, the Application-based VFX viewport demonstrates the engine game
    // loop, SceneRenderer pipeline (sky, grid, GTAO, deferred lighting, tonemap),
    // and camera controls — but without GPU particles.

    UZ_LOG(App, Info, "VFXViewportApp: started (VFX emitters deferred until SceneRenderer integration)");
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

    // VFX emitter animation and toggling deferred until SceneRenderer
    // has VFX sync pass (Task 5.5.0).
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
