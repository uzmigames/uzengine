#pragma once

/// VFX Curves Editor Widget — canvas-based editor for `VFXCurve` + `VFXColorRamp`.
///
/// Phase 11.5.0. Binds to a scalar `VFXCurve*` (Float mode) or a
/// `VFXColorRamp*` (Ramp mode). Provides keyframe authoring with pan /
/// zoom, click-to-add, drag-to-move, snap-to-grid, and debounced GPU
/// re-bake via a dirty flag.
///
/// # Key mirror
///
/// `VFXCurve` exposes `SetKeys(vector)` but no per-key getter. The widget
/// therefore keeps a local mirror vector of the authored keys and calls
/// `SetKeys(mirror)` on every mutation. Callers seeding the editor with
/// an existing curve should call `AppendInitialKey` for each existing
/// key after `BindFloat`.
///
/// # Testability
///
/// All mutation + viewport math is pure-data: `AddKey`, `MoveKey`,
/// `RemoveKey`, `AddStop`, `MoveStop`, `RemoveStop`, `WorldToScreen` /
/// `ScreenToWorld`, `HitTest`, dirty / debounce bookkeeping. The
/// `Render` call is a thin pass over the layout; the host re-bakes the
/// curve only when `ShouldRebakeNow` returns true.
///
/// @ref UE5 `SCurveEditor`, `SNiagaraCurveOverview`
/// @ref engine/vfx/curves/vfx_curve.h
/// @ref engine/vfx/curves/vfx_color_ramp.h

#include "engine/vfx/curves/vfx_color_ramp.h"
#include "engine/vfx/curves/vfx_curve.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h" // VfxStackRect

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;

// ─────────────────────────────────────────────────────────────────────────────
// Viewport pan/zoom state
// ─────────────────────────────────────────────────────────────────────────────

/// Affine mapping between curve-space (time ∈ [0,1], value) and the
/// canvas rectangle. Pure math — no widget / GPU state.
struct VfxCurveViewport {
    float panX  = 0.0f;
    float zoomX = 1.0f;
    float panY  = 0.5f;
    float zoomY = 1.0f;

    [[nodiscard]] float WorldToScreenX(float t, const VfxStackRect& canvas) const noexcept {
        const float visibleW = 1.0f / zoomX;
        return canvas.x + ((t - panX) / visibleW) * canvas.width;
    }
    [[nodiscard]] float WorldToScreenY(float v, const VfxStackRect& canvas) const noexcept {
        const float visibleH = 1.0f / zoomY;
        const float vMin = panY - visibleH * 0.5f;
        const float rel  = (v - vMin) / visibleH;
        return canvas.y + canvas.height * (1.0f - rel);
    }
    [[nodiscard]] float ScreenToWorldX(float x, const VfxStackRect& canvas) const noexcept {
        const float visibleW = 1.0f / zoomX;
        return panX + ((x - canvas.x) / canvas.width) * visibleW;
    }
    [[nodiscard]] float ScreenToWorldY(float y, const VfxStackRect& canvas) const noexcept {
        const float visibleH = 1.0f / zoomY;
        const float vMin = panY - visibleH * 0.5f;
        const float rel  = 1.0f - ((y - canvas.y) / canvas.height);
        return vMin + rel * visibleH;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout primitives
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxCurveBindingMode : uint8_t {
    None  = 0,
    Float = 1,
    Ramp  = 2,
};

enum class VfxCurveItemType : uint8_t {
    None       = 0,
    Canvas     = 1,   ///< Empty canvas area (click = add key / stop).
    RampRibbon = 2,   ///< Horizontal ribbon background for the ramp.
    KeyDot     = 3,   ///< Scalar keyframe marker.
    RampStop   = 4,   ///< Colour ramp stop marker.
    AxisLabel  = 5,
    // ── Phase 12.5.0 additions ────────────────────────────────────────
    Toolbar           = 10,  ///< Top strip background.
    ButtonAddKey      = 11,  ///< Toolbar: "+" button.
    ButtonDeleteKey   = 12,  ///< Toolbar: delete button (for selected key).
    ButtonZoomFit     = 13,  ///< Toolbar: zoom-to-fit button.
    ButtonSnapToggle  = 14,  ///< Toolbar: snap-on/off toggle.
    InterpPicker      = 15,  ///< Toolbar: interpolation dropdown trigger.
    GridMajor         = 20,  ///< Major grid line (integer time / value 0/0.5/1).
    GridMinor         = 21,  ///< Minor grid line (subdivision).
    TangentHandleLeft  = 30, ///< Left tangent drag handle on selected key.
    TangentHandleRight = 31, ///< Right tangent drag handle on selected key.
};

struct VfxCurveLayoutItem {
    VfxCurveItemType type = VfxCurveItemType::None;
    size_t index = static_cast<size_t>(-1);
    VfxStackRect rect;
    std::string  label;
};

struct VfxCurveHitResult {
    VfxCurveItemType type = VfxCurveItemType::None;
    size_t index = static_cast<size_t>(-1);
    [[nodiscard]] bool IsHit() const noexcept {
        return type != VfxCurveItemType::None;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxCurvesEditorWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float keyDotRadius       = 5.0f;
    float stopDiamondSize    = 8.0f;
    float ribbonHeight       = 24.0f;
    float snapTime           = 0.01f;
    float snapValue          = 0.001f;
    float debounceIdleSeconds = 0.1f;

    // ── Phase 12.5.0 tunables ───────────────────────────────────────────
    float toolbarHeight      = 24.0f;  ///< Top strip height.
    float toolbarButtonW     = 26.0f;  ///< Per-button width.
    float toolbarPadding     = 4.0f;   ///< Padding between buttons.
    float tangentHandleSize  = 6.0f;   ///< Tangent drag handle square size.
    float tangentHandleOffsetPx = 24.0f; ///< Screen-space offset for tangent handle.
    float keyDiamondHalfSize = 6.0f;  ///< 12.5.0 diamond handle (replaces dot).
    float axisPadding        = 24.0f;  ///< Y-axis label gutter.

    // ── Binding ─────────────────────────────────────────────────────────

    /// Binds to a Float-type `VFXCurve`. Non-Float curves leave the
    /// widget in `None` mode. Clears the key mirror.
    void BindFloat(VFXCurve* curve) noexcept;

    /// Binds to a colour ramp.
    void BindRamp(VFXColorRamp* ramp) noexcept;

    /// Unbinds any bound curve / ramp.
    void Unbind() noexcept;

    [[nodiscard]] VfxCurveBindingMode Mode()  const noexcept { return m_mode; }
    [[nodiscard]] VFXCurve*           Curve() const noexcept { return m_curve; }
    [[nodiscard]] VFXColorRamp*       Ramp()  const noexcept { return m_ramp; }

    /// Pushes an existing keyframe into the mirror (sorted insertion)
    /// without re-committing the curve or touching the dirty flag. Used
    /// by callers who need to populate the editor from a pre-authored
    /// curve that the widget can't read back (`VFXCurve` has no
    /// per-key getter).
    void AppendInitialKey(float time, float value);

    // ── Scalar key mutations ────────────────────────────────────────────

    /// Inserts a keyframe (sorted). Returns new index on success,
    /// `SIZE_MAX` on failure (wrong mode / NaN).
    size_t AddKey(float time, float value, bool snapEnabled = true);

    /// Moves an existing key. Re-sorts on crossing; returned index is
    /// the new slot.
    size_t MoveKey(size_t index, float newTime, float newValue,
                    bool snapEnabled = true);

    /// Removes a key. Returns false on out-of-range / wrong mode.
    bool RemoveKey(size_t index);

    /// Sets curve-wide interpolation.
    bool SetInterpolation(InterpolationMode mode);

    /// Returns the current keyframes (sorted).
    [[nodiscard]] const std::vector<Keyframe>& Keys() const noexcept { return m_keyMirror; }

    // ── Ramp stop mutations ─────────────────────────────────────────────

    /// Inserts a stop. `SIZE_MAX` on failure (too many stops, wrong
    /// mode). Sorted insertion.
    size_t AddStop(float t, float r, float g, float b, float a,
                    bool snapEnabled = true);

    /// Moves a stop's position.
    size_t MoveStop(size_t index, float newT, bool snapEnabled = true);

    /// Sets a stop's colour by index.
    bool SetStopColor(size_t index, float r, float g, float b, float a);

    /// Removes a stop.
    bool RemoveStop(size_t index);

    /// Returns sorted snapshot of the ramp stops.
    [[nodiscard]] const std::vector<VFXColorKey>& Stops() const noexcept { return m_stopMirror; }

    // ── Selection + drag ────────────────────────────────────────────────

    [[nodiscard]] size_t SelectedIndex() const noexcept { return m_selectedIndex; }
    void SelectIndex(size_t index) noexcept { m_selectedIndex = index; }
    void ClearSelection() noexcept { m_selectedIndex = static_cast<size_t>(-1); }

    [[nodiscard]] bool IsDragging() const noexcept { return m_dragging; }
    void BeginDrag(size_t index) noexcept;
    void EndDrag() noexcept;

    // ── Viewport ────────────────────────────────────────────────────────

    VfxCurveViewport viewport{};

    void Pan(float screenDx, float screenDy, const VfxStackRect& canvas) noexcept;
    void Zoom(float factor, float screenX, float screenY,
              const VfxStackRect& canvas) noexcept;

    /// Phase 12.5.0 — scroll-wheel zoom that keeps the world-space
    /// point under the cursor anchored on screen. `dyWheel > 0` zooms in.
    void HandleScroll(float dyWheel, float cursorX, float cursorY,
                       const VfxStackRect& canvas) noexcept;

    /// Phase 12.5.0 — middle-mouse pan gesture hooks. `dx` / `dy` are
    /// screen-space deltas accumulated since the last call.
    void BeginPan() noexcept  { m_panning = true; }
    void UpdatePan(float screenDx, float screenDy,
                    const VfxStackRect& canvas) noexcept {
        if (m_panning) Pan(screenDx, screenDy, canvas);
    }
    void EndPan() noexcept    { m_panning = false; }
    [[nodiscard]] bool IsPanning() const noexcept { return m_panning; }

    /// Phase 12.5.0 — resets viewport so every key is visible.
    void ZoomToFit() noexcept;

    /// Phase 12.5.0 — snap toggle (affects `AddKey` / `MoveKey` calls
    /// that pass `snapEnabled = m_snapEnabled`).
    [[nodiscard]] bool IsSnapEnabled() const noexcept { return m_snapEnabled; }
    void SetSnapEnabled(bool on) noexcept { m_snapEnabled = on; }
    bool ToggleSnapEnabled() noexcept { m_snapEnabled = !m_snapEnabled; return m_snapEnabled; }

    // ── Dirty / re-bake ─────────────────────────────────────────────────

    [[nodiscard]] bool IsDirty()  const noexcept { return m_dirty; }
    void MarkDirty()  noexcept { m_dirty = true; }
    void ClearDirty() noexcept { m_dirty = false; }

    /// True iff (dirty AND (not dragging OR idle enough)).
    [[nodiscard]] bool ShouldRebakeNow(float idleSecondsSinceLastInput) const noexcept;

    // ── Layout + hit-test + render ──────────────────────────────────────

    [[nodiscard]] std::vector<VfxCurveLayoutItem> ComputeLayout(
        const VfxStackRect& canvas) const;

    [[nodiscard]] VfxCurveHitResult HitTest(
        const std::vector<VfxCurveLayoutItem>& layout,
        float x, float y) const;

    void Render(TextRenderer& text, const BitmapFont& font,
                const VfxStackRect& canvas) const;

    // ── Callbacks ───────────────────────────────────────────────────────

    std::function<void()> onCurveMutated;
    std::function<void()> onRampMutated;

    /// Phase 12.5.0 — fires when the author drags either tangent
    /// handle on the selected key. `leftSlope` / `rightSlope` are in
    /// world-space value-per-time units.
    std::function<void(size_t keyIndex, float leftSlope, float rightSlope)>
        onKeyTangentChanged;

    /// Phase 12.5.0 — fires when a toolbar button is clicked
    /// (ButtonAddKey / Delete / ZoomFit / SnapToggle / InterpPicker).
    std::function<void(VfxCurveItemType button)> onToolbarAction;

    /// Rounds `v` to the nearest multiple of `step` (no-op if step ≤ 0).
    [[nodiscard]] static float Snap(float v, float step) noexcept;

private:
    VfxCurveBindingMode m_mode  = VfxCurveBindingMode::None;
    VFXCurve*           m_curve = nullptr;
    VFXColorRamp*       m_ramp  = nullptr;

    std::vector<Keyframe>     m_keyMirror;   // Float-mode canonical list
    std::vector<VFXColorKey>  m_stopMirror;  // Ramp-mode canonical list

    size_t m_selectedIndex = static_cast<size_t>(-1);
    bool   m_dragging = false;
    bool   m_dirty    = false;

    // Phase 12.5.0
    bool m_panning     = false;
    bool m_snapEnabled = true;

    /// Commits the mirror back to the bound `VFXCurve` via `SetKeys`.
    void CommitFloatKeys();

    /// Commits the mirror back to the bound `VFXColorRamp` by
    /// overwriting `stops[0..stopCount]`.
    void CommitStops();

    /// Sort-helper: in-place sort by ascending time.
    template <typename T>
    static void SortByTime(std::vector<T>& v) {
        std::sort(v.begin(), v.end(), [](const T& a, const T& b) {
            return a.time < b.time;
        });
    }

    /// Variant of SortByTime for ramp keys (use `.t` not `.time`).
    static void SortRampByT(std::vector<VFXColorKey>& v);

    /// Finds the index of `target` after mutation (by near-equal value
    /// search), or `SIZE_MAX` when missing. Used for the public API's
    /// index remap semantic after a re-sort.
    static size_t FindKeyIndex(const std::vector<Keyframe>& v, float time, float value);
    static size_t FindStopIndex(const std::vector<VFXColorKey>& v, float t);
};

} // namespace uz
