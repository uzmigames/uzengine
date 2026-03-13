/// UzEngine Pipeline Library — Cached PSO Factory for Standard Render Passes.
///
/// Provides lazily-created, cached pipeline state objects for the engine's
/// standard render passes: forward PBR, G-Buffer, deferred lighting, and
/// G-Buffer debug visualization.
///
/// # Ownership
/// Owns all `RHIPipelineHandle` and `RHIShaderHandle` objects it creates.
/// Does NOT own the `IRHIDevice` or `ShaderManager` — both must outlive
/// this object.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render/init thread (same
/// constraint as `ShaderManager`).
///
/// # Examples
/// ```cpp
/// PipelineLibrary pipelines;
/// if (!pipelines.Init(device, &shaderMgr)) { /* handle failure */ }
///
/// auto gbufferPSO = pipelines.GetGBuffer();
/// auto lightingPSO = pipelines.GetLighting();
/// // ... bind PSOs in render passes ...
///
/// pipelines.Shutdown();
/// ```

#pragma once

#include "pso_disk_cache.h"
#include "rhi_pso_cache.h"
#include "rhi_types.h"

#include <memory>
#include <string>

namespace uz {

class IRHIDevice;
class ShaderManager;

/// Cached PSO factory for standard engine render passes.
///
/// Creates pipeline state objects lazily on first request and caches them
/// for the lifetime of the library. Uses `ShaderManager` for shader
/// compilation and `VertexLayout::Standard()` for input layouts (SM5 path).
///
/// Each getter returns an `RHIPipelineHandle` — invalid on creation failure.
/// Once created, the same handle is returned on every subsequent call.
///
/// # Supported Pipelines
/// - **ForwardPBR** — Single-pass Cook-Torrance PBR (1 RT + depth).
/// - **GBuffer** — Deferred geometry pass (3 RT + depth).
/// - **Lighting** — Fullscreen deferred lighting (1 RT, no depth).
/// - **GBufferDebug** — Fullscreen G-Buffer channel visualization (1 RT, no depth).
class PipelineLibrary {
public:
    PipelineLibrary() = default;
    ~PipelineLibrary();

    PipelineLibrary(const PipelineLibrary&) = delete;
    PipelineLibrary& operator=(const PipelineLibrary&) = delete;
    PipelineLibrary(PipelineLibrary&&) = delete;
    PipelineLibrary& operator=(PipelineLibrary&&) = delete;

    /// Initializes the pipeline library with a device, shader manager, and
    /// optional PSO disk cache directory.
    ///
    /// Must be called before any `Get*()` method. No PSOs are created during
    /// init — they are compiled lazily on first access.
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this library.
    /// * `shaderMgr` — Initialized shader manager. Must outlive this library.
    /// * `psoCacheDir` — Directory for PSO disk cache (e.g., "cache/pso").
    ///   If empty, disk caching is disabled.
    ///
    /// # Returns
    /// `true` on success, `false` if either argument is null.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderManager* shaderMgr,
                            const std::string& psoCacheDir = "");


    /// Destroys all cached PSOs and shaders, releases references.
    ///
    /// Safe to call multiple times or without prior `Init()`. Called
    /// automatically by the destructor.
    void Shutdown();

    /// Returns the forward PBR pipeline (mesh_vs + mesh_ps, 1 RT + depth).
    ///
    /// Rasterizer: back-face cull, clockwise front.
    /// Depth: enabled, write enabled, less comparison.
    /// Render target: RGBA8Unorm.
    /// Depth format: D32Float.
    ///
    /// # Arguments
    /// * `bindless` — If true, omits vertex input layout (SM6.6 path).
    ///   Defaults to the device's bindless capability.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetForwardPBR(bool bindless);

    /// Returns the forward PBR pipeline using the device's default bindless mode.
    [[nodiscard]] RHIPipelineHandle GetForwardPBR();

    /// Returns the G-Buffer geometry pipeline (mesh_vs + gbuffer_ps, 3 RT + depth).
    ///
    /// Rasterizer: back-face cull, clockwise front.
    /// Depth: enabled, write enabled.
    /// Render targets: RGBA8Unorm (albedo), RGBA16Float (normal), RGBA8Unorm (metalRough).
    /// Depth format: D32Float.
    ///
    /// # Arguments
    /// * `bindless` — If true, omits vertex input layout (SM6.6 path).
    ///   Defaults to the device's bindless capability.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetGBuffer(bool bindless);

    /// Returns the G-Buffer pipeline using the device's default bindless mode.
    [[nodiscard]] RHIPipelineHandle GetGBuffer();

    /// Returns the deferred lighting pipeline (fullscreen_vs + deferred_lighting_ps, 1 RT).
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Render target: RGBA8Unorm.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetLighting();

    /// Returns the G-Buffer debug visualization pipeline (fullscreen_vs + gbuffer_debug_ps, 1 RT).
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Render target: RGBA8Unorm.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetGBufferDebug();

    /// Returns the shadow depth pipeline (shadow_depth_vs, no pixel shader, depth-only).
    ///
    /// Rasterizer: back-face cull, clockwise front.
    /// Depth: enabled, write enabled.
    /// Render targets: 0 (depth-only).
    /// Depth format: D32Float.
    ///
    /// # Arguments
    /// * `bindless` — If true, omits vertex input layout (SM6.6 path).
    ///   Defaults to the device's bindless capability.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetShadowDepth(bool bindless);

    /// Returns the shadow depth pipeline using the device's default bindless mode.
    [[nodiscard]] RHIPipelineHandle GetShadowDepth();

    /// Pre-creates all standard pipelines eagerly instead of on first use.
    ///
    /// Call at scene load time to avoid hitches during the first frame.
    /// Internally calls every `Get*()` method for both bindless and SM5 variants.
    /// Pipelines that fail to compile are logged but do not abort warmup.
    ///
    /// # Returns
    /// Number of pipelines successfully created (max 8: 4 types x 2 bindless variants,
    /// though lighting/debug have no bindless variant so max is typically 6).
    uint32_t WarmupAll();

    /// Returns true if `Init()` has been called successfully and `Shutdown()`
    /// has not been called since.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    /// Creates and caches shaders needed for a pipeline if not already compiled.
    /// Returns false if any required shader fails to compile.
    [[nodiscard]] bool EnsureMeshShaders();
    [[nodiscard]] bool EnsureFullscreenShaders();
    [[nodiscard]] bool EnsureGBufferPixelShader();
    [[nodiscard]] bool EnsureLightingPixelShader();
    [[nodiscard]] bool EnsureGBufferDebugPixelShader();
    [[nodiscard]] bool EnsureForwardPixelShader();
    [[nodiscard]] bool EnsureShadowShaders();

    /// Creates a pipeline through the PSO cache. On cache hit, returns the
    /// cached handle without calling the device. On miss, creates via the
    /// device and inserts into the cache.
    [[nodiscard]] RHIPipelineHandle CreateCachedPipeline(const RHIGraphicsPipelineDesc& desc);

    IRHIDevice*    m_device    = nullptr;
    ShaderManager* m_shaderMgr = nullptr;
    bool           m_initialized = false;

    // ── PSO content-hash cache ─────────────────────────────────────────
    PsoCache m_psoCache;

    // ── PSO disk cache (optional) ────────────────────────────────────
    std::unique_ptr<PsoDiskCache> m_psoDiskCache;

    // ── Cached shader handles ───────────────────────────────────────────
    RHIShaderHandle m_meshVS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_meshPS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_fullscreenVS     = RHIShaderHandle::Invalid();
    RHIShaderHandle m_gbufferPS        = RHIShaderHandle::Invalid();
    RHIShaderHandle m_lightingPS       = RHIShaderHandle::Invalid();
    RHIShaderHandle m_gbufferDebugPS   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_shadowVS         = RHIShaderHandle::Invalid();

    // ── Cached pipeline handles (one per bindless variant) ──────────────
    RHIPipelineHandle m_forwardPBR         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_forwardPBRBindless = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbuffer            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferBindless    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_lighting              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferDebug          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepth           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepthBindless   = RHIPipelineHandle::Invalid();
};

} // namespace uz
