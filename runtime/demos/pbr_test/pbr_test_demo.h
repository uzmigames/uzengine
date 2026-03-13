/// PBR Test Demo — DemoApplication-based Cook-Torrance renderer.
///
/// Renders 5 cubes in a row using deferred or forward PBR shading
/// (F key toggles). Deferred path outputs to a 3-channel G-Buffer
/// (albedo, normals, metallic/roughness) then resolves via a fullscreen
/// lighting pass with 4 point lights + directional sun. Forward path
/// renders directly to the backbuffer in a single pass.
///
/// Extends `DemoApplication` to eliminate window/device/swapchain/fence
/// boilerplate. All GPU resources are created in `OnInit()` and released
/// in `OnShutdown()`.
///
/// Supports `--screenshot <path>` and `--screenshot-frame <N>` for
/// automated visual testing. Debug visualization of individual G-Buffer
/// channels via number keys 0-7.

#pragma once

#include "demo_application.h"
#include "rhi_types.h"
#include "rhi_backend.h"
#include "constant_buffers.h"
#include "camera.h"
#include "fps_camera_controller.h"
#include "render_path.h"

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

/// Configuration for running the PBR test demo.
struct PBRTestConfig {
    uint32_t frameCount = 0;               ///< Number of frames to render (0 = infinite).
    std::string screenshotPath;            ///< If non-empty, save screenshot to this path.
    uint32_t screenshotFrame = 5;          ///< Frame number to capture (default: 5).
    RHIBackend backend = RHIBackend::DX12; ///< RHI backend to use.
};

/// GPU-resident PBR texture handles and their SRV descriptor indices.
struct PBRTextures {
    RHITextureHandle handles[5] = {};
    uint32_t albedoSrv    = 0;
    uint32_t normalSrv    = 0;
    uint32_t roughnessSrv = 0;
    uint32_t metallicSrv  = 0;
    uint32_t aoSrv        = 0;
};

/// Physical G-Buffer texture handles used across G-Buffer and lighting passes.
///
/// The render graph declares these as transient but the physical textures are
/// maintained externally so their SRV indices remain stable within the frame.
struct GBufferTextures {
    RHITextureHandle albedo;      ///< RGBA8Unorm — base colour + alpha.
    RHITextureHandle normal;      ///< RGBA16Float — world-space normals.
    RHITextureHandle metalRough;  ///< RGBA8Unorm — R=metallic, G=roughness.
    RHITextureHandle depth;       ///< D32Float — primary depth buffer.

    uint32_t albedoSrv     = 0;
    uint32_t normalSrv     = 0;
    uint32_t metalRoughSrv = 0;
    uint32_t depthSrv      = 0;
};

/// PBR Test Demo — renders 5 PBR-lit cubes with deferred/forward path toggle.
///
/// Extends `DemoApplication` for lifecycle management. The base class owns the
/// window, device, swapchain, command list, and fences. This class owns all
/// rendering resources: shaders, pipelines, meshes, textures, G-Buffer, shadow
/// atlas, CB ring, editor grid, stats overlay, camera, and input system.
///
/// # Lifecycle
/// - `OnInit()` — Compiles shaders, loads mesh/textures, creates pipelines and G-Buffer.
/// - `OnFrame(dt)` — Processes input, updates camera, builds draw list, records render graph.
/// - `OnResize(w, h)` — Recreates G-Buffer textures and updates camera aspect ratio.
/// - `OnShutdown()` — Releases all GPU resources.
class PBRTestDemo final : public DemoApplication {
public:
    /// Constructs the PBR test demo with the given configuration.
    ///
    /// # Arguments
    /// * `config` — Demo settings (frame count, screenshot, backend).
    explicit PBRTestDemo(const PBRTestConfig& config = {});
    ~PBRTestDemo() override;

protected:
    /// Called once after device/swapchain creation. Compiles all shaders,
    /// loads the cube mesh and PBR textures, creates pipelines, G-Buffer,
    /// shadow atlas, CB ring, editor grid, stats overlay, and camera.
    void OnInit() override;

    /// Called every frame with the command list already opened.
    /// Processes input, updates camera, builds the draw list, records the
    /// render graph (shadow + deferred/forward + overlay + screenshot passes).
    ///
    /// # Arguments
    /// * `dt` — Delta time in seconds since last frame.
    void OnFrame(float dt) override;

    /// Called when the window is resized. Recreates G-Buffer textures and
    /// updates the camera aspect ratio and stats overlay dimensions.
    ///
    /// # Arguments
    /// * `width` — New window width in pixels.
    /// * `height` — New window height in pixels.
    void OnResize(uint32_t width, uint32_t height) override;

    /// Called before shutdown. Releases all GPU resources owned by this demo.
    void OnShutdown() override;

private:
    /// Compiles all shaders (mesh, gbuffer, lighting, debug, forward, shadow)
    /// via the ShaderPipeline and creates RHI shader handles.
    void CompileShaders();

    /// Creates all PSOs (gbuffer, lighting, debug, forward, shadow).
    void CreatePipelines();

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

    // ── Input & Camera ──────────────────────────────────────────────────
    std::unique_ptr<InputSystem> input_;
    Camera camera_;
    FPSCameraController cameraCtrl_;

    // ── Render state ────────────────────────────────────────────────────
    bool bindless_ = false;
    RenderPath renderPath_ = RenderPath::Deferred;
    uint32_t debugModeValue_ = 0;  ///< GBufferDebugMode::Count = off.

    // ── Shader compilation ──────────────────────────────────────────────
    std::unique_ptr<ShaderPipeline> shaderPipeline_;
    std::unique_ptr<ShaderCompiler> compiler_;
    std::unique_ptr<ShaderCompilerFXC> compilerFXC_;

    // ── Shader handles ──────────────────────────────────────────────────
    RHIShaderHandle meshVSHandle_;
    RHIShaderHandle meshPSHandle_;
    RHIShaderHandle gbufPSHandle_;
    RHIShaderHandle fsVSHandle_;
    RHIShaderHandle lightPSHandle_;
    RHIShaderHandle debugPSHandle_;
    RHIShaderHandle shadowVSHandle_;

    // ── Pipeline handles ────────────────────────────────────────────────
    RHIPipelineHandle gbufPipeline_;
    RHIPipelineHandle lightPipeline_;
    RHIPipelineHandle debugPipeline_;
    RHIPipelineHandle forwardPipeline_;
    RHIPipelineHandle shadowPipeline_;

    // ── Mesh data ───────────────────────────────────────────────────────
    std::unique_ptr<Mesh> cubeMesh_;
    RHIBufferHandle vbHandle_;
    RHIBufferHandle ibHandle_;
    uint32_t vbSrvIndex_ = 0;
    RHIIndexFormat indexFormat_ = RHIIndexFormat::Uint32;

    // ── Textures ────────────────────────────────────────────────────────
    PBRTextures pbrTex_;
    RHITextureHandle checkerHandle_;
    uint32_t checkerSrv_ = 0;

    // ── G-Buffer ────────────────────────────────────────────────────────
    GBufferTextures gbuf_;

    // ── Shadow atlas ────────────────────────────────────────────────────
    RHITextureHandle shadowAtlas_;
    uint32_t shadowAtlasSrv_ = 0;

    // ── Constant buffer ring ────────────────────────────────────────────
    std::unique_ptr<CBRingAllocator> cbRing_;

    // ── Overlays ────────────────────────────────────────────────────────
    std::unique_ptr<EditorGrid> editorGrid_;
    std::unique_ptr<StatsOverlay> statsOverlay_;

    // ── Screenshot ──────────────────────────────────────────────────────
    std::unique_ptr<ScreenshotCapture> screenshotCapture_;
    bool screenshotPending_ = false;
    bool screenshotSaveDeferred_ = false;  ///< True when capture happened, save on next frame.

    // ── Render graph pool (persistent across frames) ────────────────────
    std::unique_ptr<rg::RgResourcePool> rgPool_;
};

} // namespace uz
