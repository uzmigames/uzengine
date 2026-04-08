/// UzEngine IrradianceProbeGrid — Author-placed L2 SH probe volume.
///
/// Maintains a 3D grid of L2 spherical harmonics probes within an authored
/// world-space AABB (driven by `LightProbeVolumeComponent`). Each probe
/// stores 9 SH coefficients per RGB channel, packed into 7 float4 values
/// matching the project-wide SH packing used by `SkyIrradianceEnvironmentMap`.
///
/// Unlike `RadianceCache` which is a camera-centered clipmap, this grid is
/// fixed in world space. It is intended for interior spaces where the artist
/// places a volume around a room/level region and the renderer samples
/// probes via trilinear interpolation during deferred lighting.
///
/// # References
/// @ref Tatarchuk 2005 "Irradiance Volumes for Games" — authored probe volumes
/// @ref Greger 1998 "The Irradiance Volume" — SH-based volumetric irradiance
/// @ref Sloan 2008 "Stupid Spherical Harmonics Tricks" — L2 projection & packing
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
struct LightProbeVolumeComponent;
struct SHCoefficients;

// ─────────────────────────────────────────────────────────────────────────────
// IrradianceProbeGrid
// ─────────────────────────────────────────────────────────────────────────────

/// Author-placed 3D grid of L2 SH irradiance probes.
///
/// # Ownership
/// Owns the SH structured buffer. Does NOT own `IRHIDevice`.
///
/// # Lifecycle
/// ```cpp
/// IrradianceProbeGrid grid;
/// grid.CreateResources(device, volume);
/// // When volume extents/dims change:
/// grid.Rebuild(volume);
/// // Populate from sky SH (uniform fill):
/// grid.FillFromSkySH(skyCoeffs);
/// grid.UploadToGpu(cmdList);
/// // Bind srv:
/// uint32_t srv = grid.GetProbeBufferSrv();
/// grid.DestroyResources();
/// ```
class IrradianceProbeGrid {
public:
    /// Number of L2 SH coefficients per channel. 3 bands: 1 + 3 + 5 = 9.
    static constexpr uint32_t kSHCoeffs = 9;

    /// Packed float4s per probe (matches `SHCoefficients` packing).
    static constexpr uint32_t kSHFloat4PerProbe = 7;

    /// Bytes per probe in the structured buffer (7 * 16 = 112).
    static constexpr uint32_t kBytesPerProbe = kSHFloat4PerProbe * 16;

    /// Maximum total probes supported by a single grid (safety cap).
    /// 32x16x32 = 16384 probes = ~1.8 MB. Prevents runaway allocations.
    static constexpr uint32_t kMaxProbes = 16384;

    IrradianceProbeGrid() = default;
    ~IrradianceProbeGrid();

    IrradianceProbeGrid(const IrradianceProbeGrid&)            = delete;
    IrradianceProbeGrid& operator=(const IrradianceProbeGrid&) = delete;
    IrradianceProbeGrid(IrradianceProbeGrid&&) noexcept        = default;
    IrradianceProbeGrid& operator=(IrradianceProbeGrid&&) noexcept = default;

    /// Allocates the SH structured buffer sized for the given volume.
    ///
    /// # Arguments
    /// * `device` — RHI device. Must outlive this object.
    /// * `volume` — Volume component providing grid dimensions & extents.
    ///
    /// # Returns
    /// `true` on success. Fails if probe count exceeds `kMaxProbes` or the
    /// GPU allocation fails.
    [[nodiscard]] bool CreateResources(IRHIDevice* device,
                                       const LightProbeVolumeComponent& volume);

    /// Releases the SH buffer and bindless slots.
    void DestroyResources();

    /// Reallocates the SH buffer if `volume`'s probe count changed, and
    /// updates cached extents. Call when the component is marked dirty.
    [[nodiscard]] bool Rebuild(const LightProbeVolumeComponent& volume);

    /// Fills every probe with a uniform SH value (e.g. the captured sky).
    /// CPU-side only — `UploadToGpu()` pushes the data to the structured buffer.
    void FillFromSkySH(const SHCoefficients& skySH);

    /// Captures the sky cubemap into L2 SH and fills every probe with the
    /// resulting coefficients.
    ///
    /// Because the sky is treated as a directional environment at infinity,
    /// the projected SH is positionally invariant — every probe in the grid
    /// receives the same coefficients. Spatial variation (parallax, occlusion,
    /// indirect bounce) is introduced later by the SDF/DXR probe update passes
    /// (tasks 3.x and 4.x).
    ///
    /// The GPU must be idle before calling this; the function performs a
    /// readback of the cubemap via `ProjectCubemapToSH(device, cmdList, ...)`.
    /// Call `UploadToGpu()` afterwards to push the new SH to the structured
    /// buffer.
    ///
    /// # Arguments
    /// * `cmdList` — Command list used for the readback copy. Must be idle
    ///   (the underlying projection helper records its own Begin/End).
    /// * `skyCubemap` — RGBA16F sky cubemap (6 faces).
    /// * `faceSize` — Width/height of one cubemap face.
    ///
    /// # Returns
    /// `true` on success, `false` if the grid has no CPU storage or the
    /// projection failed.
    ///
    /// @ref Sloan 2008 "Stupid Spherical Harmonics Tricks" — L2 projection
    /// @ref Greger 1998 "The Irradiance Volume" — sky-only initial fill
    [[nodiscard]] bool CaptureFromSkyCubemap(IRHICommandList* cmdList,
                                             RHITextureHandle skyCubemap,
                                             uint32_t         faceSize);

    /// Zeroes every probe SH coefficient (CPU side). Used as a safe init
    /// before tracing runs or when no sky is available.
    void ClearCpuProbes();

    /// Copies the CPU-side probe data into the GPU structured buffer.
    /// The buffer is mapped and written directly (upload-heap backed).
    void UploadToGpu();

    // ── Accessors ────────────────────────────────────────────────────────

    /// SRV index of the probe SH buffer for bindless sampling.
    [[nodiscard]] uint32_t GetProbeBufferSrv() const noexcept { return m_srvIndex; }

    /// UAV index of the probe SH buffer for compute writes (task 3.x).
    [[nodiscard]] uint32_t GetProbeBufferUav() const noexcept { return m_uavIndex; }

    /// Returns the number of probes per axis.
    [[nodiscard]] uint32_t GetDimX() const noexcept { return m_dimX; }
    [[nodiscard]] uint32_t GetDimY() const noexcept { return m_dimY; }
    [[nodiscard]] uint32_t GetDimZ() const noexcept { return m_dimZ; }

    /// Returns the total probe count (`dimX * dimY * dimZ`).
    [[nodiscard]] uint32_t GetProbeCount() const noexcept {
        return m_dimX * m_dimY * m_dimZ;
    }

    /// Returns the minimum corner of the volume AABB (cm).
    [[nodiscard]] Vec3 GetExtentMin() const noexcept { return m_extentMin; }

    /// Returns the maximum corner of the volume AABB (cm).
    [[nodiscard]] Vec3 GetExtentMax() const noexcept { return m_extentMax; }

    /// Returns per-axis probe spacing in centimeters.
    [[nodiscard]] Vec3 GetProbeSpacing() const noexcept;

    /// Returns true if the grid owns valid GPU resources.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

private:
    /// Allocates the GPU structured buffer and bindless descriptors for
    /// `probeCount` probes. Releases any prior buffer first.
    [[nodiscard]] bool AllocateBuffer(uint32_t probeCount);

    IRHIDevice*     m_device = nullptr;
    RHIBufferHandle m_shBuffer;
    uint32_t        m_srvIndex = UINT32_MAX;
    uint32_t        m_uavIndex = UINT32_MAX;

    // Grid layout (cached copy of the component at creation time).
    uint32_t m_dimX = 0;
    uint32_t m_dimY = 0;
    uint32_t m_dimZ = 0;
    Vec3     m_extentMin{};
    Vec3     m_extentMax{};

    // CPU-side SH storage — `probeCount * kSHFloat4PerProbe` float4s.
    // Stored as flat float array: 28 floats per probe (7 float4s * 4).
    // Flat layout allows direct memcpy into the upload-heap mapping.
    float*   m_cpuProbes   = nullptr;
    uint32_t m_cpuProbeCount = 0;

    bool m_initialized = false;
};

} // namespace uz
