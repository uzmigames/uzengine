/// Post-Process Test Demo — DemoApplication-based HDR + tonemap + FXAA renderer.
///
/// Renders 3 bright emissive spheres using a forward pass into an HDR render
/// target (RGBA16Float), then applies the post-processing chain:
///   1. Tone mapping (ACES filmic, HDR -> LDR)
///   2. FXAA (edge-detect + directional blur)
///
/// Each effect can be toggled at runtime:
///   T — toggle tone mapping
///   A — toggle FXAA
///
/// Extends `DemoApplication` which owns all engine infrastructure (input,
/// shaders, pipelines, CB ring, camera, editor grid, stats overlay,
/// screenshot capture, render graph pool). This class only owns
/// demo-specific resources: mesh, textures, HDR target, post-process PSOs.
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

class Mesh;

/// Configuration for running the post-process test demo.
struct PostProcessTestConfig {
    uint32_t frameCount = 0;               ///< Number of frames to render (0 = infinite).
    std::string screenshotPath;            ///< If non-empty, save screenshot to this path.
    uint32_t screenshotFrame = 5;          ///< Frame number to capture (default: 5).
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use.
    bool enableTonemap = true;             ///< Initial state of tone mapping (toggle with T).
    bool enableFXAA    = true;             ///< Initial state of FXAA (toggle with A).
};

/// Post-Process Test Demo — renders HDR spheres with tonemap + FXAA chain.
///
/// Extends `DemoApplication` for lifecycle management. The base class owns the
/// window, device, swapchain, command list, fences, AND all engine infrastructure
/// (input, shaders, pipelines, CB ring, camera, grid, stats, screenshot, RG pool).
/// This class only owns demo-specific resources: mesh, textures, HDR target,
/// post-process PSOs and shaders.
///
/// # Lifecycle
/// - `OnInit()` — Compiles shaders, loads mesh/textures, creates pipelines and render targets.
/// - `OnFrame(dt)` — Processes input, updates camera, records render graph (Scene3D -> ToneMap -> FXAA).
/// - `OnResize(w, h)` — Recreates render targets.
/// - `OnShutdown()` — Releases demo-specific GPU resources.
class PostProcessTestDemo final : public DemoApplication {
public:
    /// Constructs the post-process test demo with the given configuration.
    ///
    /// # Arguments
    /// * `config` — Demo settings (frame count, screenshot, backend, toggle states).
    explicit PostProcessTestDemo(const PostProcessTestConfig& config = {});
    ~PostProcessTestDemo() override;

protected:
    /// Called once after engine infrastructure is initialized.
    /// The command list is already in recording state. Compiles shaders,
    /// loads mesh/textures, creates pipelines and render targets.
    void OnInit() override;

    /// Called every frame with the command list already opened.
    /// Processes input (T/A toggles), updates camera, records the render
    /// graph (Scene3D -> ToneMap -> FXAA -> Present).
    ///
    /// # Arguments
    /// * `dt` — Delta time in seconds since last frame.
    void OnFrame(float dt) override;

    /// Called when the window is resized. Recreates render targets.
    ///
    /// # Arguments
    /// * `width` — New window width in pixels.
    /// * `height` — New window height in pixels.
    void OnResize(uint32_t width, uint32_t height) override;

    /// Called before shutdown. Releases demo-specific GPU resources.
    void OnShutdown() override;

private:
    /// Compiles all shaders (fullscreen VS, tonemap PS, FXAA PS, mesh VS/PS)
    /// via the ShaderManager's pipeline and creates RHI shader handles.
    void CompileShaders();

    /// Creates all PSOs (tonemap, FXAA, mesh HDR pipeline).
    void CreatePipelines();

    /// Creates the sphere mesh and uploads vertex/index buffers to GPU.
    void LoadMeshData();

    /// Loads the checker texture.
    void LoadTextures();

    /// Creates depth buffer and intermediate render targets at the given size.
    void CreateRenderTargets(uint32_t width, uint32_t height);

    /// Destroys and recreates render targets at the given dimensions.
    void RecreateRenderTargets(uint32_t width, uint32_t height);

    // ── Configuration ───────────────────────────────────────────────────
    PostProcessTestConfig config_;

    // ── Timing ──────────────────────────────────────────────────────────
    std::chrono::high_resolution_clock::time_point startTime_;
    float totalTime_ = 0.f;

    // ── Render state ────────────────────────────────────────────────────
    bool enableTonemap_ = true;
    bool enableFXAA_ = true;

    // ── Shader handles ──────────────────────────────────────────────────
    RHIShaderHandle fsVS_;
    RHIShaderHandle tonemapPS_;
    RHIShaderHandle fxaaPS_;
    RHIShaderHandle meshVS_;
    RHIShaderHandle meshPS_;

    // ── Pipeline handles ────────────────────────────────────────────────
    RHIPipelineHandle tonemapPSO_;
    RHIPipelineHandle fxaaPSO_;
    RHIPipelineHandle meshPipeline_;

    // ── Mesh data ───────────────────────────────────────────────────────
    std::unique_ptr<Mesh> sphereMesh_;
    RHIBufferHandle vbHandle_;
    RHIBufferHandle ibHandle_;
    uint32_t vbSrvIndex_ = 0;
    RHIIndexFormat indexFormat_ = RHIIndexFormat::Uint32;

    // ── Textures ────────────────────────────────────────────────────────
    RHITextureHandle checkerHandle_;
    uint32_t checkerSrvIndex_ = UINT32_MAX;

    // ── Render targets ──────────────────────────────────────────────────
    RHITextureHandle depthBuffer_;
    RHITextureHandle hdrTarget_;
    RHITextureHandle tonemapTarget_;
};

} // namespace uz
