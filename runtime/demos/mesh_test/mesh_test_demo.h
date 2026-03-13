/// Mesh Test Demo — DemoApplication-based lit cube renderer.
///
/// Renders a .uzmesh cube (Vertex format with normals/tangents) using the
/// standard mesh_vs/mesh_ps shaders with per-object transforms and directional
/// lighting. Tests MeshBuilder + Vertex format + PerObjectCB integration.
///
/// Extends `DemoApplication` to eliminate window/device/swapchain/fence
/// boilerplate. All GPU resources are created in `OnInit()` and released
/// in `OnShutdown()`.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for
/// automated visual testing.

#pragma once

#include "demo_application.h"
#include "rhi_types.h"
#include "rhi_backend.h"
#include "constant_buffers.h"
#include "camera.h"
#include "fps_camera_controller.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace uz {

class InputSystem;
class ShaderPipeline;
class ShaderCompiler;
class ShaderCompilerFXC;
class EditorGrid;
class StatsOverlay;
class ScreenshotCapture;
class CBRingAllocator;
class Mesh;

namespace rg { class RgResourcePool; }

/// Configuration for running the mesh test demo.
struct MeshTestConfig {
    uint32_t frameCount = 0;               ///< Number of frames to render (0 = infinite).
    std::string screenshotPath;            ///< If non-empty, save screenshot to this path.
    uint32_t screenshotFrame = 5;          ///< Frame number to capture (default: 5).
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use.
};

/// Mesh Test Demo — renders a lit cube with N-dot-L directional lighting.
///
/// Extends `DemoApplication` for lifecycle management. The base class owns the
/// window, device, swapchain, command list, and fences. This class owns all
/// rendering resources: shaders, pipeline, mesh, depth buffer, CB ring, editor
/// grid, stats overlay, camera, and input system.
///
/// # Lifecycle
/// - `OnInit()` — Compiles shaders, loads mesh, creates pipeline and depth buffer.
/// - `OnFrame(dt)` — Processes input, updates camera, records render graph.
/// - `OnResize(w, h)` — Recreates depth buffer, updates camera aspect ratio.
/// - `OnShutdown()` — Releases all GPU resources.
class MeshTestDemo final : public DemoApplication {
public:
    /// Constructs the mesh test demo with the given configuration.
    ///
    /// # Arguments
    /// * `config` — Demo settings (frame count, screenshot, backend).
    explicit MeshTestDemo(const MeshTestConfig& config = {});
    ~MeshTestDemo() override;

protected:
    /// Called once after device/swapchain creation. Compiles shaders, loads
    /// the cube mesh, creates pipeline, depth buffer, CB ring, editor grid,
    /// stats overlay, and camera.
    void OnInit() override;

    /// Called every frame with the command list already opened.
    /// Processes input, updates camera, uploads constant buffers, records
    /// the render graph (mesh scene + screenshot + present passes).
    ///
    /// # Arguments
    /// * `dt` — Delta time in seconds since last frame.
    void OnFrame(float dt) override;

    /// Called when the window is resized. Recreates depth buffer and updates
    /// camera aspect ratio and stats overlay dimensions.
    ///
    /// # Arguments
    /// * `width` — New window width in pixels.
    /// * `height` — New window height in pixels.
    void OnResize(uint32_t width, uint32_t height) override;

    /// Called before shutdown. Releases all GPU resources owned by this demo.
    void OnShutdown() override;

private:
    /// Compiles mesh vertex and pixel shaders via ShaderPipeline and creates
    /// RHI shader handles. On macOS, uses embedded MSL source strings.
    void CompileShaders();

    /// Creates the graphics pipeline (input layout, rasterizer, depth).
    void CreatePipeline();

    /// Loads the cube mesh from disk and uploads vertex/index buffers to GPU.
    void LoadMeshData();

    // ── Configuration ───────────────────────────────────────────────────
    MeshTestConfig config_;

    // ── Timing ──────────────────────────────────────────────────────────
    std::chrono::high_resolution_clock::time_point startTime_;
    float totalTime_ = 0.f;

    // ── Input & Camera ──────────────────────────────────────────────────
    std::unique_ptr<InputSystem> input_;
    Camera camera_;
    FPSCameraController cameraCtrl_;

    // ── Render state ────────────────────────────────────────────────────
    bool bindless_ = false;

    // ── Shader compilation ──────────────────────────────────────────────
    std::unique_ptr<ShaderPipeline> shaderPipeline_;
    std::unique_ptr<ShaderCompiler> compiler_;
    std::unique_ptr<ShaderCompilerFXC> compilerFXC_;

    // ── Shader handles ──────────────────────────────────────────────────
    RHIShaderHandle vsHandle_;
    RHIShaderHandle psHandle_;

    // ── Pipeline handle ─────────────────────────────────────────────────
    RHIPipelineHandle pipeline_;

    // ── Mesh data ───────────────────────────────────────────────────────
    std::unique_ptr<Mesh> cubeMesh_;
    RHIBufferHandle vbHandle_;
    RHIBufferHandle ibHandle_;
    uint32_t vbSrvIndex_ = 0;
    RHIIndexFormat indexFormat_ = RHIIndexFormat::Uint32;

    // ── Depth buffer ────────────────────────────────────────────────────
    RHITextureHandle depthBuffer_;

    // ── Constant buffer ring ────────────────────────────────────────────
    std::unique_ptr<CBRingAllocator> cbRing_;

    // ── Overlays ────────────────────────────────────────────────────────
    std::unique_ptr<EditorGrid> editorGrid_;
    std::unique_ptr<StatsOverlay> statsOverlay_;

    // ── Screenshot ──────────────────────────────────────────────────────
    std::unique_ptr<ScreenshotCapture> screenshotCapture_;
    bool screenshotPending_ = false;

    // ── Render graph pool (persistent across frames) ────────────────────
    std::unique_ptr<rg::RgResourcePool> rgPool_;
};

} // namespace uz
