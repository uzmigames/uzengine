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
#include "ibl/ibl_baker.h"
#include "postprocess/auto_exposure.h"

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

    /// Returns the current debug view mode.
    [[nodiscard]] DebugViewMode GetDebugViewMode() const { return m_debugViewMode; }

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
    RHITextureHandle m_shadowAtlas;
    uint32_t m_shadowAtlasSrv = UINT32_MAX;

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

    // ── IBL (Image-Based Lighting from sky cubemap) ────────────────────

    std::unique_ptr<IBLBaker> m_iblBaker;            ///< GPU compute pipeline for IBL bake (irradiance + prefiltered).
    RHITextureHandle m_skyCubemap;                 ///< 64x64x6 RGBA16F cubemap for sky capture.
    uint32_t m_skyCubemapSrv = UINT32_MAX;         ///< SRV index for IBL baker input.
    uint32_t m_skyCubemapUav = UINT32_MAX;         ///< UAV index for compute write.
    bool m_skyIBLDirty = true;                     ///< True when sky cubemap needs re-capture.
    IBLBaker::BakeResult m_iblResult;              ///< Irradiance + prefiltered SRV indices.
    uint32_t m_brdfLutSrvIndex = UINT32_MAX;       ///< BRDF LUT SRV (baked once at init).

    // ── Auto-Exposure ──────────────────────────────────────────────────────
    std::unique_ptr<AutoExposure> m_autoExposure;   ///< GPU histogram-based eye adaptation.

    // ── Render State ───────────────────────────────────────────────────────

    RenderPath m_renderPath = RenderPath::Deferred;
    GBufferDebugMode m_debugMode = GBufferDebugMode::Count;
    DebugViewMode m_debugViewMode = DebugViewMode::Lit;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    float m_totalTime = 0.f;
    bool m_statsOverlayInitialized = false;

    // ── Screenshot ─────────────────────────────────────────────────────────

    ScreenshotCapture m_screenshotCapture;  ///< Readback capture helper (lazily initialized).
    std::string m_screenshotPath;           ///< Path to save screenshot. Empty = no pending capture.
    bool m_screenshotPending = false;       ///< True if a screenshot was requested via RequestScreenshot().
    bool m_screenshotSaveDeferred = false;  ///< True after capture commands recorded; save on next fence wait.

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

    // ── GPU Instancing ────────────────────────────────────────────────────
    RHIBufferHandle m_instanceBuffer;        ///< Persistent instance buffer (resized on demand).
    uint32_t m_instanceBufferCapacity = 0;   ///< Current capacity in number of InstanceData elements.
    uint32_t m_instanceBufferSrvIndex = UINT32_MAX; ///< Cached SRV index for the instance buffer.

    static constexpr uint32_t kShadowAtlasSize = 4096;
    static constexpr uint32_t kShadowResolution = 2048;
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
    void CreateShadowAtlas();

    /// Destroys loaded GPU meshes and textures.
    void DestroyAssets();
};

} // namespace uz
