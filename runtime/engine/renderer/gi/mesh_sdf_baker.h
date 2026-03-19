/// UzEngine GI Mesh SDF Baker — CPU-side distance field generation for GI pipeline.
///
/// Generates a dense signed distance field from mesh triangles for use in the
/// software ray-traced GI system (Lumen-style sphere marching). Unlike the sparse
/// brick-based SDF in `sdf_types.h` (used for shadow cone tracing), this produces
/// a flat dense R16F-compatible grid optimized for direct 3D texture upload.
///
/// The baker wraps the existing `BakeMeshSdf()` from the asset importer pipeline
/// and converts its sparse brick output to a dense float grid suitable for the
/// GI trace shaders.
///
/// # Algorithm
/// 1. Compute mesh AABB, expand by 10% for narrow-band padding.
/// 2. For each voxel in the grid, compute world position at voxel center.
/// 3. Find distance to nearest triangle via spatial hash acceleration.
/// 4. Determine sign via pseudo-normal method (dot with face normal at closest point).
/// 5. Store signed distance (negative inside, positive outside) in world units (cm).
///
/// # UE5 Reference
/// @source UE5 DistanceFieldAtlas.h:36-50 — brick constants
/// @source UE5 DistanceFieldAtlas.cpp:67-79 — per-mesh resolution
/// @source UE5 MeshUtilities.cpp — mesh SDF generation
///
/// # Thread Safety
/// Thread-safe — no shared mutable state. Can be called from worker threads.

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;

// ─────────────────────────────────────────────────────────────────────────────
// MeshSDFData — CPU-side dense distance field result
// ─────────────────────────────────────────────────────────────────────────────

/// Dense signed distance field for a single mesh, ready for GPU upload.
///
/// The distance data is stored as a flat array of `float` values in Z-major
/// order: `distanceData[z * resolution * resolution + y * resolution + x]`.
/// Negative values are inside the mesh, positive values are outside.
/// Distances are in world units (centimeters).
///
/// # Memory Layout
/// The data maps directly to a 3D texture of resolution^3 texels.
/// Each texel is R16F on GPU (float16), converted from float32 during upload.
struct MeshSDFData {
    /// World-space center of the SDF volume.
    float volumeOrigin[3] = {};

    /// World-space half-extents of the SDF volume.
    float volumeExtent[3] = {};

    /// Voxels per axis (uniform cubic grid). Typically 32 or 64.
    uint32_t resolution = 0;

    /// Flat array of signed distances in world units (cm).
    /// Size: resolution^3. Negative = inside, positive = outside.
    /// Layout: distanceData[z * resolution * resolution + y * resolution + x].
    std::vector<float> distanceData;

    /// Returns true if the data contains valid SDF information.
    [[nodiscard]] bool IsValid() const noexcept {
        return resolution > 0
            && distanceData.size() == static_cast<size_t>(resolution) * resolution * resolution;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// MeshSDF — GPU-side 3D texture handle
// ─────────────────────────────────────────────────────────────────────────────

/// GPU representation of a mesh signed distance field.
///
/// Holds an R16F 3D texture and its bindless SRV index for shader access.
/// The volume origin/extent are needed by shaders to transform world
/// positions into volume UVs.
struct MeshSDF {
    RHITextureHandle texture3D;                ///< R16F 3D texture of distance values.
    uint32_t         srvIndex = UINT32_MAX;    ///< Bindless SRV index in global descriptor heap.
    float            volumeOrigin[3] = {};     ///< World-space center of the SDF volume.
    float            volumeExtent[3] = {};     ///< World-space half-extents of the SDF volume.
    uint32_t         resolution = 0;           ///< Voxels per axis.
    bool             valid = false;            ///< True if texture is created and uploaded.
};

// ─────────────────────────────────────────────────────────────────────────────
// MeshSdfBaker — CPU baking + GPU upload
// ─────────────────────────────────────────────────────────────────────────────

/// Bakes mesh triangles into a dense signed distance field and uploads to GPU.
///
/// The baker uses a spatial hash to accelerate nearest-triangle queries.
/// For a 32^3 grid, typical bake time is ~10-50ms depending on triangle count.
///
/// # Usage
/// ```cpp
/// auto data = MeshSdfBaker::BakeMesh(positions, vertexCount, stride, indices, indexCount, 32);
/// if (data.IsValid()) {
///     MeshSDF gpu = MeshSdfBaker::UploadToGPU(device, cmdList, data);
/// }
/// ```
class MeshSdfBaker {
public:
    /// Bakes a mesh into a dense signed distance field.
    ///
    /// # Arguments
    /// * `vertices` — Pointer to vertex data. Position is assumed to be the first
    ///   3 floats at each vertex (float x, float y, float z).
    /// * `vertexCount` — Number of vertices.
    /// * `vertexStride` — Byte stride between consecutive vertices.
    /// * `indices` — Triangle index buffer (3 indices per triangle).
    ///   If null, generates consecutive indices (0, 1, 2, 3, ...).
    /// * `indexCount` — Number of indices. Must be a multiple of 3.
    /// * `resolution` — Voxels per axis (uniform cubic grid). Typical: 32 or 64.
    ///
    /// # Returns
    /// Dense SDF data ready for GPU upload. Check `IsValid()` before use.
    /// Returns invalid data on failure (zero vertices, degenerate mesh, etc.).
    [[nodiscard]] static MeshSDFData BakeMesh(
        const float* vertices,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const uint32_t* indices,
        uint32_t indexCount,
        uint32_t resolution = 32);

    /// Uploads baked SDF data to GPU as an R16F 3D texture.
    ///
    /// Creates the 3D texture, allocates a staging buffer, converts float32
    /// distances to float16, and issues a copy command. The caller must
    /// execute and synchronize the command list before the MeshSDF is usable.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation.
    /// * `cmdList` — Command list for staging buffer copy. Must be in recording state.
    /// * `data` — Baked SDF data from `BakeMesh()`. Must be valid.
    ///
    /// # Returns
    /// GPU handle with texture, SRV index, and volume transform.
    /// `valid == false` on allocation failure.
    [[nodiscard]] static MeshSDF UploadToGPU(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        const MeshSDFData& data);

private:
    /// Compute distance from a point to the nearest triangle surface.
    ///
    /// Uses the Ericson closest-point-on-triangle algorithm (projection +
    /// edge/vertex clamping). Returns unsigned distance.
    ///
    /// @source Standard algorithm — Real-Time Collision Detection, Ch 5.1.5
    static float PointToTriangleDistance(
        const float p[3],
        const float a[3],
        const float b[3],
        const float c[3]);

    /// Determines whether a point is inside a closed mesh via pseudo-normal sign.
    ///
    /// Computes the dot product of the displacement vector (from closest point
    /// to the query point) with the face normal at the closest triangle. A
    /// negative dot product means the query point is on the interior side.
    ///
    /// # Returns
    /// Signed distance: negative inside, positive outside.
    static float ComputeSignedDistance(
        const float point[3],
        const float closestPoint[3],
        const float faceNormal[3],
        float unsignedDistance);
};

} // namespace uz
