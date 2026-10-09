/// UzEngine Slate UI — Brush primitive.
///
/// Phase 18 / Step 6. A Brush bundles every visual property a widget
/// might use to fill its background: solid color, rounded rect with
/// border, drop shadow, gradient, or a sprite from the icon atlas.
///
/// Brushes are **plain data** (POD) — copyable in a single memcpy,
/// trivially serialisable for a future `.uztheme` JSON loader, and
/// cheap to store inside an `Attribute<Brush>`.
///
/// Each widget translates its current `Brush` into a `DrawElement` at
/// Paint time. Helper factories below make construction terse:
///
/// ```cpp
/// auto bg = MakeSolidColor(Color{0.1, 0.1, 0.12, 1.0}, /*radius*/ 4.0f);
/// auto bord = MakeBordered(Color{0.18,0,0,0,1}, 1.0f, Color{0.4,0.4,0.4,1});
/// ```
///
/// Themes (Step 6's `StyleSet`) store named Brushes. Widgets pull them
/// via `Styles().GetBrush("button.primary")` rather than hard-coding
/// colors.

#pragma once

#include "engine/ui/slate_core.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"

#include <cstdint>

namespace uz::ui {

enum class BrushKind : std::uint8_t {
    SolidColor,   ///< Flat fill (rounded if cornerRadius > 0).
    Image,        ///< Single sprite from the icon atlas.
    Box9Slice,    ///< 9-slice sprite (TODO Step 7+; treated as Image for now).
    Gradient,     ///< Linear gradient between gradientStops; CSS-like.
};

struct Brush {
    BrushKind kind{BrushKind::SolidColor};

    // Common fill / border / shadow.
    Color tintColor{1.0f, 1.0f, 1.0f, 1.0f};
    float cornerRadius{0.0f};
    float borderThickness{0.0f};
    Color borderColor{0.0f, 0.0f, 0.0f, 0.0f};
    Vec2  shadowOffset{0.0f, 0.0f};
    float shadowBlur{0.0f};
    Color shadowColor{0.0f, 0.0f, 0.0f, 0.0f};

    // Image / Box9Slice.
    VfxUiIcon iconId{VfxUiIcon::None};

    // Gradient (4 stops max — covers every UI gradient in the design).
    // Positions are 0..1 along the brush rect; default vertical (top → bottom).
    Color gradientStop0{}, gradientStop1{}, gradientStop2{}, gradientStop3{};
    Vec4  gradientPositions{0.0f, 1.0f, 0.0f, 0.0f}; ///< 0..1 per stop.

    /// True if this Brush has a non-trivial border.
    [[nodiscard]] bool HasBorder() const noexcept {
        return borderThickness > 0.0f && borderColor.w > 0.0f;
    }
    /// True if this Brush has a non-trivial drop shadow.
    [[nodiscard]] bool HasShadow() const noexcept {
        return shadowBlur > 0.0f && shadowColor.w > 0.0f;
    }
};

static_assert(std::is_trivially_copyable_v<Brush>,
              "Brush must remain POD-copyable for theme hot-swap & tests");

// ─────────────────────────────────────────────────────────────────────────────
// Factory helpers
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] inline Brush MakeSolidColor(Color c, float radius = 0.0f) {
    Brush b{};
    b.kind = BrushKind::SolidColor;
    b.tintColor = c;
    b.cornerRadius = radius;
    return b;
}

[[nodiscard]] inline Brush MakeBordered(Color fill, float thickness, Color border,
                                        float radius = 0.0f) {
    Brush b = MakeSolidColor(fill, radius);
    b.borderThickness = thickness;
    b.borderColor = border;
    return b;
}

[[nodiscard]] inline Brush MakeShadowed(Color fill, Vec2 offset, float blur,
                                        Color shadowColor, float radius = 0.0f) {
    Brush b = MakeSolidColor(fill, radius);
    b.shadowOffset = offset;
    b.shadowBlur = blur;
    b.shadowColor = shadowColor;
    return b;
}

[[nodiscard]] inline Brush MakeGradient(Color top, Color bottom, float radius = 0.0f) {
    Brush b{};
    b.kind = BrushKind::Gradient;
    b.tintColor = top; // Solid-color fallback if gradient sampling unavailable.
    b.cornerRadius = radius;
    b.gradientStop0 = top;
    b.gradientStop1 = bottom;
    b.gradientPositions = Vec4{0.0f, 1.0f, 0.0f, 0.0f};
    return b;
}

[[nodiscard]] inline Brush MakeIcon(VfxUiIcon icon, Color tint = Color{1, 1, 1, 1}) {
    Brush b{};
    b.kind = BrushKind::Image;
    b.iconId = icon;
    b.tintColor = tint;
    return b;
}

// Brush equality for Attribute<Brush>::operator= dirty detection.
[[nodiscard]] inline bool operator==(const Brush& a, const Brush& b) noexcept {
    return a.kind == b.kind
        && a.tintColor.x == b.tintColor.x && a.tintColor.y == b.tintColor.y
        && a.tintColor.z == b.tintColor.z && a.tintColor.w == b.tintColor.w
        && a.cornerRadius == b.cornerRadius
        && a.borderThickness == b.borderThickness
        && a.borderColor.x == b.borderColor.x && a.borderColor.y == b.borderColor.y
        && a.borderColor.z == b.borderColor.z && a.borderColor.w == b.borderColor.w
        && a.shadowOffset.x == b.shadowOffset.x && a.shadowOffset.y == b.shadowOffset.y
        && a.shadowBlur == b.shadowBlur
        && a.shadowColor.x == b.shadowColor.x && a.shadowColor.y == b.shadowColor.y
        && a.shadowColor.z == b.shadowColor.z && a.shadowColor.w == b.shadowColor.w
        && a.iconId == b.iconId;
}

[[nodiscard]] inline bool operator!=(const Brush& a, const Brush& b) noexcept {
    return !(a == b);
}

} // namespace uz::ui
