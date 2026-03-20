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
/// static meshes.
struct SdfBakerConfig {
    /// Maximum grid resolution per axis (in voxels, not bricks).
    /// The actual resolution is adaptive: smaller meshes get fewer voxels.
    /// The grid is always a multiple of SdfBrick::kSize (8).
    /// Typical mesh SDF resolution: 64-128 voxels per axis.
    uint32_t maxResolution = 64;

    /// Target voxel density: voxels per cm of mesh extent along the longest axis.
    /// Used to compute the actual resolution from mesh bounds.
    /// 0 = use maxResolution directly (fixed resolution mode).
    float voxelsPerCm = 0.0f;

    /// Surface band width multiplier. Bricks with all voxels farther than
    /// `bandWidthMultiplier * voxelSize * SdfBrick::kSize` from the surface
    /// are discarded (sparse encoding).
    /// Narrow-band SDF convention — discard bricks beyond ~4 voxels from surface.
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

/// Generates MIP levels for an existing MeshSdfAsset.
///
/// Downsamples the base-level (MIP 0) brick grid up to 3 times, halving
/// the brick resolution each time: 8x8x8 -> 4x4x4 -> 2x2x2 -> 1x1x1.
/// Each 2x2x2 block of parent voxels is reduced to 1 voxel by selecting
/// the minimum absolute distance (preserves the nearest surface).
///
/// MIP generation stops early if a dimension reaches 1 brick.
///
/// # Parameters
/// - asset: The SDF asset to generate MIPs for. Must be valid.
///   Modified in place — populates `mipBricks[0..N]` and sets `mipLevels`.
///
/// MIP chain enables LOD cone tracing at coarser resolutions.
void GenerateSdfMips(MeshSdfAsset& asset);

} // namespace uz
