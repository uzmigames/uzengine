/// UzEngine Global Distance Field — Cascaded clipmap SDF for world-space ray tracing.
///
/// Manages 4 nested clipmap volumes centered on the camera. Each clipmap is a
/// dense R16F 3D texture (64^3) that stores the minimum signed distance from
/// all mesh SDFs composited into that cascade level.
///
/// The GDF is the backbone of the Lumen-style software GI pipeline:
/// radiance cache probes and screen probes trace rays against it for
/// indirect lighting and ambient occlusion.
///
/// # Clipmap Extents (world units, cm)
/// @source UE5 GlobalDistanceField.cpp:446-457
///   Cascade 0: 2500 cm  (25 m  — near field, highest detail)
///   Cascade 1: 10000 cm (100 m — mid field)
///   Cascade 2: 40000 cm (400 m — far field)
///   Cascade 3: 160000 cm (1600 m — distant field)
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.

#pragma once

#include "rhi_types.h"
#include "vector3.h"
#include "mat4.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;
class PipelineLibrary;
struct MeshSDF;

// ─────────────────────────────────────────────────────────────────────────────
// GlobalDistanceField
// ─────────────────────────────────────────────────────────────────────────────

/// Manages cascaded clipmap 3D textures for global signed distance field tracing.
///
/// # Ownership
/// Owns all GPU textures (4 clipmaps). Does NOT own IRHIDevice, PipelineLibrary,
/// or CBRingAllocator — all must outlive this object.
///
/// # Lifecycle
/// ```cpp
/// GlobalDistanceField gdf;
/// gdf.CreateResources(device);
/// // Per frame:
/// gdf.Update(cameraPos);
/// gdf.CompositeFromMeshSDFs(cmdList, meshSDFs, transforms, ...);
/// // Shutdown:
/// gdf.DestroyResources();
/// ```
class GlobalDistanceField {
public:
    /// @source UE5 GlobalDistanceField.cpp:23 — NumClipmaps = 4
    static constexpr uint32_t kNumClipmaps = 4;

    /// @source UE5 GlobalDistanceField.cpp:120 — r.AOGlobalDFResolution
    /// Simplified from UE5's 128^3 paged system to 64^3 dense volumes.
    static constexpr uint32_t kResolution  = 64;

    /// @source UE5 GlobalDistanceField.cpp:446-457 — Clipmap extent formula
    /// Extents are half-sizes in world units (cm).
    static constexpr float kClipmapExtents[kNumClipmaps] = {
        2500.0f,    // Cascade 0: 25 m radius
        10000.0f,   // Cascade 1: 100 m radius
        40000.0f,   // Cascade 2: 400 m radius
        160000.0f   // Cascade 3: 1600 m radius
    };

    /// Per-clipmap GPU data and spatial configuration.
    struct ClipmapData {
        RHITextureHandle texture;              ///< R16F 3D texture (kResolution^3).
        uint32_t         srvIndex = UINT32_MAX; ///< Bindless SRV index.
        uint32_t         uavIndex = UINT32_MAX; ///< Bindless UAV index.
        float            extent   = 0.0f;       ///< Half-size in world units (cm).
        Vec3             center;                ///< World-space clipmap center (tracks camera).
        bool             dirty    = true;       ///< True if the clipmap needs full recomposite.
    };

    GlobalDistanceField() = default;
    ~GlobalDistanceField();

    GlobalDistanceField(const GlobalDistanceField&) = delete;
    GlobalDistanceField& operator=(const GlobalDistanceField&) = delete;
    GlobalDistanceField(GlobalDistanceField&&) noexcept = default;
    GlobalDistanceField& operator=(GlobalDistanceField&&) noexcept = default;

    /// Creates all 4 clipmap R16F 3D textures and initializes metadata.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation. Must outlive this object.
    ///
    /// # Returns
    /// `true` if all textures were created successfully.
    [[nodiscard]] bool CreateResources(IRHIDevice* device);

    /// Destroys all GPU textures.
    void DestroyResources();

    /// Updates clipmap centers to track the camera position and marks dirty
    /// clipmaps that need recompositing.
    ///
    /// A clipmap is re-centered (and marked dirty) when the camera moves
    /// more than half a voxel's worth in any axis for that cascade.
    ///
    /// # Arguments
    /// * `cameraPos` — Current camera world-space position.
    void Update(const Vec3& cameraPos);

    /// Dispatches the GDF composite compute shader for each dirty clipmap.
    ///
    /// For each voxel in a dirty clipmap, the shader iterates all mesh SDFs,
    /// transforms the voxel world position into each mesh's local space,
    /// samples the mesh SDF, and stores the global minimum distance.
    ///
    /// # Arguments
    /// * `cmdList` — Command list in recording state.
    /// * `meshSDFs` — Array of GPU mesh SDF handles (from MeshSdfBaker).
    /// * `meshTransforms` — World transforms for each mesh (parallel to meshSDFs).
    /// * `compositePipeline` — The GDF composite compute pipeline.
    /// * `cbRing` — CB ring allocator for uploading mesh SDF data.
    /// * `perFrameBuffer` — PerFrameCB buffer handle (for root CBV binding).
    /// * `perFrameOffset` — Byte offset into the per-frame buffer.
    /// * `perFrameCpuPtr` — CPU pointer to the per-frame data.
    /// * `perFrameSize` — Size of the per-frame data in bytes.
    void CompositeFromMeshSDFs(
        IRHICommandList* cmdList,
        const MeshSDF* meshSDFs,
        const Mat4* meshTransforms,
        uint32_t meshSdfCount,
        RHIPipelineHandle compositePipeline,
        CBRingAllocator* cbRing,
        RHIBufferHandle perFrameBuffer,
        uint32_t perFrameOffset,
        const void* perFrameCpuPtr,
        uint32_t perFrameSize);

    /// Returns the clipmap data for the given cascade index.
    [[nodiscard]] const ClipmapData& GetClipmap(uint32_t index) const;

    /// Returns true if resources have been created and are valid.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }

    /// Returns the voxel size (world units per texel) for the given cascade.
    /// @source UE5 GlobalDistanceFieldUtils.ush:22 — VoxelExtent = Extent * TexelSize
    [[nodiscard]] float GetVoxelSize(uint32_t cascadeIndex) const;

private:
    IRHIDevice*  m_device = nullptr;
    ClipmapData  m_clipmaps[kNumClipmaps] = {};
    bool         m_initialized = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// GDF Shader Constants — CPU mirror of the compute shader cbuffer
// ─────────────────────────────────────────────────────────────────────────────

/// Per-mesh SDF data for the composite compute shader's StructuredBuffer.
///
/// Packed to 64 bytes (4 float4s) for GPU-friendly access.
struct GdfMeshSdfGPU {
    float originX, originY, originZ;  ///< World-space SDF volume center.
    float padding0;
    float extentX, extentY, extentZ;  ///< World-space SDF volume half-extents.
    float padding1;
    float invTransform[12];           ///< World-to-local 3x4 matrix (row-major top 3 rows of inverse transform).
    uint32_t srvIndex;                ///< Bindless SRV index for the R16F 3D texture.
    uint32_t resolution;              ///< Voxels per axis.
    uint32_t _pad2[2];
};

static_assert(sizeof(GdfMeshSdfGPU) == 96, "GdfMeshSdfGPU must be 96 bytes");

/// Root constants for the GDF composite compute shader.
/// Passed via SetRootConstants or a small CB from the ring allocator.
struct GdfCompositeConstants {
    uint32_t outputUav;       ///< UAV index for the target clipmap 3D texture.
    uint32_t meshSdfBufferSrv; ///< SRV index for the StructuredBuffer<GdfMeshSdfGPU>.
    uint32_t meshCount;       ///< Number of mesh SDFs to composite.
    uint32_t clipmapIndex;    ///< Index of the clipmap being composited (0-3).
    float    clipmapCenterX;  ///< World-space clipmap center X.
    float    clipmapCenterY;  ///< World-space clipmap center Y.
    float    clipmapCenterZ;  ///< World-space clipmap center Z.
    float    clipmapExtent;   ///< Clipmap half-size in world units (cm).
    uint32_t resolution;      ///< Voxels per axis (64).
    uint32_t _pad[3];         ///< Pad to 48 bytes (3 float4s).
};

static_assert(sizeof(GdfCompositeConstants) == 48, "GdfCompositeConstants must be 48 bytes");

} // namespace uz
