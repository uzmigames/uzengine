/// TrailMeshBuilder implementation -- camera-facing ribbon geometry from trail points.

#include "trail_mesh_builder.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <cmath>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TrailMeshData -- query methods and clear
// ─────────────────────────────────────────────────────────────────────────────

bool TrailMeshData::IsEmpty() const noexcept {
    return vertices.empty();
}

uint32_t TrailMeshData::GetVertexCount() const noexcept {
    return static_cast<uint32_t>(vertices.size());
}

uint32_t TrailMeshData::GetIndexCount() const noexcept {
    return static_cast<uint32_t>(indices.size());
}

uint32_t TrailMeshData::GetTriangleCount() const noexcept {
    return GetIndexCount() / 3u;
}

void TrailMeshData::Clear() noexcept {
    vertices.clear();
    indices.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Minimum length-squared threshold below which a vector is considered
/// degenerate (zero or near-zero). Avoids normalizing near-zero vectors.
constexpr float kDegenerateLengthSq = 1e-8f;

/// Minimum total distance to prevent division by zero when normalizing UV V.
constexpr float kMinTotalDistance = 0.0001f;

/// Linearly interpolates between two Vec4 values by scalar t in [0,1].
[[nodiscard]] Vec4 LerpColor(const Vec4& a, const Vec4& b, float t) noexcept {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
    };
}

/// Computes the tangent direction at point `i` in the trail.
///
/// Uses a three-point central-difference for interior points for smoother
/// ribbons. Forward-difference at the head and backward-difference at the
/// tail keep the tangent well-defined at the endpoints.
///
/// # Returns
/// Normalized tangent vector. Falls back to `Vec3::Forward()` if the
/// computed direction is degenerate (coincident points).
[[nodiscard]] Vec3 ComputeTangent(
    const TrailPoint* points,
    uint32_t i,
    uint32_t lastIndex
) noexcept {
    Vec3 dir;

    if (i == 0) {
        // Forward-difference: head tangent from first segment.
        dir = points[1].position - points[0].position;
    } else if (i == lastIndex) {
        // Backward-difference: tail tangent from last segment.
        dir = points[lastIndex].position - points[lastIndex - 1].position;
    } else {
        // Central-difference: smoother tangent across two segments.
        dir = points[i + 1].position - points[i - 1].position;
    }

    if (dir.lengthSq() < kDegenerateLengthSq) {
        // Coincident neighbours -- fall back to a stable world-axis direction.
        return Vec3::Forward();
    }

    return dir.normalize();
}

/// Computes the billboard right vector for a ribbon vertex.
///
/// The right vector is the cross product of the tangent and the
/// to-camera direction. When these are parallel (the camera is looking
/// exactly along the trail), falls back to crossing with world-up,
/// then world-forward as a final emergency fallback.
///
/// # Returns
/// Normalized right vector defining the ribbon's width axis at this point.
[[nodiscard]] Vec3 ComputeRight(
    const Vec3& tangent,
    const Vec3& toCamera
) noexcept {
    Vec3 right = tangent.Cross(toCamera);

    if (right.lengthSq() < kDegenerateLengthSq) {
        // Tangent is parallel to toCamera (camera is looking along the trail).
        // Use world-up as the secondary axis.
        right = tangent.Cross(Vec3::Up());

        if (right.lengthSq() < kDegenerateLengthSq) {
            // Tangent is also parallel to world-up (trail points straight up).
            // Use world-forward as the final fallback.
            right = tangent.Cross(Vec3::Forward());
        }
    }

    // Safe to normalize: at least one fallback always produces a non-zero result
    // unless tangent itself is zero, which ComputeTangent prevents.
    return right.normalize();
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TrailMeshBuilder::Build -- ribbon quad-strip generation
// ─────────────────────────────────────────────────────────────────────────────

TrailMeshData TrailMeshBuilder::Build(
    const TrailPoint* points,
    uint32_t pointCount,
    const Vec3& cameraPosition,
    float widthStart,
    float widthEnd,
    const Vec4& colorStart,
    const Vec4& colorEnd,
    float totalDistance,
    float pointLifetime,
    uint32_t ribbonStrands,
    float strandSpacing
) const {
    if (points == nullptr) {
        UZ_LOG(VFX, Warning, "TrailMeshBuilder::Build: points array is null");
        return {};
    }

    if (pointCount == 0) {
        UZ_LOG(VFX, Warning, "TrailMeshBuilder::Build: pointCount is 0");
        return {};
    }

    // Need at least 2 points to form a segment (one quad).
    if (pointCount < 2) {
        return {};
    }

    // Clamp strand count to at least 1.
    const uint32_t strandCount = std::max(ribbonStrands, 1u);

    const uint32_t lastIndex = pointCount - 1u;
    const float safeDistance = std::max(totalDistance, kMinTotalDistance);
    // Safe lifetime denominator for age normalization. When pointLifetime is
    // zero or negative, age-based modulation is disabled (ageFactor = 1.0).
    const float safeLifetime = std::max(pointLifetime, kMinTotalDistance);
    const bool  useAgeTaper  = (pointLifetime > 0.0f);

    TrailMeshData mesh;
    // Each point produces 2 vertices per strand (left + right).
    const size_t totalVertices = static_cast<size_t>(pointCount) * 2u * strandCount;
    // Each segment produces 6 indices per strand.
    const size_t totalIndices = static_cast<size_t>(lastIndex) * 6u * strandCount;
    mesh.vertices.reserve(totalVertices);
    mesh.indices.reserve(totalIndices);

    // ── Pre-compute per-point data shared across all strands ───────────────
    // Tangent, right, and up vectors are the same for all strands at a given
    // point — only the vertical offset differs. Computing them once avoids
    // redundant normalize() calls when strandCount > 1.

    // Stack-friendly scratch arrays for small trail counts; heap for large.
    constexpr uint32_t kStackThreshold = 256;
    Vec3 tangentStack[kStackThreshold];
    Vec3 rightStack[kStackThreshold];
    Vec3 upStack[kStackThreshold];
    float tStack[kStackThreshold];
    float ageFactorStack[kStackThreshold];
    float halfWidthStack[kStackThreshold];
    Vec4 colorStack[kStackThreshold];

    std::vector<Vec3> tangentHeap, rightHeap, upHeap;
    std::vector<float> tHeap, ageFactorHeap, halfWidthHeap;
    std::vector<Vec4> colorHeap;

    Vec3*  tangents;
    Vec3*  rights;
    Vec3*  ups;
    float* ts;
    float* ageFactors;
    float* halfWidths;
    Vec4*  colors;

    if (pointCount <= kStackThreshold) {
        tangents   = tangentStack;
        rights     = rightStack;
        ups        = upStack;
        ts         = tStack;
        ageFactors = ageFactorStack;
        halfWidths = halfWidthStack;
        colors     = colorStack;
    } else {
        tangentHeap.resize(pointCount);
        rightHeap.resize(pointCount);
        upHeap.resize(pointCount);
        tHeap.resize(pointCount);
        ageFactorHeap.resize(pointCount);
        halfWidthHeap.resize(pointCount);
        colorHeap.resize(pointCount);
        tangents   = tangentHeap.data();
        rights     = rightHeap.data();
        ups        = upHeap.data();
        ts         = tHeap.data();
        ageFactors = ageFactorHeap.data();
        halfWidths = halfWidthHeap.data();
        colors     = colorHeap.data();
    }

    for (uint32_t i = 0; i < pointCount; ++i) {
        const Vec3& pointPos = points[i].position;

        ts[i] = points[i].distanceFromHead / safeDistance;

        tangents[i] = ComputeTangent(points, i, lastIndex);
        const Vec3 toCamera = (cameraPosition - pointPos).normalize();
        rights[i]   = ComputeRight(tangents[i], toCamera);

        // Up vector: perpendicular to both tangent and right — defines the
        // offset axis for multi-strand ribbons.
        ups[i] = rights[i].Cross(tangents[i]);
        if (ups[i].lengthSq() < kDegenerateLengthSq) {
            ups[i] = Vec3::Up();
        } else {
            ups[i] = ups[i].normalize();
        }

        const float normalizedAge = useAgeTaper
            ? std::clamp(points[i].age / safeLifetime, 0.0f, 1.0f)
            : 0.0f;
        ageFactors[i] = 1.0f - normalizedAge;

        const float baseWidth = std::max(0.0f, widthStart + (widthEnd - widthStart) * ts[i]);
        halfWidths[i] = baseWidth * ageFactors[i] * 0.5f;

        const float colorParam = std::max(ts[i], normalizedAge);
        colors[i] = LerpColor(colorStart, colorEnd, colorParam);
    }

    // ── Generate vertices and indices for each strand ──────────────────────
    // For N strands, offsets are centered: strand s is offset by
    //   upOffset = (s - (N-1)/2) * strandSpacing
    // so a single strand (N=1) has zero offset, 2 strands are at ±spacing/2, etc.
    const float halfSpan = (strandCount - 1u) * strandSpacing * 0.5f;

    for (uint32_t s = 0; s < strandCount; ++s) {
        // Perpendicular offset for this strand (along up vector).
        const float strandOffset = (strandCount > 1)
            ? (static_cast<float>(s) * strandSpacing - halfSpan)
            : 0.0f;

        // Base vertex index for this strand's quad-strip.
        const uint32_t strandVertexBase = static_cast<uint32_t>(mesh.vertices.size());

        // ── Vertex generation for strand s ────────────────────────────────
        for (uint32_t i = 0; i < pointCount; ++i) {
            const Vec3& pointPos = points[i].position;
            const Vec3  offsetPos = pointPos + ups[i] * strandOffset;

            // Left vertex: u=0, ribbon left edge.
            TrailVertex& leftVert  = mesh.vertices.emplace_back();
            leftVert.position    = offsetPos - rights[i] * halfWidths[i];
            leftVert.u           = 0.0f;
            leftVert.v           = ts[i];
            leftVert.color       = colors[i];
            leftVert.strandIndex = s;

            // Right vertex: u=1, ribbon right edge.
            TrailVertex& rightVert = mesh.vertices.emplace_back();
            rightVert.position    = offsetPos + rights[i] * halfWidths[i];
            rightVert.u           = 1.0f;
            rightVert.v           = ts[i];
            rightVert.color       = colors[i];
            rightVert.strandIndex = s;
        }

        // ── Index generation for strand s ─────────────────────────────────
        // Each segment within this strand connects point i to point i+1.
        // Counter-clockwise winding (left-handed front face):
        //   Triangle 1: base, base+1, base+2
        //   Triangle 2: base+1, base+3, base+2
        for (uint32_t i = 0; i < lastIndex; ++i) {
            const uint32_t base = strandVertexBase + i * 2u;

            // Triangle 1 (bottom-left half of quad)
            mesh.indices.push_back(base);
            mesh.indices.push_back(base + 1u);
            mesh.indices.push_back(base + 2u);

            // Triangle 2 (top-right half of quad)
            mesh.indices.push_back(base + 1u);
            mesh.indices.push_back(base + 3u);
            mesh.indices.push_back(base + 2u);
        }
    }

    return mesh;
}

} // namespace uz
