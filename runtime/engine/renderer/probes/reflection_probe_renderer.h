/// UzEngine Reflection Probe Renderer — Cubemap capture, IBL bake, and GPU upload.
///
/// Manages the lifecycle of reflection probe cubemaps: gathers enabled
/// ReflectionProbeComponent entities from the ECS, captures dirty probes
/// (one per frame to amortize cost), bakes irradiance + prefiltered specular
/// cubemaps via IBLBaker, and uploads a StructuredBuffer<ReflectionProbeGPU>
/// for the deferred lighting shader to consume.
///
/// # Cubemap Capture
/// Each probe owns an RGBA16F Texture2DArray (6 slices) at the probe's
/// configured resolution, plus a shared D32Float depth buffer. When a probe
/// is dirty, the capture first renders a sun shadow map of its own (two
/// cascades centred on the probe, RenderCaptureShadowAtlas), then renders
/// the scene from the probe's world position into the 6 cubemap faces with
/// the probe-capture pipeline (one face per draw, 90-degree FOV, 1:1 aspect):
/// probe_capture_ps.hlsl lights the geometry as the deferred pass lights the
/// frame - the sun through those cascades, the sky IBL, the emissive - from
/// the frame's own PerFrameCB and the face's view-projection. The sky cubemap
/// is drawn behind the geometry (the HDRI skybox pipeline, depth == far). The
/// captured cubemap is then fed to IBLBaker for irradiance + prefiltered
/// specular generation.
///
/// One probe is captured per frame to amortize GPU cost. Faces are rendered
/// sequentially within a single capture (2 shadow cascades + 6 x 2 render
/// passes per probe).
///
/// # Ownership
/// Owns the GPU structured buffer for probe data, per-probe capture cubemaps,
/// and the shared capture depth buffer. Does NOT own the IRHIDevice, IBLBaker,
/// or any ECS entities. Both the device and baker must outlive this object.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.
///
/// @ref Lagarde 2012 "Local Image-based Lighting With Parallax-corrected Cubemaps"
/// @ref McGuire 2017 "Efficient GPU Screen-Space Ray Tracing" (probe blending)
/// @ref Akenine-Moller 2018 "Real-Time Rendering 4th Ed" Ch.10.5 — cubemap capture

#pragma once

#include "rhi_types.h"
#include "cb_ring_allocator.h"
#include "mat4.h"
#include "vector3.h"

#include <array>
#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class IBLBaker;
class World;
class DrawList;
struct GPUMesh;
struct PerFrameCB;

/// Number of cubemap faces.
static constexpr uint32_t kCubeFaceCount = 6;

/// Cubemap face view-projection matrices for rendering from a given position.
///
/// Contains 6 view-projection matrices (one per cubemap face) computed from
/// a world-space position. Face ordering follows the DX convention:
/// 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
///
/// Uses UzEngine's Z-up left-handed coordinate system internally, with the
/// same Z-up to Y-up swap matrix that the main camera applies.
///
/// @ref Akenine-Moller 2018 "Real-Time Rendering 4th Ed" Ch.10.5
struct CubemapFaceMatrices {
    Mat4 viewProj[kCubeFaceCount];  ///< Combined view * projection per face.
    Mat4 view[kCubeFaceCount];      ///< View matrix per face (for per-face rendering).
};

/// Builds 6 cubemap face view-projection matrices from a world position.
///
/// Each face uses a 90-degree vertical FOV, 1:1 aspect ratio perspective
/// projection. The view matrices apply the same Z-up to Y-up coordinate
/// swap as the main camera (Camera::UpdateView).
///
/// Face directions in UzEngine Z-up coordinates:
/// | Face | Forward | Up |
/// |------|---------|-----|
/// | 0 (+X DX) | +Y (right) | +Z (up) |
/// | 1 (-X DX) | -Y (left) | +Z (up) |
/// | 2 (+Y DX) | +Z (up) | -X (back) |
/// | 3 (-Y DX) | -Z (down) | +X (fwd) |
/// | 4 (+Z DX) | +X (fwd) | +Z (up) |
/// | 5 (-Z DX) | -X (back) | +Z (up) |
///
/// @ref Akenine-Moller 2018 "Real-Time Rendering 4th Ed" Ch.10.5
///
/// # Arguments
/// * `worldPos` — Probe center position in world space (cm, Z-up).
/// * `nearPlane` — Near clip distance in cm (default 10cm).
/// * `farPlane` — Far clip distance in cm (default 100000cm = 1km).
///
/// # Returns
/// 6 view-projection matrices ready for use as PerFrameCB::viewProj.
[[nodiscard]] CubemapFaceMatrices BuildCubemapFaceMatrices(
    const Vec3& worldPos, float nearPlane = 10.0f, float farPlane = 100000.0f);

// ─────────────────────────────────────────────────────────────────────────────
// Capture sun shadow (phase3a 3.37.0, item 1.10)
// ─────────────────────────────────────────────────────────────────────────────

/// Cascades of the sun shadow map a capture renders for itself: 0 covers the probe's region
/// (its influence sphere and parallax proxy box), 1 the surroundings out to
/// `kCaptureShadowFarScale` x that radius. Receivers beyond both are lit (no cascade covers
/// them), as the frame's CSM lights receivers past its last cascade.
static constexpr uint32_t kCaptureShadowCascades = 2;

/// Resolution of each capture shadow cascade in texels. The atlas is square
/// (`2 x 2` tiles, the lower two unused) because shadow.hlsli's tile math is: one row of tiles
/// would be read with a half-height tile. A probe cubemap face is 128-256 texels, so 1024
/// per cascade is finer than any reflection can show.
static constexpr uint32_t kCaptureShadowResolution = 1024;

/// Cascade 1's radius as a multiple of cascade 0's (clamped to kCaptureShadowFarMax).
static constexpr float kCaptureShadowFarScale = 8.0f;

/// Upper bound on cascade 1's radius (cm): the frame's default CSM range (400 m).
static constexpr float kCaptureShadowFarMax = 40000.0f;

/// One cascade of a capture's sun shadow map (BuildProbeShadowCascades).
///
/// The light-space projection is the frame CSM's sphere-bounded orthographic volume
/// (shadow_cascade.cpp): the eye pulled back 8 radii toward the sun so casters between the sun
/// and the sphere are rasterised, standard-Z depth over `[0, 9 r]`, and the frame's bias rules
/// (world-scaled depth bias, a 2 cm normal bias floored at 1.5 texels).
struct ProbeShadowCascade {
    Mat4  viewProj;            ///< Light-space view-projection (standard-Z orthographic).
    float radius     = 0.0f;   ///< Footprint radius around the probe (cm): the split distance.
    float texelSize  = 0.0f;   ///< World units (cm) per shadow texel.
    float depthRange = 0.0f;   ///< Light-space depth range (cm).
    float depthBias  = 0.0f;   ///< Normalised depth bias (ShadowDataCB::cascadeDepthBias).
    float normalBias = 0.0f;   ///< Normal-offset bias in cm (ShadowDataCB::cascadeNormalBias).
};

/// Builds the sun shadow cascades of a probe's capture.
///
/// # Arguments
/// * `probePos` — Probe world position (cm, Z-up): the centre of both cascades.
/// * `influenceRadius` — The probe's influence radius (cm).
/// * `boxExtents` — Parallax proxy box half-extents (cm); a component <= 0 means no box.
/// * `sunDir` — Direction toward the sun (PerFrameCB::lightDir), any length > 0.
/// * `resolution` — Cascade resolution in texels (kCaptureShadowResolution).
///
/// # Returns
/// Cascade 0 with radius `max(influenceRadius, |boxExtents|, 100 cm)`, cascade 1 with
/// `min(kCaptureShadowFarScale x that, kCaptureShadowFarMax)`.
[[nodiscard]] std::array<ProbeShadowCascade, kCaptureShadowCascades> BuildProbeShadowCascades(
    const Vec3& probePos, float influenceRadius, const Vec3& boxExtents, const Vec3& sunDir,
    uint32_t resolution);

/// GPU data returned by Update() for writing into PerFrameCB.
struct ProbeFrameResult {
    uint32_t probeCount = 0;                    ///< Number of active probes uploaded.
    uint32_t probeBufferSrvIndex = UINT32_MAX;  ///< SRV index of the probe StructuredBuffer.
};

/// Describes the scene rendering resources needed for cubemap capture.
///
/// The probe renderer does not own these — they are borrowed from the
/// SceneRenderer during the capture phase. The caller must ensure all
/// pointers and handles remain valid for the duration of the capture.
struct ProbeCaptureSceneDesc {
    IRHICommandList* cmdList        = nullptr;  ///< Command list in recording state.
    const DrawList*  drawList       = nullptr;  ///< Current frame's draw list (sorted, already built).
    const GPUMesh*   meshes         = nullptr;  ///< Mesh resource array.
    uint32_t         meshCount      = 0;        ///< Number of meshes in the array.
    CBRingAllocator* cbAllocator    = nullptr;  ///< CB ring for per-object constant buffers.
    /// Probe capture pipeline (PipelineLibrary::GetProbeCapture: mesh_vs + probe_capture_ps,
    /// non-instanced): the faces' geometry pass.
    RHIPipelineHandle capturePipeline;
    /// Shadow depth pipeline (PipelineLibrary::GetShadowDepth): renders the capture's sun shadow
    /// cascades. Invalid = the capture's sun is unshadowed (ShadowDataCB with no cascade).
    RHIPipelineHandle shadowPipeline;
    /// HDRI skybox pipeline (fullscreen_vs + skybox_cubemap_ps): draws the sky cubemap
    /// behind the captured geometry. Invalid = faces keep the clear colour where no
    /// geometry was drawn.
    RHIPipelineHandle skyboxPipeline;
    /// The frame's PerFrameCB (sun direction / colour, sky SH and SRVs, shading CVars). Each
    /// face copies it and overrides viewProj / invViewProj / cameraPos; the capture's shadow
    /// cascades are built along its lightDir. Null = a zero-initialised CB (unlit black
    /// geometry, no shadow cascades).
    const PerFrameCB* frameCB       = nullptr;
    bool             bindless       = false;    ///< Whether bindless descriptors are available.
};

/// Manages reflection probe cubemap capture, IBL baking, and GPU buffer upload.
///
/// Each probe gets its own irradiance + prefiltered cubemap pair via IBLBaker.
/// The probe data buffer (StructuredBuffer<ReflectionProbeGPU>) is uploaded
/// per-frame and indexed by the deferred lighting shader.
///
/// # Lifecycle
/// ```cpp
/// ReflectionProbeRenderer probeRenderer;
/// probeRenderer.Init(device, &iblBaker);
///
/// // Per frame (capture phase — before render graph):
/// ProbeCaptureSceneDesc captureDesc{...};
/// probeRenderer.CaptureIfDirty(world, captureDesc, skyCubemapSrvIndex, skyCubemapTexture);
///
/// // Per frame (update phase — after capture):
/// auto result = probeRenderer.Update(world, skyCubemapSrvIndex, skyCubemapTexture);
/// perFrameCB.reflectionProbeCount = result.probeCount;
/// perFrameCB.reflectionProbeBufferSrvIndex = result.probeBufferSrvIndex;
///
/// probeRenderer.Shutdown();
/// ```
class ReflectionProbeRenderer {
public:
    /// Maximum number of reflection probes supported simultaneously.
    /// Matches the structured buffer allocation. Higher probe counts would
    /// require a larger buffer and more per-frame bake budget.
    static constexpr uint32_t kMaxProbes = 8;

    ReflectionProbeRenderer() = default;
    ~ReflectionProbeRenderer();

    /// Non-copyable — owns GPU resources.
    ReflectionProbeRenderer(const ReflectionProbeRenderer&) = delete;
    ReflectionProbeRenderer& operator=(const ReflectionProbeRenderer&) = delete;

    /// Movable.
    ReflectionProbeRenderer(ReflectionProbeRenderer&&) noexcept;
    ReflectionProbeRenderer& operator=(ReflectionProbeRenderer&&) noexcept;

    /// Initializes the probe renderer with device and IBL baker references.
    ///
    /// Creates the GPU structured buffer for probe data upload. Does NOT
    /// capture any probes — that happens lazily in Update().
    ///
    /// # Arguments
    /// * `device` — Initialized RHI device. Must outlive this renderer.
    /// * `iblBaker` — Initialized IBL baker. Must outlive this renderer.
    ///
    /// # Returns
    /// `true` on success, `false` if resource creation fails (error logged).
    [[nodiscard]] bool Init(IRHIDevice* device, IBLBaker* iblBaker);

    /// Releases all GPU resources owned by this renderer.
    ///
    /// Safe to call multiple times or without prior Init().
    void Shutdown();

    /// Gathers all ReflectionProbeComponent entities, captures dirty probes
    /// (one per frame), bakes IBL, and uploads the probe data structured buffer.
    ///
    /// At most one dirty probe is baked per frame to amortize GPU cost.
    /// Probes are gathered, sorted by priority (descending), and the top
    /// kMaxProbes are tracked. A dirty probe is baked via IBLBaker from the
    /// scene capture CaptureIfDirty() recorded for it the frame before (see
    /// CapturePhase); a probe with no executed capture yet is skipped and
    /// contributes nothing. The sky cubemap is never baked into a probe: its
    /// face convention (R.x, R.z, R.y) differs from the capture's (R.y, R.z,
    /// R.x), which is what the lighting shader samples probes with.
    ///
    /// # Arguments
    /// * `world` — ECS world containing ReflectionProbeComponent entities.
    /// * `skyCubemapSrvIndex` — SRV index of the sky cubemap; UINT32_MAX (not
    ///   captured yet) skips the update, as no probe can have been captured.
    /// * `skyCubemapTexture` — Texture handle of the sky cubemap (unused here;
    ///   kept alongside the SRV index).
    ///
    /// # Returns
    /// `ProbeFrameResult` with the count and SRV index for PerFrameCB.
    [[nodiscard]] ProbeFrameResult Update(
        World& world,
        uint32_t skyCubemapSrvIndex,
        RHITextureHandle skyCubemapTexture);

    /// Captures the scene into a dirty probe's cubemap and bakes IBL.
    ///
    /// Finds the highest-priority dirty probe, renders the scene from its
    /// world position into 6 cubemap faces using a forward PBR pass, then
    /// feeds the captured cubemap to IBLBaker. At most one probe is captured
    /// per frame to amortize GPU cost.
    ///
    /// This must be called BEFORE Update() in the frame, after the frame's
    /// draw list has been built, while the command list is in recording state
    /// and before the render graph executes, and only once there is a scene to
    /// capture: the sky cubemap rendered by an earlier frame's command list (the
    /// faces draw it behind the geometry; the frame code waits for the sky IBL
    /// bake, SkyIBLReady) and the draw list populated. A capture taken in frame
    /// 0 read an unrendered sky cubemap and an empty draw list. Nothing is
    /// captured while `skyCubemapSrvIndex == UINT32_MAX`.
    /// If no scene desc is provided (null cmdList), falls back to sky-only
    /// IBL baking (the existing behavior).
    ///
    /// # Arguments
    /// * `world` — ECS world containing probe entities.
    /// * `sceneDesc` — Scene rendering resources. May have null cmdList to skip capture.
    /// * `skyCubemapSrvIndex` — Sky cubemap SRV drawn behind the geometry (and the
    ///   fallback IBL source if capture fails).
    /// * `skyCubemapTexture` — Sky cubemap texture handle (SM5 binding).
    void CaptureIfDirty(World& world, const ProbeCaptureSceneDesc& sceneDesc,
                        uint32_t skyCubemapSrvIndex, RHITextureHandle skyCubemapTexture);

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    /// Where a slot's scene capture is relative to the GPU.
    ///
    /// The capture is recorded on the frame's command list, which executes at
    /// the end of the frame, while IBLBaker::BakeEnvironment runs on its own
    /// command list and waits for it synchronously, so a bake must not read a
    /// capture whose list has not been submitted. CaptureIfDirty() sets
    /// Recorded, the next Update() advances it to Executed and the Update()
    /// after that bakes it (the bake's list queues behind the capture's on the
    /// same graphics queue), then back to Idle: at least one frame boundary
    /// separates the recording from the bake whichever side of Update() the
    /// capture is recorded on (SceneRenderer records it after Update(), once
    /// the draw list is built).
    enum class CapturePhase : uint8_t { Idle, Recorded, Executed };

    /// Per-probe tracking slot — maps an ECS entity to its baked IBL resources.
    struct ProbeSlot {
        bool     active                = false;       ///< Whether this slot is in use.
        bool     captured              = false;       ///< Whether IBL has been baked for this probe.
        CapturePhase capturePhase      = CapturePhase::Idle; ///< Scene capture in flight (see CapturePhase).
        bool     captureIsSrv          = false;       ///< Capture cubemap is in ShaderResource state (else its initial Common).
        uint32_t irradianceSrvIndex    = UINT32_MAX;  ///< Baked irradiance cubemap SRV.
        uint32_t prefilteredSrvIndex   = UINT32_MAX;  ///< Baked prefiltered specular SRV.

        // ── Baked IBL textures (owned per probe — caller manages lifetime) ──
        RHITextureHandle irradianceTexture;           ///< Baked irradiance cubemap (owned).
        RHITextureHandle prefilteredTexture;          ///< Baked prefiltered specular cubemap (owned).

        // ── Capture resources (owned per probe) ─────────────────────────
        RHITextureHandle captureCubemap;              ///< RGBA16F Texture2DArray (6 slices).
        uint32_t         captureCubemapSrv = UINT32_MAX; ///< SRV of the captured cubemap.
        uint32_t         captureResolution = 0;       ///< Resolution this cubemap was created at.
    };

    /// Uploads the probe data to the GPU structured buffer.
    ///
    /// Maps the buffer, writes ReflectionProbeGPU entries for all active probes,
    /// then unmaps. Returns the number of probes written.
    ///
    /// # Arguments
    /// * `world` — ECS world for reading probe component data and positions.
    uint32_t UploadProbeData(World& world);

    /// Creates the RGBA16F capture cubemap for a probe slot.
    ///
    /// If the slot already has a cubemap at the correct resolution, this is a no-op.
    /// If the resolution changed, the old cubemap is destroyed first.
    ///
    /// # Arguments
    /// * `slotIndex` — Index into m_probes[].
    /// * `resolution` — Cubemap face resolution in texels (power of 2).
    ///
    /// # Returns
    /// `true` if the cubemap is ready, `false` on allocation failure.
    [[nodiscard]] bool EnsureCaptureResources(uint32_t slotIndex, uint32_t resolution);

    /// Destroys capture cubemap resources for a slot.
    ///
    /// Safe to call on a slot that has no resources allocated.
    void DestroyCaptureResources(uint32_t slotIndex);

    /// Creates (or re-creates at another resolution) the capture's sun shadow atlas.
    ///
    /// # Returns
    /// `true` if the atlas and its SRV are ready, `false` on allocation failure (logged).
    [[nodiscard]] bool EnsureCaptureShadowResources();

    /// Renders the capture's sun shadow cascades into the shadow atlas and uploads the
    /// ShadowDataCB that probe_capture_ps.hlsl samples them through.
    ///
    /// The cascades (BuildProbeShadowCascades) are centred on the probe; the draw list is
    /// rendered depth-only into each cascade's atlas tile with the shadow depth pipeline
    /// (translucent, additive and mesh-decal materials are not occluders, as in the frame's
    /// shadow pass). With no sun (lightDir of zero length) or no shadow pipeline the uploaded
    /// CB has no cascade and the capture's sun is unshadowed.
    ///
    /// # Arguments
    /// * `probePos` — Probe center (cm, Z-up).
    /// * `influenceRadius` / `boxExtents` — The probe's region (cascade 0's radius).
    /// * `sceneDesc` — Scene rendering resources (command list, draw list, pipelines, frame CB).
    ///
    /// # Returns
    /// The ShadowDataCB allocation to bind at kSlotShadow for the faces (null cpuPtr = the CB
    /// ring is exhausted; the caller aborts the capture).
    [[nodiscard]] CBAllocation RenderCaptureShadowAtlas(const Vec3& probePos, float influenceRadius,
                                                        const Vec3& boxExtents,
                                                        const ProbeCaptureSceneDesc& sceneDesc);

    /// Renders the scene into the 6 faces of a probe's capture cubemap.
    ///
    /// First the capture's sun shadow atlas (RenderCaptureShadowAtlas). Then per face: a
    /// geometry pass (clear colour + depth, the frame's PerFrameCB with the face's
    /// view-projection, the capture's ShadowDataCB, the scene's draw list with the probe
    /// capture pipeline), then a sky pass (no depth attachment, the skybox pipeline reading
    /// the face's depth as an SRV and the sky cubemap, filling the pixels no geometry covered).
    ///
    /// # Arguments
    /// * `slotIndex` — Probe slot whose cubemap to render into.
    /// * `worldPos` — Probe center in world space (cm, Z-up).
    /// * `influenceRadius` / `boxExtents` — The probe's region, for its shadow cascades.
    /// * `sceneDesc` — Scene rendering resources (command list, draw list, etc.).
    /// * `skyCubemapSrvIndex` — Sky cubemap SRV for the sky pass.
    /// * `skyCubemapTexture` — Sky cubemap texture (SM5 binding of the sky pass).
    void RenderProbeCubemap(uint32_t slotIndex, const Vec3& worldPos, float influenceRadius,
                            const Vec3& boxExtents, const ProbeCaptureSceneDesc& sceneDesc,
                            uint32_t skyCubemapSrvIndex, RHITextureHandle skyCubemapTexture);

    IRHIDevice* m_device   = nullptr;
    IBLBaker*   m_iblBaker = nullptr;
    bool        m_initialized = false;

    // ── Per-probe tracking ────────────────────────────────────────────────
    ProbeSlot m_probes[kMaxProbes] = {};

    // ── Shared capture depth buffer (reused across faces and probes) ─────
    RHITextureHandle m_captureDepth;              ///< D32F depth for cubemap capture.
    uint32_t         m_captureDepthSrv = UINT32_MAX; ///< SRV of the depth (read by the sky pass).
    uint32_t         m_captureDepthResolution = 0; ///< Resolution the depth was created at.

    // ── Shared capture sun shadow atlas (one capture per frame) ──────────
    RHITextureHandle m_captureShadowAtlas;        ///< D32F, 2 x 2 tiles of kCaptureShadowResolution.
    uint32_t         m_captureShadowAtlasSrv = UINT32_MAX; ///< SRV sampled by probe_capture_ps.hlsl.
    bool             m_captureShadowIsSrv = false; ///< Atlas is in ShaderResource state (else its initial Common).

    // ── GPU structured buffer for probe data ──────────────────────────────
    RHIBufferHandle m_probeBuffer;
    uint32_t        m_probeBufferSrvIndex = UINT32_MAX;
};

} // namespace uz
