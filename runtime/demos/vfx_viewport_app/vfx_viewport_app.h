/// VFX Viewport Application — Engine Application Subclass
///
/// Canonical engine-integrated version of the VFX viewport demo. Uses
/// `uz::Application` instead of `DemoApplication`, delegating all RHI
/// management, window lifecycle, fence synchronization, and rendering to
/// the engine's `SceneRenderer`.
///
/// VFX particles are driven entirely through the ECS: this class creates
/// entities with `VfxEmitterComponent` + `SceneComponent` in `OnStart()`,
/// and the `SceneRenderer` automatically syncs those components to
/// `ParticleEmitter` instances each frame via `SyncVfxEmitters()`.
///
/// # Differences from VFXViewportDemo (DemoApplication)
///
/// | Aspect                | VFXViewportDemo             | VFXViewportApp          |
/// |-----------------------|-----------------------------|-------------------------|
/// | Base class            | DemoApplication             | uz::Application         |
/// | RHI access            | Direct (cmdList, device)    | Hidden (SceneRenderer)  |
/// | VFX integration       | Manual VFXParticleSystem    | ECS VfxEmitterComponent |
/// | Camera                | Manual FlyCameraController  | Owned by SceneRenderer  |
/// | Rendering             | Manual draw calls           | Automatic RenderFrame   |
/// | Window/Fences         | Manual in DemoApplication   | Automatic in Application|
///
/// # Lifecycle
/// ```
/// Application::Run()
///   -> Configure()       // window title, VFS root, camera defaults
///   -> OnStart()         // create emitter entities in ECS
///   -> OnUpdate(dt) x N  // input handling, entity transforms
///   -> OnStop()          // cleanup (entities auto-destroyed by World)
/// ```
///
/// # Thread Safety
/// NOT thread-safe. All methods called from the main thread by Application::Run().

#pragma once

#include "engine/app/application.h"
#include "engine/ecs/core/entity.h"

#include <cstdint>
#include <vector>

namespace uz {

class VFXViewportApp final : public Application {
public:
    VFXViewportApp() = default;
    ~VFXViewportApp() override = default;

protected:
    // ── Application interface ────────────────────────────────────────────

    /// Returns configuration with VFX viewport camera defaults and window title.
    ApplicationConfig Configure() override;

    /// Creates VFX emitter entities in the ECS World.
    ///
    /// Each emitter gets a `SceneComponent` (for position) and a
    /// `VfxEmitterComponent` (for particle parameters). The SceneRenderer's
    /// `SyncVfxEmitters()` pass detects these components during RenderFrame()
    /// and automatically creates the corresponding GPU `ParticleEmitter` instances.
    void OnStart() override;

    /// Handles fly-camera input and emitter transform updates.
    ///
    /// The SceneRenderer updates the fly camera controller internally, so
    /// this method only needs to handle demo-specific input (escape to quit,
    /// emitter repositioning, etc.).
    ///
    /// # Arguments
    /// * `dt` — Delta time in seconds (clamped to 0.1s max by Application).
    void OnUpdate(float dt) override;

    /// Cleanup hook. The ECS World and SceneRenderer are destroyed after
    /// this returns, so emitter entities are automatically cleaned up.
    void OnStop() override;

private:
    // ── Emitter entities ─────────────────────────────────────────────────

    /// ECS entities owning VfxEmitterComponent instances.
    /// Created in OnStart(), automatically destroyed when World shuts down.
    std::vector<Entity> m_emitterEntities;

    /// Accumulated total time for animation/orbit effects.
    float m_totalTime = 0.0f;

    /// Escape key hold timer for quit gesture (hold 1s to exit).
    float m_escapeHoldTime = 0.0f;
};

} // namespace uz
