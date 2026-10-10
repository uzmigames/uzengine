#pragma once

/// VFX Timeline Scrubber Widget — bottom-anchored preview transport.
///
/// Phase 11.6.0. Binds to a `VfxPreviewClock*` and exposes the Niagara
/// transport controls: play / pause, frame-step ±, speed ring, loop
/// toggle, restart, and a draggable seek bar with time display.
/// Interaction is routed through the preview clock's existing API
/// (`Play` / `Pause` / `StepFrame` / `Scrub` / `SetPlayRate`); loop +
/// restart have no direct clock hooks so the widget owns a local
/// `m_loop` flag and raises `onRestartRequested` / `onLoopChanged`
/// callbacks for the host to wire into the system instance.
///
/// # Testability
///
/// All control state + seek math is pure-data: `TogglePlayPause`,
/// `StepBackward`, `StepForward`, `CycleSpeed`, `ToggleLoop`,
/// `RequestRestart`, `SeekByNormalised`, `FormatTimeDisplay`,
/// `ComputeLayout`, and `HitTest` are unit-tested without any GPU
/// context. Auto-loop behaviour is exercised through `TickAutoLoop`.
///
/// @ref UE5 `SNiagaraSystemViewportToolBar`
/// @ref engine/vfx/editor/vfx_preview_clock.h
/// @ref docs/engine/25-vfx/21-preview-timeline.md

#include "engine/vfx/editor/vfx_preview_clock.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h" // VfxStackRect
#include "engine/vfx/spawn/vfx_burst_event.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;

// ─────────────────────────────────────────────────────────────────────────────
// Layout primitives
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxTimelineItemType : uint8_t {
    None        = 0,
    Strip       = 1,   ///< Bar background (fallback when nothing else matches).
    TimeLabel   = 2,   ///< Phase 12.6.0 renders as `TimeReadout` ("cur / total s").
    PlayButton  = 3,   ///< Phase 12.6.0 `TogglePlayButton` — glyph swaps on clock state.
    StepBackBtn = 4,
    StepFwdBtn  = 5,
    SpeedBtn    = 6,   ///< Phase 12.6.0 `SpeedDropdown` — trigger shows `<rate>x + v`.
    LoopBtn     = 7,   ///< Phase 12.6.0 `LoopToggle` — accent when enabled.
    RestartBtn  = 8,
    SeekTrack   = 9,
    BurstMarker = 10,  ///< Phase 12.6.0 renders as upward triangle scaled by log2(count+1).
    SeekHandle  = 11,  ///< The draggable indicator on the seek track.
    // Phase 12.6.0 additions
    SkipStartBtn = 12, ///< Skip to t=0.
    SkipEndBtn   = 13, ///< Skip to t=duration.
};

struct VfxTimelineLayoutItem {
    VfxTimelineItemType type = VfxTimelineItemType::None;
    VfxStackRect        rect;
    std::string         label;   ///< Button label when applicable.
    size_t              burstIndex = static_cast<size_t>(-1); ///< BurstMarker only.
};

struct VfxTimelineHitResult {
    VfxTimelineItemType type = VfxTimelineItemType::None;
    size_t              burstIndex = static_cast<size_t>(-1);

    [[nodiscard]] bool IsHit() const noexcept {
        return type != VfxTimelineItemType::None;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

/// Supported preview speeds, in ring order. `CycleSpeed()` advances to
/// the next ring entry; `SetSpeed(value)` sets directly.
inline constexpr std::array<float, 5> kVfxTimelineSpeeds{
    0.25f, 0.5f, 1.0f, 2.0f, 4.0f
};

class VfxTimelineScrubberWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float stripHeight    = 32.0f;
    float buttonWidth    = 28.0f;
    float buttonHeight   = 24.0f;
    float buttonGap      = 2.0f;
    float timeLabelWidth = 108.0f;
    float seekMinWidth   = 120.0f;

    /// Number of decimal places in the time display (e.g. 2 → "1.23s").
    int timeDecimals = 2;

    // ── Binding ─────────────────────────────────────────────────────────

    /// Binds the widget to a preview clock. Passing nullptr unbinds —
    /// in which case layout is empty and controls are no-ops.
    void Bind(VfxPreviewClock* clock) noexcept { m_clock = clock; }

    /// Binds the widget to a burst list so markers render over the seek
    /// track. Pass `nullptr` to stop drawing markers. Does not take
    /// ownership — the vector must outlive the widget.
    void BindBursts(const std::vector<VfxBurstEvent>* bursts) noexcept {
        m_bursts = bursts;
    }

    [[nodiscard]] VfxPreviewClock* Clock() noexcept { return m_clock; }
    [[nodiscard]] const VfxPreviewClock* Clock() const noexcept { return m_clock; }

    // ── Control state / mutations ───────────────────────────────────────

    /// Returns true when the bound clock is in `Playing` state.
    [[nodiscard]] bool IsPlaying() const noexcept;

    /// Flips play / pause on the bound clock. No-op when unbound.
    void TogglePlayPause() noexcept;

    /// Advances one `(1/60s)` frame backwards (clamps at t=0). Used via
    /// the `<` button while paused.
    void StepBackward() noexcept;

    /// Advances one `(1/60s)` frame forwards. Uses `StepFrame` on the
    /// bound clock so the single-shot Stepping state machine applies.
    void StepForward() noexcept;

    /// Phase 12.6.0 — seeks the clock to t=0.
    void SkipToStart() noexcept;

    /// Phase 12.6.0 — seeks the clock to `Duration()`.
    void SkipToEnd() noexcept;

    /// Current ring-speed index. `SetSpeed(v)` with a non-ring value
    /// picks the nearest ring entry.
    [[nodiscard]] float Speed() const noexcept;

    /// Advances to the next speed in `kVfxTimelineSpeeds`, wrapping at
    /// the end. Writes through to `clock->SetPlayRate`.
    void CycleSpeed() noexcept;

    /// Sets the speed directly. Picks the nearest ring entry to keep the
    /// UI in sync with the `CycleSpeed` behaviour.
    void SetSpeed(float rate) noexcept;

    /// Loop flag — owned by the widget (the clock has no loop concept).
    [[nodiscard]] bool IsLoopEnabled() const noexcept { return m_loop; }

    /// Toggles the loop flag. Fires `onLoopChanged`.
    bool ToggleLoop() noexcept;

    /// Explicit setter for the loop flag. Fires `onLoopChanged` only on
    /// change.
    void SetLoop(bool enabled) noexcept;

    /// Fires `onRestartRequested`. The host is responsible for routing
    /// to the bound `VfxSystemInstance`'s restart pipeline.
    void RequestRestart() noexcept;

    // ── Seek ────────────────────────────────────────────────────────────

    /// Reads the current time (seconds) from the bound clock.
    [[nodiscard]] float CurrentTime() const noexcept;

    /// Reads the total duration from the bound clock.
    [[nodiscard]] float Duration() const noexcept;

    /// Seeks the clock to an absolute time (seconds), clamped to
    /// `[0, Duration()]`.
    void Seek(float seconds) noexcept;

    /// Seeks by a normalised `[0, 1]` fraction of the duration. Used
    /// by the seek-bar drag + click.
    void SeekByNormalised(float t) noexcept;

    /// Returns the current time as a fraction in `[0, 1]`. Zero when
    /// duration is zero / clock unbound.
    [[nodiscard]] float NormalisedTime() const noexcept;

    /// Formats the time display as `"{cur:.Ns} / {total:.Ns}"` where N =
    /// `timeDecimals`. `Seconds()` values are shown in seconds with a
    /// trailing `s` suffix.
    [[nodiscard]] std::string FormatTimeDisplay() const;

    // ── Auto-loop ──────────────────────────────────────────────────────

    /// Called once per frame by the host *after* `clock->Tick()`. If
    /// looping is enabled and the clock has reached the duration, the
    /// widget fires `onRestartRequested` + resets the clock to t=0.
    /// Returns true when an auto-restart happened this call.
    bool TickAutoLoop() noexcept;

    // ── Layout + hit-test + render ──────────────────────────────────────

    /// Builds the layout rectangles for the strip within `panelBounds`.
    /// The strip is centred on `panelBounds.y` with `stripHeight` height
    /// (caller positions the panel at the bottom of the viewport).
    [[nodiscard]] std::vector<VfxTimelineLayoutItem> ComputeLayout(
        const VfxStackRect& panelBounds) const;

    /// Returns the most-specific item under the cursor. Priority follows
    /// the enum ordering (larger value wins).
    [[nodiscard]] VfxTimelineHitResult HitTest(
        const std::vector<VfxTimelineLayoutItem>& layout,
        float x, float y) const;

    /// Thin pass over `ComputeLayout`. No-op when the widget is unbound.
    void Render(TextRenderer& text, const BitmapFont& font,
                const VfxStackRect& panelBounds) const;

    // ── Callbacks (host wires into VfxSystemInstance) ──────────────────

    /// Fires on Restart button click and on auto-loop rollovers. Host
    /// should mark the system instance for restart (drains all emitters
    /// + clears particle buffers).
    std::function<void()> onRestartRequested;

    /// Fires when the loop toggle flips.
    std::function<void(bool enabled)> onLoopChanged;

    // ── Seek-drag lifecycle (host-driven) ───────────────────────────────

    /// Begins a seek-drag gesture — subsequent `SeekByNormalised` calls
    /// are expected during the drag. Pausing the clock here is the
    /// host's choice (typical Niagara-style UX is "pause while scrubbing,
    /// resume on release").
    void BeginSeek() noexcept { m_seeking = true; }

    /// Ends the seek-drag gesture.
    void EndSeek() noexcept { m_seeking = false; }

    [[nodiscard]] bool IsSeeking() const noexcept { return m_seeking; }

private:
    VfxPreviewClock*                       m_clock   = nullptr;
    const std::vector<VfxBurstEvent>*       m_bursts  = nullptr;
    bool                                   m_loop    = true;
    bool                                   m_seeking = false;

    /// Returns the index of `kVfxTimelineSpeeds` that is closest to the
    /// bound clock's current `GetPlayRate()`. Ties break towards 1x.
    [[nodiscard]] size_t ClosestSpeedIndex() const noexcept;
};

} // namespace uz
