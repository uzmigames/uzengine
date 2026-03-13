/// Mesh Test Demo — DemoApplication-based lit cube renderer.
///
/// Renders a .uzmesh cube (Vertex format with normals/tangents) using the
/// standard mesh_vs/mesh_ps shaders with per-object transforms and directional
/// lighting. Tests MeshBuilder + Vertex format + PerObjectCB integration.
///
/// Extends `DemoApplication` which owns all engine infrastructure (input,
/// shaders, pipelines, CB ring, camera, editor grid, stats overlay,
/// screenshot capture, render graph pool). This class only owns demo-specific
/// resources: mesh, shaders, pipeline, depth buffer.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for
/// automated visual testing.

#pragma once

#include "demo_application.h"
#include "uzengine.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace uz {

class ShaderPipeline;
class ShaderCompiler;
class ShaderCompilerFXC;
class Mesh;

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
/// window, device, swapchain, command list, fences, AND all engine infrastructure
/// (input, shaders, pipelines, CB ring, camera, grid, stats, screenshot, RG pool).
/// This class only owns demo-specific resources: mesh, shaders, pipeline,
/// depth buffer.
///
/// # Lifecycle
/// - `OnInit()` — Compiles shaders, loads mesh, creates pipeline and depth buffer.
/// - `OnFrame(dt)` — Processes input, updates camera, records render graph.
/// - `OnResize(w, h)` — Recreates depth buffer.
/// - `OnShutdown()` — Releases demo-specific GPU resources.
class MeshTestDemo final : public DemoApplication {
public:
    /// Constructs the mesh test demo with the given configuration.
    ///
    /// # Arguments
    /// * `config` — Demo settings (frame count, screenshot, backend).
    explicit MeshTestDemo(const MeshTestConfig& config = {});
    ~MeshTestDemo() override;

protected:
    /// Called once after engine infrastructure is initialized.
    /// The command list is already in recording state. Compiles shaders,
    /// loads the cube mesh, creates pipeline and depth buffer.
    void OnInit() override;

    /// Called every frame with the command list already opened.
    /// Processes input, updates camera, uploads constant buffers, records
    /// the render graph (mesh scene + screenshot + present passes).
    ///
    /// # Arguments
    /// * `dt` — Delta time in seconds since last frame.
    void OnFrame(float dt) override;

    /// Called when the window is resized. Recreates depth buffer.
    ///
    /// # Arguments
    /// * `width` — New window width in pixels.
    /// * `height` — New window height in pixels.
    void OnResize(uint32_t width, uint32_t height) override;

    /// Called before shutdown. Releases demo-specific GPU resources.
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
};

} // namespace uz
