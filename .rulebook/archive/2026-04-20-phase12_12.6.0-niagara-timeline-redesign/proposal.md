# Proposal: phase12_12.6.0-niagara-timeline-redesign

## Why

The phase 11.6.0 `VfxTimelineScrubberWidget` has all the state machine
semantics (play / pause / step / speed ring / loop / scrub / bursts)
but renders as a plain horizontal bar with a few text buttons.
Niagara's real timeline is the visual anchor of the bottom editor
strip — authors spend a lot of time here during iteration. It has:

- **Transport bar** on the left with icon buttons:
  `[⏮] [◀] [⏸/▶] [▶] [⏭] [⟳]` for Skip-to-start, Step-back, Play/Pause
  (one toggling icon), Step-forward, Skip-to-end, Restart.
- **Speed selector** (dropdown showing `1.0x` with ring
  `0.25x / 0.5x / 1.0x / 2.0x / 4.0x`).
- **Loop toggle** — infinity-loop icon, accent when active.
- **Time readout** center-right: `1.23 / 5.00 s` bold and monospaced.
- **Scrub track** below the controls spanning the remaining width,
  with a filled portion (play cursor) and tick marks every 0.5 s.
- **Burst markers** as upward-pointing triangles at each burst's
  time, color-coded by count (log-scaled intensity like Niagara).
- **Hover tooltip** on the scrub track: "t = 0.42 s" under the cursor.

Current rendering omits all icons (text-only), no speed dropdown,
no readout, no tick marks, and the burst markers are rectangles
instead of triangles.

## What Changes

1. **Icon transport bar** — replace text buttons with the glyph
   constants from `vfx_editor_icons.h` (`VFX_ICON_SKIP_START`,
   `VFX_ICON_STEP_BACK`, `VFX_ICON_PLAY`, `VFX_ICON_PAUSE`,
   `VFX_ICON_STEP_FWD`, `VFX_ICON_SKIP_END`, `VFX_ICON_RESTART`).
   Each button renders as a 28-px square with hover / active shading
   from `VfxTheme()`.

2. **Play-Pause toggle icon** — the single button swaps between
   `VFX_ICON_PLAY` and `VFX_ICON_PAUSE` based on the bound clock's
   state. `HitTest` returns `TogglePlayButton` for either icon.

3. **Speed dropdown** — new layout item `SpeedDropdown` with trigger
   showing `<current>x` + chevron. Expanded state shows the ring
   options stacked below.

4. **Loop toggle** — new item `LoopToggle` rendering the loop icon
   in `accentPrimary` when active, `textDim` when inactive.

5. **Time readout** — new item `TimeReadout` drawn as
   `"%.2f / %.2f s"` in a monospaced slot (right-aligned relative to
   the transport strip, left-aligned relative to the scrub track).

6. **Scrub track redesign** — filled portion of the track shows
   played time with `accentMuted` background, unplayed portion uses
   `cardBg`. Tick marks every 0.5 s (major) and 0.1 s (minor).

7. **Triangle burst markers** — replace rectangles with upward
   triangles, scaled by log2(count) and colored from `stateInfo` to
   `stateWarning` based on intensity.

8. **Hover tooltip** — when the cursor is over the scrub track, a
   small box above the cursor shows the hover time. Hidden otherwise.

## Impact

- Affected specs: `specs/vfx-timeline-redesign/spec.md` (new).
- Affected code:
  - `runtime/engine/vfx/editor/vfx_timeline_scrubber_widget.{h,cpp}`
    — layout + render rewrite.
  - `runtime/tests/engine/vfx/test_vfx_timeline_scrubber_redesign.cpp`
    — new.
  - `docs/engine/25-vfx/58-timeline-visuals.md` — new.
- Breaking change: MINOR — enum grows with new layout item kinds.
  Existing callbacks / public methods unchanged.
- User benefit: timeline finally looks like a transport bar —
  discoverable controls, precise time readout, visible burst density.
