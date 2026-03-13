/// UzEngine Rect2 — 2D axis-aligned rectangle.
///
/// Defines a rectangle by position (top-left corner) and size (width, height).
/// Used for UI layout, 2D collision, texture regions, and screen-space calculations.
///
/// # Memory Layout
/// `[position: Vec2, size: Vec2]` — 16 bytes, no padding.
///
/// # Conventions
/// - `position` is the top-left corner (minimum x, minimum y).
/// - `size` should be non-negative for a valid rectangle. Use `abs()` to normalize.
/// - `end()` returns `position + size` (bottom-right corner).
///
/// # Thread Safety
/// Trivially copyable value type. Thread-safe by value.
///
/// # Examples
/// ```cpp
/// Rect2 viewport{0, 0, 1920, 1080};
/// Rect2 button{100, 200, 150, 40};
/// bool hovered = button.contains(mousePos);
/// Rect2 combined = viewport.merge(button);
/// ```

#pragma once

#include "vector2.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Rect2 — 2D axis-aligned rectangle
// ─────────────────────────────────────────────────────────────────────────────

/// 2D axis-aligned rectangle for UI, collision, and screen-space math.
///
/// Stored as top-left `position` and `size` (width, height).
/// All query methods assume non-negative size unless documented otherwise.
struct Rect2 {
    /// Top-left corner of the rectangle.
    Vec2 position;
    /// Width and height of the rectangle.
    Vec2 size;

    // ─────────────────────────────────────────────────────────────────────────
    // Constructors
    // ─────────────────────────────────────────────────────────────────────────

    /// Default: zero position, zero size (degenerate point at origin).
    Rect2() : position{}, size{} {}

    /// Construct from explicit position and size components.
    ///
    /// # Arguments
    /// * `x` — Left edge X coordinate.
    /// * `y` — Top edge Y coordinate.
    /// * `w` — Width (should be >= 0).
    /// * `h` — Height (should be >= 0).
    Rect2(float x, float y, float w, float h)
        : position{x, y}, size{w, h} {}

    /// Construct from position and size vectors.
    ///
    /// # Arguments
    /// * `pos` — Top-left corner.
    /// * `sz` — Width and height.
    Rect2(const Vec2& pos, const Vec2& sz)
        : position(pos), size(sz) {}

    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns the bottom-right corner (position + size).
    ///
    /// # Returns
    /// End point. For valid rectangles (positive size), this is the maximum corner.
    Vec2 end() const { return position + size; }
    /// Uppercase alias for `end()`.
    Vec2 End() const { return end(); }

    /// Returns the center point of the rectangle.
    ///
    /// # Returns
    /// `position + size * 0.5`.
    Vec2 center() const { return position + size * 0.5f; }
    /// Uppercase alias for `center()`.
    Vec2 Center() const { return center(); }

    /// Returns the area of the rectangle (width * height).
    ///
    /// # Returns
    /// Signed area. Negative if either dimension is negative.
    float area() const { return size.x * size.y; }
    /// Uppercase alias for `area()`.
    float Area() const { return area(); }

    /// Returns true if the rectangle has positive area (both dimensions > 0).
    ///
    /// # Returns
    /// `true` if width > 0 AND height > 0.
    bool has_area() const { return size.x > 0.0f && size.y > 0.0f; }
    /// Uppercase alias for `has_area()`.
    bool HasArea() const { return has_area(); }

    // ─────────────────────────────────────────────────────────────────────────
    // Queries
    // ─────────────────────────────────────────────────────────────────────────

    /// Tests if a point lies inside the rectangle (inclusive on min edges, exclusive on max).
    ///
    /// # Arguments
    /// * `point` — Point to test.
    ///
    /// # Returns
    /// `true` if `point` is within `[position, position + size)`.
    bool contains(const Vec2& point) const {
        Vec2 e = end();
        return point.x >= position.x && point.y >= position.y &&
               point.x < e.x && point.y < e.y;
    }
    /// Uppercase alias for `contains()`.
    bool Contains(const Vec2& point) const { return contains(point); }

    /// Tests if another rectangle overlaps this one.
    ///
    /// Two rectangles intersect if they share any interior area. Touching
    /// edges only (zero overlap) returns `false`.
    ///
    /// # Arguments
    /// * `other` — Rectangle to test against.
    ///
    /// # Returns
    /// `true` if the rectangles overlap.
    bool intersects(const Rect2& other) const {
        Vec2 e1 = end();
        Vec2 e2 = other.end();
        return !(e1.x <= other.position.x || position.x >= e2.x ||
                 e1.y <= other.position.y || position.y >= e2.y);
    }
    /// Uppercase alias for `intersects()`.
    bool Intersects(const Rect2& other) const { return intersects(other); }

    /// Tests if this rectangle fully contains another rectangle.
    ///
    /// # Arguments
    /// * `other` — Rectangle to test.
    ///
    /// # Returns
    /// `true` if every point in `other` is also inside `*this`.
    bool encloses(const Rect2& other) const {
        Vec2 e1 = end();
        Vec2 e2 = other.end();
        return other.position.x >= position.x && other.position.y >= position.y &&
               e2.x <= e1.x && e2.y <= e1.y;
    }
    /// Uppercase alias for `encloses()`.
    bool Encloses(const Rect2& other) const { return encloses(other); }

    // ─────────────────────────────────────────────────────────────────────────
    // Operations
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns the smallest rectangle enclosing both `*this` and `other` (union).
    ///
    /// # Arguments
    /// * `other` — Rectangle to merge with.
    ///
    /// # Returns
    /// Bounding rectangle that contains both inputs.
    Rect2 merge(const Rect2& other) const {
        Vec2 e1 = end();
        Vec2 e2 = other.end();
        Vec2 new_pos{std::fmin(position.x, other.position.x),
                     std::fmin(position.y, other.position.y)};
        Vec2 new_end{std::fmax(e1.x, e2.x), std::fmax(e1.y, e2.y)};
        return {new_pos, new_end - new_pos};
    }
    /// Uppercase alias for `merge()`.
    Rect2 Merge(const Rect2& other) const { return merge(other); }

    /// Returns the overlapping region between this rectangle and `other`.
    ///
    /// If the rectangles do not overlap, returns a degenerate rectangle
    /// (zero or negative size).
    ///
    /// # Arguments
    /// * `other` — Rectangle to intersect with.
    ///
    /// # Returns
    /// Intersection rectangle. Check `has_area()` to verify a valid overlap exists.
    Rect2 intersection(const Rect2& other) const {
        Vec2 e1 = end();
        Vec2 e2 = other.end();
        Vec2 new_pos{std::fmax(position.x, other.position.x),
                     std::fmax(position.y, other.position.y)};
        Vec2 new_end{std::fmin(e1.x, e2.x), std::fmin(e1.y, e2.y)};
        return {new_pos, new_end - new_pos};
    }
    /// Uppercase alias for `intersection()`.
    Rect2 Intersection(const Rect2& other) const { return intersection(other); }

    /// Returns a rectangle expanded to include the given point.
    ///
    /// If the point is already inside, returns a copy of `*this`.
    ///
    /// # Arguments
    /// * `point` — Point to include.
    ///
    /// # Returns
    /// Rectangle guaranteed to contain both the original area and `point`.
    Rect2 expand(const Vec2& point) const {
        Vec2 e = end();
        Vec2 new_pos{std::fmin(position.x, point.x), std::fmin(position.y, point.y)};
        Vec2 new_end{std::fmax(e.x, point.x), std::fmax(e.y, point.y)};
        return {new_pos, new_end - new_pos};
    }
    /// Uppercase alias for `expand()`.
    Rect2 Expand(const Vec2& point) const { return expand(point); }

    /// Returns a rectangle grown uniformly in all four directions.
    ///
    /// Negative `amount` shrinks the rectangle.
    ///
    /// # Arguments
    /// * `amount` — Distance to expand each edge outward.
    ///
    /// # Returns
    /// Grown rectangle with position shifted by `(-amount, -amount)` and
    /// size increased by `(2*amount, 2*amount)`.
    Rect2 grow(float amount) const {
        return {Vec2{position.x - amount, position.y - amount},
                Vec2{size.x + amount * 2.0f, size.y + amount * 2.0f}};
    }
    /// Uppercase alias for `grow()`.
    Rect2 Grow(float amount) const { return grow(amount); }

    /// Returns a rectangle with one side grown by `amount`.
    ///
    /// Side indices: 0=left, 1=top, 2=right, 3=bottom.
    ///
    /// # Arguments
    /// * `side` — Side index in [0, 3].
    /// * `amount` — Distance to grow (positive = outward).
    ///
    /// # Returns
    /// Rectangle with the specified side expanded. Other sides unchanged.
    Rect2 grow_side(int side, float amount) const {
        Rect2 result = *this;
        switch (side) {
            case 0: // left
                result.position.x -= amount;
                result.size.x += amount;
                break;
            case 1: // top
                result.position.y -= amount;
                result.size.y += amount;
                break;
            case 2: // right
                result.size.x += amount;
                break;
            case 3: // bottom
                result.size.y += amount;
                break;
            default:
                break;
        }
        return result;
    }
    /// Uppercase alias for `grow_side()`.
    Rect2 GrowSide(int side, float amount) const { return grow_side(side, amount); }

    /// Returns a rectangle with guaranteed non-negative size.
    ///
    /// If either dimension is negative, the position is adjusted and the size
    /// is made positive so that the rectangle covers the same area.
    ///
    /// # Returns
    /// Normalized rectangle with `size.x >= 0` and `size.y >= 0`.
    Rect2 abs() const {
        Vec2 new_pos = position;
        Vec2 new_size = size;
        if (new_size.x < 0.0f) {
            new_pos.x += new_size.x;
            new_size.x = -new_size.x;
        }
        if (new_size.y < 0.0f) {
            new_pos.y += new_size.y;
            new_size.y = -new_size.y;
        }
        return {new_pos, new_size};
    }
    /// Uppercase alias for `abs()`.
    Rect2 Abs() const { return abs(); }

    // ─────────────────────────────────────────────────────────────────────────
    // Comparison
    // ─────────────────────────────────────────────────────────────────────────

    /// Returns true if this rectangle is approximately equal to `other`.
    ///
    /// # Arguments
    /// * `other` — Rectangle to compare against.
    ///
    /// # Returns
    /// `true` if position and size are within `Math::EPSILON`.
    bool is_equal_approx(const Rect2& other) const {
        return position.is_equal_approx(other.position) &&
               size.is_equal_approx(other.size);
    }
    /// Uppercase alias for `is_equal_approx()`.
    bool IsEqualApprox(const Rect2& other) const { return is_equal_approx(other); }

    /// Exact equality comparison.
    bool operator==(const Rect2& other) const {
        return position == other.position && size == other.size;
    }

    /// Exact inequality comparison.
    bool operator!=(const Rect2& other) const { return !(*this == other); }
};

} // namespace uz
