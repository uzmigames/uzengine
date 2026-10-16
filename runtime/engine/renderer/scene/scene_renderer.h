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

#include "entity.h"
#include "gbuffer.h"
#include "decal_pass.h"   // DecalInfo for m_frameDecals
#include "draw_list.h"
#include "mesh.h"
#include "render_path.h"
#include "gbuffer_debug_pass.h"  // GBufferDebugMode
#include "debug_view_mode.h"    // DebugViewMode
#include "show_flags.h"       // ShowFlags / ShowFlag
#include "gpu_mesh.h"
#include "texture_loader.h"
#include "screenshot_capture.h"
#include "shadow_atlas.h"
#include "shadow_cache.h"
#include "local_light_shadow_atlas.h"
#include "ibl/ibl_baker.h"
#include "ibl/env_map_sampler.h"
#include "ibl/sh_projection.h"
#include "env_mis_pass.h"
#include "probes/reflection_probe_renderer.h"
#include "postprocess/auto_exposure.h"
#include "postprocess/bloom_pass.h"
#include "postprocess/lens_flare_pass.h"
#include "postprocess/temporal_upscaler.h"
#include "temporal_history_state.h"
#include "postprocess/gtao_pass.h"
#include "postprocess/hiz_pyramid_pass.h"
#include "shadows/distance_field_shadow_config.h"
#include "camera_manager.h"
#include "gi/ssgi_renderer.h"
#include "gi/global_distance_field.h"
#include "gi/radiance_cache.h"
#include "gi/irradiance_probe_grid.h"
#include "gi/ddgi_system.h"
#include "engine/vfx/lighting/vfx_ddgi_emissive_feed.h"
#include "engine/ecs/environment/light_probe_volume_component.h"
#include "passes/gi/brixelizer_gi_pass.h"
#include "async_texture_decoder.h"  // TextureDecodeResult for m_deferredDecoded
#include "async_mesh_loader.h"      // MeshLoadResultCPU for m_asyncMeshReady
#include "gi/mesh_sdf_bake_queue.h" // MeshSdfBakeQueue (block B.3)
#include "inspector_pass.h"        // PassInspector for render target visualization
#include "upload_ring.h"           // UploadRing for zero-alloc texture staging

// Types needed by FrameContext struct (value members, not pointers)
#include "scene_renderer_frame.h"
#include "per_frame_params.h"
#include "shadow_data_params.h"
#include "cb_ring_allocator.h"
#include "instance_batcher.h"
#include "light_renderer.h"        // LightFrameData
#include "shadow_renderer.h"      // ShadowFrameData
#include "render_graph_types.h"   // rg::RgResourceId

#include <cstdint>
#include <functional>
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
class MaterialInterface;
class PlatformWindow;
class InputSystem;
class ShaderManager;
class PipelineLibrary;
class CBRingAllocator;
class EditorGrid;
class EditorInputHandler;
class StatsOverlay;
class ViewportToolbar;
class TechPanel;
class VfxEditorOverlay;
class OutlinePass;
class GizmoRenderer;
class UploadContext;
class Camera;
class FlyCameraController;
class World;
class AsyncTextureDecoder;
class AsyncMeshLoader;

class SdfAtlasManager;
class DynamicResolutionController;
class CloudPass;
struct CloudPassInput;
class VolumetricFogPass;
class VFXSystem;
class VFXHotReload;
class DecalManager;
class TrailSystem;
class PixelDebugSystem;
class RTManager;
class RTUVTestPass;
class MaterialPipeline;
class MaterialManager;
struct ParticleEmitter;
class VfxSystemInstance;
namespace rg { class RgResourcePool; class RgStateRegistry; }

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

    /// Returns true when all meshes, textures, and async decodes are complete.
    /// Used to gate screenshots — never capture before the scene is fully loaded.
    [[nodiscard]] bool IsSceneFullyLoaded() const;
    /// Debug variant — also logs which async loads are still outstanding.
    [[nodiscard]] bool IsSceneFullyLoadedDebug() const;

    /// Scene preload (nothing is presented): the asynchronous mesh SDF bakes run unthrottled
    /// while this is set (block B.3). The host sets it around its preload loop.
    void SetPreloading(bool preloading) { m_preloading = preloading; }

    /// Forwards to AutoExposure::ResetHistory (no-op when auto exposure is off).
    void ResetExposureHistory(IRHICommandList* cmdList);

    void InvalidateTemporalHistory(TemporalReset why) { m_temporal.Invalidate(why); }

    /// Returns true if there are async texture decodes still in flight,
    /// decoded textures awaiting GPU upload, or copy-queue uploads pending
    /// finalization. Used by the pre-load drain loop to block until all
    /// textures are visible before the first user-facing frame.
    [[nodiscard]] bool HasPendingTextureLoads() const;

    /// Blocks until all pending texture loads are complete (decode + upload + finalize).
    /// Calls FlushPendingLoads in a loop, issuing GPU work as needed.
    /// Use after the first RenderFrame to ensure all textures are visible.
    void DrainPendingTextureLoads(IRHICommandList* cmdList, IVFS* vfs);

    /// Initializes all rendering infrastructure.
    ///
    /// Creates ShaderManager, PipelineLibrary, CBRingAllocator (3 frames, 2MB),
    /// EditorGrid, Camera, FlyCameraController, RgResourcePool, GBuffer, and
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

    /// Enables or disables editor mode (grid, stats overlay, gizmos).
    /// Default: false (game/play mode — no editor overlays).
    void SetEditorMode(bool enabled);

    /// Returns true if editor mode is active.
    [[nodiscard]] bool IsEditorMode() const;

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

    /// Callback invoked just before the command list is ended and executed.
    /// Used by the host to inject GPU readback copies into the same submission
    /// (e.g., copy the LDR intermediate to a readback buffer for H.264 encoding).
    using PreEndCallback = std::function<void(IRHICommandList*)>;

    /// Renders one frame with an optional pre-End callback. Identical to
    /// `RenderFrame()` above, but invokes `preEndCb` after all render graph
    /// passes and atmosphere bake, just before `cmdList->End()`.
    void RenderFrame(IRHICommandList* cmdList, IRHISwapChain* swapChain,
                     World* world, IVFS* vfs, float dt, uint32_t frameIndex,
                     PreEndCallback preEndCb);

    /// Returns the LDR intermediate texture handle for frame readback.
    /// Valid after Init(). The texture is RGBA8Unorm at viewport resolution.
    /// After RenderFrame(), it contains the tonemapped LDR frame.
    [[nodiscard]] RHITextureHandle GetLdrIntermediate() const;

    /// Returns the resource state of the LDR intermediate after the render
    /// graph has executed. ShaderResource when CAS/FXAA is active, CopySrc
    /// when the LDR_Blit fallback path ran.
    [[nodiscard]] RHIResourceState GetLdrIntermediateState() const;

    /// Releases all GPU resources and resets to default state.
    ///
    /// Safe to call multiple times or without prior `Init()`. The caller must
    /// ensure the GPU is idle before calling this (no in-flight frames).
    void Shutdown();

    // ── VFX System ────────────────────────────────────────────────────────

    /// Sets an externally-owned VFX system for particle rendering.
    ///
    /// @deprecated The SceneRenderer now owns and auto-initializes its own
    /// VFXSystem (budget=500 000) during Init(). Call this only when an
    /// externally-managed VFXSystem must override the internal one (e.g.,
    /// a shared emitter pool owned by the game layer). The supplied pointer
    /// is stored as a non-owning alias; its lifetime must exceed Shutdown().
    /// Pass nullptr to re-enable the internal owned system.
    ///
    /// # Parameters
    /// - `vfx`: Non-owning pointer to an externally-managed VFX system,
    ///          or nullptr to restore use of the internally-owned system.
    void SetVFXSystem(VFXSystem* vfx) { m_vfxSystemOverride = vfx; }

    /// Returns the active VFX system pointer. The override (set by
    /// `SetVFXSystem`) takes precedence; otherwise the internally-owned
    /// system is returned. Result is nullptr only when neither has been
    /// constructed yet (pre-Init).
    [[nodiscard]] VFXSystem* GetVFXSystem() const noexcept {
        return m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();
    }

    /// Sets the decal manager reference for deferred decal rendering.
    ///
    /// The decal manager provides frustum-culled, sorted decal lists to
    /// RegisterDecalPasses(). The pointer must remain valid until Shutdown()
    /// or the next SetDecalManager() call. Pass nullptr to disable decal rendering.
    ///
    /// # Parameters
    /// - `mgr`: Non-owning pointer to the DecalManager, or nullptr.
    void SetDecalManager(DecalManager* mgr) { m_decalManager = mgr; }

    /// Sets the trail system reference for ribbon trail rendering.
    ///
    /// The trail system provides per-trail render data to RegisterParticlePasses().
    /// The pointer must remain valid until Shutdown() or the next SetTrailSystem()
    /// call. Pass nullptr to disable trail rendering.
    ///
    /// # Parameters
    /// - `sys`: Non-owning pointer to the TrailSystem, or nullptr.
    void SetTrailSystem(TrailSystem* sys) { m_trailSystem = sys; }

    /// Sets the pixel debug system reference for GPU printf diagnostics.
    ///
    /// When set, a compute post-pass (PixelDebugCS) dispatches after the render
    /// graph, reading GBuffer + HDR textures at the selected pixel and writing
    /// debug records to the PixelDebug UAV buffers.
    ///
    /// # Parameters
    /// - `pds`: Non-owning pointer to the PixelDebugSystem, or nullptr.
    void SetPixelDebugSystem(PixelDebugSystem* pds) noexcept;

    /// Returns the current pixel debug system, or nullptr if not set.
    [[nodiscard]] PixelDebugSystem* GetPixelDebugSystem() const noexcept { return m_pixelDebugSystem; }

    /// Sets the content root directory used for VFX hot-reload.
    ///
    /// The runtime is path-agnostic — it never hardcodes a content
    /// directory. The host application (engine CLI, editor, demo) is
    /// responsible for resolving the content root from its own sources
    /// (project config, `.uzproject` file, command-line flag) and forwarding
    /// it here *before* `Init()` runs, so the hot-reload watcher can attach
    /// to the correct directory.
    ///
    /// When unset (empty string), VFX hot-reload is skipped silently.
    ///
    /// # Parameters
    /// - `root`: Absolute or CWD-relative path to the content directory.
    void SetContentRoot(std::string root) { m_contentRoot = std::move(root); }

    /// Returns the content root currently configured via SetContentRoot().
    [[nodiscard]] const std::string& GetContentRoot() const noexcept { return m_contentRoot; }

    // ── Configuration ──────────────────────────────────────────────────────

    /// Switches between deferred and forward rendering.
    ///
    /// Takes effect on the next `RenderFrame()` call. Switching mid-frame
    /// is not supported.
    void SetRenderPath(RenderPath path) { m_renderPath = path; }

    /// Returns the current render path.
    [[nodiscard]] RenderPath GetRenderPath() const { return m_renderPath; }

    /// Returns the actual path tracer history counter since the last consumed
    /// reset. A queued reset is consumed on the next path-traced frame.
    [[nodiscard]] uint32_t GetPathTracerSamplesAccumulated() const { return m_ptAccumSamples; }

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

    /// Returns the current show-flag set by value. Modify via `SetShowFlag`
    /// — a copy is returned so callers can read without holding a reference
    /// into the renderer (ShowFlags is 8 bytes).
    [[nodiscard]] ShowFlags GetShowFlags() const { return m_showFlags; }

    /// Returns a mutable reference to the show-flag set. Used by CVar-bridge
    /// and IPC handlers that apply edits between frames.
    [[nodiscard]] ShowFlags& MutableShowFlags() { return m_showFlags; }

    /// Overwrites the entire show-flag set. Takes effect on the next
    /// `RenderFrame()` call.
    void SetShowFlags(ShowFlags flags) { m_showFlags = flags; }

    /// Sets a single show flag to `enabled`. Takes effect on the next frame.
    void SetShowFlag(ShowFlag flag, bool enabled) { m_showFlags.Set(flag, enabled); }

    /// Returns `true` if the given show flag is currently enabled.
    [[nodiscard]] bool GetShowFlag(ShowFlag flag) const { return m_showFlags.Get(flag); }

    /// Sets the shadow debug visualization mode (0=off, 1-8=debug views).
    /// See shadow_debug.hlsli for mode descriptions.
    void SetShadowDebugMode(uint32_t mode) { m_shadowDebugMode = mode; }

    /// Returns the current shadow debug mode.
    [[nodiscard]] uint32_t GetShadowDebugMode() const { return m_shadowDebugMode; }

    /// One rectangle of a region dump (diagnostics).
    struct RegionRect {
        std::string name;                       ///< Short label stored in the file (<= 15 chars).
        uint32_t x = 0, y = 0, w = 0, h = 0;    ///< Screen rectangle in output-resolution pixels.
    };

    /// Region dump (diagnostics): for every frame in [firstFrame, lastFrame] writes
    /// `<dir>/region_<frame>.bin` with per-pixel linear-HDR records for each rectangle.
    /// `fields` is the bitmask of shaders/runtime/common/region_dump.hlsli
    /// (1 = deferred-lighting records, 2 = TAA resolve records, 4 = TAA output only).
    /// The GPU stamps every rectangle with the frame it rendered; the readback (three
    /// frames later, no WaitIdle) rejects any file whose stamps disagree with the frame
    /// the CPU believes it read, so a stale or partial readback is never analysed as data.
    /// File layout: see `RegionFileHeader` in frame_postprocess.cpp.
    void SetRegionDump(std::string dir, std::vector<RegionRect> rects,
                       uint32_t firstFrame, uint32_t lastFrame, uint32_t fields);

    /// Number of region-dump files rejected because a GPU frame stamp did not match.
    [[nodiscard]] uint32_t GetRegionDumpMismatchCount() const noexcept { return m_regionStampMismatches; }
    /// Number of region-dump files written.
    [[nodiscard]] uint32_t GetRegionDumpFileCount() const noexcept { return m_regionFilesWritten; }
    /// Exposure the last rendered frame used (auto-exposure result x r.Exposure).
    [[nodiscard]] float GetLastFrameExposure() const noexcept { return m_lastFrameExposure; }

    /// Enables or disables shadow caching at runtime.
    ///
    /// Controls the shadow cache toggle at runtime.
    /// Takes effect on the next `RenderFrame()` call.
    ///
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

    /// Enables or disables GPU timestamp profiling.
    ///
    /// When enabled, per-pass GPU timestamps are recorded via the RHI profiler
    /// and logged after each frame. Activated by the `--gpu-profile` CLI flag.
    void SetGpuProfileEnabled(bool enabled) { m_gpuProfileEnabled = enabled; }

    /// Returns whether GPU timestamp profiling is enabled.
    [[nodiscard]] bool IsGpuProfileEnabled() const { return m_gpuProfileEnabled; }

    /// Enables or disables the depth prepass.
    ///
    /// When enabled, a depth-only pass executes before the GBuffer pass,
    /// populating the depth buffer to enable early-Z rejection and Hi-Z
    /// pyramid generation.
    void SetDepthPrepassEnabled(bool enabled) { m_depthPrepassEnabled = enabled; }

    /// Returns whether the depth prepass is currently enabled.
    [[nodiscard]] bool IsDepthPrepassEnabled() const { return m_depthPrepassEnabled; }

    /// Enables or disables GPU HiZ occlusion culling.
    ///
    ///
    /// When enabled (and depth prepass + HiZ pyramid are active), the GPU
    /// cull pass performs an additional HiZ occlusion test after frustum
    /// culling. Requires SM6.6 bindless for per-mip HiZ texture access.
    /// When disabled, only frustum culling is performed on the GPU.
    ///
    /// Default: false (disabled until visually verified).
    void SetGpuOcclusionCullingEnabled(bool enabled) { m_gpuOcclusionCullingEnabled = enabled; }

    /// Returns whether GPU HiZ occlusion culling is currently enabled.
    [[nodiscard]] bool IsGpuOcclusionCullingEnabled() const { return m_gpuOcclusionCullingEnabled; }

    /// Sets the DFS configuration at runtime.
    void SetDfsConfig(const DistanceFieldShadowConfig& cfg) { m_dfsConfig = cfg; }

    /// Returns the current DFS configuration.
    [[nodiscard]] const DistanceFieldShadowConfig& GetDfsConfig() const { return m_dfsConfig; }

    /// Sets the SDF atlas manager for distance field shadows.
    /// The atlas manager must outlive this renderer. Set to nullptr to disable DFS.
    void SetSdfAtlasManager(SdfAtlasManager* atlas) { m_sdfAtlas = atlas; }

    /// Enables or disables Screen-Space Global Illumination (SSGI) at runtime.
    ///
    ///
    /// When enabled, a 5-pass compute pipeline provides one-bounce indirect
    /// diffuse lighting from screen-space ray marching against the previous
    /// frame's HDR scene color.
    void SetSsgiEnabled(bool enabled) { m_ssgiRenderer.settings.enabled = enabled; }

    /// Returns whether SSGI is currently enabled.
    [[nodiscard]] bool IsSsgiEnabled() const { return m_ssgiRenderer.settings.enabled; }

    /// Sets the SSGI quality level (1-4). Maps to ray count: Q1=4, Q2=8, Q3=16, Q4=32.
    void SetSsgiQuality(int quality) { m_ssgiRenderer.settings.quality = quality; }

    /// Returns the current SSGI quality level.
    [[nodiscard]] int GetSsgiQuality() const { return m_ssgiRenderer.settings.quality; }

    /// Sets the SSGI intensity multiplier.
    void SetSsgiIntensity(float intensity) { m_ssgiRenderer.settings.intensity = intensity; }

    /// Returns the current SSGI intensity.
    [[nodiscard]] float GetSsgiIntensity() const { return m_ssgiRenderer.settings.intensity; }

    /// Sets the SDF GI (radiance cache) intensity multiplier.
    void SetGiIntensity(float intensity) { m_giIntensity = intensity; }

    /// Returns the current SDF GI intensity.
    [[nodiscard]] float GetGiIntensity() const { return m_giIntensity; }

    /// Enables or disables Screen-Space Reflections (SSR) at runtime.
    ///
    ///
    /// When disabled, the lighting pass uses only IBL cubemap reflections.
    /// When enabled, a Hi-Z ray trace + resolve pass provides higher-fidelity
    /// reflections for smooth surfaces, blending with IBL as fallback.
    void SetSsrEnabled(bool enabled) { m_ssrEnabled = enabled; }

    /// Returns whether SSR is currently enabled.
    [[nodiscard]] bool IsSsrEnabled() const { return m_ssrEnabled; }

    /// Enables or disables Temporal Anti-Aliasing (TAA) at runtime.
    ///
    ///
    /// When disabled, the render pipeline outputs the raw jittered frame
    /// without temporal accumulation (jitter is also disabled).
    void SetTaaEnabled(bool enabled) { m_taaEnabled = enabled; }

    /// Returns whether TAA is currently enabled.
    [[nodiscard]] bool IsTaaEnabled() const { return m_taaEnabled; }

    /// Enables or disables screen-space lens flare at runtime.
    /// Default: OFF. Enable via Lua `uz.renderer.set_lens_flare_enabled(true)`.
    /// Also sets the `r.LensFlare` CVar: ApplyCVars() re-reads the member from it
    /// every frame, so a write to the member alone was undone on the next frame.
    void SetLensFlareEnabled(bool enabled);

    /// Returns whether lens flare is currently enabled.
    [[nodiscard]] bool IsLensFlareEnabled() const { return m_lensFlareEnabled; }

    /// Sets the temporal upscaler type. Recreates the upscaler on change.
    void SetUpscalerType(UpscalerType type);

    /// Returns the current upscaler type.
    [[nodiscard]] UpscalerType GetUpscalerType() const { return m_upscalerType; }

    /// Returns the current upscaler instance (may be null if type is None).
    [[nodiscard]] ITemporalUpscaler* GetUpscaler() const { return m_upscaler.get(); }

    /// Returns the dynamic resolution controller.
    [[nodiscard]] DynamicResolutionController& GetDynamicResolution() { return *m_dynRes; }

    /// Returns the pass inspector for render target visualization and validation.
    [[nodiscard]] PassInspector& GetPassInspector() noexcept { return m_passInspector; }
    /// Const overload of GetPassInspector().
    [[nodiscard]] const PassInspector& GetPassInspector() const noexcept { return m_passInspector; }

    /// Sets the global LOD bias.
    ///
    /// The bias scales all LOD screen size thresholds:
    /// - 1.0 = default behavior
    /// - 0.5 = keep higher LODs 2x longer (higher quality, more draw calls)
    /// - 2.0 = switch to lower LODs 2x sooner (lower quality, fewer draw calls)
    ///
    /// # Arguments
    /// * `bias` — LOD bias multiplier. Clamped internally to [0.25, 4.0].
    void SetLodBias(float bias) { m_lodBias = bias; }

    /// Returns the current global LOD bias.
    [[nodiscard]] float GetLodBias() const { return m_lodBias; }

    // ── Accessors ──────────────────────────────────────────────────────────

    /// Returns the active camera used for rendering. Must only be called after `Init()`.
    [[nodiscard]] Camera& GetCamera();
    /// Const overload — returns a const reference to the active render camera.
    [[nodiscard]] const Camera& GetCamera() const;

    /// Returns the camera manager. Must only be called after `Init()`.
    [[nodiscard]] CameraManager& GetCameraManager();
    [[nodiscard]] const CameraManager& GetCameraManager() const;

    /// Returns the fly camera controller. Must only be called after `Init()`.
    [[nodiscard]] FlyCameraController& GetCameraController();

    /// Returns the input system. May return null if `Init()` has not been called.
    [[nodiscard]] InputSystem* GetInput() const;

    /// Returns the stats overlay. May return null if `Init()` has not been called.
    [[nodiscard]] StatsOverlay* GetStatsOverlay() const;

    /// Returns the VFX editor overlay. May return null if not created.
    [[nodiscard]] VfxEditorOverlay* GetVfxEditorOverlay() const;

    /// Returns the runtime ParticleEmitter for a given VfxEmitterComponent index.
    /// Returns null if index is out of range or emitter not yet created.
    [[nodiscard]] ParticleEmitter* GetRuntimeEmitter(int32_t runtimeIdx);

    /// Returns the current display resolution width in pixels.
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }

    /// Returns the current display resolution height in pixels.
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }

    /// Sets the screen percentage (render scale) for temporal upscaling.
    ///
    ///
    /// The internal render resolution is computed as:
    ///   renderWidth  = max(1, displayWidth  * pct / 100)
    ///   renderHeight = max(1, displayHeight * pct / 100)
    ///
    /// At 100% (default), render resolution equals display resolution and the
    /// TAA resolve pass is a no-op identity upscale. Below 100%, geometry and
    /// lighting run at lower resolution and TAA resolves/upscales to display res.
    /// Above 100% (supersampling), rendering runs at higher res and TAA
    /// downscales to display resolution.
    ///
    /// Triggers a full render target rebuild via Resize().
    ///
    /// # Arguments
    /// * `pct` — Screen percentage [10, 200]. Clamped internally.
    /// Loads or releases the component-replay buffer (diagnostic, off by default).
    void EnsureComponentReplay(const std::string& path);

    /// Enables the component replay from a file built by scripts/rendering/make_component_replay.py.
    void SetComponentReplay(const std::string& path) { EnsureComponentReplay(path); }

    /// Creates or releases the r.TAA.ClampFreezeAfterFrame storage (diagnostic, off by default).
    void EnsureTaaClampBoxResources(bool wanted);

    void SetScreenPercentage(float pct);

    /// Returns the current screen percentage (10-200, default 100).
    [[nodiscard]] float GetScreenPercentage() const { return m_screenPercentage; }

    /// Returns the internal render resolution width in pixels.
    /// When screen percentage is 100%, this equals GetWidth().
    [[nodiscard]] uint32_t GetRenderWidth() const { return m_renderWidth; }

    /// Returns the internal render resolution height in pixels.
    /// When screen percentage is 100%, this equals GetHeight().
    [[nodiscard]] uint32_t GetRenderHeight() const { return m_renderHeight; }

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

    /// Requests an asynchronous mesh load (background file read + parse).
    ///
    /// Reserves a placeholder slot in the mesh array keyed by `path` and
    /// submits the file to the AsyncMeshLoader worker pool. The slot's
    /// GPUMesh stays invalid until FlushPendingLoads() integrates the
    /// completed CPU mesh (budgeted by `r.LazyLoad.MeshesPerFrame`).
    /// Callers must treat an invalid slot as "not ready yet" and retry
    /// claiming on a later frame — never draw a placeholder.
    ///
    /// Falls back to the synchronous LoadMesh() path when the async loader
    /// is unavailable (init failure or pre-init call).
    ///
    /// # Arguments
    /// * `path` — VFS-relative mesh path (slot map key).
    /// * `vfs` — VFS for path resolution / background reads. May be null.
    /// * `cmdList` — Command list for the synchronous fallback only.
    ///
    /// # Returns
    /// Stable slot index (placeholder or ready), `UINT32_MAX` on empty path
    /// or synchronous-fallback failure.
    [[nodiscard]] uint32_t RequestMeshAsync(const std::string& path, IVFS* vfs,
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
                                       IRHICommandList* cmdList,
                                       bool isSRGB = false);

    /// Preloads all textures referenced by a material by submitting them to
    /// the async texture decoder immediately. Call from OnStart() after
    /// assigning materials to begin background decode before the first frame.
    ///
    /// sRGB is resolved per slot via MaterialInterface::IsTextureSlotSRGB(),
    /// so this works for any shading model (PBR, Hair, Cloth, SSS, etc.).
    void PreloadMaterialTextures(MaterialInterface* mat, IVFS* vfs,
                                  IRHICommandList* cmdList);

    /// Registers a CPU-generated procedural mesh and queues it for GPU upload.
    ///
    /// Unlike `QueueMeshLoad` which loads from VFS, this takes an already-built
    /// `Mesh` object (from `GenerateSphereMesh`, `GeneratePlaneMesh`, etc.) and
    /// registers it with a synthetic key. The mesh is uploaded on the next
    /// `FlushPendingLoads` call.
    ///
    /// # Arguments
    /// * `mesh` — CPU mesh to upload. Moved into internal storage.
    /// * `key` — Synthetic cache key (e.g. "procedural://sphere_32_16").
    ///
    /// # Returns
    /// Stable mesh ID on success. `UINT32_MAX` on failure.
    [[nodiscard]] uint32_t RegisterProceduralMesh(Mesh&& mesh, const std::string& key);

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
    [[nodiscard]] uint32_t QueueTextureLoad(const std::string& path, bool isSRGB = false);

    /// Returns the VFS path for a texture loaded via `QueueTextureLoad`.
    ///
    /// Used by the Lua API to resolve texture IDs back to paths for
    /// setting component texture path fields.
    ///
    /// # Returns
    /// The texture path, or empty string if the ID is invalid.
    [[nodiscard]] std::string GetTexturePath(uint32_t textureId) const;

    /// Returns the current loading state of a texture slot.
    /// Used by debug overlays and streaming decisions.
    [[nodiscard]] TextureLoadState GetTextureLoadState(uint32_t textureId) const {
        if (textureId < static_cast<uint32_t>(m_textures.size())) {
            return m_textures[textureId].loadState;
        }
        return TextureLoadState::Empty;
    }

    // ── HDRI Environment ──────────────────────────────────────────────────

    /// Sets an external HDRI equirectangular panorama as the environment map.
    ///
    /// Converts the equirect texture to a cubemap via compute shader, then
    /// bakes irradiance and prefiltered specular cubemaps for IBL. This
    /// replaces the procedural sky atmosphere as the source of environment
    /// lighting for the current scene.
    ///
    /// The sky atmosphere capture pass is automatically suppressed after
    /// calling this — the HDRI cubemap is used directly for IBL and skybox.
    ///
    /// # Arguments
    /// * `equirectSrvIndex` — SRV descriptor heap index of the equirect HDR
    ///   texture (RGBA16F). Obtained from `TextureLoader::LoadHDR().srvIndex`.
    /// * `cubemapSize` — Face resolution for the cubemap conversion. Default
    ///   512 is a good balance of quality vs. VRAM.
    ///
    /// # Returns
    /// `true` on success (IBL re-baked), `false` on failure (error logged).
    ///
    /// # Thread Safety
    /// Must be called from the render/init thread.
    bool SetHDRIEnvironment(uint32_t equirectSrvIndex, uint32_t cubemapSize = 512);

    /// Sets the environment from a Three.js CubeUV PMREM atlas texture.
    ///
    /// Extracts a standard cubemap with mip chain from the PMREM atlas (using
    /// `IBLBaker::PmremToCubemap`), then bakes only the irradiance cubemap
    /// (prefiltered specular is already in the PMREM mip chain). Projects SH
    /// irradiance for diffuse lighting and fog color.
    ///
    /// # Arguments
    /// * `result` — TextureLoadResult with isPMREM=true, valid handle/SRV.
    ///
    /// # Returns
    /// `true` on success, `false` on failure (error logged).
    bool SetPMREMEnvironment(const TextureLoadResult& result);

    /// Track an externally-created texture so it is destroyed during Shutdown.
    /// Used by Lua API (uz.texture.load_hdr) to prevent GPU memory leaks.
    void TrackExternalTexture(const TextureLoadResult& tex);

    // ── Low-level accessors (for subsystems needing direct GPU access) ────

    /// Returns the RHI device pointer. Valid from Init() through Shutdown().
    ///
    /// Used by subsystems that need to create GPU resources outside the render
    /// graph (e.g., GPU material baking, IBL baking).
    [[nodiscard]] IRHIDevice* GetDevice() const noexcept { return m_device; }

    /// Returns the shader manager. Valid from Init() through Shutdown().
    ///
    /// Used by subsystems that need to compile shaders at runtime (e.g.,
    /// GPU material baking with dynamically generated HLSL).
    [[nodiscard]] ShaderManager* GetShaderManager() const noexcept { return m_shaderMgr.get(); }

    // ── Editor Selection ──────────────────────────────────────────────────

    /// Sets the currently selected entity for editor outline rendering.
    ///
    /// # Arguments
    /// * `entity` — The selected entity. Pass `Entity::Invalid()` to clear.
    void SetSelectedEntity(Entity entity);

    /// Returns the currently selected entity for editor outline rendering.
    [[nodiscard]] Entity GetSelectedEntity() const;

    /// Returns the object-space AABB bounds for a mesh by its ID.
    [[nodiscard]] bool GetMeshBounds(uint32_t meshId, Vec3& outMin, Vec3& outMax) const;

    /// Returns a pointer to the GPU mesh at the given ID, or nullptr if the ID
    /// is out of range or the mesh has not finished uploading.
    ///
    /// Used by the animation subsystem to upload per-frame skinned vertex data
    /// into the mesh's vertex buffer after GPU skinning.
    [[nodiscard]] const GPUMesh* GetGPUMesh(uint32_t meshId) const;

    /// Queues an RHI buffer for destruction after the current frame's fence
    /// completes. Used by subsystems that create transient upload/staging
    /// buffers inside the render command list: the buffer must outlive the
    /// recorded GPU copies, so it cannot be destroyed immediately.
    ///
    /// Processed at the start of the NEXT frame (see
    /// `frame_setup.cpp:`m_deferredDestroyBuffers flush), after
    /// `Application::WaitForFrame` has confirmed the previous GPU submit is
    /// complete.
    void QueueBufferDestroy(RHIBufferHandle buffer);

    /// Sets the EditorInputHandler pointer used by the ViewportToolbar.
    /// Non-owning — the handler must outlive the renderer.
    void SetEditorInputHandler(EditorInputHandler* handler) { m_editorInputHandler = handler; }

    /// Sets the active gizmo mode for editor gizmo rendering.
    /// 0 = translate, 1 = rotate, 2 = scale.
    void SetGizmoMode(uint8_t mode) { m_gizmoMode = mode; }

    /// Sets the gizmo coordinate space.
    /// 0 = local, 1 = world.
    void SetGizmoSpace(uint8_t space) { m_gizmoSpace = space; }

    /// Sets which gizmo axis the mouse is currently hovering over.
    /// -1 = none, 0 = X, 1 = Y, 2 = Z.
    void SetGizmoHoveredAxis(int axis) { m_gizmoHoveredAxis = axis; }

    /// Sets the currently active (dragging) gizmo axis.
    /// -1 = none, 0 = X, 1 = Y, 2 = Z.
    void SetGizmoActiveAxis(int axis) { m_gizmoActiveAxis = axis; }

private:
    #include "scene_renderer_impl.h"

    // ── VFX members ───────────────────────────────────────────────────────
    /// Owned VFX system. Initialized during Init(), shut down during Shutdown().
    /// Budget is fixed at 500 000 particles (~60 MB GPU).
    std::unique_ptr<VFXSystem> m_vfxSystem;
    /// Non-owning override supplied via SetVFXSystem(). When non-null this
    /// takes precedence over m_vfxSystem for all render-pass queries.
    VFXSystem* m_vfxSystemOverride = nullptr;
    DecalManager* m_decalManager = nullptr;
    TrailSystem* m_trailSystem = nullptr;
    PixelDebugSystem* m_pixelDebugSystem = nullptr;
    RHIBufferHandle m_vfxDummyDynParams;      ///< Scratch buffer for dynParams when emitter has none.
    RHIBufferHandle m_vfxKillVolumeBuffer;    ///< GPU buffer for kill volume descriptors (16 × 32 bytes).
    RHIBufferHandle m_vfxDeathEventBuffer;    ///< GPU buffer for death events (4096 × 32 bytes).
    RHIBufferHandle m_vfxDeathEventCountBuffer; ///< Death event counter (single uint32).
    RHIBufferHandle m_decalCubeVB;
    RHIBufferHandle m_vfxZeroBuffer; ///< 4-byte upload buffer containing uint32 0. Used to zero-clear aliveCount.
    RHIBufferHandle m_decalCubeIB;

    /// Registers deferred decal passes that project decals onto the G-Buffer.
    void RegisterDecalPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Registers VFX particle passes: billboard rendering + sort (render graph).
    /// Simulation is dispatched inline via DispatchVFXSimulation() before the
    /// render graph, matching the standalone demo's working dispatch pattern.
    void RegisterParticlePasses(rg::RgBuilder& rgBuilder, FrameContext& ctx);

    /// Dispatches VFX particle simulation (spawn, update, indirect args) as
    /// inline compute on the main command list, BEFORE the render graph.
    ///
    /// This runs identically to the standalone VFX demo's dispatch pattern:
    /// swap ping-pong -> init if needed -> spawn CS -> UAV barrier -> update CS
    /// -> UAV barrier -> indirect args CS -> UAV->IndirectArgument barrier.
    ///
    /// The render graph only handles rendering (billboard draw) and barrier
    /// tracking for the SRV transitions needed by the draw pass.
    void DispatchVFXSimulation(IRHICommandList* cmdList, float dt, uint32_t frameIndex);

    /// Dispatches the bitonic depth sort for emitters that need back-to-front
    /// rendering. Runs inline immediately after `DispatchVFXSimulation` so the
    /// post-update particle attributes are visible to the sort CS.
    ///
    /// Emitters that opt in:
    /// - Sprite emitters with `blendMode == AlphaBlend` (mode 0). Sort uses
    ///   `GpuSortMode::ViewDepth`.
    /// - Mesh emitters where `MeshRenderModule::sortMode != None`. Mode is
    ///   forwarded to the sort CS (ViewDepth or CustomAttribute).
    ///
    /// Other emitters (additive sprites, unsorted meshes) are skipped — alpha
    /// blend on additive is order-independent, and unsorted meshes opt out
    /// explicitly.
    ///
    /// The sort consumes positions / ages / lifespans (and optionally a
    /// custom-attribute SRV) from `gpuBuf.WriteSet()` and produces the
    /// `gpuBuf.sort.sortIndices` buffer, which downstream passes index into
    /// to translate logical "back-to-front draw slot N" -> source particle
    /// index.
    void DispatchVFXSort(IRHICommandList* cmdList);

    /// Walks the active emitter list and produces up to
    /// `VfxDDGIEmissiveFeed::kMaxSamples` `VfxEmissiveSample` entries
    /// for the brightest emitters this frame, then forwards them to
    /// `m_vfxDDGIEmissiveFeed.UploadFrame` for GPU upload.
    ///
    /// CVar gates:
    /// - `r.VFX.DDGIInjection` (default 1): when 0, the function is a
    ///   logged no-op and `m_vfxDDGIEmissiveFeed.GetSampleCount() == 0`.
    /// - `r.VFX.DDGIInjection.MaxEmitters` (default 32, clamped to
    ///   `kMaxSamples`): hard cap on the number of emitters considered
    ///   so a runaway content count cannot starve the readback path.
    ///
    /// Each produced sample uses the emitter's world position, the
    /// emitter's `initialColor.rgb * emissiveIntensity` as the radiance,
    /// and `spawnRadius * 4` (clamped to a minimum) as the influence
    /// radius. Emitters with zero alive particles or zero emissive
    /// intensity are skipped before the top-N sort. The DDGI probe-
    /// update CS reading the buffer is gated by
    /// `phase16_vfx-lumen-gi-hardware-validation` — until that ships
    /// the gather + upload still runs but the probe shader does not yet
    /// consume the SRV index.
    void GatherAndUploadVfxEmissiveSamples();
};

} // namespace uz
