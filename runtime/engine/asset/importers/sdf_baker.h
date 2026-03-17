/// UzEngine SDF Baker — Mesh Signed Distance Field Generator
///
/// Generates a sparse signed distance field from mesh triangle data at import time.
/// The output `MeshSdfAsset` is stored alongside the mesh asset and uploaded to
/// the GPU atlas at runtime for distance field shadow cone tracing.
///
/// # Algorithm
/// For each voxel in the 3D grid:
/// 1. Find the closest triangle using a spatial hash for acceleration.
/// 2. Compute the unsigned distance from the voxel center to the triangle.
/// 3. Determine the sign using the pseudo-normal method (dot product of the
///    displacement vector with the triangle's normal at the closest point).
/// 4. Quantize to 8-bit: 128 = zero-crossing (surface).
///
/// # Sparse Encoding
/// Only voxel bricks that contain at least one voxel within the surface band
/// (|distance| < bandWidth) are stored. Empty bricks get UINT32_MAX in the
/// indirection table.
///
/// # UE5 Reference
/// - DistanceFieldAtlas.h:34-50 — brick layout, sparse storage
/// - MeshUtilities.cpp — mesh SDF generation (Embree-accelerated)
///
/// # Performance
/// CPU-only, O(V * T/B) where V = voxel count, T = triangles, B = spatial hash
/// bucket size. Typical: ~100ms for 64^3 on a medium-complexity mesh.
///
/// # Thread Safety
/// Thread-safe — no shared mutable state. Can be called from import worker threads.

#pragma once

#include "../../../engine/renderer/shadows/sdf_types.h"

#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SdfBakerConfig — controls SDF generation quality and performance
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for SDF baking.
///
/// Controls the voxel resolution, surface band width, and spatial hash grid
/// resolution. Higher `maxResolution` produces more accurate SDFs at the cost
/// of bake time and storage.
///
/// Default values match UE5's default mesh SDF settings for medium-complexity
/// static meshes.
struct SdfBakerConfig {
    /// Maximum grid resolution per axis (in voxels, not bricks).
    /// The actual resolution is adaptive: smaller meshes get fewer voxels.
    /// The grid is always a multiple of SdfBrick::kSize (8).
    /// @source UE5 DistanceFieldAtlas.h — default mesh SDF resolution ~64-128
    uint32_t maxResolution = 64;

    /// Target voxel density: voxels per cm of mesh extent along the longest axis.
    /// Used to compute the actual resolution from mesh bounds.
    /// 0 = use maxResolution directly (fixed resolution mode).
    float voxelsPerCm = 0.0f;

    /// Surface band width multiplier. Bricks with all voxels farther than
    /// `bandWidthMultiplier * voxelSize * SdfBrick::kSize` from the surface
    /// are discarded (sparse encoding).
    /// @source UE5 DistanceFieldAtlas.h:38 — band limited to ~4 voxels
    float bandWidthMultiplier = 4.0f;

    /// Spatial hash grid resolution (cells per axis). Higher values accelerate
    /// the nearest-triangle search for large meshes. 0 = auto-select.
    uint32_t spatialHashResolution = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// SdfBaker — main API
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a sparse signed distance field from mesh triangle data.
///
/// # Parameters
/// - positions: Vertex positions as a flat float array [x0, y0, z0, x1, y1, z1, ...].
///   Must contain `vertexCount * 3` floats. Positions are in local mesh space (cm).
/// - vertexCount: Number of vertices.
/// - indices: Triangle indices (3 indices per triangle). Must contain `indexCount` entries.
///   If null, generates consecutive indices (0, 1, 2, 3, 4, 5, ...).
/// - indexCount: Number of indices. Must be a multiple of 3.
/// - config: Baking configuration. Use defaults for typical meshes.
///
/// # Returns
/// A `MeshSdfAsset` with the sparse brick data and indirection table.
/// Returns an invalid asset (IsValid() == false) on failure (zero vertices,
/// degenerate mesh, etc.).
///
/// # Example
/// ```cpp
/// SdfBakerConfig config;
/// config.maxResolution = 64;
/// MeshSdfAsset sdf = BakeMeshSdf(positions, vtxCount, indices, idxCount, config);
/// if (sdf.IsValid()) {
///     uint32_t atlasOffset = atlasManager.RegisterMesh(meshId, sdf);
/// }
/// ```
[[nodiscard]] MeshSdfAsset BakeMeshSdf(
    const float* positions,
    uint32_t vertexCount,
    const uint32_t* indices,
    uint32_t indexCount,
    const SdfBakerConfig& config = {});

} // namespace uz
