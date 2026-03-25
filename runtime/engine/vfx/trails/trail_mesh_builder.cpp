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
    float totalDistance
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

    const uint32_t lastIndex = pointCount - 1u;
    const float safeDistance = std::max(totalDistance, kMinTotalDistance);

    TrailMeshData mesh;
    // Each point produces one left vertex and one right vertex.
    mesh.vertices.reserve(static_cast<size_t>(pointCount) * 2u);
    // Each segment (pair of adjacent points) produces 2 triangles = 6 indices.
    mesh.indices.reserve(static_cast<size_t>(lastIndex) * 6u);

    // ── Vertex generation ──────────────────────────────────────────────────
    // Walk head-to-tail (index 0 = newest = t=0), emitting left/right pairs.
    for (uint32_t i = 0; i < pointCount; ++i) {
        const Vec3& pointPos = points[i].position;

        // Normalized arc-length parameter: 0 at head, 1 at tail.
        const float t = points[i].distanceFromHead / safeDistance;

        const Vec3 tangent  = ComputeTangent(points, i, lastIndex);
        const Vec3 toCamera = (cameraPosition - pointPos).normalize();
        const Vec3 right    = ComputeRight(tangent, toCamera);

        const float halfWidth = std::max(0.0f, widthStart + (widthEnd - widthStart) * t) * 0.5f;
        const Vec4  color     = LerpColor(colorStart, colorEnd, t);

        // Left vertex: u=0, ribbon left edge.
        TrailVertex& leftVert  = mesh.vertices.emplace_back();
        leftVert.position = pointPos - right * halfWidth;
        leftVert.u        = 0.0f;
        leftVert.v        = t;
        leftVert.color    = color;

        // Right vertex: u=1, ribbon right edge.
        TrailVertex& rightVert = mesh.vertices.emplace_back();
        rightVert.position = pointPos + right * halfWidth;
        rightVert.u        = 1.0f;
        rightVert.v        = t;
        rightVert.color    = color;
    }

    // ── Index generation ───────────────────────────────────────────────────
    // For segment i (connecting points i and i+1):
    //   base   = i * 2       → left  vertex of point i
    //   base+1 = i * 2 + 1   → right vertex of point i
    //   base+2 = (i+1) * 2   → left  vertex of point i+1
    //   base+3 = (i+1) * 2 + 1 → right vertex of point i+1
    //
    // Counter-clockwise winding (left-handed front face):
    //   Triangle 1: base, base+1, base+2  (lower-left, lower-right, upper-left)
    //   Triangle 2: base+1, base+3, base+2 (lower-right, upper-right, upper-left)
    for (uint32_t i = 0; i < lastIndex; ++i) {
        const uint32_t base = i * 2u;

        // Triangle 1 (bottom-left half of quad)
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1u);
        mesh.indices.push_back(base + 2u);

        // Triangle 2 (top-right half of quad)
        mesh.indices.push_back(base + 1u);
        mesh.indices.push_back(base + 3u);
        mesh.indices.push_back(base + 2u);
    }

    return mesh;
}

} // namespace uz
