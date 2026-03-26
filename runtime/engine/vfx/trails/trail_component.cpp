/// trail_component.cpp — TrailPointBuffer ring buffer and TrailComponent methods.
///
/// Implements the ring buffer index arithmetic and the per-frame trail
/// simulation (emit, age, cull, recalculate distances).

#include "engine/vfx/trails/trail_component.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer — construction
// ─────────────────────────────────────────────────────────────────────────────

TrailPointBuffer::TrailPointBuffer(uint32_t capacity)
    : storage_(capacity)
    , capacity_(capacity)
    , head_(0)
    , tail_(0)
    , count_(0)
{
    // Pre-allocated storage size is exactly capacity_ so no reallocation
    // will occur during the component's lifetime.
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer::ToPhysicalIndex — logical-to-physical ring mapping
// ─────────────────────────────────────────────────────────────────────────────

uint32_t TrailPointBuffer::ToPhysicalIndex(uint32_t logicalIndex) const noexcept {
    // head_ points to the next write slot, so the most recently written
    // physical index is (head_ + capacity_ - 1) % capacity_.
    // Each subsequent logical index steps one slot toward the tail.
    return (head_ + capacity_ - 1u - logicalIndex) % capacity_;
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer::AddPoint — insert newest point at head
// ─────────────────────────────────────────────────────────────────────────────

void TrailPointBuffer::AddPoint(const TrailPoint& point) {
    if (capacity_ == 0) {
        return;
    }

    storage_[head_] = point;
    head_ = (head_ + 1u) % capacity_;

    if (count_ < capacity_) {
        ++count_;
    } else {
        // Buffer is full: the write has overwritten the oldest slot, so the
        // tail must advance to remain at the true oldest live point.
        tail_ = (tail_ + 1u) % capacity_;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer::RemoveOldest — discard tail point
// ─────────────────────────────────────────────────────────────────────────────

void TrailPointBuffer::RemoveOldest() {
    if (count_ == 0) {
        return;
    }

    tail_ = (tail_ + 1u) % capacity_;
    --count_;

    // When the last point is removed, reset head_ so that the next AddPoint
    // always writes to slot 0, keeping addresses stable for debugging.
    if (count_ == 0) {
        head_ = 0;
        tail_ = 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer::Clear — reset without reallocation
// ─────────────────────────────────────────────────────────────────────────────

void TrailPointBuffer::Clear() noexcept {
    head_  = 0;
    tail_  = 0;
    count_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer::GetPoint — ordered indexed access
// ─────────────────────────────────────────────────────────────────────────────

const TrailPoint& TrailPointBuffer::GetPoint(uint32_t index) const {
    assert(index < count_ && "TrailPointBuffer::GetPoint: index out of range");
    return storage_[ToPhysicalIndex(index)];
}

TrailPoint& TrailPointBuffer::GetPoint(uint32_t index) {
    assert(index < count_ && "TrailPointBuffer::GetPoint: index out of range");
    return storage_[ToPhysicalIndex(index)];
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer::CopyOrdered — contiguous snapshot for mesh builder
// ─────────────────────────────────────────────────────────────────────────────

uint32_t TrailPointBuffer::CopyOrdered(std::vector<TrailPoint>& out) const {
    out.resize(count_);
    for (uint32_t i = 0; i < count_; ++i) {
        out[i] = storage_[ToPhysicalIndex(i)];
    }
    return count_;
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailPointBuffer — capacity and state queries
// ─────────────────────────────────────────────────────────────────────────────

uint32_t TrailPointBuffer::Count() const noexcept {
    return count_;
}

uint32_t TrailPointBuffer::Capacity() const noexcept {
    return capacity_;
}

bool TrailPointBuffer::IsEmpty() const noexcept {
    return count_ == 0;
}

bool TrailPointBuffer::IsFull() const noexcept {
    return capacity_ > 0 && count_ == capacity_;
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailComponent — construction
// ─────────────────────────────────────────────────────────────────────────────

TrailComponent::TrailComponent()
    : points(maxTrailPoints)
    , hasFirstPoint_(false)
{
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailComponent::Tick — emit, age, cull, recalculate distances
// ─────────────────────────────────────────────────────────────────────────────

void TrailComponent::Tick(float dt, const Vec3& currentPosition) {
    if (!isActive) {
        return;
    }

    // ── Step 1: Age all existing points by dt ────────────────────────────────
    // Aging happens before emission so that a newly emitted point starts at
    // age=0 and is not immediately aged by the same frame's dt.
    for (uint32_t i = 0; i < points.Count(); ++i) {
        points.GetPoint(i).age += dt;
    }

    // ── Step 2: Remove tail points whose age exceeds the lifetime ─────────────
    // Removal is done from the tail (oldest) moving inward. The tail is always
    // logical index Count()-1.
    while (!points.IsEmpty() && points.GetPoint(points.Count() - 1u).age > pointLifetime) {
        UZ_LOG(VFX, Debug, "Trail point expired: material='{}', age={:.3f}s",
               materialPath, points.GetPoint(points.Count() - 1u).age);
        points.RemoveOldest();
    }

    // ── Step 3: Emit a new point if far enough (or if this is the first one) ──
    const Vec3  delta    = currentPosition - lastAddedPosition;
    const float distance = delta.Length();
    const bool shouldEmit = !hasFirstPoint_ || (distance >= minPointDistance);

    if (shouldEmit) {
        TrailPoint newPoint{};
        newPoint.position = currentPosition;
        newPoint.age      = 0.0f;
        newPoint.width    = widthStart;
        newPoint.color    = colorStart;
        newPoint.distanceFromHead = 0.0f;

        // Compute tangent from movement direction; fall back to world-up when
        // the entity is stationary (avoids a degenerate zero-length normalize).
        if (distance > 1e-6f) {
            newPoint.direction = delta * (1.0f / distance);
        } else {
            newPoint.direction = Vec3{0.0f, 1.0f, 0.0f};
        }

        points.AddPoint(newPoint);
        lastAddedPosition = currentPosition;
        hasFirstPoint_    = true;
    }

    // ── Step 4: Recalculate distanceFromHead and update totalDistance ─────────
    RecalculateDistances();
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailComponent::RecalculateDistances — arc-length accumulation head → tail
// ─────────────────────────────────────────────────────────────────────────────

void TrailComponent::RecalculateDistances() {
    const uint32_t n = points.Count();

    if (n == 0) {
        totalDistance = 0.0f;
        return;
    }

    // Index 0 is always the head: distance from head to itself is 0.
    points.GetPoint(0).distanceFromHead = 0.0f;

    float accumulated = 0.0f;
    for (uint32_t i = 1; i < n; ++i) {
        const Vec3 diff = points.GetPoint(i - 1u).position - points.GetPoint(i).position;
        accumulated += diff.Length();
        points.GetPoint(i).distanceFromHead = accumulated;
    }

    totalDistance = accumulated;
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailComponent::Clear — reset to empty state
// ─────────────────────────────────────────────────────────────────────────────

void TrailComponent::Clear() {
    points.Clear();
    totalDistance    = 0.0f;
    hasFirstPoint_   = false;
    lastAddedPosition = Vec3{0.0f, 0.0f, 0.0f};
    UZ_LOG(VFX, Debug, "Trail cleared: material='{}'", materialPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailComponent — state queries
// ─────────────────────────────────────────────────────────────────────────────

const TrailPointBuffer& TrailComponent::GetPoints() const noexcept {
    return points;
}

bool TrailComponent::IsEmpty() const {
    return points.IsEmpty();
}

uint32_t TrailComponent::GetActivePointCount() const {
    return points.Count();
}

float TrailComponent::GetTotalLength() const {
    return totalDistance;
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailComponent::GetColorAtNormalized — head-to-tail color interpolation
// ─────────────────────────────────────────────────────────────────────────────

Vec4 TrailComponent::GetColorAtNormalized(float t) const {
    const float clamped = std::clamp(t, 0.0f, 1.0f);

    // Component-wise linear interpolation: colorStart at t=0, colorEnd at t=1.
    return Vec4{
        colorStart.x + (colorEnd.x - colorStart.x) * clamped,
        colorStart.y + (colorEnd.y - colorStart.y) * clamped,
        colorStart.z + (colorEnd.z - colorStart.z) * clamped,
        colorStart.w + (colorEnd.w - colorStart.w) * clamped,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailComponent::GetWidthAtNormalized — head-to-tail width interpolation
// ─────────────────────────────────────────────────────────────────────────────

float TrailComponent::GetWidthAtNormalized(float t) const {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return widthStart + (widthEnd - widthStart) * clamped;
}

} // namespace uz
