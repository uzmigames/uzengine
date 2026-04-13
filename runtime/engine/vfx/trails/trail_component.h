#pragma once

/// trail_component.h — ECS component for ribbon/trail VFX effects.
///
/// Attach `TrailComponent` to any entity that should leave a ribbon trail.
/// Each frame, `Tick(dt, currentPosition)` samples the entity's position,
/// emits new `TrailPoint`s into the ring buffer when the entity has moved
/// far enough, ages all existing points, and culls expired ones from the tail.
///
/// # Lifecycle
/// 1. Construct `TrailComponent` with desired configuration (material, width,
///    lifetime, etc.). The ring buffer is sized by `maxTrailPoints` at
///    construction time via the `points` field.
/// 2. Each frame, call `Tick(dt, entityWorldPosition)` to advance the trail.
/// 3. Query `points` for the ordered set of trail samples (index 0 = newest).
/// 4. Call `Clear()` to reset state (e.g., on teleport or emitter re-use).
///
/// # Thread Safety
/// Not thread-safe. All methods must be called on the main thread.
///
/// # Example
/// ```cpp
/// TrailComponent trail;
/// trail.materialPath    = "materials/vfx/sword_trail.uzmat";
/// trail.maxTrailPoints  = 64;
/// trail.widthStart      = 0.4f;
/// trail.widthEnd        = 0.02f;
/// trail.pointLifetime   = 0.5f;
/// trail.minPointDistance = 0.05f;
///
/// // Each game frame:
/// trail.Tick(dt, entity.GetWorldPosition());
/// ```

#include "engine/vfx/trails/trail_point.h"
#include "vector3.h"
#include "vector4.h"

#include <cstdint>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TrailComponent — ECS component for ribbon/trail effects
// ─────────────────────────────────────────────────────────────────────────────

/// ECS component for ribbon/trail VFX effects.
///
/// Manages a ring buffer of world-space sample points (`TrailPointBuffer`),
/// each carrying position, tangent direction, width, color, age, and distance
/// from the head. The mesh builder (a separate system) reads the ordered point
/// list each frame to produce a camera-facing quad-strip mesh.
///
/// ## Point Emission
/// A new point is added whenever the entity's movement since `lastAddedPosition`
/// meets or exceeds `minPointDistance`. This threshold prevents degenerate
/// zero-length segments and keeps point density proportional to path curvature.
///
/// ## Point Aging & Culling
/// Every point's `age` is incremented by `dt` each frame. Tail points (oldest)
/// whose `age > pointLifetime` are removed via `TrailPointBuffer::RemoveOldest`.
/// Removal happens after aging so the last visible frame of a dying point is
/// still rendered.
///
/// ## UV Mapping
/// `distanceFromHead` is recalculated each frame after all add/remove
/// operations. The mesh builder divides each point's `distanceFromHead` by
/// `GetTotalLength()` to derive a normalized V coordinate for texture mapping.
struct TrailComponent {
    // ── Configuration ─────────────────────────────────────────────────────────

    /// Path to the ribbon material asset.
    /// Resolved through the engine's virtual filesystem at runtime.
    std::string materialPath;

    /// Maximum number of simultaneous live trail points.
    ///
    /// Determines the ring buffer capacity allocated at startup. Increasing
    /// this value also increases memory usage proportionally. Typical values:
    /// 16 (short sparks) to 512 (long environmental trails).
    ///
    /// # Note
    /// Changing this field after the component is initialized has no effect;
    /// the buffer capacity is fixed at construction.
    uint32_t maxTrailPoints = 100;

    /// Cross-section width of the ribbon at the head (newest) point (metres).
    float widthStart = 0.5f;

    /// Cross-section width of the ribbon at the tail (oldest) point (metres).
    float widthEnd = 0.05f;

    /// Scroll speed of the V texture coordinate along the ribbon's length
    /// (UV units per second). 0 = static UV; positive values scroll toward
    /// the tail; negative values scroll toward the head.
    float uvScrollRate = 0.0f;

    /// Maximum lifetime of a single trail point (seconds).
    ///
    /// Points are removed from the tail once their `age` exceeds this value.
    /// A shorter lifetime produces a shorter, more transient trail.
    float pointLifetime = 2.0f;

    /// Minimum world-space distance (metres) the entity must travel before a
    /// new point is emitted.
    ///
    /// Lower values produce smoother ribbons but emit more points per second.
    /// Values smaller than ~0.001 may cause degenerate zero-length segments
    /// and should be avoided.
    float minPointDistance = 0.1f;

    /// RGBA color of the ribbon at the head (newest) point.
    Vec4 colorStart{1.0f, 1.0f, 1.0f, 1.0f};

    /// RGBA color of the ribbon at the tail (oldest) point.
    /// Typically set to a transparent value so the trail fades out.
    Vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};

    /// Number of parallel ribbon strands generated from the same trail points.
    ///
    /// When set to 1 (default), produces a single flat ribbon. Values > 1
    /// produce N parallel quad-strip ribbons offset perpendicular to the
    /// camera-facing plane, approximating a tube or wing shape. Each strand
    /// receives a unique `strandIndex` in the vertex data for per-strand
    /// color/width modulation in the shader.
    ///
    /// @ref Latta 2004 "Building an Advanced Particle System" — multi-strand
    ///      ribbon geometry for volumetric trail effects.
    ///
    /// # Note
    /// Vertex count scales linearly with strand count: `pointCount * 2 * ribbonStrands`.
    /// Keep strand count low (2–4) for real-time use.
    uint32_t ribbonStrands = 1;

    /// World-space spacing between adjacent ribbon strands (metres).
    ///
    /// Controls how far apart parallel strands are offset along the ribbon's
    /// up vector (perpendicular to both the tangent and camera-facing right).
    /// Only meaningful when `ribbonStrands > 1`. A value of 0 collapses all
    /// strands into a single overlapping ribbon.
    float strandSpacing = 0.1f;

    /// Render sort priority. Higher values draw on top of lower values within
    /// the VFX render pass. Trails with equal sort order have undefined
    /// relative ordering.
    int32_t sortOrder = 0;

    /// Whether this trail should emit new points and be rendered.
    ///
    /// When set to `false`, `Tick()` is a no-op. Existing points are not
    /// aged or culled — use `Clear()` for a full reset.
    bool isActive = true;

    // ── Runtime State (managed by TrailSystem — do not modify directly) ───────

    /// Ring buffer of active trail points.
    ///
    /// Sized to `maxTrailPoints` at construction. Index 0 is the newest
    /// (head); index `Count()-1` is the oldest (tail). Updated each frame
    /// by `Tick()`.
    TrailPointBuffer points;

    /// World-space position of the last emitted point.
    ///
    /// Used to compute movement distance each frame. Initialized to an
    /// out-of-range sentinel so the very first call to `Tick()` always
    /// emits a point regardless of `minPointDistance`.
    Vec3 lastAddedPosition{0.0f, 0.0f, 0.0f};

    /// Accumulated arc-length of the entire live trail (metres).
    ///
    /// Equals `points.GetPoint(points.Count()-1).distanceFromHead` when
    /// the buffer is non-empty, or 0 when empty. Updated each frame by
    /// `Tick()` after aging and culling.
    float totalDistance = 0.0f;

    // ── Methods ───────────────────────────────────────────────────────────────

    /// Constructs the component and allocates the ring buffer.
    ///
    /// Sizes `points` to `maxTrailPoints`. All other fields use their
    /// default-initialized values. Call after setting `maxTrailPoints` if
    /// you want a non-default capacity.
    TrailComponent();

    /// Advances the trail simulation by one frame.
    ///
    /// Performs the following steps in order:
    /// 1. Returns immediately if `!isActive`.
    /// 2. Computes the distance from `lastAddedPosition` to `currentPosition`.
    /// 3. If the buffer is empty **or** distance >= `minPointDistance`:
    ///    - Emits a new `TrailPoint` at `currentPosition` with `age=0`,
    ///      `width=widthStart`, `color=colorStart`, and `direction` set to
    ///      the normalized movement vector (or `Vec3(0,1,0)` when stationary).
    ///    - Updates `lastAddedPosition`.
    /// 4. Ages all points: `point.age += dt`.
    /// 5. Removes tail points whose `age > pointLifetime`.
    /// 6. Recalculates `distanceFromHead` for all remaining points by summing
    ///    consecutive distances from head toward tail.
    /// 7. Updates `totalDistance` to the tail point's `distanceFromHead`,
    ///    or 0 if the buffer is empty.
    ///
    /// # Parameters
    /// - `dt`:              Frame delta time in seconds. Must be >= 0.
    /// - `currentPosition`: World-space position of the owning entity this frame.
    ///
    /// # Side Effects
    /// - Modifies `points`, `lastAddedPosition`, and `totalDistance`.
    void Tick(float dt, const Vec3& currentPosition);

    /// Resets the trail to its empty initial state.
    ///
    /// Clears the ring buffer, resets `totalDistance` to 0, and marks
    /// `lastAddedPosition` as uninitialized so the next `Tick()` call
    /// emits a fresh first point regardless of distance.
    ///
    /// # Side Effects
    /// - Calls `points.Clear()`.
    /// - Resets `totalDistance` and `lastAddedPosition`.
    void Clear();

    // ── Queries ───────────────────────────────────────────────────────────────

    /// Returns true if the ring buffer holds no active points.
    [[nodiscard]] bool IsEmpty() const;

    /// Returns the number of currently active trail points.
    [[nodiscard]] uint32_t GetActivePointCount() const;

    /// Returns a const reference to the underlying ring buffer of trail points.
    ///
    /// Provides direct access to the ordered point data for systems such as
    /// `TrailMeshBuilder` and `TrailSystem` that need to iterate or inspect
    /// points without going through the component's higher-level API.
    ///
    /// # Returns
    /// Const reference to `points`. Valid for the lifetime of this component.
    [[nodiscard]] const TrailPointBuffer& GetPoints() const noexcept;

    /// Returns the total arc-length of the live trail (metres).
    ///
    /// Returns 0 when the trail is empty or has only one point.
    [[nodiscard]] float GetTotalLength() const;

    /// Interpolates the ribbon color at a normalized position along the trail.
    ///
    /// Linearly interpolates between `colorStart` and `colorEnd`.
    ///
    /// # Parameters
    /// - `t`: Normalized position. 0 = head (newest), 1 = tail (oldest).
    ///        Values outside [0,1] are clamped.
    ///
    /// # Returns
    /// Interpolated RGBA color.
    [[nodiscard]] Vec4 GetColorAtNormalized(float t) const;

    /// Interpolates the ribbon width at a normalized position along the trail.
    ///
    /// Linearly interpolates between `widthStart` and `widthEnd`.
    ///
    /// # Parameters
    /// - `t`: Normalized position. 0 = head (newest), 1 = tail (oldest).
    ///        Values outside [0,1] are clamped.
    ///
    /// # Returns
    /// Interpolated width in metres.
    [[nodiscard]] float GetWidthAtNormalized(float t) const;

private:
    // ── Internal Helpers ──────────────────────────────────────────────────────

    /// Returns true if `lastAddedPosition` has been set by at least one
    /// successful `AddPoint` call (i.e., the first-point sentinel is cleared).
    bool hasFirstPoint_ = false;

    /// Recomputes `distanceFromHead` for all points and updates `totalDistance`.
    ///
    /// Iterates from index 0 (head) to `Count()-1` (tail), summing the
    /// Euclidean distance between consecutive positions. Sets the head point's
    /// `distanceFromHead` to 0.0f.
    void RecalculateDistances();
};

} // namespace uz
