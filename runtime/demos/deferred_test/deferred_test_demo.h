/// Deferred Rendering Test Demo — DemoApplication-based multi-pass pipeline.
///
/// Renders 5 cubes through a deferred shading pipeline (G-Buffer + lighting)
/// or a single-pass forward PBR path, toggled at runtime with the F key.
///
/// Debug visualization: keys 1-7 inspect individual G-Buffer channels,
/// key 0 returns to the composed deferred result. Supports `--screenshot`
/// and `--screenshot-frame` for automated visual testing.
///
/// Extends `DemoApplication` which owns all engine infrastructure (input,
/// shaders, pipelines, CB ring, camera, editor grid, stats overlay,
/// screenshot capture, render graph pool). This class only owns
/// demo-specific resources: shaders, pipelines, mesh, textures, G-Buffer.

#pragma once

#include "demo_application.h"
#include "uzengine.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

class ShaderPipeline;
class ShaderCompiler;
class ShaderCompilerFXC;
class Mesh;

/// Configuration for the deferred rendering test demo.
///
/// Screenshot fields are parsed from command-line flags. The `maxFrames`
/// field maps to `DemoConfig::maxFrames` in the base class.
struct DeferredTestConfig {
    uint32_t    maxFrames       = 0;    ///< Frames to render (0 = until window close).
    std::string screenshotPath;         ///< If non-empty, save screenshot at `screenshotFrame`.
    uint32_t    screenshotFrame = 60;   ///< Frame at which to capture the screenshot.
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

/// Deferred rendering test demo — extends DemoApplication for lifecycle management.
///
/// Owns demo-specific GPU resources (shaders, pipelines, mesh buffers, textures,
/// G-Buffer). All engine infrastructure (input, camera, CB ring, editor grid,
/// stats overlay, screenshot capture, render graph pool) is owned by the base class.
///
/// All setup happens in `OnInit()`, per-frame rendering in `OnFrame(dt)`,
/// resize-dependent resource recreation in `OnResize()`, and cleanup in `OnShutdown()`.
class DeferredTestDemo final : public DemoApplication {
public:
    /// Constructs with optional configuration for frame cap and screenshots.
    explicit DeferredTestDemo(const DeferredTestConfig& config = {});
    ~DeferredTestDemo() override;

protected:
    /// Compiles shaders, creates pipelines, loads mesh and textures,
    /// initializes G-Buffer. Calls SetReady() when complete.
    void OnInit() override;

    /// Records one frame via the render graph: G-Buffer or forward pass,
    /// overlay pass (grid + stats), optional screenshot capture, and present.
    ///
    /// # Arguments
    /// * `dt` — Time since last frame in seconds (clamped by base class).
    void OnFrame(float dt) override;

    /// Recreates G-Buffer textures and updates camera aspect ratio.
    ///
    /// # Arguments
    /// * `width`  — New window width in pixels.
    /// * `height` — New window height in pixels.
    void OnResize(uint32_t width, uint32_t height) override;

    /// Releases all demo-specific GPU resources.
    void OnShutdown() override;

private:
    /// Compiles all deferred/forward shaders via ShaderPipeline.
    void CompileShaders();

    /// Creates G-Buffer, lighting, debug, and forward PSOs.
    void CreatePipelines();

    /// Loads cube.uzmesh and uploads vertex/index buffers to GPU.
    void LoadAndUploadMesh();

    /// Loads PBR texture set (albedo, normal, roughness, metallic, AO).
    void LoadPBRTextures();

    /// Creates G-Buffer textures at the current window dimensions.
    bool CreateGBuffer();

    /// Destroys all G-Buffer textures.
    void DestroyGBuffer();

    // ── Config ───────────────────────────────────────────────────────────
    DeferredTestConfig config_;

    // ── Shader compilation ───────────────────────────────────────────────
    std::unique_ptr<ShaderPipeline> shaderPipeline_;
    std::unique_ptr<ShaderCompiler> compiler_;
    std::unique_ptr<ShaderCompilerFXC> compilerFXC_;

    // ── Shader handles ───────────────────────────────────────────────────
    RHIShaderHandle meshVSHandle_;
    RHIShaderHandle gbufPSHandle_;
    RHIShaderHandle fsVSHandle_;
    RHIShaderHandle lightPSHandle_;
    RHIShaderHandle debugPSHandle_;
    RHIShaderHandle meshPSHandle_;

    // ── Pipeline handles ─────────────────────────────────────────────────
    RHIPipelineHandle gbufPipeline_;
    RHIPipelineHandle lightPipeline_;
    RHIPipelineHandle debugPipeline_;
    RHIPipelineHandle forwardPipeline_;

    // ── Mesh buffers ─────────────────────────────────────────────────────
    RHIBufferHandle vbHandle_;
    RHIBufferHandle ibHandle_;
    uint32_t vbSrvIndex_  = 0;
    uint32_t indexCount_   = 0;
    uint32_t vertexCount_  = 0;
    RHIIndexFormat indexFormat_ = RHIIndexFormat::Uint32;
    bool bindless_ = false;

    // ── Textures ─────────────────────────────────────────────────────────
    PBRTextures pbrTex_;
    GBufferTextures gbuf_;

    // ── Frame state ──────────────────────────────────────────────────────
    float totalTime_       = 0.f;
    uint32_t debugModeValue_ = static_cast<uint32_t>(GBufferDebugMode::Count);
    RenderPath renderPath_ = RenderPath::Deferred;
};

} // namespace uz
