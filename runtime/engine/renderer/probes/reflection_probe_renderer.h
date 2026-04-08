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
/// is dirty, the scene is rendered from the probe's world position into the
/// 6 cubemap faces using a forward PBR pass (one face per draw, 90-degree
/// FOV, 1:1 aspect). The captured cubemap is then fed to IBLBaker for
/// irradiance + prefiltered specular generation.
///
/// One probe is captured per frame to amortize GPU cost. Faces are rendered
/// sequentially within a single capture (6 render passes per probe).
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
#include "mat4.h"
#include "vector3.h"

#include <array>
#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class IBLBaker;
class CBRingAllocator;
class World;
class DrawList;
struct GPUMesh;
struct CBAllocation;

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
    const DrawList*  drawList       = nullptr;  ///< Current frame's draw list (sorted).
    const GPUMesh*   meshes         = nullptr;  ///< Mesh resource array.
    uint32_t         meshCount      = 0;        ///< Number of meshes in the array.
    CBRingAllocator* cbAllocator    = nullptr;  ///< CB ring for per-object constant buffers.
    RHIPipelineHandle forwardPipeline;          ///< Forward PBR pipeline (non-instanced).
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
    /// At most one dirty probe is captured per frame to amortize GPU cost.
    /// Probes are gathered, sorted by priority (descending), and the top
    /// kMaxProbes are tracked. When a probe is dirty, its cubemap is baked
    /// via IBLBaker using the provided sky cubemap as source.
    ///
    /// # Arguments
    /// * `world` — ECS world containing ReflectionProbeComponent entities.
    /// * `skyCubemapSrvIndex` — SRV index of the sky cubemap (input to IBLBaker).
    /// * `skyCubemapTexture` — Texture handle of the sky cubemap (for SM5 binding).
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
    /// This must be called BEFORE Update() in the frame, while the command
    /// list is in recording state and before the render graph executes.
    /// If no scene desc is provided (null cmdList), falls back to sky-only
    /// IBL baking (the existing behavior).
    ///
    /// # Arguments
    /// * `world` — ECS world containing probe entities.
    /// * `sceneDesc` — Scene rendering resources. May have null cmdList to skip capture.
    /// * `skyCubemapSrvIndex` — Fallback sky cubemap SRV (used if capture fails).
    /// * `skyCubemapTexture` — Fallback sky cubemap texture handle.
    void CaptureIfDirty(World& world, const ProbeCaptureSceneDesc& sceneDesc,
                        uint32_t skyCubemapSrvIndex, RHITextureHandle skyCubemapTexture);

    /// Returns true if Init() has been called and Shutdown() has not.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    /// Per-probe tracking slot — maps an ECS entity to its baked IBL resources.
    struct ProbeSlot {
        bool     active                = false;       ///< Whether this slot is in use.
        bool     captured              = false;       ///< Whether IBL has been baked for this probe.
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

    /// Renders the scene into the 6 faces of a probe's capture cubemap.
    ///
    /// Issues 6 sequential render passes (one per face), each clearing the
    /// color and depth targets, setting the face's view-projection matrix,
    /// and drawing the scene's opaque draw list with the forward PBR pipeline.
    ///
    /// # Arguments
    /// * `slotIndex` — Probe slot whose cubemap to render into.
    /// * `worldPos` — Probe center in world space (cm, Z-up).
    /// * `sceneDesc` — Scene rendering resources (command list, draw list, etc.).
    void RenderProbeCubemap(uint32_t slotIndex, const Vec3& worldPos,
                            const ProbeCaptureSceneDesc& sceneDesc);

    IRHIDevice* m_device   = nullptr;
    IBLBaker*   m_iblBaker = nullptr;
    bool        m_initialized = false;

    // ── Per-probe tracking ────────────────────────────────────────────────
    ProbeSlot m_probes[kMaxProbes] = {};

    // ── Shared capture depth buffer (reused across faces and probes) ─────
    RHITextureHandle m_captureDepth;              ///< D32F depth for cubemap capture.
    uint32_t         m_captureDepthResolution = 0; ///< Resolution the depth was created at.

    // ── GPU structured buffer for probe data ──────────────────────────────
    RHIBufferHandle m_probeBuffer;
    uint32_t        m_probeBufferSrvIndex = UINT32_MAX;
};

} // namespace uz
