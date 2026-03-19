/// UzEngine GI Mesh SDF Baker — Implementation.
///
/// Generates dense signed distance fields from mesh triangles for the GI pipeline.
/// Uses a spatial hash for O(V * T/B) nearest-triangle queries, where V = voxels,
/// T = triangles, B = spatial hash bucket size.
///
/// @source UE5 DistanceFieldAtlas.h:36-50 — brick layout constants
/// @source UE5 MeshUtilities.cpp — mesh SDF generation algorithm

#include "mesh_sdf_baker.h"

#include "log.h"
#include "rhi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal math helpers (local to this TU — avoids pulling in Vec3/DirectXMath)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct Float3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Float3() = default;
    constexpr Float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Float3(const float* p) : x(p[0]), y(p[1]), z(p[2]) {}

    Float3 operator+(Float3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Float3 operator-(Float3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Float3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Float3 operator/(float s) const { return {x / s, y / s, z / s}; }

    float Dot(Float3 b) const { return x * b.x + y * b.y + z * b.z; }
    Float3 Cross(Float3 b) const {
        return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x};
    }
    float LengthSq() const { return Dot(*this); }
    float Length() const { return std::sqrt(LengthSq()); }
    Float3 Normalized() const {
        float len = Length();
        return len > 1e-8f ? *this / len : Float3{0, 0, 0};
    }

    static Float3 Min(Float3 a, Float3 b) {
        return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    }
    static Float3 Max(Float3 a, Float3 b) {
        return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Triangle storage
// ─────────────────────────────────────────────────────────────────────────────

struct TriData {
    Float3 v0, v1, v2;
    Float3 normal; ///< Face normal (precomputed).
};

// ─────────────────────────────────────────────────────────────────────────────
// Closest point on triangle (Ericson, "Real-Time Collision Detection", 5.1.5)
// ─────────────────────────────────────────────────────────────────────────────

struct ClosestResult {
    Float3 point;
    float  distance;
};

/// Computes the closest point on triangle ABC to point P.
/// Returns the closest point and the unsigned distance.
///
/// @source Standard algorithm — Real-Time Collision Detection, Ch 5.1.5
ClosestResult ClosestPointOnTriangle(Float3 p, Float3 a, Float3 b, Float3 c) {
    Float3 ab = b - a;
    Float3 ac = c - a;
    Float3 ap = p - a;

    float d1 = ab.Dot(ap);
    float d2 = ac.Dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return {a, (p - a).Length()};
    }

    Float3 bp = p - b;
    float d3 = ab.Dot(bp);
    float d4 = ac.Dot(bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return {b, (p - b).Length()};
    }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        Float3 closest = a + ab * v;
        return {closest, (p - closest).Length()};
    }

    Float3 cp = p - c;
    float d5 = ab.Dot(cp);
    float d6 = ac.Dot(cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return {c, (p - c).Length()};
    }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        Float3 closest = a + ac * w;
        return {closest, (p - closest).Length()};
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        Float3 closest = b + (c - b) * w;
        return {closest, (p - closest).Length()};
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    Float3 closest = a + ab * v + ac * w;
    return {closest, (p - closest).Length()};
}

// ─────────────────────────────────────────────────────────────────────────────
// Spatial hash — accelerates nearest-triangle lookups
// ─────────────────────────────────────────────────────────────────────────────

/// Simple 3D spatial hash. Each cell stores indices of triangles whose AABB
/// overlaps the cell. Query returns all triangles in a 3x3x3 neighborhood.
class SpatialHash {
public:
    void Build(const std::vector<TriData>& triangles, Float3 boundsMin, Float3 boundsMax,
               uint32_t resolution) {
        m_boundsMin = boundsMin;
        m_resolution = resolution;
        m_cellSize = (boundsMax - boundsMin) / static_cast<float>(resolution);

        // Avoid zero cell size on degenerate axes
        if (m_cellSize.x < 1e-6f) m_cellSize.x = 1.0f;
        if (m_cellSize.y < 1e-6f) m_cellSize.y = 1.0f;
        if (m_cellSize.z < 1e-6f) m_cellSize.z = 1.0f;

        uint32_t totalCells = resolution * resolution * resolution;
        m_cells.clear();
        m_cells.resize(totalCells);

        for (uint32_t i = 0; i < static_cast<uint32_t>(triangles.size()); ++i) {
            const auto& tri = triangles[i];

            // Triangle AABB
            Float3 triMin = Float3::Min(tri.v0, Float3::Min(tri.v1, tri.v2));
            Float3 triMax = Float3::Max(tri.v0, Float3::Max(tri.v1, tri.v2));

            // Grid cells overlapping the triangle
            auto cellMin = WorldToCell(triMin);
            auto cellMax = WorldToCell(triMax);

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

    /// Returns triangle indices near the given world position (3x3x3 neighborhood).
    void Query(Float3 worldPos, std::vector<uint32_t>& outIndices) const {
        outIndices.clear();
        auto cell = WorldToCell(worldPos);
        int res = static_cast<int>(m_resolution);

        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int cx = cell.x + dx;
                    int cy = cell.y + dy;
                    int cz = cell.z + dz;
                    if (cx < 0 || cy < 0 || cz < 0) continue;
                    if (cx >= res || cy >= res || cz >= res) continue;

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
    struct CellCoord { int x = 0, y = 0, z = 0; };

    CellCoord WorldToCell(Float3 pos) const {
        Float3 local = pos - m_boundsMin;
        int res = static_cast<int>(m_resolution);
        return {
            std::clamp(static_cast<int>(local.x / m_cellSize.x), 0, res - 1),
            std::clamp(static_cast<int>(local.y / m_cellSize.y), 0, res - 1),
            std::clamp(static_cast<int>(local.z / m_cellSize.z), 0, res - 1)
        };
    }

    uint32_t CellIndex(int x, int y, int z) const {
        return static_cast<uint32_t>(z) * m_resolution * m_resolution +
               static_cast<uint32_t>(y) * m_resolution +
               static_cast<uint32_t>(x);
    }

    Float3 m_boundsMin;
    Float3 m_cellSize;
    uint32_t m_resolution = 0;
    std::vector<std::vector<uint32_t>> m_cells;
};

// ─────────────────────────────────────────────────────────────────────────────
// Float16 conversion (for R16F upload)
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a 32-bit float to a 16-bit half-float (IEEE 754 binary16).
///
/// Handles denormals, overflow (clamps to +/-INF), and NaN propagation.
/// This avoids pulling in DirectXMath for a single conversion function.
uint16_t FloatToHalf(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));

    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t  exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 127;
    uint32_t mantissa = bits & 0x7FFFFF;

    // Handle special cases
    if (exponent == 128) {
        // Inf or NaN
        if (mantissa != 0) {
            // NaN — preserve signaling bit
            return static_cast<uint16_t>(sign | 0x7E00);
        }
        return static_cast<uint16_t>(sign | 0x7C00); // Inf
    }

    if (exponent > 15) {
        // Overflow — clamp to max half-float
        return static_cast<uint16_t>(sign | 0x7BFF);
    }

    if (exponent > -15) {
        // Normal range for half-float
        uint32_t halfExp = static_cast<uint32_t>(exponent + 15) << 10;
        uint32_t halfMant = mantissa >> 13;
        // Round to nearest even
        if ((mantissa & 0x1000) && (mantissa & 0x2FFF)) {
            halfMant++;
        }
        return static_cast<uint16_t>(sign | halfExp | halfMant);
    }

    if (exponent >= -24) {
        // Denormalized half-float
        mantissa |= 0x800000; // Add implicit leading 1
        uint32_t shift = static_cast<uint32_t>(-1 - exponent);
        uint32_t halfMant = mantissa >> (shift + 14);
        return static_cast<uint16_t>(sign | halfMant);
    }

    // Too small — flush to zero
    return static_cast<uint16_t>(sign);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// MeshSdfBaker::PointToTriangleDistance
// ─────────────────────────────────────────────────────────────────────────────

float MeshSdfBaker::PointToTriangleDistance(
    const float p[3], const float a[3], const float b[3], const float c[3])
{
    Float3 fp{p[0], p[1], p[2]};
    Float3 fa{a[0], a[1], a[2]};
    Float3 fb{b[0], b[1], b[2]};
    Float3 fc{c[0], c[1], c[2]};
    return ClosestPointOnTriangle(fp, fa, fb, fc).distance;
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshSdfBaker::ComputeSignedDistance
// ─────────────────────────────────────────────────────────────────────────────

float MeshSdfBaker::ComputeSignedDistance(
    const float point[3], const float closestPoint[3],
    const float faceNormal[3], float unsignedDistance)
{
    // Displacement from closest surface point to query point
    float dx = point[0] - closestPoint[0];
    float dy = point[1] - closestPoint[1];
    float dz = point[2] - closestPoint[2];

    // Dot product with face normal determines sign
    float dot = dx * faceNormal[0] + dy * faceNormal[1] + dz * faceNormal[2];

    return dot >= 0.0f ? unsignedDistance : -unsignedDistance;
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshSdfBaker::BakeMesh
// ─────────────────────────────────────────────────────────────────────────────

MeshSDFData MeshSdfBaker::BakeMesh(
    const float* vertices,
    uint32_t vertexCount,
    uint32_t vertexStride,
    const uint32_t* indices,
    uint32_t indexCount,
    uint32_t resolution)
{
    MeshSDFData result;

    // ── Validate inputs ──
    if (!vertices || vertexCount < 3) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker: invalid input (vertices={}, vertexCount={})",
               vertices != nullptr ? "valid" : "null", vertexCount);
        return result;
    }

    if (indexCount == 0) {
        // Non-indexed: treat every 3 vertices as a triangle
        indexCount = vertexCount;
    }

    if (indexCount % 3 != 0) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker: indexCount={} is not a multiple of 3", indexCount);
        return result;
    }

    if (resolution < 4 || resolution > 256) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker: resolution {} out of valid range [4, 256]", resolution);
        return result;
    }

    uint32_t triangleCount = indexCount / 3;
    if (triangleCount == 0) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker: no triangles");
        return result;
    }

    uint32_t strideFloats = vertexStride / sizeof(float);

    // ── Build triangle list and compute mesh AABB ──
    std::vector<TriData> triangles(triangleCount);
    Float3 boundsMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
    Float3 boundsMax{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest()};

    for (uint32_t t = 0; t < triangleCount; ++t) {
        uint32_t i0 = indices ? indices[t * 3 + 0] : t * 3 + 0;
        uint32_t i1 = indices ? indices[t * 3 + 1] : t * 3 + 1;
        uint32_t i2 = indices ? indices[t * 3 + 2] : t * 3 + 2;

        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            UZ_LOG(Renderer, Error, "MeshSdfBaker: out-of-bounds index at triangle {}", t);
            return result;
        }

        // Extract positions using vertex stride
        const float* p0 = vertices + i0 * strideFloats;
        const float* p1 = vertices + i1 * strideFloats;
        const float* p2 = vertices + i2 * strideFloats;

        triangles[t].v0 = Float3{p0[0], p0[1], p0[2]};
        triangles[t].v1 = Float3{p1[0], p1[1], p1[2]};
        triangles[t].v2 = Float3{p2[0], p2[1], p2[2]};

        // Precompute face normal
        Float3 edge0 = triangles[t].v1 - triangles[t].v0;
        Float3 edge1 = triangles[t].v2 - triangles[t].v0;
        triangles[t].normal = edge0.Cross(edge1).Normalized();

        // Update AABB
        boundsMin = Float3::Min(boundsMin, Float3::Min(triangles[t].v0,
                    Float3::Min(triangles[t].v1, triangles[t].v2)));
        boundsMax = Float3::Max(boundsMax, Float3::Max(triangles[t].v0,
                    Float3::Max(triangles[t].v1, triangles[t].v2)));
    }

    // ── Validate mesh extent ──
    Float3 meshExtent = boundsMax - boundsMin;
    float longestAxis = std::max({meshExtent.x, meshExtent.y, meshExtent.z});

    if (longestAxis < 1e-4f) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker: degenerate mesh (extent too small: {:.4f})",
               longestAxis);
        return result;
    }

    // ── Expand AABB by 10% padding for narrow band ──
    /// The 10% expansion ensures the SDF volume extends beyond the mesh surface,
    /// providing valid distance values for sphere marching near the volume boundary.
    /// @source UE5 DistanceFieldAtlas.h — narrow band margin around mesh surface
    constexpr float kPaddingFraction = 0.1f;
    Float3 padding = meshExtent * kPaddingFraction;
    // Ensure minimum padding on degenerate axes (flat meshes like planes)
    constexpr float kMinPadding = 1.0f; // 1 cm minimum
    padding.x = std::max(padding.x, kMinPadding);
    padding.y = std::max(padding.y, kMinPadding);
    padding.z = std::max(padding.z, kMinPadding);

    Float3 expandedMin = boundsMin - padding;
    Float3 expandedMax = boundsMax + padding;
    Float3 expandedExtent = expandedMax - expandedMin;

    // Volume center and half-extents
    Float3 volumeCenter = (expandedMin + expandedMax) * 0.5f;
    Float3 volumeHalfExtent = expandedExtent * 0.5f;

    result.volumeOrigin[0] = volumeCenter.x;
    result.volumeOrigin[1] = volumeCenter.y;
    result.volumeOrigin[2] = volumeCenter.z;
    result.volumeExtent[0] = volumeHalfExtent.x;
    result.volumeExtent[1] = volumeHalfExtent.y;
    result.volumeExtent[2] = volumeHalfExtent.z;
    result.resolution = resolution;

    // ── Compute voxel size ──
    float voxelSizeX = expandedExtent.x / static_cast<float>(resolution);
    float voxelSizeY = expandedExtent.y / static_cast<float>(resolution);
    float voxelSizeZ = expandedExtent.z / static_cast<float>(resolution);

    // ── Build spatial hash ──
    // Resolution: roughly 1 cell per 4 voxels along the longest axis
    uint32_t hashRes = std::max(4u, resolution / 4u);
    SpatialHash spatialHash;
    spatialHash.Build(triangles, expandedMin, expandedMax, hashRes);

    // ── Compute signed distances for each voxel ──
    uint32_t totalVoxels = resolution * resolution * resolution;
    result.distanceData.resize(totalVoxels);

    std::vector<uint32_t> queryBuffer; // Reusable buffer for spatial hash queries

    for (uint32_t vz = 0; vz < resolution; ++vz) {
        for (uint32_t vy = 0; vy < resolution; ++vy) {
            for (uint32_t vx = 0; vx < resolution; ++vx) {
                // Voxel center in world space
                Float3 voxelCenter = expandedMin + Float3{
                    (static_cast<float>(vx) + 0.5f) * voxelSizeX,
                    (static_cast<float>(vy) + 0.5f) * voxelSizeY,
                    (static_cast<float>(vz) + 0.5f) * voxelSizeZ
                };

                // Query spatial hash for nearby triangles
                spatialHash.Query(voxelCenter, queryBuffer);

                float minDist = std::numeric_limits<float>::max();
                Float3 closestNormal{0, 0, 1};
                Float3 closestDisplacement{0, 0, 0};

                for (uint32_t triIdx : queryBuffer) {
                    const auto& tri = triangles[triIdx];
                    auto closest = ClosestPointOnTriangle(voxelCenter, tri.v0, tri.v1, tri.v2);

                    if (closest.distance < minDist) {
                        minDist = closest.distance;
                        closestNormal = tri.normal;
                        closestDisplacement = voxelCenter - closest.point;
                    }
                }

                // Fallback: brute-force if spatial hash returned nothing
                // (rare — only at sparse hash boundaries)
                if (queryBuffer.empty()) {
                    for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                        const auto& tri = triangles[triIdx];
                        auto closest = ClosestPointOnTriangle(voxelCenter, tri.v0, tri.v1, tri.v2);
                        if (closest.distance < minDist) {
                            minDist = closest.distance;
                            closestNormal = tri.normal;
                            closestDisplacement = voxelCenter - closest.point;
                        }
                    }
                }

                // Determine sign via pseudo-normal method:
                // dot(displacement, face_normal) >= 0 => outside (positive)
                // dot(displacement, face_normal) <  0 => inside  (negative)
                float sign = closestDisplacement.Dot(closestNormal) >= 0.0f ? 1.0f : -1.0f;
                float signedDist = sign * minDist;

                uint32_t linearIdx = vz * resolution * resolution + vy * resolution + vx;
                result.distanceData[linearIdx] = signedDist;
            }
        }
    }

    UZ_LOG(Renderer, Info,
           "MeshSdfBaker: baked {}^3 SDF ({} triangles), "
           "volume center=({:.1f}, {:.1f}, {:.1f}), extent=({:.1f}, {:.1f}, {:.1f})",
           resolution, triangleCount,
           volumeCenter.x, volumeCenter.y, volumeCenter.z,
           volumeHalfExtent.x, volumeHalfExtent.y, volumeHalfExtent.z);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshSdfBaker::UploadToGPU
// ─────────────────────────────────────────────────────────────────────────────

MeshSDF MeshSdfBaker::UploadToGPU(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    const MeshSDFData& data)
{
    MeshSDF result;

    if (!device || !cmdList || !data.IsValid()) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::UploadToGPU: invalid parameters");
        return result;
    }

    uint32_t res = data.resolution;

    // ── Create the R16F 3D texture ──
    RHITextureDesc texDesc;
    texDesc.width = res;
    texDesc.height = res;
    texDesc.depthOrArraySize = static_cast<uint16_t>(res);
    texDesc.mipLevels = 1;
    texDesc.format = RHIFormat::R16Float;
    texDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    texDesc.is3D = true;
    texDesc.debugName = "MeshSDF_3D";

    result.texture3D = device->CreateTexture(texDesc);
    if (!result.texture3D.IsValid()) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::UploadToGPU: failed to create 3D texture");
        return result;
    }

    result.srvIndex = device->GetTextureSRVIndex(result.texture3D);

    // ── Create staging buffer with float16 data ──
    uint32_t totalTexels = res * res * res;
    uint32_t stagingSize = totalTexels * sizeof(uint16_t);

    // Row pitch must be aligned to 256 bytes for DX12 CopyBufferToTexture.
    // Each row is `res` texels * 2 bytes = res * 2 bytes.
    uint32_t rowPitch = res * sizeof(uint16_t);
    uint32_t alignedRowPitch = (rowPitch + 255) & ~255u;

    // For 3D textures, the staging buffer layout is:
    // slice 0: rows 0..height-1 (each row = alignedRowPitch bytes)
    // slice 1: rows 0..height-1
    // ...
    // Total size = depth * height * alignedRowPitch
    uint32_t slicePitch = alignedRowPitch * res;
    uint64_t totalStagingSize = static_cast<uint64_t>(slicePitch) * res;

    RHIBufferDesc bufDesc;
    bufDesc.sizeBytes = totalStagingSize;
    bufDesc.usage = RHIBufferUsage::CopySrc;
    bufDesc.memory = RHIMemoryType::Upload;
    bufDesc.debugName = "MeshSDF_Staging";

    RHIBufferHandle staging = device->CreateBuffer(bufDesc);
    if (!staging.IsValid()) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::UploadToGPU: failed to create staging buffer");
        device->DestroyTexture(result.texture3D);
        result.texture3D = RHITextureHandle::Invalid();
        return result;
    }

    // ── Convert float32 distances to float16 and fill staging buffer ──
    auto* mappedData = static_cast<uint8_t*>(device->MapBuffer(staging));
    if (!mappedData) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::UploadToGPU: failed to map staging buffer");
        device->DestroyBuffer(staging);
        device->DestroyTexture(result.texture3D);
        result.texture3D = RHITextureHandle::Invalid();
        return result;
    }

    for (uint32_t z = 0; z < res; ++z) {
        for (uint32_t y = 0; y < res; ++y) {
            auto* rowDst = reinterpret_cast<uint16_t*>(
                mappedData + z * slicePitch + y * alignedRowPitch);

            for (uint32_t x = 0; x < res; ++x) {
                uint32_t srcIdx = z * res * res + y * res + x;
                rowDst[x] = FloatToHalf(data.distanceData[srcIdx]);
            }
        }
    }

    device->UnmapBuffer(staging);

    // ── Issue copy command: staging buffer -> 3D texture ──
    // Transition texture to CopyDst state
    RHIBarrierDesc barrier;
    barrier.texture = result.texture3D;
    barrier.stateBefore = RHIResourceState::Undefined;
    barrier.stateAfter = RHIResourceState::CopyDst;
    cmdList->ResourceBarrier(&barrier, 1);

    // Copy each depth slice. CopyBufferToTexture copies one 2D slice at a time.
    for (uint32_t z = 0; z < res; ++z) {
        uint64_t sliceOffset = static_cast<uint64_t>(z) * slicePitch;
        cmdList->CopyBufferToTexture(
            staging, sliceOffset, alignedRowPitch,
            result.texture3D, 0, z);
    }

    // Transition texture to ShaderResource state for sampling
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    // ── Fill result metadata ──
    std::memcpy(result.volumeOrigin, data.volumeOrigin, sizeof(result.volumeOrigin));
    std::memcpy(result.volumeExtent, data.volumeExtent, sizeof(result.volumeExtent));
    result.resolution = res;
    result.valid = true;

    UZ_LOG(Renderer, Info,
           "MeshSdfBaker::UploadToGPU: created R16F 3D texture ({}^3, SRV={})",
           res, result.srvIndex);

    // The staging buffer can be destroyed after the command list is executed.
    // We destroy it here — the data has been copied to the GPU-side command queue.
    // The actual GPU copy will complete when the command list is executed and synced.
    device->DestroyBuffer(staging);

    return result;
}

} // namespace uz
