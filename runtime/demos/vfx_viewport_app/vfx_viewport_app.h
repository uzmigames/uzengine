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
#include <string>
#include <vector>

namespace uz {

class VFXViewportApp final : public Application {
public:
    VFXViewportApp() = default;
    ~VFXViewportApp() override = default;

    /// Sets the sprite texture path for all emitters (from --sprite CLI arg).
    /// Must be called before Run(). If non-empty, the engine loads this texture
    /// during emitter sync and binds it as a billboard sprite.
    void SetSpriteTexturePath(const std::string& path) { m_spriteTexturePath = path; }

    /// Sets flipbook grid dimensions for the sprite texture (from --flipbook CLI arg).
    /// Only meaningful when a sprite texture is set. Rows/cols default to 1 (no flipbook).
    void SetFlipbookGrid(uint32_t rows, uint32_t cols) {
        m_flipbookRows = rows;
        m_flipbookCols = cols;
        m_flipbookTotalFrames = rows * cols;
    }

    /// Sets the flipbook playback rate in frames per second (from --flipbook-fps CLI arg).
    /// Drives how fast the SubUV sheet cycles across the particle lifetime.
    void SetFlipbookFPS(float fps) { m_flipbookFPS = fps; }

    /// Sets the sprite texture channel mask (from --channel CLI arg).
    /// 0=RGBA pass-through, 1=R, 2=G, 3=B, 4=A.
    void SetSpriteChannelMask(uint32_t mask) { m_spriteChannelMask = mask; }

    /// Overrides the VfxSystem asset path used for the compound fireball
    /// effect (from `--vfxsys` CLI arg). Defaults to
    /// `"vfx/systems/fireball.uzvfxsys"` when unset.
    void SetSystemAssetPath(const std::string& path) { m_systemAssetPath = path; }

    /// Phase 13.5 — overrides the window dimensions for headless
    /// screenshot capture at arbitrary resolutions.
    void SetWindowSize(uint32_t w, uint32_t h) {
        m_overrideWidth  = w;
        m_overrideHeight = h;
    }

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

    /// Current preset index for cycling.
    int m_currentPreset = 0;

    /// VFX emitter entity (for projectile-style animation of position).
    /// Now carries a VfxSystemComponent — a single entity that spawns the
    /// full fireball compound (head + sparks + smoke) internally.
    Entity m_vfxEntity{};

    /// Projectile animation phase [0..1] — advances each frame, loops.
    float m_projectilePhase = 0.0f;

    /// Pending preset switch (set by Left/Right keys or overlay).
    bool m_switchPresetPending = false;

    /// Pending restart (set by R/Delete or overlay button).
    bool m_restartPending = false;

    // ── Sprite texture (from --sprite CLI arg) ──────────────────────────

    /// Path to sprite texture set via SetSpriteTexturePath().
    std::string m_spriteTexturePath;

    /// Flipbook grid dimensions set via SetFlipbookGrid().
    uint32_t m_flipbookRows        = 1;
    uint32_t m_flipbookCols        = 1;
    uint32_t m_flipbookTotalFrames = 1;

    /// Flipbook playback rate. 0 = lifetime-driven (one full cycle per lifetime). >0 = fixed FPS.
    float m_flipbookFPS = 0.0f;

    /// Sprite texture channel mask: 0=RGBA, 1=R, 2=G, 3=B, 4=A.
    uint32_t m_spriteChannelMask = 0;

    /// Path to the `.uzvfxsys` asset driving the compound fireball effect.
    /// Overridable via the `--vfxsys <path>` CLI argument.
    std::string m_systemAssetPath = "vfx/systems/fireball.uzvfxsys";

    /// Window-size override (0 = use defaults). Set by `--window-size WxH`.
    uint32_t m_overrideWidth  = 0;
    uint32_t m_overrideHeight = 0;

    /// Phase 14.14 — exponentially-smoothed FPS for the status-bar
    /// chip. 0.1 s time constant damps hitch spikes without adding
    /// perceptible lag.
    float m_fpsSmoothed = 60.0f;
};

} // namespace uz
