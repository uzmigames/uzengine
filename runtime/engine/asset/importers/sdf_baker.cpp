/// UzEngine SDF Baker — Implementation.
///
/// Generates sparse signed distance fields from mesh triangles.
/// Uses a spatial hash for O(1) nearest-triangle lookups per voxel.
///
/// @source UE5 DistanceFieldAtlas.h:34-50 — brick layout, sparse storage
/// @source UE5 MeshUtilities.cpp — mesh SDF generation algorithm

#include "sdf_baker.h"

#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

UZ_DECLARE_LOG_CATEGORY(Asset);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Math Helpers
// ─────────────────────────────────────────────────────────────────────────────

struct Vec3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3f() = default;
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3f operator+(Vec3f b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3f operator-(Vec3f b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3f operator/(float s) const { return {x / s, y / s, z / s}; }

    float Dot(Vec3f b) const { return x * b.x + y * b.y + z * b.z; }
    Vec3f Cross(Vec3f b) const {
        return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
    }
    float LengthSq() const { return Dot(*this); }
    float Length() const { return std::sqrt(LengthSq()); }
    Vec3f Normalized() const {
        float len = Length();
        return len > 1e-8f ? *this / len : Vec3f{0, 0, 0};
    }

    static Vec3f Min(Vec3f a, Vec3f b) {
        return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    }
    static Vec3f Max(Vec3f a, Vec3f b) {
        return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Triangle
// ─────────────────────────────────────────────────────────────────────────────

struct Triangle {
    Vec3f v0, v1, v2;
    Vec3f normal; // Face normal (precomputed)
    Vec3f center; // Centroid (for spatial hashing)

    void ComputeDerived() {
        Vec3f edge0 = v1 - v0;
        Vec3f edge1 = v2 - v0;
        normal = edge0.Cross(edge1).Normalized();
        center = (v0 + v1 + v2) / 3.0f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Point-Triangle Distance (exact)
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the closest point on a triangle to a given point.
/// Returns the unsigned distance and the closest point.
///
/// Uses the projection + edge/vertex clamping method from
/// Ericson, "Real-Time Collision Detection", Chapter 5.1.5.
///
/// @source Standard algorithm — no UE5-specific constants
struct ClosestPointResult {
    Vec3f closestPoint;
    float distance;
};

static ClosestPointResult ClosestPointOnTriangle(Vec3f p, Vec3f a, Vec3f b, Vec3f c) {
    Vec3f ab = b - a;
    Vec3f ac = c - a;
    Vec3f ap = p - a;

    float d1 = ab.Dot(ap);
    float d2 = ac.Dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return {a, (p - a).Length()};
    }

    Vec3f bp = p - b;
    float d3 = ab.Dot(bp);
    float d4 = ac.Dot(bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return {b, (p - b).Length()};
    }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        Vec3f closest = a + ab * v;
        return {closest, (p - closest).Length()};
    }

    Vec3f cp = p - c;
    float d5 = ab.Dot(cp);
    float d6 = ac.Dot(cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return {c, (p - c).Length()};
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        Vec3f closest = a + ac * w;
        return {closest, (p - closest).Length()};
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        Vec3f closest = b + (c - b) * w;
        return {closest, (p - closest).Length()};
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    Vec3f closest = a + ab * v + ac * w;
    return {closest, (p - closest).Length()};
}

// ─────────────────────────────────────────────────────────────────────────────
// Spatial Hash — accelerates nearest-triangle lookups
// ─────────────────────────────────────────────────────────────────────────────

/// Simple 3D spatial hash grid. Each cell stores indices of triangles whose
/// AABB overlaps the cell. Query returns all triangles in the voxel's cell
/// plus its 26 neighbors (3x3x3 neighborhood).
class SpatialHash {
public:
    void Build(const std::vector<Triangle>& triangles, Vec3f boundsMin, Vec3f boundsMax,
               uint32_t resolution) {
        m_boundsMin = boundsMin;
        m_boundsMax = boundsMax;
        m_resolution = resolution;
        m_cellSize = (boundsMax - boundsMin) / static_cast<float>(resolution);

        // Avoid zero cell size
        if (m_cellSize.x < 1e-6f) m_cellSize.x = 1.0f;
        if (m_cellSize.y < 1e-6f) m_cellSize.y = 1.0f;
        if (m_cellSize.z < 1e-6f) m_cellSize.z = 1.0f;

        uint32_t totalCells = resolution * resolution * resolution;
        m_cells.resize(totalCells);

        for (uint32_t i = 0; i < static_cast<uint32_t>(triangles.size()); ++i) {
            const auto& tri = triangles[i];

            // Compute triangle AABB
            Vec3f triMin = Vec3f::Min(tri.v0, Vec3f::Min(tri.v1, tri.v2));
            Vec3f triMax = Vec3f::Max(tri.v0, Vec3f::Max(tri.v1, tri.v2));

            // Find grid cells that the triangle AABB overlaps
            int3 cellMin = WorldToCell(triMin);
            int3 cellMax = WorldToCell(triMax);

            for (int z = cellMin.z; z <= cellMax.z; ++z) {
                for (int y = cellMin.y; y <= cellMax.y; ++y) {
                    for (int x = cellMin.x; x <= cellMax.x; ++x) {
                        uint32_t idx = CellIndex(x, y, z);
                        if (idx < totalCells) {
                            m_cells[idx].push_back(i);
                        }
                    }
                }
            }
        }
    }

    /// Queries triangles in a 3x3x3 neighborhood around the given world position.
    void Query(Vec3f worldPos, std::vector<uint32_t>& outIndices) const {
        outIndices.clear();
        int3 cell = WorldToCell(worldPos);

        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int cx = cell.x + dx;
                    int cy = cell.y + dy;
                    int cz = cell.z + dz;
                    if (cx < 0 || cy < 0 || cz < 0) continue;
                    if (cx >= static_cast<int>(m_resolution) ||
                        cy >= static_cast<int>(m_resolution) ||
                        cz >= static_cast<int>(m_resolution)) continue;

                    uint32_t idx = CellIndex(cx, cy, cz);
                    const auto& bucket = m_cells[idx];
                    outIndices.insert(outIndices.end(), bucket.begin(), bucket.end());
                }
            }
        }

        // Remove duplicates
        std::sort(outIndices.begin(), outIndices.end());
        outIndices.erase(std::unique(outIndices.begin(), outIndices.end()), outIndices.end());
    }

private:
    struct int3 {
        int x = 0, y = 0, z = 0;
    };

    int3 WorldToCell(Vec3f pos) const {
        Vec3f local = pos - m_boundsMin;
        return {
            std::clamp(static_cast<int>(local.x / m_cellSize.x), 0, static_cast<int>(m_resolution) - 1),
            std::clamp(static_cast<int>(local.y / m_cellSize.y), 0, static_cast<int>(m_resolution) - 1),
            std::clamp(static_cast<int>(local.z / m_cellSize.z), 0, static_cast<int>(m_resolution) - 1)
        };
    }

    uint32_t CellIndex(int x, int y, int z) const {
        return static_cast<uint32_t>(z) * m_resolution * m_resolution +
               static_cast<uint32_t>(y) * m_resolution +
               static_cast<uint32_t>(x);
    }

    Vec3f m_boundsMin;
    Vec3f m_boundsMax;
    Vec3f m_cellSize;
    uint32_t m_resolution = 0;
    std::vector<std::vector<uint32_t>> m_cells;
};

// ─────────────────────────────────────────────────────────────────────────────
// BakeMeshSdf
// ─────────────────────────────────────────────────────────────────────────────

MeshSdfAsset BakeMeshSdf(
    const float* positions,
    uint32_t vertexCount,
    const uint32_t* indices,
    uint32_t indexCount,
    const SdfBakerConfig& config)
{
    MeshSdfAsset result;

    // ── Validate inputs ──
    if (!positions || vertexCount < 3) {
        UZ_LOG(Asset, Error, "SdfBaker: invalid input (positions={}, vertexCount={})",
               positions != nullptr ? "valid" : "null", vertexCount);
        return result;
    }

    if (indexCount == 0) {
        indexCount = vertexCount;
    }

    if (indexCount % 3 != 0) {
        UZ_LOG(Asset, Error, "SdfBaker: indexCount={} is not a multiple of 3", indexCount);
        return result;
    }

    uint32_t triangleCount = indexCount / 3;
    if (triangleCount == 0) {
        UZ_LOG(Asset, Error, "SdfBaker: no triangles");
        return result;
    }

    // ── Build triangle list ──
    std::vector<Triangle> triangles(triangleCount);
    Vec3f boundsMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3f boundsMax{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    for (uint32_t t = 0; t < triangleCount; ++t) {
        uint32_t i0 = indices ? indices[t * 3 + 0] : t * 3 + 0;
        uint32_t i1 = indices ? indices[t * 3 + 1] : t * 3 + 1;
        uint32_t i2 = indices ? indices[t * 3 + 2] : t * 3 + 2;

        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            UZ_LOG(Asset, Error, "SdfBaker: out-of-bounds index at triangle {}", t);
            return result;
        }

        triangles[t].v0 = {positions[i0 * 3 + 0], positions[i0 * 3 + 1], positions[i0 * 3 + 2]};
        triangles[t].v1 = {positions[i1 * 3 + 0], positions[i1 * 3 + 1], positions[i1 * 3 + 2]};
        triangles[t].v2 = {positions[i2 * 3 + 0], positions[i2 * 3 + 1], positions[i2 * 3 + 2]};
        triangles[t].ComputeDerived();

        // Update mesh AABB
        boundsMin = Vec3f::Min(boundsMin, Vec3f::Min(triangles[t].v0, Vec3f::Min(triangles[t].v1, triangles[t].v2)));
        boundsMax = Vec3f::Max(boundsMax, Vec3f::Max(triangles[t].v0, Vec3f::Max(triangles[t].v1, triangles[t].v2)));
    }

    // ── Compute grid resolution ──
    Vec3f meshExtent = boundsMax - boundsMin;
    float longestAxis = std::max({meshExtent.x, meshExtent.y, meshExtent.z});

    if (longestAxis < 1e-4f) {
        UZ_LOG(Asset, Error, "SdfBaker: degenerate mesh (extent too small: {:.4f})", longestAxis);
        return result;
    }

    // Compute voxel resolution (in voxels, not bricks)
    uint32_t voxelRes = config.maxResolution;
    if (config.voxelsPerCm > 0.0f) {
        voxelRes = std::min(config.maxResolution,
                            static_cast<uint32_t>(longestAxis * config.voxelsPerCm));
    }

    // Round up to a multiple of brick size
    voxelRes = std::max(voxelRes, static_cast<uint32_t>(SdfBrick::kSize));
    voxelRes = ((voxelRes + SdfBrick::kSize - 1) / SdfBrick::kSize) * SdfBrick::kSize;

    float voxelSize = longestAxis / static_cast<float>(voxelRes);

    // Compute per-axis resolution (may differ for non-cubic meshes)
    uint32_t resVoxelX = std::max(static_cast<uint32_t>(std::ceil(meshExtent.x / voxelSize)), SdfBrick::kSize);
    uint32_t resVoxelY = std::max(static_cast<uint32_t>(std::ceil(meshExtent.y / voxelSize)), SdfBrick::kSize);
    uint32_t resVoxelZ = std::max(static_cast<uint32_t>(std::ceil(meshExtent.z / voxelSize)), SdfBrick::kSize);

    // Round up to brick boundaries
    resVoxelX = ((resVoxelX + SdfBrick::kSize - 1) / SdfBrick::kSize) * SdfBrick::kSize;
    resVoxelY = ((resVoxelY + SdfBrick::kSize - 1) / SdfBrick::kSize) * SdfBrick::kSize;
    resVoxelZ = ((resVoxelZ + SdfBrick::kSize - 1) / SdfBrick::kSize) * SdfBrick::kSize;

    uint32_t brickResX = resVoxelX / SdfBrick::kSize;
    uint32_t brickResY = resVoxelY / SdfBrick::kSize;
    uint32_t brickResZ = resVoxelZ / SdfBrick::kSize;

    // Expand bounds by a half-brick margin for the narrow band
    /// @source UE5 DistanceFieldAtlas.h — narrow band margin around mesh surface
    float margin = voxelSize * static_cast<float>(SdfBrick::kSize) * 0.5f;
    Vec3f expandedMin = boundsMin - Vec3f{margin, margin, margin};
    Vec3f expandedMax = expandedMin + Vec3f{
        static_cast<float>(resVoxelX) * voxelSize,
        static_cast<float>(resVoxelY) * voxelSize,
        static_cast<float>(resVoxelZ) * voxelSize
    };

    // ── Build spatial hash ──
    uint32_t hashRes = config.spatialHashResolution;
    if (hashRes == 0) {
        // Auto-select: roughly 1 cell per 4 bricks along the longest axis
        hashRes = std::max(4u, voxelRes / (SdfBrick::kSize * 4));
    }

    SpatialHash spatialHash;
    spatialHash.Build(triangles, expandedMin, expandedMax, hashRes);

    // ── Compute SDF for each voxel ──
    // First pass: compute raw signed distances in a dense grid,
    // then extract sparse bricks.
    uint32_t totalVoxels = resVoxelX * resVoxelY * resVoxelZ;
    std::vector<float> denseDistances(totalVoxels);
    float bandWidth = config.bandWidthMultiplier * voxelSize * static_cast<float>(SdfBrick::kSize);

    std::vector<uint32_t> queryBuffer; // Reusable buffer for spatial hash queries

    for (uint32_t vz = 0; vz < resVoxelZ; ++vz) {
        for (uint32_t vy = 0; vy < resVoxelY; ++vy) {
            for (uint32_t vx = 0; vx < resVoxelX; ++vx) {
                // Voxel center in world space
                Vec3f voxelCenter = expandedMin + Vec3f{
                    (static_cast<float>(vx) + 0.5f) * voxelSize,
                    (static_cast<float>(vy) + 0.5f) * voxelSize,
                    (static_cast<float>(vz) + 0.5f) * voxelSize
                };

                // Query spatial hash for nearby triangles
                spatialHash.Query(voxelCenter, queryBuffer);

                float minDist = std::numeric_limits<float>::max();
                Vec3f closestNormal{0, 0, 1};
                Vec3f closestDisplacement{0, 0, 0};

                for (uint32_t triIdx : queryBuffer) {
                    const auto& tri = triangles[triIdx];
                    auto result = ClosestPointOnTriangle(voxelCenter, tri.v0, tri.v1, tri.v2);

                    if (result.distance < minDist) {
                        minDist = result.distance;
                        closestNormal = tri.normal;
                        closestDisplacement = voxelCenter - result.closestPoint;
                    }
                }

                // If no triangles were nearby, fall back to brute-force search
                // for this voxel (rare — only happens at sparse hash boundaries)
                if (queryBuffer.empty()) {
                    for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                        const auto& tri = triangles[triIdx];
                        auto r = ClosestPointOnTriangle(voxelCenter, tri.v0, tri.v1, tri.v2);
                        if (r.distance < minDist) {
                            minDist = r.distance;
                            closestNormal = tri.normal;
                            closestDisplacement = voxelCenter - r.closestPoint;
                        }
                    }
                }

                // Determine sign: positive = outside, negative = inside
                // Uses the pseudo-normal method: dot(displacement, face_normal) > 0 → outside
                float sign = closestDisplacement.Dot(closestNormal) >= 0.0f ? 1.0f : -1.0f;
                float signedDist = sign * minDist;

                // Clamp to band width
                signedDist = std::clamp(signedDist, -bandWidth, bandWidth);

                uint32_t linearIdx = vz * resVoxelY * resVoxelX + vy * resVoxelX + vx;
                denseDistances[linearIdx] = signedDist;
            }
        }
    }

    // ── Extract sparse bricks ──
    uint32_t totalBricks = brickResX * brickResY * brickResZ;
    result.indirection.resize(totalBricks, UINT32_MAX);
    result.bricks.clear();
    result.bricks.reserve(totalBricks / 4); // Typical: ~25% of bricks contain surface data

    for (uint32_t bz = 0; bz < brickResZ; ++bz) {
        for (uint32_t by = 0; by < brickResY; ++by) {
            for (uint32_t bx = 0; bx < brickResX; ++bx) {
                // Check if any voxel in this brick is within the surface band
                bool hasSurfaceVoxel = false;
                SdfBrick brick{};

                uint32_t baseVoxelX = bx * SdfBrick::kSize;
                uint32_t baseVoxelY = by * SdfBrick::kSize;
                uint32_t baseVoxelZ = bz * SdfBrick::kSize;

                for (uint32_t lz = 0; lz < SdfBrick::kSize; ++lz) {
                    for (uint32_t ly = 0; ly < SdfBrick::kSize; ++ly) {
                        for (uint32_t lx = 0; lx < SdfBrick::kSize; ++lx) {
                            uint32_t vx = baseVoxelX + lx;
                            uint32_t vy = baseVoxelY + ly;
                            uint32_t vz = baseVoxelZ + lz;

                            if (vx >= resVoxelX || vy >= resVoxelY || vz >= resVoxelZ) {
                                // Voxel outside grid — treat as max distance (outside)
                                brick.data[lz * 64 + ly * 8 + lx] = 255;
                                continue;
                            }

                            uint32_t denseIdx = vz * resVoxelY * resVoxelX + vy * resVoxelX + vx;
                            float signedDist = denseDistances[denseIdx];

                            // Check if this voxel is within the surface band
                            if (std::abs(signedDist) < bandWidth) {
                                hasSurfaceVoxel = true;
                            }

                            // Quantize: map [-bandWidth, +bandWidth] to [0, 255]
                            // 128 = zero-crossing (surface)
                            /// @source UE5 DistanceFieldAtlas.h:34-50 — 8-bit quantization
                            float normalized = (signedDist + bandWidth) / (2.0f * bandWidth);
                            normalized = std::clamp(normalized, 0.0f, 1.0f);
                            brick.data[lz * 64 + ly * 8 + lx] = static_cast<uint8_t>(normalized * 255.0f + 0.5f);
                        }
                    }
                }

                uint32_t brickIdx = bz * brickResY * brickResX + by * brickResX + bx;

                if (hasSurfaceVoxel) {
                    // Store the brick and record the mapping
                    uint32_t storedBrickIdx = static_cast<uint32_t>(result.bricks.size());
                    result.bricks.push_back(brick);
                    result.indirection[brickIdx] = storedBrickIdx;
                }
                // else: indirection stays UINT32_MAX (empty brick)
            }
        }
    }

    // ── Fill output asset ──
    result.localBoundsMin[0] = expandedMin.x;
    result.localBoundsMin[1] = expandedMin.y;
    result.localBoundsMin[2] = expandedMin.z;
    result.localBoundsMax[0] = expandedMax.x;
    result.localBoundsMax[1] = expandedMax.y;
    result.localBoundsMax[2] = expandedMax.z;
    result.voxelSize = voxelSize;
    result.resolutionX = brickResX;
    result.resolutionY = brickResY;
    result.resolutionZ = brickResZ;
    result.mipLevels = 1; // Updated by GenerateSdfMips() below

    float sparsity = 1.0f - static_cast<float>(result.bricks.size()) / static_cast<float>(totalBricks);
    UZ_LOG(Asset, Info,
           "SdfBaker: generated {}x{}x{} brick grid ({} surface bricks / {} total, {:.0f}% sparse), "
           "voxelSize={:.2f}cm, band={:.1f}cm",
           brickResX, brickResY, brickResZ,
           result.bricks.size(), totalBricks,
           sparsity * 100.0f,
           voxelSize, bandWidth);

    // Generate MIP levels after base SDF is complete.
    GenerateSdfMips(result);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// GenerateSdfMips — downsample brick grid up to 3 times
// ─────────────────────────────────────────────────────────────────────────────

/// Downsamples a single brick's voxels by 2x in each dimension.
///
/// Takes a source brick (8x8x8 = 512 voxels) and produces a destination
/// region of 4x4x4 = 64 voxels at the specified offset within the destination brick.
/// Each 2x2x2 block of source voxels is reduced to 1 voxel using minimum
/// absolute distance from the zero-crossing (value 128).
///
/// The "minimum absolute distance" rule preserves the nearest surface:
/// among the 8 source voxels in a 2x2x2 block, the one closest to 128
/// (the zero-crossing) wins, because it represents the shortest distance
/// to the mesh surface.
///
/// @source UE5 DistanceFieldAtlas.h — min-distance MIP downsampling
static void DownsampleBrick2x(const SdfBrick& src, uint8_t* dstVoxels) {
    constexpr uint32_t kHalf = SdfBrick::kSize / 2; // 4

    for (uint32_t dz = 0; dz < kHalf; ++dz) {
        for (uint32_t dy = 0; dy < kHalf; ++dy) {
            for (uint32_t dx = 0; dx < kHalf; ++dx) {
                // Sample the 2x2x2 block from source
                uint32_t sx = dx * 2;
                uint32_t sy = dy * 2;
                uint32_t sz = dz * 2;

                // Find the voxel with minimum absolute distance from 128 (zero-crossing)
                uint8_t bestVal = 128;
                int bestAbsDist = 256; // Larger than any possible |val - 128|

                for (uint32_t oz = 0; oz < 2; ++oz) {
                    for (uint32_t oy = 0; oy < 2; ++oy) {
                        for (uint32_t ox = 0; ox < 2; ++ox) {
                            uint32_t srcIdx = (sz + oz) * 64 + (sy + oy) * 8 + (sx + ox);
                            uint8_t val = src.data[srcIdx];
                            int absDist = std::abs(static_cast<int>(val) - 128);
                            if (absDist < bestAbsDist) {
                                bestAbsDist = absDist;
                                bestVal = val;
                            }
                        }
                    }
                }

                uint32_t dstIdx = dz * 16 + dy * 4 + dx; // 4x4x4 layout
                dstVoxels[dstIdx] = bestVal;
            }
        }
    }
}

/// Reconstructs a full 8x8x8 brick from 2x2x2 arrangement of downsampled 4x4x4 sub-blocks.
///
/// When the parent MIP has multiple adjacent bricks that map to one brick at
/// the child MIP level, we need to gather the 4x4x4 contributions from each
/// parent brick into different octants of the child's 8x8x8 brick.
///
/// If a parent brick is empty (indirection = UINT32_MAX), its octant is filled
/// with 255 (maximum outside distance).
static void PlaceSubBlock(SdfBrick& dst, const uint8_t* subVoxels,
                           uint32_t octantX, uint32_t octantY, uint32_t octantZ) {
    constexpr uint32_t kHalf = SdfBrick::kSize / 2; // 4
    uint32_t baseX = octantX * kHalf;
    uint32_t baseY = octantY * kHalf;
    uint32_t baseZ = octantZ * kHalf;

    for (uint32_t lz = 0; lz < kHalf; ++lz) {
        for (uint32_t ly = 0; ly < kHalf; ++ly) {
            for (uint32_t lx = 0; lx < kHalf; ++lx) {
                uint32_t dstIdx = (baseZ + lz) * 64 + (baseY + ly) * 8 + (baseX + lx);
                uint32_t srcIdx = lz * 16 + ly * 4 + lx;
                dst.data[dstIdx] = subVoxels[srcIdx];
            }
        }
    }
}

void GenerateSdfMips(MeshSdfAsset& asset) {
    if (!asset.IsValid()) {
        return;
    }

    // Determine how many MIP levels we can generate (up to 3).
    // Stop when any dimension reaches 1 brick (can't halve further).
    uint32_t parentResX = asset.resolutionX;
    uint32_t parentResY = asset.resolutionY;
    uint32_t parentResZ = asset.resolutionZ;

    uint32_t mipCount = 0;
    {
        uint32_t rx = parentResX, ry = parentResY, rz = parentResZ;
        for (uint32_t m = 0; m < MeshSdfAsset::kMaxMipLevels; ++m) {
            // Each MIP halves the brick grid. We use ceiling division so that
            // a 3-brick axis becomes 2-brick at the next MIP, not 1.
            uint32_t nrx = (rx + 1) / 2;
            uint32_t nry = (ry + 1) / 2;
            uint32_t nrz = (rz + 1) / 2;
            if (nrx == 0 || nry == 0 || nrz == 0) break;
            mipCount = m + 1;
            rx = nrx;
            ry = nry;
            rz = nrz;
            // If all dimensions are 1, this is the last MIP
            if (rx == 1 && ry == 1 && rz == 1) {
                break;
            }
        }
    }

    if (mipCount == 0) {
        asset.mipLevels = 1;
        return;
    }

    // For each MIP level, downsample the parent level's bricks.
    // Parent for MIP 1 = base (bricks/indirection), parent for MIP 2 = mipBricks[0], etc.

    const std::vector<SdfBrick>* parentBricks = &asset.bricks;
    const std::vector<uint32_t>* parentIndirection = &asset.indirection;
    uint32_t pResX = asset.resolutionX;
    uint32_t pResY = asset.resolutionY;
    uint32_t pResZ = asset.resolutionZ;

    for (uint32_t m = 0; m < mipCount; ++m) {
        uint32_t childResX = (pResX + 1) / 2;
        uint32_t childResY = (pResY + 1) / 2;
        uint32_t childResZ = (pResZ + 1) / 2;

        uint32_t childTotalBricks = childResX * childResY * childResZ;

        auto& mip = asset.mipBricks[m];
        mip.resolutionX = childResX;
        mip.resolutionY = childResY;
        mip.resolutionZ = childResZ;
        mip.indirection.resize(childTotalBricks, UINT32_MAX);
        mip.bricks.clear();
        mip.bricks.reserve(childTotalBricks / 4);

        // Temporary buffer for 4x4x4 downsampled sub-blocks
        uint8_t subBlock[64]; // 4x4x4

        for (uint32_t cbz = 0; cbz < childResZ; ++cbz) {
            for (uint32_t cby = 0; cby < childResY; ++cby) {
                for (uint32_t cbx = 0; cbx < childResX; ++cbx) {
                    // The child brick at (cbx, cby, cbz) is formed from up to
                    // 2x2x2 = 8 parent bricks at (cbx*2+ox, cby*2+oy, cbz*2+oz).
                    SdfBrick childBrick{};
                    // Fill with 255 (max outside) by default
                    std::memset(childBrick.data, 255, SdfBrick::kVoxelCount);

                    bool hasSurfaceVoxel = false;

                    for (uint32_t oz = 0; oz < 2; ++oz) {
                        for (uint32_t oy = 0; oy < 2; ++oy) {
                            for (uint32_t ox = 0; ox < 2; ++ox) {
                                uint32_t pbx = cbx * 2 + ox;
                                uint32_t pby = cby * 2 + oy;
                                uint32_t pbz = cbz * 2 + oz;

                                // Check bounds (parent grid may be odd-sized)
                                if (pbx >= pResX || pby >= pResY || pbz >= pResZ) {
                                    // Out of bounds — fill octant with 255
                                    std::memset(subBlock, 255, sizeof(subBlock));
                                    PlaceSubBlock(childBrick, subBlock, ox, oy, oz);
                                    continue;
                                }

                                uint32_t parentBrickIdx = pbz * pResY * pResX + pby * pResX + pbx;
                                uint32_t parentStoredIdx = (*parentIndirection)[parentBrickIdx];

                                if (parentStoredIdx == UINT32_MAX) {
                                    // Empty parent brick — fill octant with 255
                                    std::memset(subBlock, 255, sizeof(subBlock));
                                    PlaceSubBlock(childBrick, subBlock, ox, oy, oz);
                                    continue;
                                }

                                // Downsample parent brick 8x8x8 -> 4x4x4
                                DownsampleBrick2x((*parentBricks)[parentStoredIdx], subBlock);
                                PlaceSubBlock(childBrick, subBlock, ox, oy, oz);

                                // Check if any voxel is near the surface
                                for (uint32_t i = 0; i < 64; ++i) {
                                    if (subBlock[i] != 255 && subBlock[i] != 0) {
                                        hasSurfaceVoxel = true;
                                    }
                                }
                            }
                        }
                    }

                    // Also check if any voxel in the assembled brick is near-surface
                    // (the sub-block check above catches most cases but let's be thorough)
                    if (!hasSurfaceVoxel) {
                        for (uint32_t i = 0; i < SdfBrick::kVoxelCount; ++i) {
                            int absDist = std::abs(static_cast<int>(childBrick.data[i]) - 128);
                            if (absDist < 127) { // Not at the extreme boundary values
                                hasSurfaceVoxel = true;
                                break;
                            }
                        }
                    }

                    uint32_t childBrickIdx = cbz * childResY * childResX + cby * childResX + cbx;

                    if (hasSurfaceVoxel) {
                        uint32_t storedIdx = static_cast<uint32_t>(mip.bricks.size());
                        mip.bricks.push_back(childBrick);
                        mip.indirection[childBrickIdx] = storedIdx;
                    }
                    // else: indirection stays UINT32_MAX
                }
            }
        }

        UZ_LOG(Asset, Info,
               "SdfBaker: MIP {} — {}x{}x{} bricks ({} surface bricks / {} total)",
               m + 1, childResX, childResY, childResZ,
               mip.bricks.size(), childTotalBricks);

        // Next iteration: this MIP becomes the parent
        parentBricks = &mip.bricks;
        parentIndirection = &mip.indirection;
        pResX = childResX;
        pResY = childResY;
        pResZ = childResZ;
    }

    asset.mipLevels = 1 + mipCount; // base + N mips
}

} // namespace uz
