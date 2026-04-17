# Proposal: phase11_11.6.0-niagara-timeline-scrubber-widget

## Why
`VfxPreviewClock` (phase 8.4) already exposes scrub / pause / frame-step state, but only through Lua calls — no visual timeline. Niagara's timeline scrubber is the most-used playback control: a horizontal bar showing current emitter time, a play/pause button, a speed multiplier, and a restart button. Without it, iterating on a 5-second explosion means waiting for it to fire naturally each time.

## What Changes
New `VfxTimelineScrubberWidget` in `runtime/engine/vfx/editor/`. Horizontal bar at the bottom of the viewport showing: current time display (`1.35s / 5.00s`), play/pause toggle, frame-step buttons (< / >), speed selector (`0.25x / 0.5x / 1x / 2x / 4x`), restart button, loop toggle, and a clickable seek bar with a draggable handle. Binds to `VfxPreviewClock` + the active `VfxSystemInstance` so scrubbing reseeks the simulation (via existing `restartRequested` + deterministic seed replay).

## Impact
- Affected specs: docs/engine/25-vfx/21-preview-timeline.md (augmented)
- Affected code: runtime/engine/vfx/editor/vfx_timeline_scrubber_widget.{h,cpp} (new); `VfxPreviewClock` exposes `SetSeekTime(t)` helper if not already there
- Breaking change: NO (read/write layer on existing clock)
- User benefit: artists iterate effects in seconds, not frames
- Estimated effort: M

## Source
UE5 `SNiagaraPreviewGrid` + `SNiagaraSystemEditor` timeline + phase 8.4 preview clock.
