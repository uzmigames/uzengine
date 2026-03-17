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

#include "rhi_pso_disk_cache.h"
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

    /// Returns the G-Buffer geometry pipeline (mesh_vs + gbuffer_ps, 2 RT + depth).
    ///
    /// Rasterizer: back-face cull, clockwise front.
    /// Depth: enabled, write enabled.
    /// Render targets: RGBA8Unorm (albedo+AO), RGBA8Unorm (octNormal+metallic+roughness).
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

    /// Returns the debug view mode pipeline (fullscreen_vs + debug_view_ps, 1 RT).
    ///
    /// Used by `AddDebugViewPass` to visualize individual G-Buffer channels
    /// (albedo, normals, depth, etc.) selected at runtime via root constants.
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Render target: RGBA8Unorm.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetDebugView();

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

    /// Returns the instanced G-Buffer pipeline (mesh_vs+UZ_INSTANCED + gbuffer_ps, 3 RT + depth).
    ///
    /// Uses `DrawIndexedInstanced` with per-instance transforms read from a
    /// StructuredBuffer via `SV_InstanceID`. Only available on SM6.6 bindless path.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetGBufferInstanced();

    /// Returns the instanced shadow depth pipeline (shadow_depth_vs+UZ_INSTANCED, depth-only).
    ///
    /// Uses `DrawIndexedInstanced` with per-instance world matrices read from a
    /// StructuredBuffer via `SV_InstanceID`. Only available on SM6.6 bindless path.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetShadowDepthInstanced();

    /// Returns the sky atmosphere pipeline (fullscreen_vs + sky_atmosphere_ps, 1 RT, no depth).
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Render target: RGBA8Unorm.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetSkyAtmosphere();

    /// Returns the transmittance LUT compute pipeline (transmittance_lut_cs.hlsl).
    ///
    /// Compute pipeline dispatched once per scene to fill a 256x64 RGBA16F texture
    /// encoding optical transmittance vs (camera height, sun zenith angle).
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetTransmittanceLut();

    /// Returns the multi-scattering LUT compute pipeline (multi_scatter_lut_cs.hlsl).
    /// 32x32 R11G11B10F, Hillaire 2020 isotropic integration + geometric series.
    [[nodiscard]] RHIPipelineHandle GetMultiScatterLut();

    /// Returns the sky cubemap capture compute pipeline (sky_cubemap_cs.hlsl).
    ///
    /// Compute pipeline dispatched to capture sky radiance into a 64x64x6 RGBA16F
    /// cubemap for IBL baking. Reads the transmittance LUT via bindless SRV index.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetSkyCubemapCapture();

    /// Returns the sky cubemap capture pixel shader pipeline (fullscreen_vs + sky_cubemap_ps).
    ///
    /// SM5-compatible alternative to the compute path. Renders 6 fullscreen
    /// triangles (one per cubemap face) using explicit register(t0) binding
    /// for the transmittance LUT — no bindless/ResourceDescriptorHeap required.
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Render target: RGBA16F (cubemap face).
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetSkyCubemapCapturePS();

    /// Returns the tonemap pipeline (fullscreen_vs + tonemap_ps, 1 RT RGBA8).
    ///
    /// Reads HDR scene color (RGBA16F) via bindless SRV, applies exposure + ACES
    /// tonemap + sRGB gamma, outputs LDR to backbuffer.
    [[nodiscard]] RHIPipelineHandle GetTonemap();

    /// Returns the height fog pipeline (fullscreen_vs + height_fog_ps, 1 RT, alpha blend).
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Blend: SrcAlpha / InvSrcAlpha (fog compositing).
    /// Render target: RGBA8Unorm.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetHeightFog();

    /// Returns the depth prepass pipeline (shadow_depth_vs, no pixel shader, depth-only).
    ///
    /// Similar to the shadow depth pipeline but WITHOUT depth bias (no slope-scaled
    /// or constant bias) since the prepass writes to the GBuffer depth, not a shadow
    /// atlas. Uses LessEqual comparison (reversed-Z).
    ///
    /// @source UE5 DepthRendering.cpp — FDepthPassMeshProcessor
    ///
    /// # Arguments
    /// * `bindless` — If true, omits vertex input layout (SM6.6 path).
    ///   Defaults to the device's bindless capability.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetDepthPrepass(bool bindless);

    /// Returns the depth prepass pipeline using the device's default bindless mode.
    [[nodiscard]] RHIPipelineHandle GetDepthPrepass();

    /// Returns the instanced depth prepass pipeline (shadow_depth_vs+UZ_INSTANCED, depth-only, no bias).
    ///
    /// Uses `DrawIndexedInstanced` with per-instance world matrices. Only available
    /// on SM6.6 bindless path. Identical to `GetDepthPrepass(true)` but with the
    /// instanced vertex shader variant.
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetDepthPrepassInstanced();

    /// Returns the Hi-Z pyramid compute pipeline (hiz_pyramid_cs.hlsl).
    ///
    /// Compute pipeline dispatched once per mip level to downsample the depth
    /// buffer into a hierarchical Z-buffer for occlusion culling, SSAO, and SSR.
    ///
    /// @source UE5 HZBOcclusion.cpp — BuildHZB pipeline creation
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetHiZPyramid();

    /// Returns the DFS object culling compute pipeline (dfs_cull_objects_cs.hlsl).
    ///
    /// Frustum-culls SDF objects against the shadow convex hull and appends
    /// surviving indices to a compacted buffer for the cone tracing pass.
    ///
    /// @source UE5 DistanceFieldShadowing.usf:82-156 — CullObjectsForShadowCS
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetDfsCullObjects();

    /// Returns the DFS cone tracing compute pipeline (dfs_cone_trace_cs.hlsl).
    ///
    /// Per-pixel ray march through mesh SDFs with cone expansion for soft shadows.
    /// Reads the culled object list and outputs a shadow factor texture.
    ///
    /// @source UE5 DistanceFieldShadowing.usf:612-811 — DistanceFieldShadowingCS
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetDfsConeTrace();

    /// Returns the TAA resolve compute pipeline (taa_resolve_cs.hlsl).
    ///
    /// Temporal anti-aliasing resolve pass: samples current frame and history,
    /// performs YCoCg neighborhood clamping, and blends with HDR weighting.
    ///
    /// @source UE5 TemporalAA.usf:2327-2407 — MainCS entry point
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetTaaResolve();

    /// Returns the SSR trace compute pipeline (ssr_trace_cs.hlsl).
    ///
    /// Per-pixel screen-space reflection ray marching using the Hi-Z depth
    /// pyramid for acceleration. Reads depth, normals, Hi-Z pyramid, and scene
    /// color; writes ray-hit color + confidence to an RGBA16F output texture.
    ///
    /// @source UE5 SSRTRayCast.ush:255-627 — CastScreenSpaceRay
    /// @source UE5 SSRTReflections.usf:441-462 — ScreenSpaceReflectionsCS
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetSsrTrace();

    /// Returns the SSR resolve compute pipeline (ssr_resolve_cs.hlsl).
    ///
    /// Composites SSR trace output with Fresnel-weighted blending and
    /// roughness-based filtering. Reads trace result + G-Buffer normals +
    /// depth; writes final SSR reflection color with blend weight in alpha.
    ///
    /// @source UE5 SSRTReflections.usf:105-416 — ScreenSpaceReflections resolve
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetSsrResolve();

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
    [[nodiscard]] bool EnsureDebugViewPixelShader();
    [[nodiscard]] bool EnsureForwardPixelShader();
    [[nodiscard]] bool EnsureInstancedMeshShaders();
    [[nodiscard]] bool EnsureInstancedShadowShaders();
    [[nodiscard]] bool EnsureShadowShaders();
    [[nodiscard]] bool EnsureSkyAtmospherePixelShader();
    [[nodiscard]] bool EnsureTonemapPixelShader();
    [[nodiscard]] bool EnsureHeightFogPixelShader();
    [[nodiscard]] bool EnsureTransmittanceLutShader();
    [[nodiscard]] bool EnsureMultiScatterLutShader();
    [[nodiscard]] bool EnsureSkyCubemapCaptureShader();
    [[nodiscard]] bool EnsureSkyCubemapCapturePSShader();
    [[nodiscard]] bool EnsureHiZPyramidShader();
    [[nodiscard]] bool EnsureDfsCullObjectsShader();
    [[nodiscard]] bool EnsureDfsConeTraceShader();
    [[nodiscard]] bool EnsureTaaResolveShader();
    [[nodiscard]] bool EnsureSsrTraceShader();
    [[nodiscard]] bool EnsureSsrResolveShader();

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
    RHIShaderHandle m_debugViewPS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_instancedMeshVS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_instancedShadowVS = RHIShaderHandle::Invalid();
    RHIShaderHandle m_shadowVS         = RHIShaderHandle::Invalid();
    RHIShaderHandle m_skyAtmospherePS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_tonemapPS            = RHIShaderHandle::Invalid();
    RHIShaderHandle m_heightFogPS          = RHIShaderHandle::Invalid();
    RHIShaderHandle m_transmittanceLutCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_multiScatterLutCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_skyCubemapCaptureCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_skyCubemapCapturePSShader = RHIShaderHandle::Invalid();
    RHIShaderHandle m_hizPyramidCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dfsCullObjectsCS         = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dfsConeTraceCS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_taaResolveCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssrTraceCS               = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssrResolveCS             = RHIShaderHandle::Invalid();

    // ── Cached pipeline handles (one per bindless variant) ──────────────
    RHIPipelineHandle m_forwardPBR         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_forwardPBRBindless = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbuffer            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferBindless    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_lighting              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferDebug          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_debugView             = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepth           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepthBindless   = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferInstanced      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepthInstanced  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyAtmosphere         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tonemap               = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_heightFog             = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_transmittanceLut      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_multiScatterLut      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyCubemapCapture    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyCubemapCapturePSPipeline = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepass          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepassBindless  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepassInstanced = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_hizPyramid            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dfsCullObjects        = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dfsConeTrace          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_taaResolve            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssrTrace              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssrResolve            = RHIPipelineHandle::Invalid();
};

} // namespace uz
