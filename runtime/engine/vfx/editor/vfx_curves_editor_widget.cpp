/// VfxCurvesEditorWidget — implementation.
///
/// Phase 11.5.0. See `vfx_curves_editor_widget.h` for the contract. The
/// widget keeps a mirror of the bound curve's keyframes (necessary
/// because `VFXCurve` has no per-key getter) and commits the mirror via
/// `SetKeys` on every mutation. Colour ramp mutations directly write
/// the `VFXColorRamp::stops` array.

#include "vfx_curves_editor_widget.h"

#include "editor/font/text_renderer.h"
#include "editor/font/bitmap_font.h"
#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace uz {

namespace {

// ─── Palette (phase 12.1.0 — theme-sourced) ─────────────────────────────────

const Vec4& kColorBg         = VfxThemeColors().backgroundBase;
const Vec4& kColorGrid       = VfxThemeColors().gridMinor;
const Vec4& kColorGridMajor  = VfxThemeColors().gridMajor;
const Vec4& kColorPath       = VfxThemeColors().accentPrimary;
const Vec4& kColorKey        = VfxThemeColors().accentPrimary;
const Vec4& kColorKeySel     = VfxThemeColors().stateInfo;
const Vec4& kColorRibbonBg   = VfxThemeColors().inputBg;
const Vec4& kColorStopBorder = VfxThemeColors().borderStrong;
const Vec4& kColorStopSel    = VfxThemeColors().textPrimary;
const Vec4& kColorAxisText   = VfxThemeColors().textDim;

/// How many line segments to sample across the visible curve.
constexpr int kPathSegments = 96;

/// Hit-test tolerance (pixels).
constexpr float kKeyHitRadius  = 8.0f;
constexpr float kStopHitRadius = 10.0f;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Snap
// ─────────────────────────────────────────────────────────────────────────────

float VfxCurvesEditorWidget::Snap(float v, float step) noexcept {
    if (step <= 0.0f) return v;
    return std::round(v / step) * step;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sort helper for ramp keys (`.t` field)
// ─────────────────────────────────────────────────────────────────────────────

void VfxCurvesEditorWidget::SortRampByT(std::vector<VFXColorKey>& v) {
    std::sort(v.begin(), v.end(),
              [](const VFXColorKey& a, const VFXColorKey& b) { return a.t < b.t; });
}

// ─────────────────────────────────────────────────────────────────────────────
// Binding
// ─────────────────────────────────────────────────────────────────────────────

void VfxCurvesEditorWidget::BindFloat(VFXCurve* curve) noexcept {
    m_ramp = nullptr;
    m_stopMirror.clear();
    if (curve == nullptr || curve->GetType() != CurveType::Float) {
        m_curve = nullptr;
        m_mode  = VfxCurveBindingMode::None;
        return;
    }
    m_curve = curve;
    m_mode  = VfxCurveBindingMode::Float;
    m_keyMirror.clear();
    m_selectedIndex = static_cast<size_t>(-1);
    m_dragging = false;
    m_dirty    = false;
}

void VfxCurvesEditorWidget::BindRamp(VFXColorRamp* ramp) noexcept {
    m_curve = nullptr;
    m_keyMirror.clear();
    if (ramp == nullptr) {
        m_ramp = nullptr;
        m_mode = VfxCurveBindingMode::None;
        return;
    }
    m_ramp = ramp;
    m_mode = VfxCurveBindingMode::Ramp;
    m_stopMirror.clear();
    m_stopMirror.reserve(static_cast<size_t>(ramp->stopCount));
    for (int i = 0; i < ramp->stopCount; ++i) {
        m_stopMirror.push_back(ramp->stops[i]);
    }
    SortRampByT(m_stopMirror);
    m_selectedIndex = static_cast<size_t>(-1);
    m_dragging = false;
    m_dirty    = false;
}

void VfxCurvesEditorWidget::Unbind() noexcept {
    m_curve = nullptr;
    m_ramp  = nullptr;
    m_mode  = VfxCurveBindingMode::None;
    m_keyMirror.clear();
    m_stopMirror.clear();
    m_selectedIndex = static_cast<size_t>(-1);
    m_dragging = false;
    m_dirty    = false;
}

void VfxCurvesEditorWidget::AppendInitialKey(float time, float value) {
    if (m_mode != VfxCurveBindingMode::Float) return;
    m_keyMirror.push_back({time, value});
    SortByTime(m_keyMirror);
}

// ─────────────────────────────────────────────────────────────────────────────
// Commit helpers
// ─────────────────────────────────────────────────────────────────────────────

void VfxCurvesEditorWidget::CommitFloatKeys() {
    if (m_curve == nullptr) return;
    m_curve->SetKeys(m_keyMirror);
}

void VfxCurvesEditorWidget::CommitStops() {
    if (m_ramp == nullptr) return;
    const int cap = VFXColorRamp::kMaxStops;
    const int n = static_cast<int>(std::min<size_t>(m_stopMirror.size(),
                                                      static_cast<size_t>(cap)));
    for (int i = 0; i < n; ++i) {
        m_ramp->stops[i] = m_stopMirror[static_cast<size_t>(i)];
    }
    m_ramp->stopCount = n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Index helpers (post-sort re-find)
// ─────────────────────────────────────────────────────────────────────────────

size_t VfxCurvesEditorWidget::FindKeyIndex(const std::vector<Keyframe>& v,
                                             float time, float value) {
    for (size_t i = 0; i < v.size(); ++i) {
        if (std::fabs(v[i].time - time) < 1e-6f &&
            std::fabs(v[i].value - value) < 1e-6f) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

size_t VfxCurvesEditorWidget::FindStopIndex(const std::vector<VFXColorKey>& v,
                                              float t) {
    for (size_t i = 0; i < v.size(); ++i) {
        if (std::fabs(v[i].t - t) < 1e-6f) return i;
    }
    return static_cast<size_t>(-1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scalar key mutations
// ─────────────────────────────────────────────────────────────────────────────

size_t VfxCurvesEditorWidget::AddKey(float time, float value, bool snapEnabled) {
    if (m_mode != VfxCurveBindingMode::Float || m_curve == nullptr) {
        return static_cast<size_t>(-1);
    }
    if (std::isnan(time) || std::isnan(value)) return static_cast<size_t>(-1);

    const float t = snapEnabled ? Snap(time, snapTime) : time;
    const float v = snapEnabled ? Snap(value, snapValue) : value;

    m_keyMirror.push_back({t, v});
    SortByTime(m_keyMirror);
    CommitFloatKeys();
    MarkDirty();
    if (onCurveMutated) onCurveMutated();
    return FindKeyIndex(m_keyMirror, t, v);
}

size_t VfxCurvesEditorWidget::MoveKey(size_t index, float newTime, float newValue,
                                       bool snapEnabled) {
    if (m_mode != VfxCurveBindingMode::Float || m_curve == nullptr) {
        return static_cast<size_t>(-1);
    }
    if (index >= m_keyMirror.size()) return static_cast<size_t>(-1);

    const float t = snapEnabled ? Snap(newTime, snapTime) : newTime;
    const float v = snapEnabled ? Snap(newValue, snapValue) : newValue;

    m_keyMirror[index].time  = t;
    m_keyMirror[index].value = v;
    SortByTime(m_keyMirror);
    CommitFloatKeys();
    MarkDirty();
    if (onCurveMutated) onCurveMutated();
    return FindKeyIndex(m_keyMirror, t, v);
}

bool VfxCurvesEditorWidget::RemoveKey(size_t index) {
    if (m_mode != VfxCurveBindingMode::Float || m_curve == nullptr) return false;
    if (index >= m_keyMirror.size()) return false;

    m_keyMirror.erase(m_keyMirror.begin() + static_cast<std::ptrdiff_t>(index));
    CommitFloatKeys();
    MarkDirty();
    if (m_selectedIndex == index) m_selectedIndex = static_cast<size_t>(-1);
    if (onCurveMutated) onCurveMutated();
    return true;
}

bool VfxCurvesEditorWidget::SetInterpolation(InterpolationMode mode) {
    if (m_mode != VfxCurveBindingMode::Float || m_curve == nullptr) return false;
    m_curve->SetInterpolation(mode);
    MarkDirty();
    if (onCurveMutated) onCurveMutated();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ramp stop mutations
// ─────────────────────────────────────────────────────────────────────────────

size_t VfxCurvesEditorWidget::AddStop(float t, float r, float g, float b, float a,
                                        bool snapEnabled) {
    if (m_mode != VfxCurveBindingMode::Ramp || m_ramp == nullptr) {
        return static_cast<size_t>(-1);
    }
    if (m_stopMirror.size() >=
        static_cast<size_t>(VFXColorRamp::kMaxStops)) {
        return static_cast<size_t>(-1);
    }

    const float st = snapEnabled ? Snap(t, snapTime) : t;
    VFXColorKey s{st, r, g, b, a};
    m_stopMirror.push_back(s);
    SortRampByT(m_stopMirror);
    CommitStops();
    MarkDirty();
    if (onRampMutated) onRampMutated();
    return FindStopIndex(m_stopMirror, st);
}

size_t VfxCurvesEditorWidget::MoveStop(size_t index, float newT, bool snapEnabled) {
    if (m_mode != VfxCurveBindingMode::Ramp || m_ramp == nullptr) {
        return static_cast<size_t>(-1);
    }
    if (index >= m_stopMirror.size()) return static_cast<size_t>(-1);

    const float st = snapEnabled ? Snap(newT, snapTime) : newT;
    m_stopMirror[index].t = st;
    SortRampByT(m_stopMirror);
    CommitStops();
    MarkDirty();
    if (onRampMutated) onRampMutated();
    return FindStopIndex(m_stopMirror, st);
}

bool VfxCurvesEditorWidget::SetStopColor(size_t index, float r, float g, float b, float a) {
    if (m_mode != VfxCurveBindingMode::Ramp || m_ramp == nullptr) return false;
    if (index >= m_stopMirror.size()) return false;
    m_stopMirror[index].r = r;
    m_stopMirror[index].g = g;
    m_stopMirror[index].b = b;
    m_stopMirror[index].a = a;
    CommitStops();
    MarkDirty();
    if (onRampMutated) onRampMutated();
    return true;
}

bool VfxCurvesEditorWidget::RemoveStop(size_t index) {
    if (m_mode != VfxCurveBindingMode::Ramp || m_ramp == nullptr) return false;
    if (index >= m_stopMirror.size()) return false;
    m_stopMirror.erase(m_stopMirror.begin() + static_cast<std::ptrdiff_t>(index));
    CommitStops();
    MarkDirty();
    if (m_selectedIndex == index) m_selectedIndex = static_cast<size_t>(-1);
    if (onRampMutated) onRampMutated();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Drag
// ─────────────────────────────────────────────────────────────────────────────

void VfxCurvesEditorWidget::BeginDrag(size_t index) noexcept {
    m_dragging = true;
    m_selectedIndex = index;
}

void VfxCurvesEditorWidget::EndDrag() noexcept {
    m_dragging = false;
    m_dirty = true;  // ensure one last re-bake after drag ends
}

// ─────────────────────────────────────────────────────────────────────────────
// Viewport
// ─────────────────────────────────────────────────────────────────────────────

void VfxCurvesEditorWidget::Pan(float screenDx, float screenDy,
                                  const VfxStackRect& canvas) noexcept {
    if (canvas.width <= 0.0f || canvas.height <= 0.0f) return;
    const float visibleW = 1.0f / viewport.zoomX;
    const float visibleH = 1.0f / viewport.zoomY;
    viewport.panX -= (screenDx / canvas.width)  * visibleW;
    viewport.panY += (screenDy / canvas.height) * visibleH;
}

void VfxCurvesEditorWidget::Zoom(float factor, float screenX, float screenY,
                                   const VfxStackRect& canvas) noexcept {
    if (canvas.width <= 0.0f || canvas.height <= 0.0f) return;
    if (factor <= 0.0f) return;

    // Anchor world coord under cursor before zoom.
    const float worldX = viewport.ScreenToWorldX(screenX, canvas);
    const float worldY = viewport.ScreenToWorldY(screenY, canvas);

    viewport.zoomX = std::clamp(viewport.zoomX * factor, 0.05f, 40.0f);
    viewport.zoomY = std::clamp(viewport.zoomY * factor, 0.05f, 40.0f);

    // Re-anchor: adjust pan so the same world coord stays under cursor.
    const float newWorldX = viewport.ScreenToWorldX(screenX, canvas);
    const float newWorldY = viewport.ScreenToWorldY(screenY, canvas);
    viewport.panX += (worldX - newWorldX);
    viewport.panY += (worldY - newWorldY);
}

// Phase 12.5.0
void VfxCurvesEditorWidget::HandleScroll(float dyWheel, float cursorX,
                                          float cursorY,
                                          const VfxStackRect& canvas) noexcept {
    // Positive wheel zooms in (factor > 1). One notch = 1.2x.
    const float factor = (dyWheel >= 0.0f) ? std::pow(1.2f, dyWheel)
                                            : std::pow(1.0f / 1.2f, -dyWheel);
    Zoom(factor, cursorX, cursorY, canvas);
}

void VfxCurvesEditorWidget::ZoomToFit() noexcept {
    if (m_mode == VfxCurveBindingMode::Float) {
        if (m_keyMirror.empty()) {
            viewport = VfxCurveViewport{};
            return;
        }
        float tMin = m_keyMirror.front().time;
        float tMax = m_keyMirror.back().time;
        float vMin = m_keyMirror.front().value;
        float vMax = vMin;
        for (const auto& k : m_keyMirror) {
            if (k.time  < tMin) tMin = k.time;
            if (k.time  > tMax) tMax = k.time;
            if (k.value < vMin) vMin = k.value;
            if (k.value > vMax) vMax = k.value;
        }
        const float spanT = std::max(1e-3f, tMax - tMin);
        const float spanV = std::max(1e-3f, vMax - vMin);
        viewport.panX  = tMin - spanT * 0.05f;
        viewport.zoomX = 1.0f / (spanT * 1.10f);
        viewport.panY  = (vMin + vMax) * 0.5f;
        viewport.zoomY = 1.0f / (spanV * 1.10f);
    } else if (m_mode == VfxCurveBindingMode::Ramp) {
        viewport.panX  = 0.0f;
        viewport.zoomX = 1.0f;
        viewport.panY  = 0.5f;
        viewport.zoomY = 1.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Re-bake scheduling
// ─────────────────────────────────────────────────────────────────────────────

bool VfxCurvesEditorWidget::ShouldRebakeNow(float idleSecondsSinceLastInput) const noexcept {
    if (!m_dirty) return false;
    if (!m_dragging) return true;
    return idleSecondsSinceLastInput >= debounceIdleSeconds;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxCurveLayoutItem> VfxCurvesEditorWidget::ComputeLayout(
    const VfxStackRect& canvas) const {
    std::vector<VfxCurveLayoutItem> out;
    if (m_mode == VfxCurveBindingMode::None) return out;

    // ── Phase 12.5.0 — top toolbar strip ───────────────────────────────
    // The canvas coordinate we receive is the FULL widget area; the
    // toolbar sits at the top and the plot area is reduced accordingly.
    VfxStackRect plotCanvas = canvas;
    if (canvas.height > toolbarHeight + 4.0f) {
        VfxCurveLayoutItem bar;
        bar.type = VfxCurveItemType::Toolbar;
        bar.rect = { canvas.x, canvas.y, canvas.width, toolbarHeight };
        out.push_back(bar);

        // Buttons laid out left to right, each `toolbarButtonW` wide.
        float bx = canvas.x + toolbarPadding;
        const float by = canvas.y + 2.0f;
        const float bh = toolbarHeight - 4.0f;

        auto emitButton = [&](VfxCurveItemType t) {
            VfxCurveLayoutItem b;
            b.type = t;
            b.rect = { bx, by, toolbarButtonW, bh };
            out.push_back(b);
            bx += toolbarButtonW + toolbarPadding;
        };
        emitButton(VfxCurveItemType::ButtonAddKey);
        emitButton(VfxCurveItemType::ButtonDeleteKey);
        emitButton(VfxCurveItemType::ButtonZoomFit);
        emitButton(VfxCurveItemType::ButtonSnapToggle);
        // Interp picker is wider (label + chevron).
        {
            VfxCurveLayoutItem p;
            p.type = VfxCurveItemType::InterpPicker;
            p.rect = { bx, by, toolbarButtonW * 2.5f, bh };
            out.push_back(p);
        }

        // Shrink plot canvas so downstream items render below the bar.
        plotCanvas.y      = canvas.y + toolbarHeight;
        plotCanvas.height = canvas.height - toolbarHeight;
    }

    // Canvas background (under plot area only).
    VfxCurveLayoutItem bg;
    bg.type = VfxCurveItemType::Canvas;
    bg.rect = plotCanvas;
    out.push_back(bg);

    // ── Phase 12.5.0 — grid lines + axis labels (Float mode only) ─────
    if (m_mode == VfxCurveBindingMode::Float &&
        plotCanvas.width > 0.0f && plotCanvas.height > 0.0f) {
        // World-space visible range.
        const float tMin = viewport.ScreenToWorldX(plotCanvas.x,
                                                    plotCanvas);
        const float tMax = viewport.ScreenToWorldX(plotCanvas.x + plotCanvas.width,
                                                    plotCanvas);
        const float vMaxScreenY = plotCanvas.y;
        const float vMinScreenY = plotCanvas.y + plotCanvas.height;
        const float vMax = viewport.ScreenToWorldY(vMaxScreenY, plotCanvas);
        const float vMin = viewport.ScreenToWorldY(vMinScreenY, plotCanvas);

        // Major vertical lines at integer time values within [tMin, tMax].
        const int tLo = static_cast<int>(std::floor(tMin));
        const int tHi = static_cast<int>(std::ceil (tMax));
        for (int i = tLo; i <= tHi; ++i) {
            const float sx = viewport.WorldToScreenX(
                static_cast<float>(i), plotCanvas);
            if (sx < plotCanvas.x || sx > plotCanvas.x + plotCanvas.width) continue;
            VfxCurveLayoutItem g;
            g.type = VfxCurveItemType::GridMajor;
            g.rect = { sx, plotCanvas.y, 1.0f, plotCanvas.height };
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%d", i);
            g.label = buf;
            out.push_back(g);

            // 4 minor subdivisions per major interval.
            for (int m = 1; m < 4; ++m) {
                const float t = static_cast<float>(i) + m * 0.25f;
                const float sxm = viewport.WorldToScreenX(t, plotCanvas);
                if (sxm < plotCanvas.x || sxm > plotCanvas.x + plotCanvas.width) continue;
                VfxCurveLayoutItem mg;
                mg.type = VfxCurveItemType::GridMinor;
                mg.rect = { sxm, plotCanvas.y, 1.0f, plotCanvas.height };
                out.push_back(mg);
            }
        }

        // Major horizontal lines at 0.0, 0.5, 1.0.
        const float kMajorY[] = { 0.0f, 0.5f, 1.0f };
        for (float v : kMajorY) {
            if (v < vMin || v > vMax) continue;
            const float sy = viewport.WorldToScreenY(v, plotCanvas);
            VfxCurveLayoutItem g;
            g.type = VfxCurveItemType::GridMajor;
            g.rect = { plotCanvas.x, sy, plotCanvas.width, 1.0f };
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%.1f", v);
            g.label = buf;
            out.push_back(g);
        }
    }

    if (m_mode == VfxCurveBindingMode::Float) {
        for (size_t i = 0; i < m_keyMirror.size(); ++i) {
            const auto& k = m_keyMirror[i];
            const float sx = viewport.WorldToScreenX(k.time, plotCanvas);
            const float sy = viewport.WorldToScreenY(k.value, plotCanvas);
            VfxCurveLayoutItem it;
            it.type  = VfxCurveItemType::KeyDot;
            it.index = i;
            it.rect  = {
                sx - kKeyHitRadius,
                sy - kKeyHitRadius,
                kKeyHitRadius * 2.0f,
                kKeyHitRadius * 2.0f
            };
            out.push_back(it);
        }

        // Phase 12.5.0 — tangent handles for the selected key.
        if (m_selectedIndex < m_keyMirror.size()) {
            const auto& k = m_keyMirror[m_selectedIndex];
            const float sx = viewport.WorldToScreenX(k.time,  plotCanvas);
            const float sy = viewport.WorldToScreenY(k.value, plotCanvas);
            VfxCurveLayoutItem lh;
            lh.type  = VfxCurveItemType::TangentHandleLeft;
            lh.index = m_selectedIndex;
            lh.rect  = {
                sx - tangentHandleOffsetPx - tangentHandleSize * 0.5f,
                sy - tangentHandleSize * 0.5f,
                tangentHandleSize, tangentHandleSize
            };
            out.push_back(lh);
            VfxCurveLayoutItem rh;
            rh.type  = VfxCurveItemType::TangentHandleRight;
            rh.index = m_selectedIndex;
            rh.rect  = {
                sx + tangentHandleOffsetPx - tangentHandleSize * 0.5f,
                sy - tangentHandleSize * 0.5f,
                tangentHandleSize, tangentHandleSize
            };
            out.push_back(rh);
        }
    } else {
        // Ramp ribbon sits horizontally centred in the plot area.
        VfxCurveLayoutItem rib;
        rib.type = VfxCurveItemType::RampRibbon;
        rib.rect = {
            plotCanvas.x,
            plotCanvas.y + (plotCanvas.height - ribbonHeight) * 0.5f,
            plotCanvas.width,
            ribbonHeight
        };
        out.push_back(rib);

        for (size_t i = 0; i < m_stopMirror.size(); ++i) {
            const auto& s = m_stopMirror[i];
            const float sx = plotCanvas.x + s.t * plotCanvas.width;
            const float sy = rib.rect.y + rib.rect.height * 0.5f;
            VfxCurveLayoutItem it;
            it.type  = VfxCurveItemType::RampStop;
            it.index = i;
            it.rect  = {
                sx - kStopHitRadius,
                sy - kStopHitRadius,
                kStopHitRadius * 2.0f,
                kStopHitRadius * 2.0f
            };
            out.push_back(it);
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxCurveHitResult VfxCurvesEditorWidget::HitTest(
    const std::vector<VfxCurveLayoutItem>& layout,
    float x, float y) const {
    VfxCurveHitResult hit;
    for (const auto& item : layout) {
        if (!item.rect.Contains(x, y)) continue;
        // Grid lines and axis labels are paint-only. Their enum values sit
        // above KeyDot, so letting them compete would hide every key that
        // lies on a grid line (t = 0, 0.25, ..., v = 0, 0.5, 1).
        if (item.type == VfxCurveItemType::GridMajor ||
            item.type == VfxCurveItemType::GridMinor ||
            item.type == VfxCurveItemType::AxisLabel) {
            continue;
        }
        if (static_cast<int>(item.type) > static_cast<int>(hit.type) ||
            hit.type == VfxCurveItemType::None) {
            hit.type  = item.type;
            hit.index = item.index;
        }
    }
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxCurvesEditorWidget::Render(TextRenderer& text, const BitmapFont& font,
                                     const VfxStackRect& canvas) const {
    if (m_mode == VfxCurveBindingMode::None) return;

    const auto& palette = VfxThemeColors();
    const auto layout   = ComputeLayout(canvas);

    // Infer the plot-canvas sub-rect from the Canvas item so renders of
    // the curve stroke / ramp gradient stay inside the toolbar-aware
    // region computed by `ComputeLayout`.
    VfxStackRect plotCanvas = canvas;
    for (const auto& it : layout) {
        if (it.type == VfxCurveItemType::Canvas) { plotCanvas = it.rect; break; }
    }

    // Outermost background fills the full widget area.
    text.AddRect(canvas.x, canvas.y, canvas.width, canvas.height, palette.backgroundBase);

    // ── Pass 1 — items that should render under keys/ribbon ───────────
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxCurveItemType::Toolbar:
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.barBg);
            break;
        case VfxCurveItemType::Canvas:
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.panelBg);
            break;
        case VfxCurveItemType::GridMinor:
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.gridMinor);
            break;
        case VfxCurveItemType::GridMajor: {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.gridMajor);
            // Axis label at the gridline's origin.
            if (!it.label.empty()) {
                const bool isVertical = (it.rect.height > it.rect.width);
                const float lx = isVertical ? it.rect.x + 2.0f
                                              : plotCanvas.x + 2.0f;
                const float ly = isVertical ? plotCanvas.y + 2.0f
                                              : it.rect.y + 2.0f;
                text.AddText(font, it.label, lx, ly, palette.textDim);
            }
            break;
        }
        default: break;
        }
    }

    // ── Curve stroke (sampled densely, 2-px segments) ─────────────────
    if (m_mode == VfxCurveBindingMode::Float && m_curve != nullptr &&
        plotCanvas.width > 0.0f) {
        // One sample every 2 px of X gives visual smoothness without
        // GPU overhead.
        const int samples = std::max(8, static_cast<int>(plotCanvas.width * 0.5f));
        float prevSx = 0.0f, prevSy = 0.0f;
        for (int i = 0; i <= samples; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(samples);
            const float sx = plotCanvas.x + u * plotCanvas.width;
            const float t  = viewport.ScreenToWorldX(sx, plotCanvas);
            const float v  = m_curve->SampleFloat(std::clamp(t, 0.0f, 1.0f));
            const float sy = viewport.WorldToScreenY(v, plotCanvas);
            if (i > 0) {
                const float x0 = std::min(prevSx, sx);
                const float w  = std::max(1.0f, std::fabs(sx - prevSx));
                const float y0 = std::min(prevSy, sy) - 1.0f;
                const float h  = std::max(2.0f, std::fabs(sy - prevSy) + 2.0f);
                text.AddRect(x0, y0, w, h, palette.accentPrimary);
            }
            prevSx = sx;
            prevSy = sy;
        }
    }

    // ── Pass 2 — ramp gradient + stops, keys, tangents, toolbar ───────
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxCurveItemType::RampRibbon: {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.inputBg);
            // Gradient: 64 stepped segments across the ribbon.
            if (!m_stopMirror.empty()) {
                const int N = 64;
                for (int i = 0; i < N; ++i) {
                    const float u = static_cast<float>(i) /
                                     static_cast<float>(N - 1);
                    float r = m_stopMirror.front().r;
                    float g = m_stopMirror.front().g;
                    float b = m_stopMirror.front().b;
                    float a = m_stopMirror.front().a;
                    for (size_t j = 0; j + 1 < m_stopMirror.size(); ++j) {
                        const auto& s0 = m_stopMirror[j];
                        const auto& s1 = m_stopMirror[j + 1];
                        if (u >= s0.t && u <= s1.t && s1.t > s0.t) {
                            const float lt = (u - s0.t) / (s1.t - s0.t);
                            r = s0.r + (s1.r - s0.r) * lt;
                            g = s0.g + (s1.g - s0.g) * lt;
                            b = s0.b + (s1.b - s0.b) * lt;
                            a = s0.a + (s1.a - s0.a) * lt;
                            break;
                        }
                    }
                    if (u >= m_stopMirror.back().t) {
                        r = m_stopMirror.back().r;
                        g = m_stopMirror.back().g;
                        b = m_stopMirror.back().b;
                        a = m_stopMirror.back().a;
                    }
                    const float x0 = it.rect.x + u * it.rect.width;
                    const float x1 = it.rect.x + ((i + 1.0f) / N) * it.rect.width;
                    text.AddRect(x0, it.rect.y,
                                 std::max(1.0f, x1 - x0),
                                 it.rect.height, Vec4{r, g, b, a});
                }
            }
            break;
        }
        case VfxCurveItemType::KeyDot: {
            // Phase 13.5 — sprite diamond from the VFX icon atlas.
            const float cx = it.rect.x + it.rect.width * 0.5f;
            const float cy = it.rect.y + it.rect.height * 0.5f;
            const bool selected = (it.index == m_selectedIndex);
            const Vec4 fill = selected ? palette.stateInfo
                                          : palette.accentPrimary;
            const float size = keyDiamondHalfSize * 2.0f;
            text.AddSprite(VfxUiIcon::Diamond,
                            cx - size * 0.5f, cy - size * 0.5f,
                            size, size, fill);
            break;
        }
        case VfxCurveItemType::RampStop: {
            if (it.index < m_stopMirror.size()) {
                const auto& s = m_stopMirror[it.index];
                const bool sel = (it.index == m_selectedIndex);
                const Vec4 border = sel ? palette.textPrimary
                                           : palette.borderStrong;
                // Outer border
                text.AddRect(it.rect.x - 1.0f, it.rect.y - 1.0f,
                             it.rect.width + 2.0f, it.rect.height + 2.0f,
                             border);
                // Colour fill
                text.AddRect(it.rect.x, it.rect.y, it.rect.width,
                             it.rect.height, Vec4{s.r, s.g, s.b, 1.0f});
            }
            break;
        }
        case VfxCurveItemType::TangentHandleLeft:
        case VfxCurveItemType::TangentHandleRight: {
            // Phase 13.5 — sprite circle tangent handle.
            text.AddSprite(VfxUiIcon::Circle,
                            it.rect.x, it.rect.y,
                            it.rect.width, it.rect.height,
                            palette.stateInfo);
            break;
        }

        // Toolbar buttons
        case VfxCurveItemType::ButtonAddKey: {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.cardBg);
            text.AddText(font, "+", it.rect.x + 8.0f, it.rect.y + 4.0f,
                         palette.textPrimary);
            break;
        }
        case VfxCurveItemType::ButtonDeleteKey: {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.cardBg);
            text.AddText(font, "x", it.rect.x + 8.0f, it.rect.y + 4.0f,
                         palette.textPrimary);
            break;
        }
        case VfxCurveItemType::ButtonZoomFit: {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.cardBg);
            text.AddText(font, "[]", it.rect.x + 4.0f, it.rect.y + 4.0f,
                         palette.textPrimary);
            break;
        }
        case VfxCurveItemType::ButtonSnapToggle: {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         m_snapEnabled ? palette.accentMuted : palette.cardBg);
            text.AddText(font, m_snapEnabled ? "::" : ":.",
                         it.rect.x + 4.0f, it.rect.y + 4.0f,
                         palette.textPrimary);
            break;
        }
        case VfxCurveItemType::InterpPicker: {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.cardBg);
            const char* label = "Linear";
            if (m_curve != nullptr) {
                switch (m_curve->GetInterpolation()) {
                    case InterpolationMode::Linear:     label = "Linear"; break;
                    case InterpolationMode::Cubic:      label = "Cubic"; break;
                    case InterpolationMode::Smoothstep: label = "Smooth"; break;
                }
            }
            text.AddText(font, label, it.rect.x + 6.0f, it.rect.y + 4.0f,
                         palette.textPrimary);
            text.AddText(font, "v",
                         it.rect.x + it.rect.width - 10.0f,
                         it.rect.y + 4.0f, palette.textDim);
            break;
        }
        default: break;
        }
    }
}

} // namespace uz
