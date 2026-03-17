/// UzEngine Scene Renderer — Self-contained ECS-to-GPU rendering pipeline.
///
/// Owns all rendering infrastructure (shaders, pipelines, CB ring, camera,
/// grid, stats overlay, render graph pool, GBuffer, shadow atlas) and renders
/// a World (ECS) each frame through the render graph.
///
/// Designed to replace the clear-screen loop in `uzengine.cpp` with a full
/// PBR pipeline driven by ECS components (CameraComponent, StaticMeshComponent,
/// DirectionalLightComponent, PointLightComponent).
///
/// # Lifecycle
/// ```cpp
/// SceneRenderer renderer;
/// renderer.Init(device, window, 1280, 720);
///
/// // Per frame (cmdList NOT in recording state):
/// renderer.RenderFrame(cmdList, swapChain, &world, &vfs, dt, frameIndex);
///
/// renderer.Shutdown();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.

#pragma once

#include "gbuffer.h"
#include "draw_list.h"
#include "render_path.h"
#include "gbuffer_debug_pass.h"  // GBufferDebugMode
#include "debug_view_mode.h"    // DebugViewMode
#include "gpu_mesh.h"
#include "texture_loader.h"
#include "screenshot_capture.h"
#include "shadow_atlas.h"
#include "shadow_cache.h"
#include "ibl/ibl_baker.h"
#include "ibl/sh_projection.h"
#include "postprocess/auto_exposure.h"
#include "postprocess/bloom_pass.h"
#include "postprocess/gtao_pass.h"
#include "postprocess/hiz_pyramid_pass.h"
#include "shadows/distance_field_shadow_config.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// Forward declarations
class IRHIDevice;
class IRHICommandList;
class IRHISwapChain;
class IVFS;
class PlatformWindow;
class InputSystem;
class ShaderManager;
class PipelineLibrary;
class CBRingAllocator;
class EditorGrid;
class StatsOverlay;
class UploadContext;
class Camera;
class FPSCameraController;
class World;

class SdfAtlasManager;
namespace rg { class RgResourcePool; }

// ─────────────────────────────────────────────────────────────────────────────
// SceneRenderer
// ─────────────────────────────────────────────────────────────────────────────

/// Self-contained scene renderer that bridges the ECS World to the GPU.
///
/// Owns all rendering infrastructure and renders a complete PBR frame from
/// ECS data. Supports both deferred and forward render paths, shadow mapping,
/// editor grid, stats overlay, and GBuffer debug visualization.
///
/// # Ownership
/// Owns all infrastructure objects (ShaderManager, PipelineLibrary, etc.)
/// via `std::unique_ptr`. Does NOT own the `IRHIDevice`, `World`, `IVFS`,
/// `IRHICommandList`, or `IRHISwapChain` — those must outlive this object.
///
/// # Asset Management
/// Meshes and textures are loaded on demand via `EnsureMeshLoaded()` and
/// `EnsureTextureLoaded()`. Loaded assets are cached by path for the lifetime
/// of the renderer. GPU resources are released in `Shutdown()`.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    /// Non-copyable — owns GPU resources and unique_ptrs.
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    /// Movable.
    SceneRenderer(SceneRenderer&&) noexcept;
    SceneRenderer& operator=(SceneRenderer&&) noexcept;

    /// Initializes all rendering infrastructure.
    ///
    /// Creates ShaderManager, PipelineLibrary, CBRingAllocator (3 frames, 2MB),
    /// EditorGrid, Camera, FPSCameraController, RgResourcePool, GBuffer, and
    /// shadow atlas (4096x4096 D32F).
    ///
    /// StatsOverlay initialization is deferred to the first `RenderFrame()` call
    /// because it requires the command list in recording state.
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this renderer.
    /// * `window` — Platform window for input binding. Must outlive this renderer.
    /// * `width` — Initial render target width in pixels.
    /// * `height` — Initial render target height in pixels.
    ///
    /// # Returns
    /// `true` if all subsystems initialized successfully.
    /// `false` on any failure (error logged).
    [[nodiscard]] bool Init(IRHIDevice* device, PlatformWindow* window,
                            uint32_t width, uint32_t height);

    /// Handles window resize — recreates GBuffer and updates camera aspect ratio.
    ///
    /// # Arguments
    /// * `width` — New render target width in pixels. Must be > 0.
    /// * `height` — New render target height in pixels. Must be > 0.
    void Resize(uint32_t width, uint32_t height);

    /// Renders one complete frame from ECS data.
    ///
    /// The command list must NOT be in recording state — this function handles
    /// Begin/End internally. The full pipeline is:
    /// 1. Begin command list recording
    /// 2. Deferred StatsOverlay init (first frame only)
    /// 3. Input polling + camera update
    /// 4. Sync ECS World to DrawList (load meshes/textures on demand)
    /// 5. Build PerFrameCB, LightingCB, ShadowDataCB from ECS components
    /// 6. Record render graph (shadow -> GBuffer/forward -> lighting/debug -> overlay -> present)
    /// 7. Compile + execute render graph
    /// 8. End command list recording + execute
    ///
    /// # Arguments
    /// * `cmdList` — Command list (NOT recording). Owned by the caller.
    /// * `swapChain` — Swapchain for backbuffer access. Owned by the caller.
    /// * `world` — ECS world containing scene entities. May be null (renders empty scene).
    /// * `vfs` — Virtual file system for loading assets. May be null (no asset loading).
    /// * `dt` — Frame delta time in seconds.
    /// * `frameIndex` — Monotonic frame counter (for CB ring triple-buffering).
    void RenderFrame(IRHICommandList* cmdList, IRHISwapChain* swapChain,
                     World* world, IVFS* vfs, float dt, uint32_t frameIndex);

    /// Releases all GPU resources and resets to default state.
    ///
    /// Safe to call multiple times or without prior `Init()`. The caller must
    /// ensure the GPU is idle before calling this (no in-flight frames).
    void Shutdown();

    // ── Configuration ──────────────────────────────────────────────────────

    /// Switches between deferred and forward rendering.
    ///
    /// Takes effect on the next `RenderFrame()` call. Switching mid-frame
    /// is not supported.
    void SetRenderPath(RenderPath path) { m_renderPath = path; }

    /// Returns the current render path.
    [[nodiscard]] RenderPath GetRenderPath() const { return m_renderPath; }

    /// Sets the GBuffer debug visualization mode.
    ///
    /// Pass `GBufferDebugMode::Count` to disable debug visualization and
    /// show the normal lit result.
    void SetDebugMode(GBufferDebugMode mode) { m_debugMode = mode; }

    /// Returns the current debug mode.
    [[nodiscard]] GBufferDebugMode GetDebugMode() const { return m_debugMode; }

    /// Sets the debug view mode for G-Buffer channel visualization.
    ///
    /// When set to anything other than `DebugViewMode::Lit`, the debug view
    /// pass replaces the lighting pass output with a diagnostic visualization
    /// of the selected channel. Takes effect on the next `RenderFrame()` call.
    ///
    /// # Arguments
    /// * `mode` — The debug view mode to activate. `Lit` disables debug view.
    void SetDebugViewMode(DebugViewMode mode) { m_debugViewMode = mode; }

    /// Requests a screenshot to be saved at the end of the next `RenderFrame()`.
    ///
    /// The capture is performed inside the render graph (before the Present
    /// transition), so the command list is still open and the backbuffer is in
    /// a valid state. The PNG is saved after the GPU fence wait.
    ///
    /// # Arguments
    /// * `path` — Absolute or relative path for the output PNG file.
    void RequestScreenshot(const std::string& path);

    /// Requests a screenshot that will be printed to stdout as base64 PNG.
    ///
    /// Instead of writing a file to disk, the captured backbuffer is encoded
    /// as a PNG in memory, base64-encoded, and printed to stdout with a
    /// `SCREENSHOT_BASE64:` prefix. This is faster for LLM/AI workflows
    /// that consume images without filesystem round-trips.
    ///
    /// Output format: `SCREENSHOT_BASE64:<base64-encoded-png-data>\n`
    void RequestScreenshotBase64();

    /// Returns the current debug view mode.
    [[nodiscard]] DebugViewMode GetDebugViewMode() const { return m_debugViewMode; }

    /// Sets the shadow debug visualization mode (0=off, 1-8=debug views).
    /// See shadow_debug.hlsli for mode descriptions.
    void SetShadowDebugMode(uint32_t mode) { m_shadowDebugMode = mode; }

    /// Returns the current shadow debug mode.
    [[nodiscard]] uint32_t GetShadowDebugMode() const { return m_shadowDebugMode; }

    /// Enables or disables shadow caching at runtime.
    ///
    /// Equivalent to UE5's `r.Shadow.CacheEnabled` CVar.
    /// Takes effect on the next `RenderFrame()` call.
    ///
    /// @source UE5 ShadowSetup.cpp:393-398 — r.Shadow.CacheEnabled
    ///
    /// # Arguments
    /// * `enabled` — True to enable shadow caching.
    void SetShadowCacheEnabled(bool enabled) { m_shadowCacheManager.SetEnabled(enabled); }

    /// Returns whether shadow caching is currently enabled.
    [[nodiscard]] bool IsShadowCacheEnabled() const { return m_shadowCacheManager.IsEnabled(); }

    /// Cycles through shadow debug modes (0 -> 1 -> ... -> 8 -> 0).
    void CycleShadowDebugMode() { m_shadowDebugMode = (m_shadowDebugMode + 1) % 9; }

    /// Sets the BRDF debug mode (0=off, 1=split-screen: left=old, right=new).
    ///
    /// When enabled, the deferred lighting pass renders the left half with the
    /// old BRDF (Lambertian diffuse + Schlick-GGX) and the right half with the
    /// current BRDF (Burley diffuse + SmithJointApprox + multiscatter). A thin
    /// white vertical line marks the center split.
    void SetBrdfDebugMode(uint32_t mode) { m_brdfDebugMode = mode; }

    /// Returns the current BRDF debug mode.
    [[nodiscard]] uint32_t GetBrdfDebugMode() const { return m_brdfDebugMode; }

    /// Enables or disables the depth prepass.
    ///
    /// When enabled, a depth-only pass executes before the GBuffer pass,
    /// populating the depth buffer to enable early-Z rejection and Hi-Z
    /// pyramid generation.
    void SetDepthPrepassEnabled(bool enabled) { m_depthPrepassEnabled = enabled; }

    /// Returns whether the depth prepass is currently enabled.
    [[nodiscard]] bool IsDepthPrepassEnabled() const { return m_depthPrepassEnabled; }

    /// Sets the DFS configuration at runtime.
    void SetDfsConfig(const DistanceFieldShadowConfig& cfg) { m_dfsConfig = cfg; }

    /// Returns the current DFS configuration.
    [[nodiscard]] const DistanceFieldShadowConfig& GetDfsConfig() const { return m_dfsConfig; }

    /// Sets the SDF atlas manager for distance field shadows.
    /// The atlas manager must outlive this renderer. Set to nullptr to disable DFS.
    void SetSdfAtlasManager(SdfAtlasManager* atlas) { m_sdfAtlas = atlas; }

    /// Enables or disables Temporal Anti-Aliasing (TAA) at runtime.
    ///
    /// @source UE5 TemporalAA.cpp:40-44 — r.TemporalAACurrentFrameWeight CVar
    ///
    /// When disabled, the render pipeline outputs the raw jittered frame
    /// without temporal accumulation (jitter is also disabled).
    void SetTaaEnabled(bool enabled) { m_taaEnabled = enabled; }

    /// Returns whether TAA is currently enabled.
    [[nodiscard]] bool IsTaaEnabled() const { return m_taaEnabled; }


    // ── Accessors ──────────────────────────────────────────────────────────

    /// Returns the camera used for rendering. Must only be called after `Init()`.
    [[nodiscard]] Camera& GetCamera();
    [[nodiscard]] const Camera& GetCamera() const;

    /// Returns the FPS camera controller. Must only be called after `Init()`.
    [[nodiscard]] FPSCameraController& GetCameraController();

    /// Returns the input system. May return null if `Init()` has not been called.
    [[nodiscard]] InputSystem* GetInput() const;

    /// Returns the stats overlay. May return null if `Init()` has not been called.
    [[nodiscard]] StatsOverlay* GetStatsOverlay() const;

    /// Returns the current render target width in pixels.
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }

    /// Returns the current render target height in pixels.
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }

    // ── Asset Management ───────────────────────────────────────────────────

    /// Loads a mesh from the VFS and uploads it to the GPU.
    ///
    /// Returns the index into the internal mesh array. If the mesh was already
    /// loaded (same path), returns the cached index without re-uploading.
    ///
    /// # Arguments
    /// * `path` — VFS-relative mesh path (e.g., "meshes/cube.uzmesh").
    /// * `vfs` — VFS backend to read from. Must not be null.
    /// * `cmdList` — Command list in recording state for GPU upload.
    ///
    /// # Returns
    /// Index into the mesh array on success, `UINT32_MAX` on failure.
    [[nodiscard]] uint32_t LoadMesh(const std::string& path, IVFS* vfs,
                                    IRHICommandList* cmdList);

    /// Reads back the shadow atlas from GPU and logs diagnostic depth values.
    /// Called when shadowDebugMode >= 2. One-shot per session (first frame only).
    void PerformShadowAtlasReadback(IRHICommandList* cmdList);

    /// Loads a texture from the VFS and uploads it to the GPU.
    ///
    /// Returns the index into the internal texture array. If the texture was
    /// already loaded (same path), returns the cached index without re-uploading.
    ///
    /// # Arguments
    /// * `path` — VFS-relative texture path (e.g., "textures/albedo.png").
    /// * `vfs` — VFS backend to read from. Must not be null.
    /// * `cmdList` — Command list in recording state for GPU upload.
    ///
    /// # Returns
    /// Index into the texture array on success, `UINT32_MAX` on failure.
    [[nodiscard]] uint32_t LoadTexture(const std::string& path, IVFS* vfs,
                                       IRHICommandList* cmdList);

    /// Queues a mesh path for deferred GPU upload.
    ///
    /// Safe to call from outside the render loop (e.g., from a Lua callback).
    /// If the path is already cached, returns the cached ID immediately.
    /// Otherwise reserves a stable slot index and schedules the upload for the
    /// next `RenderFrame()` call.
    ///
    /// # Arguments
    /// * `path` — VFS-relative mesh path (e.g., "meshes/cube.gltf").
    ///
    /// # Returns
    /// Stable mesh ID on success. `UINT32_MAX` if the path is empty.
    [[nodiscard]] uint32_t QueueMeshLoad(const std::string& path);

    /// Queues a texture path for deferred GPU upload.
    ///
    /// Safe to call from outside the render loop (e.g., from a Lua callback).
    /// If the path is already cached, returns the cached ID immediately.
    /// Otherwise reserves a stable slot index and schedules the upload for the
    /// next `RenderFrame()` call.
    ///
    /// # Arguments
    /// * `path` — VFS-relative texture path (e.g., "textures/albedo.png").
    ///
    /// # Returns
    /// Stable texture ID on success. `UINT32_MAX` if the path is empty.
    [[nodiscard]] uint32_t QueueTextureLoad(const std::string& path);

    /// Returns the VFS path for a texture loaded via `QueueTextureLoad`.
    ///
    /// Used by the Lua API to resolve texture IDs back to paths for
    /// setting component texture path fields.
    ///
    /// # Returns
    /// The texture path, or empty string if the ID is invalid.
    [[nodiscard]] std::string GetTexturePath(uint32_t textureId) const;

private:
    IRHIDevice* m_device = nullptr;

    // ── Engine Infrastructure ──────────────────────────────────────────────

    std::unique_ptr<InputSystem> m_input;
    std::unique_ptr<ShaderManager> m_shaderMgr;
    std::unique_ptr<PipelineLibrary> m_pipelines;
    std::unique_ptr<CBRingAllocator> m_cbRing;
    std::unique_ptr<EditorGrid> m_editorGrid;
    std::unique_ptr<StatsOverlay> m_statsOverlay;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<FPSCameraController> m_cameraCtrl;
    std::unique_ptr<rg::RgResourcePool> m_rgPool;

    // ── Fallback Textures (SM5 unconditional sampling) ────────────────────

    RHITextureHandle m_fallbackWhite;  ///< 1x1 RGBA8 white (255,255,255,255) — default for missing PBR maps.
    RHITextureHandle m_fallbackBlack;  ///< 1x1 RGBA8 black (0,0,0,255) — default for metallic map.
    RHITextureHandle m_fallbackNormal; ///< 1x1 RGBA8 flat normal (128,128,255,255) — default for missing normal maps.

    // ── GBuffer + Shadow Atlas ─────────────────────────────────────────────

    GBuffer m_gbuffer;
    ShadowAtlas m_shadowAtlas;                     ///< Cascade shadow map depth atlas (dynamic per-frame).
    ShadowCacheManager m_shadowCacheManager;       ///< Manages cached static shadow maps.
    RHITextureHandle m_staticShadowAtlas;           ///< Separate depth atlas for cached static shadows.
    uint32_t m_staticShadowAtlasSrv = UINT32_MAX;  ///< SRV heap index for static shadow atlas.

    // ── HDR Scene Color (intermediate before tonemap) ───────────────────
    RHITextureHandle m_hdrSceneColor;           ///< RGBA16F HDR render target for scene color.
    uint32_t m_hdrSceneColorSrv = UINT32_MAX;   ///< SRV for tonemap pass to read.
    bool m_hdrHasContent = false;               ///< True after the first render graph execution fills the HDR target.

    // ── Atmosphere LUT ─────────────────────────────────────────────────────

    RHITextureHandle m_transmittanceLut;         ///< 256x64 RGBA16F precomputed transmittance LUT.
    uint32_t m_transmittanceLutSrv = UINT32_MAX; ///< SRV index for sky shader sampling.
    uint32_t m_transmittanceLutUav = UINT32_MAX; ///< UAV index for compute write.
    bool m_transmittanceLutDirty = true;         ///< True if LUT needs to be (re)computed.
    bool m_transmittanceLutFilled = false;       ///< True once the LUT has been computed at least once (state = SRV).

    // ── Multi-Scattering LUT ──────────────────────────────────────────────

    RHITextureHandle m_multiScatterLut;             ///< 32x32 R11G11B10F precomputed multi-scattering LUT.
    uint32_t m_multiScatterLutSrv = UINT32_MAX;     ///< SRV index for sky shader sampling.
    uint32_t m_multiScatterLutUav = UINT32_MAX;     ///< UAV index for compute write.
    bool m_multiScatterLutDirty = true;             ///< True if multi-scatter LUT needs recompute.
    bool m_multiScatterLutFilled = false;           ///< True once the LUT has been computed.

    // ── IBL (Image-Based Lighting from sky cubemap) ────────────────────

    std::unique_ptr<IBLBaker> m_iblBaker;            ///< GPU compute pipeline for IBL bake (irradiance + prefiltered).
    RHITextureHandle m_skyCubemap;                 ///< 64x64x6 RGBA16F cubemap for sky capture.
    uint32_t m_skyCubemapSrv = UINT32_MAX;         ///< SRV index for IBL baker input.
    uint32_t m_skyCubemapUav = UINT32_MAX;         ///< UAV index for compute write.
    bool m_skyIBLDirty = true;                     ///< True when sky cubemap needs re-capture.
    IBLBaker::BakeResult m_iblResult;              ///< Irradiance + prefiltered SRV indices.
    uint32_t m_brdfLutSrvIndex = UINT32_MAX;       ///< BRDF LUT SRV (baked once at init).
    SHCoefficients m_skyIrradianceSH;              ///< L2 SH for sky diffuse irradiance.

    // ── Auto-Exposure ──────────────────────────────────────────────────────
    std::unique_ptr<AutoExposure> m_autoExposure;   ///< GPU histogram-based eye adaptation.

    // ── GTAO ─────────────────────────────────────────────────────────────
    std::unique_ptr<GTAOPass> m_gtaoPass;            ///< GTAO ambient occlusion compute pass.

    // ── Bloom ────────────────────────────────────────────────────────────
    std::unique_ptr<BloomPass> m_bloomPass;          ///< GPU compute bloom (threshold + downsample + upsample).

    // ── Distance Field Shadows ───────────────────────────────────────────

    DistanceFieldShadowConfig m_dfsConfig;             ///< DFS runtime configuration.
    SdfAtlasManager* m_sdfAtlas = nullptr;             ///< Externally-owned SDF atlas manager (may be null).
    RHITextureHandle m_dfsOutputTexture;               ///< RG16F DFS output (shadow_factor, depth).
    uint32_t m_dfsOutputSrvIndex = UINT32_MAX;         ///< SRV index for lighting pass to read DFS result.
    uint32_t m_dfsOutputUavIndex = UINT32_MAX;         ///< UAV index for cone trace pass to write.
    RHIBufferHandle  m_dfsCulledIndicesBuffer;         ///< StructuredBuffer<uint> culled SDF object indices.
    uint32_t m_dfsCulledIndicesSrvIndex = UINT32_MAX;  ///< SRV for cone trace to read culled list.
    uint32_t m_dfsCulledIndicesUavIndex = UINT32_MAX;  ///< UAV for cull pass to append indices.
    RHIBufferHandle  m_dfsIndirectArgsBuffer;          ///< RWByteAddressBuffer for indirect dispatch args.
    uint32_t m_dfsIndirectArgsUavIndex = UINT32_MAX;   ///< UAV for cull pass to write indirect args.


    // ── TAA (Temporal Anti-Aliasing) ─────────────────────────────────────────

    /// @source UE5 TemporalAA.cpp:41 — CurrentFrameWeight default 0.04
    RHITextureHandle m_taaHistory[2];          ///< Ping-pong RGBA16F history textures.
    uint32_t m_taaHistorySrv[2] = {UINT32_MAX, UINT32_MAX}; ///< SRV indices for history read.
    uint32_t m_taaHistoryUav[2] = {UINT32_MAX, UINT32_MAX}; ///< UAV indices for history write.
    uint32_t m_taaHistoryIndex = 0;            ///< Index of current write target (0 or 1).
    bool     m_taaEnabled = true;              ///< Runtime toggle for TAA.
    Mat4     m_prevViewProj;                   ///< Previous frame's unjittered viewProj for reprojection.
    bool     m_prevViewProjValid = false;      ///< False on first frame (no valid previous viewProj).

    /// Creates TAA history textures at the given resolution.
    /// Called from Init() and Resize().
    void CreateTaaResources(uint32_t width, uint32_t height);

    /// Destroys TAA history textures. Called from Shutdown() and Resize().
    void DestroyTaaResources();

    // ── Depth Prepass + Hi-Z Pyramid ────────────────────────────────────────

    bool m_depthPrepassEnabled = true;  ///< When true, depth prepass runs before GBuffer.

    /// Per-mip R32_FLOAT textures for the Hi-Z pyramid.
    /// mipLevels[0] = half-res, mipLevels[1] = quarter-res, etc.
    HiZMipLevel m_hizMipLevels[kHiZMaxMipLevels] = {};

    /// Number of mip levels in the Hi-Z pyramid (depends on render target size).
    uint32_t m_hizMipCount = 0;

    // ── Render State ───────────────────────────────────────────────────────

    RenderPath m_renderPath = RenderPath::Deferred;
    GBufferDebugMode m_debugMode = GBufferDebugMode::Count;
    DebugViewMode m_debugViewMode = DebugViewMode::Lit;
    uint32_t m_shadowDebugMode = 0;   ///< Shadow debug view mode (0=off, 1=grid, 2=readback). See shadow_debug.hlsli.
    uint32_t m_brdfDebugMode = 0;    ///< BRDF debug mode (0=off, 1=split-screen old vs new).
    RHIBufferHandle m_shadowReadbackBuffer;  ///< Persistent readback buffer for shadow atlas diagnostic (mode 2).
    uint32_t m_shadowReadbackRowPitch = 0;   ///< Row pitch of the readback (256-aligned).
    uint32_t m_shadowReadbackWidth = 0;      ///< Atlas width in pixels.
    int m_shadowReadbackPhase = 0;           ///< 0=issue copy, 1=wait+read, 2=done.
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    float m_totalTime = 0.f;
    bool m_statsOverlayInitialized = false;

    // ── Screenshot ─────────────────────────────────────────────────────────

    ScreenshotCapture m_screenshotCapture;  ///< Readback capture helper (lazily initialized).
    std::string m_screenshotPath;           ///< Path to save screenshot. Empty = no pending capture.
    bool m_screenshotPending = false;       ///< True if a screenshot was requested via RequestScreenshot().
    bool m_screenshotSaveDeferred = false;  ///< True after capture commands recorded; save on next fence wait.
    bool m_screenshotBase64 = false;        ///< If true, print base64 PNG to stdout instead of saving to disk.

    // ── Loaded Assets ──────────────────────────────────────────────────────

    std::vector<GPUMesh> m_meshes;
    std::unordered_map<std::string, uint32_t> m_meshMap;
    std::vector<TextureLoadResult> m_textures;
    std::unordered_map<std::string, uint32_t> m_textureMap;

    // ── Deferred Load Queues (Lua API — safe to enqueue outside render loop) ──

    /// Paths enqueued via `QueueMeshLoad`; flushed at the start of `RenderFrame`.
    std::vector<std::string> m_pendingMeshPaths;

    /// Paths enqueued via `QueueTextureLoad`; flushed at the start of `RenderFrame`.
    std::vector<std::string> m_pendingTexturePaths;

    // ── Constants ──────────────────────────────────────────────────────────

    bool m_hasSkyAtmosphere = false;  ///< True if a SkyAtmosphereComponent was found this frame.
    bool m_hasFog = false;            ///< True if a HeightFogComponent (enabled) was found this frame.

    // ── GPU Instancing (triple-buffered) ─────────────────────────────────
    static constexpr uint32_t kInstanceBufferCount = 3; ///< One per frame-in-flight.
    RHIBufferHandle m_instanceBuffers[kInstanceBufferCount]; ///< Per-frame instance buffers.
    uint32_t m_instanceBufferCapacity = 0;   ///< Current capacity in number of InstanceData elements.
    uint32_t m_instanceBufferSrvIndices[kInstanceBufferCount] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};

    // Shadow constants moved to shadow_atlas.h (kShadowResolution, kShadowAtlasSize)
    static constexpr uint32_t kCBRingFrames = 3;
    static constexpr uint64_t kCBRingBytesPerFrame = 2 * 1024 * 1024;

    // ── Internal Helpers ───────────────────────────────────────────────────

    /// Iterates the ECS World and populates a DrawList from entities with
    /// StaticMeshComponent + SceneComponent. Loads meshes/textures on demand.
    void SyncWorldToDrawList(World* world, IVFS* vfs, IRHICommandList* cmdList,
                             DrawList& outDrawList);

    /// Flushes deferred mesh and texture load queues populated by `QueueMeshLoad`
    /// and `QueueTextureLoad`.  Called at the start of each `RenderFrame()`.
    ///
    /// Requires a command list in recording state for GPU uploads.
    void FlushPendingLoads(IRHICommandList* cmdList, IVFS* vfs);

    /// Creates the shadow atlas depth texture.
    // Shadow atlas creation moved to ShadowAtlas::Init()

    /// Destroys loaded GPU meshes and textures.
    void DestroyAssets();

    /// Creates the Hi-Z pyramid per-mip textures for the given resolution.
    /// Called from Init() and Resize().
    void CreateHiZPyramid(uint32_t width, uint32_t height);

    /// Destroys all Hi-Z pyramid textures. Called from Shutdown() and Resize().
    void DestroyHiZPyramid();

    /// Creates DFS output texture, culled indices buffer, and indirect args buffer.
    /// Called from Init() and Resize().
    void CreateDfsResources(uint32_t width, uint32_t height);

    /// Destroys all DFS GPU resources. Called from Shutdown() and Resize().
    void DestroyDfsResources();
};

} // namespace uz
