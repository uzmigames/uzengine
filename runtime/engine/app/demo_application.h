#pragma once

/// DemoApplication — Base class that eliminates GPU boilerplate from demos.
///
/// Owns the full lifecycle: window creation, RHI device/swapchain/command list,
/// per-frame fence synchronization, resize handling, minimize sleep, and frame
/// timing. Subclasses implement only `OnInit()` and `OnFrame()`.
///
/// # Usage
/// ```cpp
/// class MyDemo : public DemoApplication {
/// public:
///     MyDemo() : DemoApplication("My Demo") {}
/// protected:
///     void OnInit() override { /* load shaders, meshes */ }
///     void OnFrame(float dt) override { /* render */ }
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
///
/// # Minimize
/// Rendering is skipped while minimized (swapchain surface is 0x0).
/// The message pump continues running so the window stays responsive.
///
/// # Shutdown Stats
/// On exit, logs total frames, elapsed time, and average FPS.

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class IRHISwapChain;
class IRHIFence;
class PlatformWindow;
enum class RHIBackend : uint8_t;

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
};

/// Abstract base class that eliminates GPU boilerplate from demo applications.
///
/// Owns the full application lifecycle: window creation, RHI device/swapchain
/// initialization, per-frame fence synchronization, resize handling, minimize
/// sleep, and frame timing. Subclasses override only `OnInit()` and `OnFrame()`
/// to provide demo-specific rendering logic.
///
/// # Usage
/// ```cpp
/// class MyDemo : public DemoApplication {
/// public:
///     MyDemo() : DemoApplication("My Demo") {}
/// protected:
///     void OnInit() override { /* load shaders, meshes */ }
///     void OnFrame(float dt) override { /* record draw commands */ }
/// };
///
/// int main() { return MyDemo().Run(); }
/// ```
///
/// # Lifecycle
/// `Run()` drives the full sequence:
/// 1. **Init** — creates window, device, swapchain, command list, fences.
/// 2. **OnInit()** — subclass loads shaders, meshes, pipelines.
/// 3. **Frame loop** — pump messages, wait oldest fence, call `OnFrame(dt)`,
///    execute, present, signal fence.
/// 4. **OnShutdown()** — subclass releases its resources.
/// 5. **Teardown** — destroys all platform/GPU objects in reverse order.
///
/// # Frame Sync
/// Uses triple-buffering (`kFramesInFlight = 3`) with per-frame fences.
/// The base class waits on the oldest fence before recording new commands,
/// ensuring the GPU is not still reading that frame's resources.
///
/// # Resize
/// On `WasResized()`, the base class calls `WaitIdle()`, resizes the swapchain,
/// then invokes `OnResize()` so subclasses can recreate size-dependent resources.
///
/// # Thread Safety
/// NOT thread-safe. All methods (including `Run()`) must be called from the
/// main thread. The render loop is single-threaded by design.
class DemoApplication {
public:
    /// Constructs a demo application with the given configuration.
    ///
    /// Does NOT create the window or device — that happens in `Run()`.
    ///
    /// # Arguments
    /// * `config` — Initialization parameters (title, size, frame cap, vsync).
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

    /// Runs the application: creates window/device, enters the render loop,
    /// cleans up on exit.
    ///
    /// # Returns
    /// `0` on success, non-zero on initialization failure.
    ///
    /// # Lifecycle
    /// 1. Creates window, device, swapchain, command list, fences.
    /// 2. Calls `OnInit()` for subclass resource setup.
    /// 3. Enters render loop: pump messages, wait fence, record, execute, present.
    /// 4. On exit: calls `OnShutdown()`, destroys all GPU objects and window.
    [[nodiscard]] int Run();

protected:
    // ── Subclass hooks ──────────────────────────────────────────────────

    /// Called once after device/swapchain creation. Load shaders, buffers,
    /// textures, and pipelines here.
    ///
    /// The device, command list, and swapchain are fully initialized when
    /// this is called. Use `GetDevice()`, `GetCommandList()`, etc.
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
    /// The swapchain has already been resized when this is called.
    /// `GetWidth()` and `GetHeight()` return the new dimensions.
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
    virtual void OnShutdown() {}

    // ── Accessors ───────────────────────────────────────────────────────

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

private:
    static constexpr uint32_t kFramesInFlight = 3;

    /// Maximum delta time clamp — prevents physics/animation explosions
    /// after breakpoints or system stalls.
    static constexpr float kMaxDeltaTime = 0.1f;

    // ── Configuration ───────────────────────────────────────────────────
    DemoConfig m_config;

    // ── Core objects (owned, destroyed in reverse creation order) ────────
    PlatformWindow* m_window = nullptr;
    IRHIDevice* m_device = nullptr;
    IRHISwapChain* m_swapChain = nullptr;
    IRHICommandList* m_cmdList = nullptr;
    IRHIFence* m_frameFences[kFramesInFlight] = {};
    uint64_t m_fenceValues[kFramesInFlight] = {};
    uint64_t m_globalFenceValue = 0;

    // ── Frame state ─────────────────────────────────────────────────────
    uint32_t m_frameIndex = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // ── Internal methods ────────────────────────────────────────────────

    /// Creates window, device, swapchain, command list, and fences.
    /// Returns true on success.
    bool InitPlatform();

    /// Destroys all platform objects in reverse creation order.
    void ShutdownPlatform();
};

} // namespace uz
