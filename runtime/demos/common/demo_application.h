#pragma once

/// DemoApplication — Base class that eliminates GPU and engine boilerplate from demos.
///
/// Owns the full lifecycle: window creation, RHI device/swapchain/command list,
/// per-frame fence synchronization, resize handling, minimize sleep, frame
/// timing, AND common engine infrastructure (input, shaders, pipelines, camera,
/// CB ring, editor grid, stats overlay, screenshot capture, render graph pool).
///
/// Subclasses implement only `OnInit()` and `OnFrame()` with demo-specific logic.
/// The command list is already in recording state when `OnInit()` is called, so
/// subclasses can immediately upload meshes and textures.
///
/// # Usage
/// ```cpp
/// class MyDemo : public DemoApplication {
/// public:
///     MyDemo() : DemoApplication(DemoConfig{.title = "My Demo"}) {}
/// protected:
///     void OnInit() override { /* load meshes, textures — cmdList is recording */ }
///     void OnFrame(float dt) override { /* render using GetCamera(), GetCBRing(), etc. */ }
/// };
///
/// int main() { return MyDemo().Run(); }
/// ```
///
/// # Frame Sync
/// Uses triple-buffering (`kFramesInFlight = 3`) with per-frame fences.
/// The base class waits on the oldest fence before recording new commands,
/// ensuring the GPU is not still reading that frame's resources.
///
/// # Resize
/// On `WasResized()`, the base class calls `WaitIdle()`, resizes the swapchain,
/// then invokes `OnResize()` so subclasses can recreate size-dependent resources.
/// The base class also updates the camera aspect ratio and stats overlay dimensions.
///
/// # Minimize
/// Rendering is skipped while minimized (swapchain surface is 0x0).
/// The message pump continues running so the window stays responsive.
///
/// # Screenshot
/// Set `DemoConfig::screenshotPath` to enable automatic screenshot capture.
/// The screenshot is NOT taken until the subclass calls `SetReady()`, signaling
/// that all resources are loaded. After ready, it waits `screenshotFrame` more
/// frames (default 5) to allow GPU pipeline warmup before capturing.

#include <cstdint>
#include <memory>
#include <string>

namespace uz {

// Forward declarations — keeps header lightweight (no uzengine.h)
class IRHIDevice;
class IRHICommandList;
class IRHISwapChain;
class IRHIFence;
class PlatformWindow;
class InputSystem;
class ShaderManager;
class PipelineLibrary;
class CBRingAllocator;
class EditorGrid;
class StatsOverlay;
class ScreenshotCapture;
class Camera;
class FPSCameraController;
enum class RHIBackend : uint8_t;

namespace rg { class RgResourcePool; }

struct Vec3;

/// Configuration for DemoApplication initialization.
///
/// Passed to the constructor to override defaults. All fields have sensible
/// defaults for a windowed demo.
struct DemoConfig {
    const char* title = "UzEngine Demo";  ///< Window title bar text.
    uint32_t width = 1280;                ///< Initial window width (pixels).
    uint32_t height = 720;                ///< Initial window height (pixels).
    uint32_t maxFrames = 0;               ///< Max frames to render (0 = unlimited).
    bool vsync = false;                   ///< Enable vertical sync on present.
    bool enableValidation = true;         ///< Enable GPU validation layer (debug builds).

    // ── Screenshot ──────────────────────────────────────────────────────
    std::string screenshotPath;           ///< If non-empty, capture a screenshot to this path.
    uint32_t screenshotFrame = 5;         ///< Frames to wait AFTER SetReady() before capturing.

    // ── Camera defaults ─────────────────────────────────────────────────
    float cameraMoveSpeed = 10.f;         ///< FPS camera movement speed (units/sec).
    float cameraPositionX = 0.f;          ///< Default camera X position.
    float cameraPositionY = 5.f;          ///< Default camera Y position.
    float cameraPositionZ = 2.f;          ///< Default camera Z position.
    float cameraYawDeg = -90.f;           ///< Default camera yaw in degrees.
    float cameraPitchDeg = -20.f;         ///< Default camera pitch in degrees.
};

/// Abstract base class that eliminates GPU and engine boilerplate from demos.
///
/// Owns the full application lifecycle: window, RHI device/swapchain, fences,
/// AND common engine systems (input, shaders, pipelines, CB ring, camera,
/// editor grid, stats overlay, screenshot capture, render graph resource pool).
///
/// # Lifecycle
/// `Run()` drives the full sequence:
/// 1. Creates window, device, swapchain, command list, fences.
/// 2. Initializes engine infrastructure (input, shaders, pipelines, camera, etc.).
/// 3. Opens the command list (`Begin()`) so subclass can upload resources.
/// 4. Calls `OnInit()` — subclass loads meshes, textures, creates demo-specific resources.
/// 5. Flushes init uploads (End + Execute + WaitIdle).
/// 6. Enters render loop: pump messages, wait fence, Begin, OnFrame(dt), End, Execute, Present.
/// 7. On exit: calls `OnShutdown()`, destroys all GPU/engine objects and window.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the main thread.
class DemoApplication {
public:
    /// Constructs a demo application with the given configuration.
    ///
    /// Does NOT create the window or device — that happens in `Run()`.
    ///
    /// # Arguments
    /// * `config` — Initialization parameters (title, size, frame cap, vsync, camera, screenshot).
    explicit DemoApplication(const DemoConfig& config);

    /// Constructs a demo application with just a title and default settings.
    ///
    /// Convenience overload for simple demos that only need a custom title.
    ///
    /// # Arguments
    /// * `title` — Window title bar text.
    /// * `width` — Initial window width in pixels (default 1280).
    /// * `height` — Initial window height in pixels (default 720).
    explicit DemoApplication(const char* title,
                             uint32_t width = 1280,
                             uint32_t height = 720);

    virtual ~DemoApplication();

    DemoApplication(const DemoApplication&) = delete;
    DemoApplication& operator=(const DemoApplication&) = delete;

    /// Runs the application: creates window/device, initializes engine systems,
    /// enters the render loop, cleans up on exit.
    ///
    /// # Returns
    /// `0` on success, non-zero on initialization failure.
    [[nodiscard]] int Run();

protected:
    // ── Subclass hooks ──────────────────────────────────────────────────

    /// Called once after device/swapchain/engine-infrastructure creation.
    /// Load meshes, textures, and demo-specific resources here.
    ///
    /// The command list is already in recording state (`Begin()` has been called),
    /// so `UploadMesh()` and `TextureLoader::Load()` can be used immediately.
    /// The base class will flush all init uploads after `OnInit()` returns.
    virtual void OnInit() = 0;

    /// Called every frame. Record rendering commands here.
    ///
    /// The command list has already been opened (`Begin()` called).
    /// The base class handles `End()`, `Execute()`, `Present()`, and fence
    /// signaling after this returns.
    ///
    /// # Arguments
    /// * `dt` — Time since last frame in seconds (clamped to 0.1s max).
    virtual void OnFrame(float dt) = 0;

    /// Called when the window is resized. Recreate size-dependent resources
    /// (depth buffers, render targets, etc.) here.
    ///
    /// The swapchain has already been resized. The base class has already updated
    /// the camera aspect ratio and stats overlay dimensions. `GetWidth()` and
    /// `GetHeight()` return the new dimensions.
    ///
    /// # Arguments
    /// * `width` — New window width in pixels.
    /// * `height` — New window height in pixels.
    virtual void OnResize(uint32_t width, uint32_t height) {
        (void)width; (void)height;
    }

    /// Called before shutdown. Release subclass-owned GPU resources here.
    ///
    /// The device is still valid. `WaitIdle()` has already been called.
    /// Base-class-owned systems (shaders, pipelines, etc.) are destroyed AFTER this.
    virtual void OnShutdown() {}

    // ── Core RHI Accessors ──────────────────────────────────────────────

    /// Returns the RHI device. Valid after `Run()` enters the main loop.
    [[nodiscard]] IRHIDevice* GetDevice() const { return m_device; }

    /// Returns the primary graphics command list.
    [[nodiscard]] IRHICommandList* GetCommandList() const { return m_cmdList; }

    /// Returns the swapchain for the demo window.
    [[nodiscard]] IRHISwapChain* GetSwapChain() const { return m_swapChain; }

    /// Returns the platform window.
    [[nodiscard]] PlatformWindow* GetWindow() const { return m_window; }

    /// Returns the current frame index (monotonically increasing from 0).
    [[nodiscard]] uint32_t GetFrameIndex() const { return m_frameIndex; }

    /// Returns the current swapchain/window width in pixels.
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }

    /// Returns the current swapchain/window height in pixels.
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }

    /// Returns true if the device supports SM6.6+ bindless resource access.
    [[nodiscard]] bool IsBindless() const;

    // ── Engine Infrastructure Accessors ─────────────────────────────────

    /// Returns the input system. Valid after `OnInit()` is called.
    [[nodiscard]] InputSystem* GetInput() const;

    /// Returns the shader manager. Valid after `OnInit()` is called.
    [[nodiscard]] ShaderManager* GetShaderManager() const;

    /// Returns the pipeline library. Valid after `OnInit()` is called.
    [[nodiscard]] PipelineLibrary* GetPipelines() const;

    /// Returns the per-frame constant buffer ring allocator (3 frames, 128KB).
    [[nodiscard]] CBRingAllocator* GetCBRing() const;

    /// Returns the editor grid overlay renderer.
    [[nodiscard]] EditorGrid* GetEditorGrid() const;

    /// Returns the stats overlay (FPS, draw calls, etc.).
    [[nodiscard]] StatsOverlay* GetStatsOverlay() const;

    /// Returns the camera. Valid after `OnInit()` is called.
    /// Subclasses may reposition/reconfigure via SetPosition/SetPerspective.
    [[nodiscard]] Camera& GetCamera();

    /// Returns the FPS camera controller. Valid after `OnInit()` is called.
    [[nodiscard]] FPSCameraController& GetCameraController();

    /// Returns the render graph resource pool (persistent across frames).
    [[nodiscard]] rg::RgResourcePool* GetRgPool() const;

    /// Returns the screenshot capture object (may be null if no screenshot path set).
    [[nodiscard]] ScreenshotCapture* GetScreenshotCapture() const;

    /// Returns true if a screenshot is pending (not yet captured).
    [[nodiscard]] bool IsScreenshotPending() const { return m_screenshotPending; }

    /// Returns true if a screenshot was captured and deferred save is pending.
    [[nodiscard]] bool IsScreenshotSaveDeferred() const { return m_screenshotSaveDeferred; }

    /// Marks the deferred screenshot save as completed.
    void ClearScreenshotSaveDeferred() { m_screenshotSaveDeferred = false; m_screenshotPending = false; }

    /// Marks that a screenshot was captured and deferred save is needed next frame.
    void SetScreenshotSaveDeferred() { m_screenshotSaveDeferred = true; }

    /// Returns the configured screenshot path.
    [[nodiscard]] const std::string& GetScreenshotPath() const;

    /// Returns the configured screenshot frame number.
    [[nodiscard]] uint32_t GetScreenshotFrame() const;

    /// Marks the demo as fully loaded and ready to render.
    ///
    /// Subclasses MUST call this once all resources (meshes, textures, PSOs,
    /// render targets) are loaded and at least one clean frame has been rendered.
    /// Screenshots are only captured after `IsReady()` returns true AND at least
    /// `screenshotFrame` frames have elapsed since the ready signal.
    void SetReady() { m_ready = true; m_readyFrame = m_frameIndex; }

    /// Returns true if the demo has signaled that all resources are loaded.
    [[nodiscard]] bool IsReady() const { return m_ready; }

    /// Returns true if a screenshot should be captured this frame.
    ///
    /// Requires: screenshot path is set, not yet captured, demo is ready,
    /// and enough frames have passed since ready to allow GPU pipeline warmup.
    [[nodiscard]] bool ShouldCaptureScreenshot() const {
        return m_screenshotPending && m_ready
            && (m_frameIndex - m_readyFrame) >= m_config.screenshotFrame;
    }

private:
    static constexpr uint32_t kFramesInFlight = 3;

    /// Maximum delta time clamp — prevents physics/animation explosions
    /// after breakpoints or system stalls.
    static constexpr float kMaxDeltaTime = 0.1f;

    // ── Configuration ───────────────────────────────────────────────────
    DemoConfig m_config;

    // ── Core RHI objects (owned, destroyed in reverse creation order) ───
    PlatformWindow* m_window = nullptr;
    IRHIDevice* m_device = nullptr;
    IRHISwapChain* m_swapChain = nullptr;
    IRHICommandList* m_cmdList = nullptr;
    IRHIFence* m_frameFences[kFramesInFlight] = {};
    uint64_t m_fenceValues[kFramesInFlight] = {};
    uint64_t m_globalFenceValue = 0;

    // ── Engine infrastructure (owned via unique_ptr) ────────────────────
    std::unique_ptr<InputSystem> m_input;
    std::unique_ptr<ShaderManager> m_shaderMgr;
    std::unique_ptr<PipelineLibrary> m_pipelines;
    std::unique_ptr<CBRingAllocator> m_cbRing;
    std::unique_ptr<EditorGrid> m_editorGrid;
    std::unique_ptr<StatsOverlay> m_statsOverlay;
    std::unique_ptr<ScreenshotCapture> m_screenshotCapture;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<FPSCameraController> m_cameraCtrl;
    std::unique_ptr<rg::RgResourcePool> m_rgPool;

    // ── Screenshot state ────────────────────────────────────────────────
    bool m_screenshotPending = false;
    bool m_screenshotSaveDeferred = false;

    // ── Ready state ──────────────────────────────────────────────────────
    bool m_ready = false;              ///< True after subclass calls SetReady().
    uint32_t m_readyFrame = 0;         ///< Frame index when SetReady() was called.

    // ── Frame state ─────────────────────────────────────────────────────
    uint32_t m_frameIndex = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // ── Internal methods ────────────────────────────────────────────────

    /// Creates window, device, swapchain, command list, and fences.
    /// Returns true on success.
    bool InitPlatform();

    /// Initializes engine infrastructure: input, shaders, pipelines, CB ring,
    /// camera, editor grid, stats overlay, screenshot capture, RG pool.
    /// Called after InitPlatform(), before OnInit().
    void InitInfrastructure();

    /// Shuts down engine infrastructure in reverse creation order.
    void ShutdownInfrastructure();

    /// Destroys all platform objects in reverse creation order.
    void ShutdownPlatform();
};

} // namespace uz
