/// uz::Application — Top-level lifecycle owner for UzEngine games and demos.
///
/// Encapsulates window creation, RHI device/swapchain/fences, SceneRenderer,
/// ECS World, and VFS — so that subclasses only contain game logic.
///
/// # Minimal Usage
/// ```cpp
/// class PBRTestApp : public uz::Application {
///     uz::Entity cube_;
///
///     void OnStart() override {
///         cube_ = GetWorld().CreateEntity();
///         GetWorld().Add<StaticMeshComponent>(cube_, "meshes/cube.uzmesh");
///     }
///
///     void OnUpdate(float dt) override {
///         GetWorld().Get<TransformComponent>(cube_).rotation.y += dt * 45.f;
///     }
/// };
///
/// UZ_APPLICATION_MAIN(PBRTestApp)
/// ```
///
/// # What Game Code NEVER Sees
/// - `IRHIDevice`, `IRHICommandList`, `IRHISwapChain`, or fence objects
/// - GBuffer, shadow atlas, render graph, or PSO construction
/// - Texture/mesh GPU upload details
/// - Constant buffer allocation or shadow cascade math
///
/// # Lifecycle (see application-layer spec for full diagram)
/// `Run()` drives: init → OnStart → frame loop → OnStop → shutdown.
/// All rendering is delegated to `SceneRenderer::RenderFrame()` per frame.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the main thread.

#pragma once

#include "app_config.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace uz {

// Forward declarations — keeps include footprint minimal
class IRHIDevice;
class IRHICommandList;
class IRHISwapChain;
class IRHIFence;
class PlatformWindow;
class InputSystem;
class SceneRenderer;
class Camera;
class FPSCameraController;
class IVFS;
class World;

// ─────────────────────────────────────────────────────────────────────────────
// UZ_APPLICATION_MAIN — Entry point macro
// ─────────────────────────────────────────────────────────────────────────────

/// Emits a `main()` function that runs the given Application subclass.
///
/// Handles `--compose-grid` and `--diff` modes before constructing the app.
///
/// # Example
/// ```cpp
/// UZ_APPLICATION_MAIN(PBRTestApp)
/// ```
#define UZ_APPLICATION_MAIN(AppClass)                                        \
    int main(int argc, char** argv) {                                        \
        if (int r = uz::Application::TryComposeGrid(argc, argv); r >= 0)    \
            return r;                                                        \
        if (int r = uz::Application::TryDiffImages(argc, argv);  r >= 0)    \
            return r;                                                        \
        AppClass app;                                                        \
        return app.Run(argc, argv);                                          \
    }

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────

/// Top-level engine lifecycle owner.
///
/// Subclasses override `Configure()`, `OnStart()`, `OnUpdate()`, and `OnStop()`
/// to implement game logic. The engine handles all rendering, resource loading,
/// fence synchronization, and window management internally.
class Application {
public:
    Application();
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // ── Subclass Interface ───────────────────────────────────────────────

    /// Override to return a custom ApplicationConfig.
    ///
    /// Called once at the start of `Run()`, before window/device creation.
    /// CLI arguments parsed from argv may override fields in the returned config.
    ///
    /// # Returns
    /// `ApplicationConfig` with engine initialization parameters.
    virtual ApplicationConfig Configure() { return {}; }

    /// Called once after all engine systems are initialized.
    ///
    /// Safe to call `GetWorld()`, `GetVFS()`, `GetCamera()`, `GetInput()`,
    /// and `GetRenderer()` from this method. The command list is NOT exposed —
    /// asset loading is deferred and handled by `SceneRenderer`.
    virtual void OnStart() {}

    /// Called every frame after camera update and before rendering.
    ///
    /// Implement all game logic here: entity transforms, spawning, physics queries.
    /// The renderer reads ECS state after this returns — mutations made here
    /// appear in the current frame.
    ///
    /// # Arguments
    /// * `dt` — Delta time in seconds (clamped to 0.1s max).
    virtual void OnUpdate(float dt) {}

    /// Called once before shutdown. GPU is still valid.
    ///
    /// Release any non-ECS resources here. ECS World and SceneRenderer are
    /// destroyed after this returns.
    virtual void OnStop() {}

    // ── Entry Point ───────────────────────────────────────────────────────

    /// Runs the full application lifecycle.
    ///
    /// Parses CLI args, calls `Configure()`, initializes all engine systems,
    /// calls `OnStart()`, enters the render loop calling `OnUpdate()` each frame,
    /// then calls `OnStop()` and shuts everything down.
    ///
    /// # Arguments
    /// * `argc` — Argument count from `main()`.
    /// * `argv` — Argument vector from `main()`.
    ///
    /// # Returns
    /// `0` on clean exit, non-zero on initialization failure.
    [[nodiscard]] int Run(int argc, char** argv);

    // ── Accessors (valid after OnStart is called) ────────────────────────

    /// Returns the ECS world. Valid from `OnStart()` through `OnStop()`.
    [[nodiscard]] World& GetWorld();

    /// Returns the virtual file system. Valid from `OnStart()` through `OnStop()`.
    [[nodiscard]] IVFS& GetVFS();

    /// Returns the scene camera. Valid from `OnStart()` through `OnStop()`.
    [[nodiscard]] Camera& GetCamera();

    /// Returns the input system. Valid from `OnStart()` through `OnStop()`.
    [[nodiscard]] InputSystem& GetInput();

    /// Returns the scene renderer. Valid from `OnStart()` through `OnStop()`.
    [[nodiscard]] SceneRenderer& GetRenderer();

    // ── Control ───────────────────────────────────────────────────────────

    /// Signals the main loop to exit after the current frame completes.
    ///
    /// `OnStop()` will be called before shutdown. Safe to call from `OnUpdate()`
    /// or from a Lua callback.
    void Quit();

    /// Loads a scene file into the ECS World asynchronously.
    ///
    /// The scene is parsed and entities are created at the start of the next frame.
    /// The current World is cleared before loading.
    ///
    /// # Arguments
    /// * `path` — VFS-relative path to a `.uzscene` file (e.g., "scenes/demo.uzscene").
    void LoadScene(std::string_view path);

    // ── CLI Utilities (static) ────────────────────────────────────────────

    /// Handles `--compose-grid` mode: composes multiple PNGs into a labeled grid.
    ///
    /// Call from `main()` before constructing the Application (already done by
    /// `UZ_APPLICATION_MAIN`). Returns -1 if the flag is not present.
    ///
    /// # Returns
    /// `0` on success, `1` on failure, `-1` if `--compose-grid` not in argv.
    static int TryComposeGrid(int argc, char* argv[]);

    /// Handles `--diff` mode: compares two PNGs with pixel diff heatmap.
    ///
    /// Returns -1 if `--diff` is not in argv. See `DemoApplication::TryDiffImages`
    /// for CLI format.
    ///
    /// # Returns
    /// `0` on success, `1` on failure, `-1` if `--diff` not in argv.
    static int TryDiffImages(int argc, char* argv[]);

private:
    static constexpr uint32_t kFramesInFlight = 3;
    static constexpr float    kMaxDeltaTime   = 0.1f;

    // ── Configuration ────────────────────────────────────────────────────
    ApplicationConfig m_config;
    uint32_t m_shadowDebugMode = 0;  ///< Shadow/buffer debug mode from --shadow-debug CLI.

    // ── Platform objects (raw — matched lifetime to Run() scope) ─────────
    std::unique_ptr<PlatformWindow> m_window;
    std::unique_ptr<IRHIDevice> m_device;
    IRHISwapChain*   m_swapChain = nullptr;
    IRHICommandList* m_cmdList   = nullptr;
    IRHIFence*       m_frameFences[kFramesInFlight] = {};
    uint64_t         m_fenceValues[kFramesInFlight] = {};
    uint64_t         m_globalFenceValue = 0;

    // ── Engine systems (owned) ───────────────────────────────────────────
    std::unique_ptr<SceneRenderer> m_renderer;
    std::unique_ptr<World>         m_world;
    std::unique_ptr<IVFS>          m_vfs;

    // ── Frame state ──────────────────────────────────────────────────────
    uint32_t m_frameIndex = 0;
    uint32_t m_width      = 0;
    uint32_t m_height     = 0;
    bool     m_quitRequested    = false;
    bool     m_screenshotPending = false;
    bool     m_screenshotBase64 = false;  ///< --screenshot-stdout: print base64 PNG to stdout
    bool     m_headless = false;          ///< --headless: create window but keep it hidden (no GUI)

    // ── Pass Inspector ─────────────────────────────────────────────────────
    std::string m_visTarget;           ///< --vis <target>: visualization target name (empty = off).
    bool        m_passValidate = false; ///< --pass-validate: run pass validation after rendering.

    // ── Deferred scene load ───────────────────────────────────────────────
    std::string m_pendingScenePath;

    // ── RenderDoc integration ─────────────────────────────────────────────
    /// Frame on which to auto-trigger a RenderDoc capture (UINT32_MAX = disabled).
    /// Set by the `--renderdoc-capture <N>` CLI flag.
    uint32_t m_renderdocCaptureFrame = UINT32_MAX;

    // ── All-modes capture state ───────────────────────────────────────────
    bool     m_allModesActive    = false;
    uint32_t m_allModeCurrentIdx = 0;
    uint32_t m_allModeCaptureFrame = UINT32_MAX;

    // ── Internal lifecycle ───────────────────────────────────────────────

    /// Parses common CLI arguments into m_config.
    void ParseArgs(int argc, char* argv[]);

    /// Creates window, device, swapchain, command list, and fences.
    ///
    /// # Returns
    /// `true` on success, `false` on any failure (logged).
    [[nodiscard]] bool InitPlatform();

    /// Creates VFS backed by m_config.vfsRoot (defaults to exeDir/assets/).
    void InitVFS();

    /// Creates the ECS World.
    void InitWorld();

    /// Handles a single-frame resize event: swapchain, renderer, camera.
    void HandleResize();

    /// Flushes m_pendingScenePath if set, loading entities into World.
    void FlushPendingSceneLoad();

    /// Destroys all platform objects in reverse creation order.
    void ShutdownPlatform();
};

} // namespace uz
