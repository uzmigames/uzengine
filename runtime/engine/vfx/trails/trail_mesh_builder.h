#pragma once

/// TrailMeshBuilder -- camera-facing ribbon geometry generator for VFX trails.
///
/// Converts an ordered array of TrailPoints into a quad-strip ribbon mesh
/// suitable for direct GPU upload. Each pair of adjacent trail points produces
/// one quad (2 triangles, 4 vertices). The ribbon faces the camera by computing
/// a per-point right vector from `cross(tangent, toCamera)`, so the ribbon
/// always appears as a flat band regardless of viewing angle.
///
/// # Coordinate System
/// Operates in world space. Tangents and camera vectors are computed in world
/// space; the resulting vertex positions are also world-space.
///
/// # Thread Safety
/// `TrailMeshBuilder` is stateless -- `Build()` is a pure function and is safe
/// to call from any thread. `TrailMeshData` is not thread-safe; callers are
/// responsible for ownership.

#include "engine/vfx/trails/trail_point.h"
#include "vector3.h"
#include "vector4.h"

#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TrailVertex -- per-vertex layout for trail ribbon geometry (GPU-ready)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-vertex data for trail ribbon geometry.
///
/// Matches the GPU input layout declared in the trail vertex shader.
/// Total size is 40 bytes: position (12) + u (4) + v (4) + color (16) + strandIndex (4).
///
/// # UV Convention
/// - `u` runs across the ribbon width: 0.0 = left edge, 1.0 = right edge.
/// - `v` runs along the ribbon length: 0.0 = head (newest), 1.0 = tail (oldest).
///
/// # Multi-Strand
/// When `ribbonStrands > 1`, multiple parallel quad-strip ribbons are generated
/// from the same trail points. Each strand carries a unique `strandIndex`
/// (0-based) that the shader can use for per-strand color/width modulation.
///
/// # Color
/// RGBA linear color sampled from the trail gradient at parameter `t`, where
/// `t = distanceFromHead / totalDistance`. Alpha carries trail opacity.
struct TrailVertex {
    /// World-space position of this vertex. (12 bytes)
    Vec3 position;

    /// Texture U coordinate: 0 = left edge of ribbon, 1 = right edge. (4 bytes)
    float u;

    /// Texture V coordinate: normalized arc-length position along trail. (4 bytes)
    float v;

    /// Vertex color sampled from the trail gradient (RGBA linear). (16 bytes)
    Vec4 color;

    /// Zero-based index of the ribbon strand this vertex belongs to. (4 bytes)
    /// 0 for single-strand trails. Used by the shader for per-strand modulation.
    uint32_t strandIndex;
};

static_assert(sizeof(TrailVertex) == 40,
    "TrailVertex size mismatch -- GPU input layout expects 40 bytes.");

// ─────────────────────────────────────────────────────────────────────────────
// TrailMeshData -- generated ribbon geometry ready for GPU upload
// ─────────────────────────────────────────────────────────────────────────────

/// Generated ribbon geometry produced by `TrailMeshBuilder::Build()`.
///
/// Contains a flat vertex array and a 32-bit index array describing a quad
/// strip. The index winding is counter-clockwise (standard left-handed front
/// face). Upload both arrays to dynamic GPU buffers each frame.
///
/// # Layout
/// For a single strand (ribbonStrands=1), vertices are ordered left-right per
/// trail point: index `2*i` is the left vertex for point `i`, and `2*i+1` is
/// the right vertex. Indices form a contiguous quad strip.
///
/// For multi-strand (ribbonStrands=N), each strand is a complete quad-strip
/// appended sequentially. Strand `s` vertices start at offset `s * pointCount * 2`.
/// Strands are offset perpendicular to the ribbon plane (along the "up" vector).
///
/// # Lifetime
/// `TrailMeshData` is value-typed. `TrailMeshBuilder::Build()` returns a new
/// instance each call; callers own the returned data.
struct TrailMeshData {
    /// Flat array of ribbon vertices (left-right pairs, head-to-tail order).
    std::vector<TrailVertex> vertices;

    /// Triangle list indices into `vertices`. Always a multiple of 3.
    std::vector<uint32_t> indices;

    /// Returns true if the mesh has no geometry (fewer than 2 trail points
    /// were provided, so no quads could be generated).
    [[nodiscard]] bool IsEmpty() const noexcept;

    /// Returns the number of vertices in the ribbon mesh.
    ///
    /// For N trail points, this is `N * 2` (one left + one right vertex per point).
    [[nodiscard]] uint32_t GetVertexCount() const noexcept;

    /// Returns the number of indices in the ribbon mesh.
    ///
    /// For N trail points, this is `(N - 1) * 6` (2 triangles × 3 indices per segment).
    [[nodiscard]] uint32_t GetIndexCount() const noexcept;

    /// Returns the number of triangles in the ribbon mesh.
    ///
    /// Equals `GetIndexCount() / 3`.
    [[nodiscard]] uint32_t GetTriangleCount() const noexcept;

    /// Clears all vertices and indices, resetting to an empty mesh.
    ///
    /// Does not release underlying vector capacity (avoids re-allocation on reuse).
    void Clear() noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// TrailMeshBuilder -- stateless ribbon geometry generator
// ─────────────────────────────────────────────────────────────────────────────

/// Generates camera-facing ribbon geometry from an ordered array of TrailPoints.
///
/// For each pair of adjacent trail points, generates one quad (2 triangles,
/// 4 vertices). The ribbon faces the camera by computing a per-point right
/// vector from `cross(tangent, toCamera)`. When the tangent and toCamera are
/// nearly parallel (degenerate case), a world-up fallback is used to keep the
/// ribbon stable.
///
/// # Tangent Computation
/// - First point: `normalize(points[1] - points[0])` (forward-difference).
/// - Interior points: `normalize(points[i+1] - points[i-1])` (central-difference,
///   equivalent to a Catmull-Rom tangent without the scale factor).
/// - Last point: `normalize(points[N-1] - points[N-2])` (backward-difference).
///
/// # Width & Color Interpolation
/// Width and color are linearly interpolated along the trail using normalized
/// arc-length `t = distanceFromHead / totalDistance`.
///
/// # Usage
/// ```cpp
/// TrailMeshBuilder builder;
/// TrailMeshData mesh = builder.Build(
///     points.data(), static_cast<uint32_t>(points.size()),
///     cameraPos,
///     widthStart, widthEnd,
///     colorStart, colorEnd,
///     totalTrailDistance
/// );
/// // Upload mesh.vertices and mesh.indices to per-frame GPU dynamic buffers.
/// ```
class TrailMeshBuilder {
public:
    TrailMeshBuilder() = default;
    ~TrailMeshBuilder() = default;

    // Non-copyable -- stateless, so copies are harmless but semantically
    // meaningless. Prevent accidental copies for clarity.
    TrailMeshBuilder(const TrailMeshBuilder&) = delete;
    TrailMeshBuilder& operator=(const TrailMeshBuilder&) = delete;

    // Movable -- required by TrailSystem's defaulted move operations.
    // The builder is stateless so moves are trivially correct.
    TrailMeshBuilder(TrailMeshBuilder&&) noexcept = default;
    TrailMeshBuilder& operator=(TrailMeshBuilder&&) noexcept = default;

    /// Builds ribbon geometry from an ordered array of trail points.
    ///
    /// Iterates over `points[0..pointCount-1]` (head-to-tail order, index 0 is
    /// the newest point) and generates a camera-facing quad-strip ribbon mesh.
    ///
    /// Width and color are modulated by **normalized age** (`point.age /
    /// pointLifetime`), which gives a natural time-based taper and color shift:
    /// - Width: multiplied by `(1 - normalizedAge)`, tapering from full width
    ///   at age=0 (freshly emitted) to zero at age=pointLifetime (about to expire).
    /// - Color: interpolated from `colorStart` toward `colorEnd` as points age,
    ///   combined with the arc-length parameter `t` for smooth spatial gradient.
    ///
    /// # Parameters
    /// * `points` - Pointer to an array of TrailPoints in head-to-tail order.
    ///              Must be non-null and have at least `pointCount` elements.
    ///              Index 0 is the head (newest point, t=0).
    /// * `pointCount` - Number of points. Requires >= 2 to produce any geometry.
    /// * `cameraPosition` - World-space camera position used for billboard
    ///                      orientation. The ribbon's right vector is computed
    ///                      as `cross(tangent, normalize(cameraPosition - point))`.
    /// * `widthStart` - Full ribbon width in world units at the head (t=0).
    ///                  Must be >= 0.
    /// * `widthEnd` - Full ribbon width in world units at the tail (t=1).
    ///                Must be >= 0.
    /// * `colorStart` - RGBA color at the head (t=0). Linear color space.
    /// * `colorEnd` - RGBA color at the tail (t=1). Linear color space.
    /// * `totalDistance` - Total arc-length of the trail in world units.
    ///                     Used to normalize `distanceFromHead` into [0,1] UV V.
    ///                     Clamped to a minimum of 0.0001 internally to avoid
    ///                     divide-by-zero on zero-length trails.
    /// * `pointLifetime` - Maximum lifetime of trail points (seconds). Used to
    ///                     compute normalized age for width taper and color shift.
    ///                     Clamped to a minimum of 0.0001 internally to avoid
    ///                     divide-by-zero. Pass 0 or negative to disable
    ///                     age-based modulation (falls back to arc-length only).
    /// * `ribbonStrands` - Number of parallel ribbon strands (default 1).
    ///                     Each strand is a full quad-strip offset along the
    ///                     ribbon's up vector. Must be >= 1.
    /// * `strandSpacing` - World-space distance between adjacent strands (metres).
    ///                     Only meaningful when ribbonStrands > 1.
    ///
    /// # Returns
    /// A `TrailMeshData` with `pointCount * 2 * ribbonStrands` vertices and
    /// `(pointCount - 1) * 6 * ribbonStrands` indices, or an empty
    /// `TrailMeshData` if `pointCount < 2`.
    ///
    /// # Side Effects
    /// Logs `VFX::Warning` if `points` is null or `pointCount` is 0.
    [[nodiscard]] TrailMeshData Build(
        const TrailPoint* points,
        uint32_t pointCount,
        const Vec3& cameraPosition,
        float widthStart,
        float widthEnd,
        const Vec4& colorStart,
        const Vec4& colorEnd,
        float totalDistance,
        float pointLifetime,
        uint32_t ribbonStrands = 1,
        float strandSpacing = 0.1f
    ) const;
};

} // namespace uz
