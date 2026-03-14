/// PBR Test Demo — DemoApplication-based Cook-Torrance renderer.
///
/// Renders 5 cubes in a row using deferred or forward PBR shading
/// (F key toggles). Deferred path outputs to a 3-channel G-Buffer
/// (albedo, normals, metallic/roughness) then resolves via a fullscreen
/// lighting pass with 4 point lights + directional sun. Forward path
/// renders directly to the backbuffer in a single pass.
///
/// Extends `DemoApplication` which owns all engine infrastructure (input,
/// shaders, pipelines, CB ring, camera, editor grid, stats overlay,
/// screenshot capture, render graph pool). This class only owns
/// demo-specific resources: mesh, textures, G-Buffer, shadow atlas.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for
/// automated visual testing. Debug visualization of individual G-Buffer
/// channels via number keys 0-7.

#pragma once

#include "demo_application.h"
#include "uzengine.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace uz {

/// Configuration for running the PBR test demo.
struct PBRTestConfig {
    uint32_t frameCount = 0;               ///< Number of frames to render (0 = infinite).
    std::string screenshotPath;            ///< If non-empty, save screenshot to this path.
    uint32_t screenshotFrame = 5;          ///< Frame number to capture (default: 5).
    std::optional<RHIBackend> backend;     ///< If set, overrides default DX12 backend.
    std::optional<ShaderModel> shaderModel; ///< If set, forces a specific shader model.
};

/// PBR Test Demo — renders 5 PBR-lit cubes with deferred/forward path toggle.
///
/// Extends `DemoApplication` for lifecycle management. The base class owns the
/// window, device, swapchain, command list, fences, AND all engine infrastructure
/// (input, shaders, pipelines, CB ring, camera, grid, stats, screenshot, RG pool).
/// This class only owns demo-specific resources: mesh, textures, G-Buffer,
/// shadow atlas.
///
/// # Lifecycle
/// - `OnInit()` — Loads mesh/textures, creates G-Buffer and shadow atlas.
/// - `OnFrame(dt)` — Processes input, updates camera, builds draw list, records render graph.
/// - `OnResize(w, h)` — Recreates G-Buffer textures.
/// - `OnShutdown()` — Releases demo-specific GPU resources.
class PBRTestDemo final : public DemoApplication {
public:
    /// Constructs the PBR test demo with the given configuration.
    ///
    /// # Arguments
    /// * `config` — Demo settings (frame count, screenshot, backend).
    explicit PBRTestDemo(const PBRTestConfig& config = {});
    ~PBRTestDemo() override;

protected:
    /// Called once after engine infrastructure is initialized.
    /// The command list is already in recording state. Loads mesh and textures,
    /// creates G-Buffer and shadow atlas.
    void OnInit() override;

    /// Called every frame with the command list already opened.
    /// Processes input, updates camera, builds the draw list, records the
    /// render graph (shadow + deferred/forward + overlay + screenshot passes).
    ///
    /// # Arguments
    /// * `dt` — Delta time in seconds since last frame.
    void OnFrame(float dt) override;

    /// Called when the window is resized. Recreates G-Buffer textures.
    ///
    /// # Arguments
    /// * `width` — New window width in pixels.
    /// * `height` — New window height in pixels.
    void OnResize(uint32_t width, uint32_t height) override;

    /// Called before shutdown. Releases demo-specific GPU resources.
    void OnShutdown() override;

private:
    /// Loads the cube mesh from disk and uploads vertex/index buffers to GPU.
    void LoadMeshData();

    /// Loads PBR material textures (albedo, normal, roughness, metallic, AO)
    /// and the checker texture for the ground plane.
    void LoadTextures();

    /// Creates G-Buffer textures at the current window dimensions.
    void CreateGBuffer();

    /// Destroys and recreates G-Buffer textures at the given dimensions.
    void RecreateGBuffer(uint32_t width, uint32_t height);

    // ── Configuration ───────────────────────────────────────────────────
    PBRTestConfig config_;

    // ── Timing ──────────────────────────────────────────────────────────
    std::chrono::high_resolution_clock::time_point startTime_;
    float totalTime_ = 0.f;

    // ── Render state ────────────────────────────────────────────────────
    RenderPath renderPath_ = RenderPath::Deferred;
    uint32_t debugModeValue_ = 0;  ///< GBufferDebugMode::Count = off.

    // ── Mesh data ───────────────────────────────────────────────────────
    GPUMesh cubeMesh_;

    // ── Textures ────────────────────────────────────────────────────────
    TextureLoadResult albedoTex_;
    TextureLoadResult normalTex_;
    TextureLoadResult roughnessTex_;
    TextureLoadResult metallicTex_;
    TextureLoadResult aoTex_;
    TextureLoadResult checkerTex_;

    // ── G-Buffer ────────────────────────────────────────────────────────
    GBuffer gbuffer_;

    // ── Shadow atlas ────────────────────────────────────────────────────
    RHITextureHandle shadowAtlas_;
    uint32_t shadowAtlasSrv_ = 0;

    // ── Fallback textures (SM5/Vulkan non-bindless path) ─────────────────
    RHITextureHandle fallbackWhite_;   ///< 1x1 white (missing albedo/roughness/AO)
    RHITextureHandle fallbackBlack_;   ///< 1x1 black (missing metallic — metallic=0)
    RHITextureHandle fallbackNormal_;  ///< 1x1 flat normal (128,128,255,255) for missing normal maps
};

} // namespace uz
