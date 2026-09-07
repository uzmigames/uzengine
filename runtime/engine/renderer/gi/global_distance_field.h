/// UzEngine Global Distance Field — Cascaded clipmap SDF for world-space ray tracing.
///
/// Manages 4 nested clipmap volumes centered on the camera. Each clipmap is a
/// dense RGBA16F 3D texture (64^3) storing minimum distance in R and the
/// nearest mesh's average diffuse reflectance in GBA.
///
/// The GDF is the backbone of the Lumen-style software GI pipeline:
/// radiance cache probes and screen probes trace rays against it for
/// indirect lighting and ambient occlusion.
///
/// # Clipmap Extents (world units, cm)
/// Each cascade covers 4x the extent of the previous one:
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
#include <cstddef>
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
/// Owns all GPU textures (4 expanded/raw clipmap pairs). Does NOT own IRHIDevice, PipelineLibrary,
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
    /// 4-level clipmap hierarchy covers near to distant field.
    static constexpr uint32_t kNumClipmaps = 4;

    /// 64^3 dense volume per clipmap (simplified from paged 128^3 systems).
    static constexpr uint32_t kResolution  = 64;

    /// Clipmap half-extents in cm. Each cascade is 4x the previous one.
    static constexpr float kClipmapExtents[kNumClipmaps] = {
        2500.0f,    // Cascade 0: 25 m radius
        10000.0f,   // Cascade 1: 100 m radius
        40000.0f,   // Cascade 2: 400 m radius
        160000.0f   // Cascade 3: 1600 m radius
    };

    /// Per-clipmap GPU data and spatial configuration.
    struct ClipmapData {
        RHITextureHandle texture;              ///< RGBA16F distance and diffuse reflectance.
        uint32_t         srvIndex = UINT32_MAX; ///< Bindless SRV index.
        uint32_t         uavIndex = UINT32_MAX; ///< Bindless UAV index.
        RHITextureHandle rawTexture;           ///< R16F distance without conservative surface expansion.
        uint32_t         rawSrvIndex = UINT32_MAX;
        uint32_t         rawUavIndex = UINT32_MAX;
        float            extent   = 0.0f;       ///< Half-size in world units (cm).
        Vec3             center;                ///< World-space clipmap center (tracks camera).
        bool             dirty    = true;       ///< True if the clipmap needs full recomposite.
        bool             hasData  = false;
    };

    GlobalDistanceField() = default;
    ~GlobalDistanceField();

    GlobalDistanceField(const GlobalDistanceField&) = delete;
    GlobalDistanceField& operator=(const GlobalDistanceField&) = delete;
    GlobalDistanceField(GlobalDistanceField&&) noexcept = default;
    GlobalDistanceField& operator=(GlobalDistanceField&&) noexcept = default;

    /// Creates four RGBA16F expanded/material and R16F raw-distance clipmap pairs.
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
    /// VoxelSize = (Extent * 2) / Resolution for the given cascade.
    [[nodiscard]] float GetVoxelSize(uint32_t cascadeIndex) const;
    [[nodiscard]] uint64_t GetSceneRevision() const noexcept { return m_sceneRevision; }

private:
    IRHIDevice*  m_device = nullptr;
    ClipmapData  m_clipmaps[kNumClipmaps] = {};
    bool         m_initialized = false;
    uint64_t     m_meshDataHash = 0;
    uint32_t     m_meshDataCount = UINT32_MAX;
    uint64_t     m_sceneRevision = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GDF Shader Constants — CPU mirror of the compute shader cbuffer
// ─────────────────────────────────────────────────────────────────────────────

/// Eight float4 registers per mesh, shared with the composite shader's b2 array.
inline constexpr uint32_t kGdfMeshesPerBatch = 512;
struct GdfMeshSdfGPU {
    float originX, originY, originZ;  ///< Mesh-local SDF volume center.
    float distanceScale;              ///< Conservative local-to-world distance scale.
    float extentX, extentY, extentZ;  ///< Mesh-local SDF volume half-extents.
    float surfaceBias;                ///< Half a local voxel for two-sided surface coverage; zero for solids.
    float invTransform[12];           ///< Three world-to-local float4 dot products, including translation.
    uint32_t srvIndex;                ///< Bindless SRV index for the R16F 3D texture.
    uint32_t resolution;              ///< Voxels per axis.
    uint32_t albedoSrv;
    uint32_t metallicSrv;
    float baseColorTint[3];
    float metallicMultiplier;
    uint32_t materialFlags;
    uint32_t _pad2[3];
};

static_assert(sizeof(GdfMeshSdfGPU) == 128, "GdfMeshSdfGPU must be 128 bytes");
static_assert(offsetof(GdfMeshSdfGPU, albedoSrv) == 88);
static_assert(offsetof(GdfMeshSdfGPU, baseColorTint) == 96);
static_assert(offsetof(GdfMeshSdfGPU, materialFlags) == 112);
static_assert(sizeof(GdfMeshSdfGPU) * kGdfMeshesPerBatch <= 65536);

/// Root constants for the GDF composite compute shader (9 DWORDs at b0).
///
/// Passed via SetRootConstants(0, ..., 9). Must exactly match the root
/// constants layout in global_sdf_composite_cs.hlsl and fit within the
/// 16-DWORD root parameter at slot 0.
struct GdfCompositeConstants {
    uint32_t outputUav;       ///< [0] UAV index for the target clipmap 3D texture.
    uint32_t meshCount;       ///< [1] Number of mesh SDFs to composite.
    uint32_t accumulate;      ///< [2] Read the prior batch's minimum when nonzero.
    uint32_t resolution;      ///< [3] Voxels per axis (64).
    float    clipmapCenterX;  ///< [4] World-space clipmap center X.
    float    clipmapCenterY;  ///< [5] World-space clipmap center Y.
    float    clipmapCenterZ;  ///< [6] World-space clipmap center Z.
    float    clipmapExtent;   ///< [7] Clipmap half-size in world units (cm).
    uint32_t rawOutputUav;    ///< [8] UAV index for unexpanded R16F distance.
};

static_assert(sizeof(GdfCompositeConstants) == 36, "GdfCompositeConstants must be 9 DWORDs");
static_assert(offsetof(GdfCompositeConstants, rawOutputUav) == 32);

} // namespace uz
