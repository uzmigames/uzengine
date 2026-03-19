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

    /// Returns the G-Buffer Masked pipeline (mesh_vs + gbuffer_ps, 4 RT + depth, no cull).
    ///
    /// Identical to the standard GBuffer pipeline except:
    /// - Rasterizer cull mode is None (two-sided for masked materials).
    /// - The pixel shader performs alpha test via `if (opacity < threshold) discard;`
    ///   controlled by `blendMode == 1` in the per-object CB.
    ///
    /// @source UE5 BasePassRendering.cpp — FBasePassMeshProcessor uses CullMode::None for two-sided masked
    ///
    /// # Arguments
    /// * `bindless` — If true, omits vertex input layout (SM6.6 path).
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetGBufferMasked(bool bindless);

    /// Returns the G-Buffer Masked pipeline using the device's default bindless mode.
    [[nodiscard]] RHIPipelineHandle GetGBufferMasked();

    /// Returns the translucent forward pipeline (mesh_vs + mesh_ps, 1 HDR RT, depth read-only).
    ///
    /// Uses SrcAlpha/InvSrcAlpha blending for Porter-Duff alpha compositing.
    /// Depth is read but NOT written (translucent objects do not occlude).
    /// Must be drawn after the deferred lighting pass, sorted back-to-front.
    ///
    /// @source UE5 TranslucentRendering.cpp — FTranslucencyPassMeshProcessor
    ///
    /// # Arguments
    /// * `bindless` — If true, omits vertex input layout (SM6.6 path).
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetTranslucent(bool bindless);

    /// Returns the translucent pipeline using the device's default bindless mode.
    [[nodiscard]] RHIPipelineHandle GetTranslucent();

    /// Returns the additive forward pipeline (mesh_vs + mesh_ps, 1 HDR RT, depth read-only).
    ///
    /// Uses One/One additive blending for emissive/particle effects.
    /// Depth is read but NOT written. No sorting required.
    ///
    /// @source UE5 TranslucentRendering.cpp — FTranslucencyPassMeshProcessor (Additive)
    ///
    /// # Arguments
    /// * `bindless` — If true, omits vertex input layout (SM6.6 path).
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetAdditive(bool bindless);

    /// Returns the additive pipeline using the device's default bindless mode.
    [[nodiscard]] RHIPipelineHandle GetAdditive();

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

    /// Returns the velocity buffer pipeline (fullscreen_vs + velocity_ps, 1 RT RG16Float, no depth).
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Blend: None (write velocity directly).
    /// Render target: RG16Float.
    ///
    /// @source UE5 VelocityRendering.cpp — velocity buffer generation PSO
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetVelocityPass();

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

    /// Returns the GPU frustum + HiZ occlusion culling compute pipeline (gpu_cull_cs.hlsl).
    ///
    /// Two-phase culling: frustum test (always) + HiZ occlusion test (optional,
    /// requires bindless SM6.6). Projects bounding spheres to screen-space rects,
    /// selects Hi-Z mip level, and rejects occluded instances.
    ///
    /// @source UE5 HZBOcclusion.usf — HZBTestPS pipeline
    /// @source UE5 NaniteHZBCull.ush:565-595 — ProjectSphere, SphereToScreenRect
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetGpuCull();

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

    /// Returns the TSR velocity dilation compute pipeline (tsr_resolve_cs.hlsl + TSR_PASS_DILATE_VELOCITY).
    ///
    /// @source UE5 TSRDilateVelocity.usf — closest depth search, velocity dilation
    [[nodiscard]] RHIPipelineHandle GetTsrDilateVelocity();

    /// Returns the TSR shading rejection compute pipeline (tsr_resolve_cs.hlsl + TSR_PASS_REJECT_SHADING).
    ///
    /// @source UE5 TSRRejectShading.usf — luminance-based shading rejection
    [[nodiscard]] RHIPipelineHandle GetTsrRejectShading();

    /// Returns the TSR history update compute pipeline (tsr_resolve_cs.hlsl + TSR_PASS_UPDATE_HISTORY).
    ///
    /// @source UE5 TSRUpdateHistory.usf — temporal resolve with neighborhood clamping
    [[nodiscard]] RHIPipelineHandle GetTsrUpdateHistory();

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

    /// Returns the color grading LUT compute pipeline (color_grading_lut_cs.hlsl).
    ///
    /// Generates a 32x32x32 3D LUT that bakes the entire color grading +
    /// tonemapping pipeline into a single texture lookup. Dispatched once
    /// when settings change, then the tonemap shader samples the LUT.
    ///
    /// @source UE5 PostProcessCombineLUTs.usf:445-455 — MainCS
    /// @source UE5 PostProcessCombineLUTs.cpp:322 — GroupSize = 8
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetColorGradingLut();

    /// Returns the motion blur compute pipeline (motion_blur_cs.hlsl).
    ///
    /// Per-pixel velocity-based directional blur. Reads HDR scene color,
    /// velocity buffer, and depth; writes blurred scene color.
    ///
    /// @source UE5 MotionBlurApply.usf:269-714 — MainCS
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetMotionBlur();

    /// Returns the motion blur tile max compute pipeline (motionblur_tilemax_cs.hlsl).
    ///
    /// For each 16x16 tile of the velocity buffer, finds the maximum velocity
    /// magnitude and outputs the dominant velocity vector. Produces a downsampled
    /// velocity texture at (width/16) x (height/16).
    ///
    /// @source UE5 MotionBlurVelocityFlatten.usf:79-165 — MainCS
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetMotionBlurTileMax();

    /// Returns the motion blur neighbor max compute pipeline (motionblur_neighbormax_cs.hlsl).
    ///
    /// For each tile, finds the maximum velocity from its 3x3 neighborhood.
    /// Ensures fast-moving objects produce blur extending beyond their tile.
    ///
    /// @source UE5 MotionBlurTileGather.usf:24-91 — MainCS
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetMotionBlurNeighborMax();

    /// Returns the DOF setup compute pipeline (dof_setup_cs.hlsl).
    ///
    /// Computes per-pixel Circle of Confusion from the depth buffer using
    /// the thin-lens equation. Outputs signed R16F CoC texture.
    ///
    /// @source UE5 DOFSetup.usf:396-437 — SetupCS
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetDofSetup();

    /// Returns the DOF blur compute pipeline (dof_blur_cs.hlsl).
    ///
    /// Disc-shaped gather blur driven by the CoC texture. Samples in concentric
    /// rings with per-sample CoC weighting.
    ///
    /// @source UE5 DOFGatherPass.usf:150-464 — GatherMainCS
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetDofBlur();

    /// Returns the FXAA 3.11 pipeline (fullscreen_vs + fxaa_ps, 1 RT RGBA8Unorm).
    ///
    /// Fullscreen anti-aliasing pass that runs on LDR scene color after tonemapping.
    /// Only used when TAA is disabled (FXAA and TAA are mutually exclusive).
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Blend: None.
    /// Render target: RGBA8Unorm (backbuffer).
    ///
    /// @source UE5 PostProcessAA.cpp:94-96 — FXAA pass setup
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetFxaa();

    /// Returns the CAS sharpening pipeline (fullscreen_vs + cas_sharpen_ps, 1 RT RGBA8Unorm).
    ///
    /// Contrast Adaptive Sharpening pass that runs on LDR scene color after
    /// tonemapping and before FXAA. Enhances edges without amplifying noise.
    ///
    /// Rasterizer: no cull (fullscreen triangle).
    /// Depth: disabled.
    /// Blend: None.
    /// Render target: RGBA8Unorm (backbuffer or FXAA intermediate).
    ///
    /// @source AMD FidelityFX CAS — ffx_cas.h
    ///
    /// # Returns
    /// Valid `RHIPipelineHandle` on success, invalid handle on failure (error logged).
    [[nodiscard]] RHIPipelineHandle GetCasSharpen();

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
    [[nodiscard]] bool EnsureVelocityPixelShader();
    [[nodiscard]] bool EnsureHeightFogPixelShader();
    [[nodiscard]] bool EnsureTransmittanceLutShader();
    [[nodiscard]] bool EnsureMultiScatterLutShader();
    [[nodiscard]] bool EnsureSkyCubemapCaptureShader();
    [[nodiscard]] bool EnsureSkyCubemapCapturePSShader();
    [[nodiscard]] bool EnsureHiZPyramidShader();
    [[nodiscard]] bool EnsureGpuCullShader();
    [[nodiscard]] bool EnsureDfsCullObjectsShader();
    [[nodiscard]] bool EnsureDfsConeTraceShader();
    [[nodiscard]] bool EnsureTaaResolveShader();
    [[nodiscard]] bool EnsureTsrDilateVelocityShader();
    [[nodiscard]] bool EnsureTsrRejectShadingShader();
    [[nodiscard]] bool EnsureTsrUpdateHistoryShader();
    [[nodiscard]] bool EnsureSsrTraceShader();
    [[nodiscard]] bool EnsureSsrResolveShader();
    [[nodiscard]] bool EnsureColorGradingLutShader();
    [[nodiscard]] bool EnsureMotionBlurShader();
    [[nodiscard]] bool EnsureMotionBlurTileMaxShader();
    [[nodiscard]] bool EnsureMotionBlurNeighborMaxShader();
    [[nodiscard]] bool EnsureDofSetupShader();
    [[nodiscard]] bool EnsureDofBlurShader();
    [[nodiscard]] bool EnsureFxaaPixelShader();
    [[nodiscard]] bool EnsureCasSharpenPixelShader();

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
    RHIShaderHandle m_velocityPS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_heightFogPS          = RHIShaderHandle::Invalid();
    RHIShaderHandle m_transmittanceLutCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_multiScatterLutCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_skyCubemapCaptureCS    = RHIShaderHandle::Invalid();
    RHIShaderHandle m_skyCubemapCapturePSShader = RHIShaderHandle::Invalid();
    RHIShaderHandle m_hizPyramidCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_gpuCullCS                = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dfsCullObjectsCS         = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dfsConeTraceCS           = RHIShaderHandle::Invalid();
    RHIShaderHandle m_taaResolveCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_tsrDilateVelocityCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_tsrRejectShadingCS       = RHIShaderHandle::Invalid();
    RHIShaderHandle m_tsrUpdateHistoryCS       = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssrTraceCS               = RHIShaderHandle::Invalid();
    RHIShaderHandle m_ssrResolveCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_colorGradingLutCS        = RHIShaderHandle::Invalid();
    RHIShaderHandle m_motionBlurCS             = RHIShaderHandle::Invalid();
    RHIShaderHandle m_motionBlurTileMaxCS      = RHIShaderHandle::Invalid();
    RHIShaderHandle m_motionBlurNeighborMaxCS  = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dofSetupCS               = RHIShaderHandle::Invalid();
    RHIShaderHandle m_dofBlurCS                = RHIShaderHandle::Invalid();
    RHIShaderHandle m_fxaaPS                   = RHIShaderHandle::Invalid();
    RHIShaderHandle m_casSharpenPS             = RHIShaderHandle::Invalid();

    // ── Cached pipeline handles (one per bindless variant) ──────────────
    RHIPipelineHandle m_forwardPBR         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_forwardPBRBindless = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbuffer            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferBindless    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferMasked         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferMaskedBindless = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_translucent           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_translucentBindless   = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_additive              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_additiveBindless      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_lighting              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferDebug          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_debugView             = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepth           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepthBindless   = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gbufferInstanced      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_shadowDepthInstanced  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyAtmosphere         = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tonemap               = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_velocityPass           = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_heightFog             = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_transmittanceLut      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_multiScatterLut      = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyCubemapCapture    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_skyCubemapCapturePSPipeline = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepass          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepassBindless  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_depthPrepassInstanced = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_hizPyramid            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_gpuCull              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dfsCullObjects        = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dfsConeTrace          = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_taaResolve            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tsrDilateVelocity    = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tsrRejectShading     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_tsrUpdateHistory     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssrTrace              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_ssrResolve            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_colorGradingLut       = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_motionBlur            = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_motionBlurTileMax     = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_motionBlurNeighborMax = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dofSetup              = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_dofBlur               = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_fxaa                  = RHIPipelineHandle::Invalid();
    RHIPipelineHandle m_casSharpen            = RHIPipelineHandle::Invalid();
};

} // namespace uz
