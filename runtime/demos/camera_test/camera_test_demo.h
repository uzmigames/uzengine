/// Camera Test Demo — DemoApplication-based colored cube renderer.
///
/// Renders a colored cube in 3D with FPS camera controls (WASD + mouse look).
/// Extends `DemoApplication` which owns all engine infrastructure (input,
/// shaders, pipelines, CB ring, camera, editor grid, stats overlay,
/// screenshot capture, render graph pool). This class only owns
/// demo-specific resources: the cube vertex buffer and depth buffer.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for automated
/// visual testing. When a screenshot is requested, the demo captures the back
/// buffer on frame N, saves it as PNG, and exits.

#pragma once

#include "demo_application.h"
#include "uzengine.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace uz {

/// Configuration for running the camera test demo.
struct CameraTestConfig {
    uint32_t frameCount = 0;               ///< Number of frames to render (0 = infinite).
    std::string screenshotPath;            ///< If non-empty, save screenshot to this path.
    uint32_t screenshotFrame = 5;          ///< Frame number to capture (default: 5).
};

/// Camera Test Demo — renders a colored cube with FPS camera controls.
///
/// Extends `DemoApplication` for lifecycle management. The base class owns the
/// window, device, swapchain, command list, fences, AND all engine infrastructure
/// (input, shaders, pipelines, CB ring, camera, grid, stats, screenshot, RG pool).
/// This class only owns demo-specific resources: cube vertex buffer and depth buffer.
///
/// # Lifecycle
/// - `OnInit()` — Creates cube vertex buffer, depth buffer.
/// - `OnFrame(dt)` — Processes input, updates camera, records render graph.
/// - `OnResize(w, h)` — Recreates depth buffer.
/// - `OnShutdown()` — Releases demo-specific GPU resources.
class CameraTestDemo final : public DemoApplication {
public:
    /// Constructs the camera test demo with the given configuration.
    ///
    /// # Arguments
    /// * `config` — Demo settings (frame count, screenshot).
    explicit CameraTestDemo(const CameraTestConfig& config = {});
    ~CameraTestDemo() override;

protected:
    /// Called once after engine infrastructure is initialized.
    /// The command list is already in recording state. Creates cube vertex
    /// buffer and depth buffer.
    void OnInit() override;

    /// Called every frame with the command list already opened.
    /// Processes input, updates camera, builds the render graph
    /// (cube draw + grid + stats overlay + screenshot + present).
    ///
    /// # Arguments
    /// * `dt` — Delta time in seconds since last frame.
    void OnFrame(float dt) override;

    /// Called when the window is resized. Recreates depth buffer at new size.
    ///
    /// # Arguments
    /// * `width` — New window width in pixels.
    /// * `height` — New window height in pixels.
    void OnResize(uint32_t width, uint32_t height) override;

    /// Called before shutdown. Releases demo-specific GPU resources.
    void OnShutdown() override;

private:
    // ── Configuration ───────────────────────────────────────────────────
    CameraTestConfig config_;

    // ── Timing ──────────────────────────────────────────────────────────
    std::chrono::high_resolution_clock::time_point startTime_;
    float totalTime_ = 0.f;

    // ── Cube geometry ───────────────────────────────────────────────────
    RHIBufferHandle cubeVB_;          ///< Cube vertex buffer (36 vertices).
    uint32_t cubeVBSrvIndex_ = 0;    ///< Bindless SRV index (SM6.6+).

    // ── Pipeline ────────────────────────────────────────────────────────
    RHIShaderHandle vsHandle_;        ///< Cube vertex shader.
    RHIShaderHandle psHandle_;        ///< Cube pixel shader.
    RHIPipelineHandle pipeline_;      ///< Cube draw pipeline.

    // ── Depth buffer ────────────────────────────────────────────────────
    RHITextureHandle depthBuffer_;    ///< D32Float depth buffer, window-sized.
};

} // namespace uz
