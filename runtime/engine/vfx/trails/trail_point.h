#pragma once

/// trail_point.h — Trail sample and fixed-capacity ring buffer for ribbon VFX.
///
/// Defines two types:
/// - `TrailPoint`: a single world-space sample along a ribbon trail, carrying
///   position, tangent direction, width, color, age, and accumulated distance.
/// - `TrailPointBuffer`: a fixed-capacity ring buffer of TrailPoints. The head
///   holds the newest point (index 0), and the tail holds the oldest. When full,
///   new points overwrite the oldest automatically.
///
/// # Design Rationale
/// Using a ring buffer avoids heap reallocation on every point addition. The
/// capacity is set once at construction (from `TrailComponent::maxTrailPoints`)
/// and never changes, making memory behaviour predictable and cache-friendly.
///
/// # Thread Safety
/// Not thread-safe. All methods must be called on the main thread.

#include "vector3.h"
#include "vector4.h"

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TrailPoint — single sample along a ribbon trail
// ─────────────────────────────────────────────────────────────────────────────

/// A single positional sample along a trail ribbon.
///
/// Points are created by `TrailComponent::Tick()` each time the owning entity
/// moves far enough from the previous point. The mesh builder consumes an
/// ordered span of TrailPoints (head = newest, tail = oldest) to generate a
/// camera-facing ribbon quad strip.
///
/// # Field Semantics
/// - `direction`       Normalized world-space tangent. Points from the previous
///                     point toward this one (or the entity's forward vector for
///                     the head). Used by the mesh builder to orient ribbon quads.
/// - `distanceFromHead` Monotonically increasing along the tail direction. Set
///                      to 0 for the head; updated each frame by `TrailComponent`.
///                      Used to compute normalized UV V coordinates.
struct TrailPoint {
    /// World-space position of this sample.
    Vec3 position;

    /// Normalized world-space tangent direction at this sample.
    /// Computed as (position - prevPosition).Normalized(). Identity fallback
    /// (0,1,0) is used when the emitter is stationary.
    Vec3 direction;

    /// Cross-section width of the ribbon at this sample (metres).
    float width;

    /// RGBA color at this sample. Alpha is used for fade-out.
    Vec4 color;

    /// Time in seconds since this point was emitted. Advanced each frame by
    /// `TrailComponent::Tick()`. Points are removed when age > pointLifetime.
    float age;

    /// Accumulated arc-length distance from the head (newest) point (metres).
    /// 0 at the head; increases toward the tail. Recalculated each frame by
    /// `TrailComponent::Tick()` after adding/removing points.
    float distanceFromHead;
};

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer — fixed-capacity ring buffer of TrailPoints
// ─────────────────────────────────────────────────────────────────────────────

/// Fixed-capacity ring buffer of TrailPoints for ribbon trail VFX.
///
/// Points are stored in a flat `std::vector<TrailPoint>` allocated at
/// construction. Head and tail are tracked via logical indices that wrap around
/// using modular arithmetic. Index 0 (via `GetPoint`) always refers to the
/// newest point; index `Count()-1` refers to the oldest.
///
/// The physical storage layout is NOT necessarily contiguous from head to tail.
/// Use `GetOrderedSpan()` only when the buffer has NOT wrapped around;
/// otherwise iterate via `GetPoint(i)` for safe ordered access. For rendering,
/// `CopyOrdered()` provides a fully contiguous snapshot.
///
/// # Capacity
/// Set at construction and immutable. When full, `AddPoint` silently overwrites
/// the oldest (tail) entry — the tail advances automatically.
///
/// # Example
/// ```cpp
/// TrailPointBuffer buf(64);
/// buf.AddPoint(point);
/// for (uint32_t i = 0; i < buf.Count(); ++i) {
///     const TrailPoint& p = buf.GetPoint(i);  // 0 = newest
/// }
/// ```
class TrailPointBuffer {
public:
    // ── Construction ─────────────────────────────────────────────────────────

    /// Constructs a ring buffer with the given fixed capacity.
    ///
    /// Allocates `capacity` slots in the internal storage vector. If
    /// `capacity` is 0, the buffer is permanently empty (all operations
    /// are no-ops or return defaults).
    ///
    /// # Parameters
    /// - `capacity`: Maximum number of TrailPoints that can be alive
    ///               simultaneously. Typical values: 16–512.
    explicit TrailPointBuffer(uint32_t capacity);

    // ── Modification ─────────────────────────────────────────────────────────

    /// Adds a new point at the head (newest slot) of the buffer.
    ///
    /// If the buffer is full, the oldest (tail) point is silently overwritten
    /// and the tail index advances. This matches the expected trail behaviour:
    /// old points at the tail are the first to be dropped.
    ///
    /// # Parameters
    /// - `point`: The TrailPoint to add. Copied into the internal storage.
    ///
    /// # Side Effects
    /// - Increments `count_` (clamped to `capacity_`).
    /// - Advances `head_` (wraps around).
    /// - Advances `tail_` when the buffer was already full.
    void AddPoint(const TrailPoint& point);

    /// Removes the oldest (tail) point from the buffer.
    ///
    /// No-op if the buffer is empty. Used by `TrailComponent::Tick()` when
    /// a tail point exceeds its lifetime.
    ///
    /// # Side Effects
    /// - Decrements `count_`.
    /// - Advances `tail_` (wraps around).
    void RemoveOldest();

    /// Removes all points without changing the capacity.
    ///
    /// Resets head, tail, and count to zero. Does not free or reallocate
    /// the internal storage vector.
    void Clear() noexcept;

    // ── Accessors ─────────────────────────────────────────────────────────────

    /// Returns a const reference to the point at logical index `index`.
    ///
    /// Logical index 0 is the **newest** (head) point; logical index
    /// `Count()-1` is the **oldest** (tail) point. This ordering is consistent
    /// regardless of how the internal ring buffer has wrapped.
    ///
    /// # Parameters
    /// - `index`: Logical position in [0, Count()-1].
    ///
    /// # Returns
    /// A const reference to the requested TrailPoint. The reference remains
    /// valid until the next call to `AddPoint`, `RemoveOldest`, or `Clear`.
    ///
    /// # Preconditions
    /// - `index < Count()` (asserted in debug builds).
    [[nodiscard]] const TrailPoint& GetPoint(uint32_t index) const;

    /// Returns a mutable reference to the point at logical index `index`.
    ///
    /// Use to update `age` and `distanceFromHead` in-place during `Tick()`
    /// without copying points out of and back into the buffer.
    ///
    /// # Parameters
    /// - `index`: Logical position in [0, Count()-1].
    ///
    /// # Preconditions
    /// - `index < Count()` (asserted in debug builds).
    [[nodiscard]] TrailPoint& GetPoint(uint32_t index);

    /// Copies all active points into `out` in newest-to-oldest order.
    ///
    /// Provides a contiguous, ordered snapshot for cases where the caller
    /// needs a flat array (e.g., for the mesh builder or debug rendering).
    /// `out` is resized to `Count()` before filling.
    ///
    /// # Parameters
    /// - `out`: Destination vector. Resized and overwritten.
    ///
    /// # Returns
    /// The number of points written (same as `Count()`).
    uint32_t CopyOrdered(std::vector<TrailPoint>& out) const;

    // ── Capacity & State Queries ───────────────────────────────────────────────

    /// Returns the number of active points currently stored.
    ///
    /// Always in [0, Capacity()].
    [[nodiscard]] uint32_t Count() const noexcept;

    /// Returns the fixed capacity set at construction.
    [[nodiscard]] uint32_t Capacity() const noexcept;

    /// Returns true if no points are currently stored.
    [[nodiscard]] bool IsEmpty() const noexcept;

    /// Returns true if the buffer holds `Capacity()` points.
    ///
    /// The next `AddPoint` call will overwrite the oldest point.
    [[nodiscard]] bool IsFull() const noexcept;

private:
    // ── Internal Helpers ──────────────────────────────────────────────────────

    /// Maps a logical index (0 = newest) to a physical storage index.
    ///
    /// The head_ slot holds the most recently written point. Physical index
    /// is computed by walking backward from head_ in the ring:
    ///   physical = (head_ + capacity_ - 1 - logicalIndex) % capacity_
    ///
    /// # Parameters
    /// - `logicalIndex`: Validated to be in [0, count_-1] by callers.
    [[nodiscard]] uint32_t ToPhysicalIndex(uint32_t logicalIndex) const noexcept;

    // ── Storage ───────────────────────────────────────────────────────────────

    std::vector<TrailPoint> storage_;  ///< Pre-allocated flat storage.
    uint32_t                capacity_; ///< Immutable capacity (== storage_.size()).
    uint32_t                head_;     ///< Physical index of the next write slot.
    uint32_t                tail_;     ///< Physical index of the oldest point.
    uint32_t                count_;    ///< Number of active points.
};

} // namespace uz
