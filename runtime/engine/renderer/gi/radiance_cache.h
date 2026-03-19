/// UzEngine Radiance Cache — World-space probe grid for diffuse GI.
///
/// Maintains a uniform 3D grid of 16^3 = 4096 probes centered on the camera.
/// Each probe stores L2 spherical harmonics (9 coefficients, 3 channels = 27 floats,
/// packed into 7 float4s following UE5's SkyIrradianceEnvironmentMap layout).
///
/// The radiance cache is updated incrementally: each frame, a compute shader
/// traces 32 rays from each probe through the global distance field, accumulates
/// incoming radiance into SH, and blends with the previous frame's SH via
/// exponential moving average (alpha = 0.05).
///
/// # UE5 Reference
/// @source UE5 LumenRadianceCacheInterpolation.ush:1-95 — probe clipmap layout
/// @source UE5 LumenRadianceCacheTracingCommon.ush:1-72 — trace tile grouping
/// @source UE5 LumenRadianceCache.usf:132-200 — ComputeProbeWorldOffsetsCS
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.

#pragma once

#include "rhi_types.h"
#include "vector3.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class GlobalDistanceField;

// ─────────────────────────────────────────────────────────────────────────────
// RadianceCache
// ─────────────────────────────────────────────────────────────────────────────

/// World-space probe grid for diffuse global illumination via SH.
///
/// # Ownership
/// Owns the SH buffer GPU resource. Does NOT own IRHIDevice or GlobalDistanceField.
///
/// # Lifecycle
/// ```cpp
/// RadianceCache rc;
/// rc.CreateResources(device, 5000.0f);
/// // Per frame:
/// rc.UpdateProbes(cmdList, gdf, tracePipeline, ...);
/// uint32_t shSrv = rc.GetProbeSHBufferSrv(); // bind in lighting shader
/// // Shutdown:
/// rc.DestroyResources();
/// ```
class RadianceCache {
public:
    /// Number of probes per axis. 16^3 = 4096 total probes.
    /// @source UE5 LumenRadianceCacheInterpolation.ush:21 — RadianceProbeClipmapResolution
    static constexpr uint32_t kProbesPerAxis = 16;

    /// Total number of probes in the grid.
    static constexpr uint32_t kTotalProbes = kProbesPerAxis * kProbesPerAxis * kProbesPerAxis;

    /// Number of L2 SH coefficients (per channel). 3 bands: 1 + 3 + 5 = 9.
    static constexpr uint32_t kSHCoeffs = 9;

    /// Number of float4 values per probe for packed SH storage.
    /// UE5 uses 7 float4s to pack 3-channel L2 SH coefficients:
    ///   [0..2]: R/G/B L0 + L1 terms (3 float4)
    ///   [3..5]: R/G/B L2 terms (3 float4)
    ///   [6]:    remaining L2 terms (1 float4)
    /// @source UE5 SkyIrradianceEnvironmentMap — 7 float4 packing
    static constexpr uint32_t kSHFloat4PerProbe = 7;

    /// Number of rays traced per probe per frame.
    /// @source UE5 LumenRadianceCacheTracingCommon.ush:4 — RADIANCE_CACHE_TRACE_TILE_SIZE_2D=8
    /// 32 rays is a balanced budget for 4096 probes (~131K rays/frame).
    static constexpr uint32_t kRaysPerProbe = 32;

    /// Temporal blend factor (EMA alpha). Lower = more temporal stability.
    /// @source UE5 LumenRadianceCache.usf:156 — ProbeHistoryWeight
    static constexpr float kTemporalBlendAlpha = 0.05f;

    RadianceCache() = default;
    ~RadianceCache();

    RadianceCache(const RadianceCache&) = delete;
    RadianceCache& operator=(const RadianceCache&) = delete;
    RadianceCache(RadianceCache&&) noexcept = default;
    RadianceCache& operator=(RadianceCache&&) noexcept = default;

    /// Creates the SH buffer and initializes the probe grid.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation. Must outlive this object.
    /// * `worldExtent` — Half-size of the probe grid in world units (cm).
    ///   Default: 5000 cm (50 m). Probes cover a cube of this half-extent
    ///   centered on the camera.
    ///
    /// # Returns
    /// `true` if all resources were created successfully.
    [[nodiscard]] bool CreateResources(IRHIDevice* device, float worldExtent = 5000.0f);

    /// Destroys the SH buffer.
    void DestroyResources();

    /// Updates the probe grid center to track the camera. Marks the cache
    /// dirty when the grid shifts by one or more probe spacings.
    ///
    /// # Arguments
    /// * `cameraPos` — Current camera world-space position.
    void Update(const Vec3& cameraPos);

    /// Dispatches the radiance cache trace compute shader.
    ///
    /// Each thread traces rays from one probe through the global distance field,
    /// accumulates incoming radiance into L2 SH coefficients, and blends the
    /// new SH with the previous frame's history.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `gdf` — Global distance field for ray tracing.
    /// * `tracePipeline` — The radiance cache trace compute pipeline.
    /// * `cbRing` — CB ring allocator for uploading per-dispatch constants.
    /// * `perFrameBuffer` — PerFrameCB buffer handle.
    /// * `perFrameOffset` — Byte offset into the per-frame buffer.
    /// * `perFrameCpuPtr` — CPU pointer to the per-frame data.
    /// * `perFrameSize` — Size of the per-frame data in bytes.
    /// * `frameIndex` — Monotonic frame counter (for temporal jitter).
    void UpdateProbes(
        IRHICommandList* cmdList,
        const GlobalDistanceField& gdf,
        RHIPipelineHandle tracePipeline,
        CBRingAllocator* cbRing,
        RHIBufferHandle perFrameBuffer,
        uint32_t perFrameOffset,
        const void* perFrameCpuPtr,
        uint32_t perFrameSize,
        uint32_t frameIndex);

    /// Returns the SRV index for the probe SH buffer.
    /// Used by the deferred lighting shader to sample radiance cache probes.
    [[nodiscard]] uint32_t GetProbeSHBufferSrv() const noexcept { return m_shBufferSrv; }

    /// Returns the UAV index for the probe SH buffer.
    [[nodiscard]] uint32_t GetProbeSHBufferUav() const noexcept { return m_shBufferUav; }

    /// Returns the world-space origin (corner) of the probe grid.
    [[nodiscard]] Vec3 GetGridOrigin() const noexcept { return m_gridOrigin; }

    /// Returns the half-extent of the probe grid.
    [[nodiscard]] float GetGridExtent() const noexcept { return m_gridExtent; }

    /// Returns the spacing between adjacent probes in world units (cm).
    [[nodiscard]] float GetProbeSpacing() const noexcept { return m_probeSpacing; }

    /// Returns the grid center (camera-tracked position).
    [[nodiscard]] Vec3 GetGridCenter() const noexcept { return m_gridCenter; }

    /// Returns true if resources have been created and are valid.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    IRHIDevice* m_device = nullptr;

    /// StructuredBuffer<float4> — kTotalProbes * kSHFloat4PerProbe float4 elements.
    /// Total: 4096 * 7 * 16 = 458,752 bytes (~449 KB).
    RHIBufferHandle m_shBuffer;
    uint32_t        m_shBufferSrv = UINT32_MAX;
    uint32_t        m_shBufferUav = UINT32_MAX;

    /// World-space grid origin (minimum corner = center - extent).
    Vec3   m_gridOrigin;

    /// World-space grid center (tracks camera).
    Vec3   m_gridCenter;

    /// Half-size of the probe grid in world units (cm).
    float  m_gridExtent   = 5000.0f;

    /// Distance between adjacent probes in world units (cm).
    float  m_probeSpacing = 0.0f;

    /// Monotonic counter for temporal variation.
    uint32_t m_frameCounter = 0;

    bool   m_initialized = false;
    bool   m_dirty       = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Radiance Cache Shader Constants — CPU mirror of compute shader cbuffer
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants for the radiance cache trace compute shader.
struct RadianceCacheTraceConstants {
    float    gridOriginX;        ///< Grid origin X (minimum corner).
    float    gridOriginY;        ///< Grid origin Y.
    float    gridOriginZ;        ///< Grid origin Z.
    float    probeSpacing;       ///< Distance between probes (cm).

    uint32_t probesPerAxis;      ///< Probes per axis (16).
    uint32_t totalProbes;        ///< Total probes (4096).
    uint32_t raysPerProbe;       ///< Rays per probe per frame (32).
    uint32_t frameIndex;         ///< Frame counter for temporal jitter.

    float    maxTraceDistance;    ///< Maximum ray trace distance (cm).
    float    temporalBlendAlpha; ///< EMA blend factor (0.05).
    uint32_t shBufferUav;        ///< UAV index for SH buffer read/write.
    uint32_t _pad;

    // GDF clipmap parameters for the trace shader
    float    gdfCenter0X, gdfCenter0Y, gdfCenter0Z, gdfExtent0;
    float    gdfCenter1X, gdfCenter1Y, gdfCenter1Z, gdfExtent1;
    float    gdfCenter2X, gdfCenter2Y, gdfCenter2Z, gdfExtent2;
    float    gdfCenter3X, gdfCenter3Y, gdfCenter3Z, gdfExtent3;

    uint32_t gdfClipmapSrv0;     ///< SRV indices for GDF clipmap textures.
    uint32_t gdfClipmapSrv1;
    uint32_t gdfClipmapSrv2;
    uint32_t gdfClipmapSrv3;

    uint32_t numGdfClipmaps;     ///< Number of active GDF clipmaps (1-4).
    float    skyColorR;          ///< Sky color for rays that miss all geometry.
    float    skyColorG;
    float    skyColorB;
};

static_assert(sizeof(RadianceCacheTraceConstants) == 144, "RadianceCacheTraceConstants must be 144 bytes");

} // namespace uz
