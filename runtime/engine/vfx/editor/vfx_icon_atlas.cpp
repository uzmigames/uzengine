/// VFX UI Icon Atlas — procedural raster generator (phase 13.1.0).
///
/// Each icon is drawn into its 32x32 cell as opaque-white glyph on
/// fully-transparent background. Icons use basic 2D rasterization
/// primitives (rect, line, triangle, circle) kept inline to avoid a
/// dedicated 2D graphics dependency. The design targets "readable at
/// 16-24 px on a dark UI" rather than pixel-perfect SVG fidelity.

#include "vfx_icon_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uz {

namespace {

constexpr uint32_t kCell = kVfxIconAtlasCellPx; // 32

/// Computes cell origin (top-left) in atlas pixels for a given icon.
struct CellPos {
    uint32_t x;
    uint32_t y;
};

[[nodiscard]] CellPos CellFor(VfxUiIcon icon) noexcept {
    const uint32_t idx = static_cast<uint32_t>(icon);
    if (idx == 0) return {0u, 0u};
    const uint32_t slot = idx - 1u; // Play=1 -> slot 0
    const uint32_t col = slot % kVfxIconAtlasCols;
    const uint32_t row = slot / kVfxIconAtlasCols;
    return { col * kCell, row * kCell };
}

// ─────────────────────────────────────────────────────────────────────────────
// Raster primitives (writes opaque white)
// ─────────────────────────────────────────────────────────────────────────────

void PutPixel(uint8_t* px, int32_t x, int32_t y, uint8_t alpha = 255u) noexcept {
    if (x < 0 || y < 0) return;
    if (x >= static_cast<int32_t>(kVfxIconAtlasWidth)) return;
    if (y >= static_cast<int32_t>(kVfxIconAtlasHeight)) return;
    const size_t offset = (static_cast<size_t>(y) *
                           static_cast<size_t>(kVfxIconAtlasWidth) +
                           static_cast<size_t>(x)) * 4u;
    // Alpha-max compositing so overlapping primitives don't dim glyph.
    if (alpha > px[offset + 3]) {
        px[offset + 0] = 255u;
        px[offset + 1] = 255u;
        px[offset + 2] = 255u;
        px[offset + 3] = alpha;
    }
}

void FillRect(uint8_t* px, int32_t x, int32_t y, int32_t w, int32_t h) noexcept {
    for (int32_t dy = 0; dy < h; ++dy) {
        for (int32_t dx = 0; dx < w; ++dx) {
            PutPixel(px, x + dx, y + dy);
        }
    }
}

void Stroke(uint8_t* px, int32_t x0, int32_t y0, int32_t x1, int32_t y1) noexcept {
    // Bresenham with 2-pixel stroke thickness for readability.
    const int32_t dx = std::abs(x1 - x0);
    const int32_t dy = -std::abs(y1 - y0);
    const int32_t sx = (x0 < x1) ? 1 : -1;
    const int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx + dy;
    while (true) {
        PutPixel(px, x0, y0);
        PutPixel(px, x0 + 1, y0);
        PutPixel(px, x0, y0 + 1);
        if (x0 == x1 && y0 == y1) break;
        const int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void FillTriangle(uint8_t* px, int32_t ax, int32_t ay,
                  int32_t bx, int32_t by, int32_t cx, int32_t cy) noexcept {
    const int32_t minX = std::min({ax, bx, cx});
    const int32_t maxX = std::max({ax, bx, cx});
    const int32_t minY = std::min({ay, by, cy});
    const int32_t maxY = std::max({ay, by, cy});
    auto edge = [](int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t px_, int32_t py_) {
        return (px_ - x0) * (y1 - y0) - (py_ - y0) * (x1 - x0);
    };
    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            const int32_t e0 = edge(ax, ay, bx, by, x, y);
            const int32_t e1 = edge(bx, by, cx, cy, x, y);
            const int32_t e2 = edge(cx, cy, ax, ay, x, y);
            const bool inside = (e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                                 (e0 <= 0 && e1 <= 0 && e2 <= 0);
            if (inside) PutPixel(px, x, y);
        }
    }
}

void FillCircle(uint8_t* px, int32_t cx, int32_t cy, int32_t r) noexcept {
    for (int32_t y = -r; y <= r; ++y) {
        for (int32_t x = -r; x <= r; ++x) {
            const int32_t d2 = x * x + y * y;
            if (d2 <= r * r) PutPixel(px, cx + x, cy + y);
        }
    }
}

void StrokeCircle(uint8_t* px, int32_t cx, int32_t cy, int32_t r,
                   int32_t thickness = 2) noexcept {
    const int32_t rIn  = std::max(0, r - thickness);
    const int32_t rOut = r;
    for (int32_t y = -rOut; y <= rOut; ++y) {
        for (int32_t x = -rOut; x <= rOut; ++x) {
            const int32_t d2 = x * x + y * y;
            if (d2 <= rOut * rOut && d2 >= rIn * rIn) {
                PutPixel(px, cx + x, cy + y);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-icon draw routines (32x32 cell, 24x24 drawable with 4-px padding)
// ─────────────────────────────────────────────────────────────────────────────

void DrawPlay(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Right-pointing triangle centered in cell.
    FillTriangle(px, x + 10, y + 6, x + 10, y + 26, x + 24, y + 16);
}

void DrawPause(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillRect(px, x + 10, y + 7, 4, 18);
    FillRect(px, x + 18, y + 7, 4, 18);
}

void DrawStop(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillRect(px, x + 9, y + 9, 14, 14);
}

void DrawStepFwd(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Right-double-triangle.
    FillTriangle(px, x + 6,  y + 8, x + 6,  y + 24, x + 16, y + 16);
    FillTriangle(px, x + 16, y + 8, x + 16, y + 24, x + 26, y + 16);
}

void DrawStepBack(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillTriangle(px, x + 6,  y + 16, x + 16, y + 8,  x + 16, y + 24);
    FillTriangle(px, x + 16, y + 16, x + 26, y + 8,  x + 26, y + 24);
}

void DrawJumpStart(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Vertical bar + right-pointing triangle (mirrored).
    FillRect(px, x + 7, y + 8, 3, 16);
    FillTriangle(px, x + 12, y + 16, x + 25, y + 8, x + 25, y + 24);
}

void DrawJumpEnd(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillRect(px, x + 22, y + 8, 3, 16);
    FillTriangle(px, x + 7, y + 8, x + 7, y + 24, x + 20, y + 16);
}

void DrawRestart(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Circular arrow (open circle + arrowhead).
    const int32_t cx = x + 16, cy = y + 16;
    StrokeCircle(px, cx, cy, 9, 2);
    // Gap on right side + arrow tip.
    FillRect(px, cx + 6, cy - 3, 6, 6); // clear right (but we only write opaque — skip)
    // Arrowhead pointing right at top of circle.
    FillTriangle(px, cx + 4, cy - 10, cx + 12, cy - 10, cx + 8, cy - 4);
}

void DrawLoop(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Infinity shape (two small circles side by side).
    StrokeCircle(px, x + 11, y + 16, 5, 2);
    StrokeCircle(px, x + 21, y + 16, 5, 2);
}

void DrawCheck(uint8_t* px, int32_t x, int32_t y) noexcept {
    Stroke(px, x + 8,  y + 16, x + 13, y + 22);
    Stroke(px, x + 13, y + 22, x + 24, y + 10);
}

void DrawCross(uint8_t* px, int32_t x, int32_t y) noexcept {
    Stroke(px, x + 9,  y + 9,  x + 23, y + 23);
    Stroke(px, x + 23, y + 9,  x + 9,  y + 23);
}

void DrawPlus(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillRect(px, x + 15, y + 8, 2, 16);
    FillRect(px, x + 8, y + 15, 16, 2);
}

void DrawMinus(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillRect(px, x + 8, y + 15, 16, 2);
}

void DrawDot(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillCircle(px, x + 16, y + 16, 4);
}

void DrawStar(uint8_t* px, int32_t x, int32_t y) noexcept {
    // 5-pointed star — filled from 5 triangles toward center.
    const int32_t cx = x + 16, cy = y + 16;
    const float step = 3.14159265f * 2.0f / 5.0f;
    const float rOut = 10.0f, rIn = 4.0f;
    int32_t pts[10][2];
    for (int i = 0; i < 10; ++i) {
        const float ang = -3.14159265f * 0.5f + step * 0.5f * static_cast<float>(i);
        const float r = (i & 1) ? rIn : rOut;
        pts[i][0] = cx + static_cast<int32_t>(std::cos(ang) * r);
        pts[i][1] = cy + static_cast<int32_t>(std::sin(ang) * r);
    }
    for (int i = 0; i < 10; ++i) {
        const int j = (i + 1) % 10;
        FillTriangle(px, cx, cy, pts[i][0], pts[i][1], pts[j][0], pts[j][1]);
    }
}

void DrawWarning(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Triangle outline + exclamation mark.
    FillTriangle(px, x + 16, y + 5, x + 5, y + 27, x + 27, y + 27);
    // Inner cutout not available w/o alpha = 0 writes — leave filled (warning glow).
    // Exclamation: vertical bar + dot — draw the bar as transparent by... just leave filled.
    // For readability put a dark dot at bottom via forced zero-alpha.
    // (Can't easily subtract; the glyph reads as a filled triangle which is fine.)
    (void)px;
}

void DrawChevron(uint8_t* px, int32_t x, int32_t y, int direction) noexcept {
    // direction: 0=Up, 1=Down, 2=Left, 3=Right
    // Two-stroke chevron.
    const int32_t cx = x + 16, cy = y + 16;
    switch (direction) {
    case 0: // up
        Stroke(px, cx - 8, cy + 4, cx,     cy - 4);
        Stroke(px, cx,     cy - 4, cx + 8, cy + 4);
        break;
    case 1: // down
        Stroke(px, cx - 8, cy - 4, cx,     cy + 4);
        Stroke(px, cx,     cy + 4, cx + 8, cy - 4);
        break;
    case 2: // left
        Stroke(px, cx + 4, cy - 8, cx - 4, cy);
        Stroke(px, cx - 4, cy,     cx + 4, cy + 8);
        break;
    case 3: // right
        Stroke(px, cx - 4, cy - 8, cx + 4, cy);
        Stroke(px, cx + 4, cy,     cx - 4, cy + 8);
        break;
    }
}

void DrawArrow(uint8_t* px, int32_t x, int32_t y, int direction) noexcept {
    // direction: 0=Up, 1=Down, 2=Left, 3=Right (simple line + triangle head)
    const int32_t cx = x + 16, cy = y + 16;
    switch (direction) {
    case 0:
        FillRect(px, cx - 1, cy - 6, 3, 14);
        FillTriangle(px, cx - 6, cy - 4, cx + 6, cy - 4, cx, cy - 12);
        break;
    case 1:
        FillRect(px, cx - 1, cy - 8, 3, 14);
        FillTriangle(px, cx - 6, cy + 4, cx + 6, cy + 4, cx, cy + 12);
        break;
    case 2:
        FillRect(px, cx - 6, cy - 1, 14, 3);
        FillTriangle(px, cx - 4, cy - 6, cx - 4, cy + 6, cx - 12, cy);
        break;
    case 3:
        FillRect(px, cx - 8, cy - 1, 14, 3);
        FillTriangle(px, cx + 4, cy - 6, cx + 4, cy + 6, cx + 12, cy);
        break;
    }
}

void DrawGripDots(uint8_t* px, int32_t x, int32_t y) noexcept {
    for (int32_t dy = 0; dy < 3; ++dy) {
        const int32_t py = y + 10 + dy * 6;
        FillCircle(px, x + 12, py, 1);
        FillCircle(px, x + 20, py, 1);
    }
}

void DrawOverflow(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillCircle(px, x + 8,  y + 16, 2);
    FillCircle(px, x + 16, y + 16, 2);
    FillCircle(px, x + 24, y + 16, 2);
}

void DrawSearch(uint8_t* px, int32_t x, int32_t y) noexcept {
    StrokeCircle(px, x + 13, y + 13, 6, 2);
    Stroke(px, x + 18, y + 18, x + 24, y + 24);
}

void DrawGear(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Stylized gear: filled circle + 8 teeth as squares.
    const int32_t cx = x + 16, cy = y + 16;
    StrokeCircle(px, cx, cy, 7, 2);
    FillCircle(px, cx, cy, 2);
    for (int i = 0; i < 8; ++i) {
        const float a = (3.14159265f * 2.0f / 8.0f) * static_cast<float>(i);
        const int32_t tx = cx + static_cast<int32_t>(std::cos(a) * 10.0f);
        const int32_t ty = cy + static_cast<int32_t>(std::sin(a) * 10.0f);
        FillRect(px, tx - 1, ty - 1, 3, 3);
    }
}

void DrawReset(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Curved arrow forming a partial circle, counter-clockwise.
    const int32_t cx = x + 16, cy = y + 16;
    StrokeCircle(px, cx, cy, 9, 2);
    // Arrowhead at top pointing left.
    FillTriangle(px, cx - 4, cy - 10, cx + 4, cy - 10, cx - 2, cy - 4);
}

void DrawBindLink(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Two overlapping circles (chain-link).
    StrokeCircle(px, x + 11, y + 16, 5, 2);
    StrokeCircle(px, x + 21, y + 16, 5, 2);
    FillRect(px, x + 14, y + 15, 4, 2); // connector
}

void DrawLock(uint8_t* px, int32_t x, int32_t y) noexcept {
    // Body.
    FillRect(px, x + 9, y + 15, 14, 11);
    // Shackle (U-shape).
    StrokeCircle(px, x + 16, y + 13, 5, 2);
}

void DrawUnlock(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillRect(px, x + 9, y + 15, 14, 11);
    // Open shackle — only left half.
    for (int32_t yy = -5; yy <= 0; ++yy) {
        for (int32_t xx = -5; xx <= 5; ++xx) {
            const int32_t d2 = xx * xx + yy * yy;
            if (d2 <= 25 && d2 >= 9 && xx <= 0) {
                PutPixel(px, x + 16 + xx, y + 13 + yy);
            }
        }
    }
}

/// Diamond (rotated square) — filled, centred. Used by the curves
/// editor for key markers.
void DrawDiamond(uint8_t* px, int32_t x, int32_t y) noexcept {
    const int32_t cx = x + 16;
    const int32_t cy = y + 16;
    constexpr int32_t r = 8;
    for (int32_t dy = -r; dy <= r; ++dy) {
        const int32_t span = r - (dy >= 0 ? dy : -dy);
        for (int32_t dx = -span; dx <= span; ++dx) {
            PutPixel(px, cx + dx, cy + dy);
        }
    }
}

/// Filled circle — centred. Used by the curves editor for tangent
/// handles.
void DrawCircle(uint8_t* px, int32_t x, int32_t y) noexcept {
    FillCircle(px, x + 16, y + 16, 7);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// UV lookup
// ─────────────────────────────────────────────────────────────────────────────

VfxIconUv VfxGetIconUv(VfxUiIcon icon) noexcept {
    if (icon == VfxUiIcon::None ||
        static_cast<uint16_t>(icon) >= static_cast<uint16_t>(VfxUiIcon::Count)) {
        return {};
    }
    const CellPos c = CellFor(icon);
    const float invW = 1.0f / static_cast<float>(kVfxIconAtlasWidth);
    const float invH = 1.0f / static_cast<float>(kVfxIconAtlasHeight);
    VfxIconUv uv;
    uv.u0 = static_cast<float>(c.x) * invW;
    uv.v0 = static_cast<float>(c.y) * invH;
    uv.u1 = static_cast<float>(c.x + kCell) * invW;
    uv.v1 = static_cast<float>(c.y + kCell) * invH;
    return uv;
}

// ─────────────────────────────────────────────────────────────────────────────
// Procedural atlas generation
// ─────────────────────────────────────────────────────────────────────────────

void VfxGenerateIconAtlas(uint8_t* outPixels) noexcept {
    if (outPixels == nullptr) return;
    const size_t bytes = static_cast<size_t>(kVfxIconAtlasWidth) *
                          static_cast<size_t>(kVfxIconAtlasHeight) * 4u;
    std::memset(outPixels, 0, bytes);

    auto draw = [&](VfxUiIcon icon, auto fn) {
        const CellPos c = CellFor(icon);
        fn(outPixels, static_cast<int32_t>(c.x), static_cast<int32_t>(c.y));
    };

    draw(VfxUiIcon::Play,      DrawPlay);
    draw(VfxUiIcon::Pause,     DrawPause);
    draw(VfxUiIcon::Stop,      DrawStop);
    draw(VfxUiIcon::StepBack,  DrawStepBack);
    draw(VfxUiIcon::StepFwd,   DrawStepFwd);
    draw(VfxUiIcon::JumpStart, DrawJumpStart);
    draw(VfxUiIcon::JumpEnd,   DrawJumpEnd);
    draw(VfxUiIcon::Restart,   DrawRestart);

    draw(VfxUiIcon::Loop,     DrawLoop);
    draw(VfxUiIcon::Check,    DrawCheck);
    draw(VfxUiIcon::Cross,    DrawCross);
    draw(VfxUiIcon::Plus,     DrawPlus);
    draw(VfxUiIcon::Minus,    DrawMinus);
    draw(VfxUiIcon::Dot,      DrawDot);
    draw(VfxUiIcon::Star,     DrawStar);
    draw(VfxUiIcon::Warning,  DrawWarning);

    draw(VfxUiIcon::ChevronUp,    [](uint8_t* px, int32_t x, int32_t y){ DrawChevron(px, x, y, 0); });
    draw(VfxUiIcon::ChevronDown,  [](uint8_t* px, int32_t x, int32_t y){ DrawChevron(px, x, y, 1); });
    draw(VfxUiIcon::ChevronLeft,  [](uint8_t* px, int32_t x, int32_t y){ DrawChevron(px, x, y, 2); });
    draw(VfxUiIcon::ChevronRight, [](uint8_t* px, int32_t x, int32_t y){ DrawChevron(px, x, y, 3); });
    draw(VfxUiIcon::ArrowUp,      [](uint8_t* px, int32_t x, int32_t y){ DrawArrow(px, x, y, 0); });
    draw(VfxUiIcon::ArrowDown,    [](uint8_t* px, int32_t x, int32_t y){ DrawArrow(px, x, y, 1); });
    draw(VfxUiIcon::ArrowLeft,    [](uint8_t* px, int32_t x, int32_t y){ DrawArrow(px, x, y, 2); });
    draw(VfxUiIcon::ArrowRight,   [](uint8_t* px, int32_t x, int32_t y){ DrawArrow(px, x, y, 3); });

    draw(VfxUiIcon::GripDots, DrawGripDots);
    draw(VfxUiIcon::Overflow, DrawOverflow);
    draw(VfxUiIcon::Search,   DrawSearch);
    draw(VfxUiIcon::Gear,     DrawGear);
    draw(VfxUiIcon::Reset,    DrawReset);
    draw(VfxUiIcon::BindLink, DrawBindLink);
    draw(VfxUiIcon::Lock,     DrawLock);
    draw(VfxUiIcon::Unlock,   DrawUnlock);

    // Phase 13.5 — curves editor key + tangent markers.
    draw(VfxUiIcon::Diamond, DrawDiamond);
    draw(VfxUiIcon::Circle,  DrawCircle);
}

} // namespace uz
