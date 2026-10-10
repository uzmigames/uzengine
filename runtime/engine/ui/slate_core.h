/// UzEngine Slate UI — core type aliases and primitives.
///
/// Phase 18 / Step 1. See `docs/engine/45-slate-ui/00-decisions.md` for
/// the locked decisions that drive the type choices here:
///
/// - Memory model (Decision 2): `SP<T>` / `WP<T>` are project-wide
///   aliases for `std::shared_ptr` / `std::weak_ptr`. Containers store
///   `SP<Widget>`; back-references store `WP<Widget>`.
/// - Coordinates (Decision 5): `Vec2`/`Rect` carry Slate units (virtual
///   pixels @ 96 DPI). The compositor multiplies by `dpiScale` once at
///   submit time.
///
/// This header is C++17 and depends only on the engine math types and
/// the standard library. It MUST not pull in any rendering / RHI code.

#pragma once

#include "vector2.h"
#include "vector4.h"

#include <memory>

namespace uz::ui {

// ─────────────────────────────────────────────────────────────────────────────
// Smart pointer aliases (Decision 2)
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
using SP = std::shared_ptr<T>;

template <typename T>
using WP = std::weak_ptr<T>;

// ─────────────────────────────────────────────────────────────────────────────
// Geometry primitives (Decision 5: Slate units = vpx @ 96 DPI)
// ─────────────────────────────────────────────────────────────────────────────

/// RGBA color, linear-space, alpha straight (not premultiplied).
using Color = Vec4;

/// Axis-aligned rectangle in Slate units. `pos` is the top-left corner.
struct Rect {
    Vec2 pos{};
    Vec2 size{};

    [[nodiscard]] constexpr float Left()   const { return pos.x; }
    [[nodiscard]] constexpr float Top()    const { return pos.y; }
    [[nodiscard]] constexpr float Right()  const { return pos.x + size.x; }
    [[nodiscard]] constexpr float Bottom() const { return pos.y + size.y; }

    /// True if `p` lies inside the rect (left/top inclusive, right/bottom exclusive).
    [[nodiscard]] constexpr bool Contains(Vec2 p) const {
        return p.x >= pos.x && p.x < pos.x + size.x &&
               p.y >= pos.y && p.y < pos.y + size.y;
    }

    /// Intersection with another rect; returns an empty rect (size 0) if disjoint.
    [[nodiscard]] Rect Intersect(const Rect& other) const {
        const float left   = pos.x > other.pos.x ? pos.x : other.pos.x;
        const float top    = pos.y > other.pos.y ? pos.y : other.pos.y;
        const float right  = (pos.x + size.x < other.pos.x + other.size.x)
                                ? pos.x + size.x : other.pos.x + other.size.x;
        const float bottom = (pos.y + size.y < other.pos.y + other.size.y)
                                ? pos.y + size.y : other.pos.y + other.size.y;
        if (right <= left || bottom <= top) {
            return Rect{};
        }
        return Rect{Vec2{left, top}, Vec2{right - left, bottom - top}};
    }

    [[nodiscard]] constexpr bool IsEmpty() const {
        return size.x <= 0.0f || size.y <= 0.0f;
    }
};

} // namespace uz::ui
