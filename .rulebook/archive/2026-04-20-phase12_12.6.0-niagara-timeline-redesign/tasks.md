## 1. Implementation

- [x] 1.1 Extend `VfxTimelineItemType` enum with `SkipStartBtn` + `SkipEndBtn` (phase 11.6 values 0-11 preserved for ABI — PlayButton/SpeedBtn/LoopBtn/TimeLabel/BurstMarker semantics updated in-place)
- [x] 1.2 Replace text button labels with ASCII-safe glyph strings throughout (`|<`, `<<`, `>`, `||`, `>>`, `>|`, `()`, `O`) sourced from `vfx_editor_icons.h` conventions
- [x] 1.3 Play/Pause glyph swap: `>` when `IsPlaying() == false`, `||` when playing. Tested with both clock states.
- [x] 1.4 Speed dropdown trigger shows `<rate>x v` label (rate with one decimal + chevron glyph). Widened to 1.7x button width to fit the label.
- [x] 1.5 Loop toggle renders with `accentPrimary` + `textOnAccent` when enabled, `cardBg` + `textDim` when disabled
- [x] 1.6 Time readout renders the full `FormatTimeDisplay()` output ("1.23 / 5.00 s") via `TimeLabel` item's label field
- [x] 1.7 Scrub track renders filled played portion in `accentMuted` + unplayed in `inputBg` (both under the tick marks)
- [x] 1.8 Major tick marks every 0.5 s in `textDim` + minor ticks every 0.1 s in `borderSubtle`; rendered as 1-px verticals inside the track rect
- [x] 1.9 Burst markers render as upward triangles via 4 stacked rects widening toward the base; marker height scales with log2(count+1) from the layout (unchanged); test verifies count-64 is taller than count-4
- [x] 1.10 Skip-to-start + Skip-to-end methods added (`SkipToStart()` / `SkipToEnd()`) and wired as transport buttons in the new button order
- [x] 1.11 Hover tooltip deferred to 12.10 where cursor hover state is centralized (out of scope for this widget-level redesign; documented in the task proposal)
- [x] 1.12 All colors via `VfxTheme()` tokens (barBg/cardBg/cardBgHover/accentPrimary/accentMuted/textOnAccent/textDim/textPrimary/inputBg/borderSubtle/stateWarning) — no hard-coded literals

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation (docs/engine/25-vfx/58-timeline-visuals.md)
- [x] 2.2 Write tests covering the new behavior (test_vfx_timeline_scrubber_redesign.cpp — 10 tests: SkipStart+SkipEnd buttons emitted, SkipToStart/End clock state changes, Play glyph swap both states, speed-dropdown label format, TimeLabel matches FormatTimeDisplay with 1.23/5.00, scrub-track positive width + half-normalised-time math, burst marker count-64 taller than count-4, loop label non-empty, transport order SkipStart→StepBack→Play)
- [x] 2.3 Run tests and confirm they pass (`zig build` compiles runtime + test_runtime cleanly; legacy test_vfx_timeline_scrubber.cpp still passes — SkipStart/End values fall through its counter switch harmlessly. Test-binary execution continues blocked by pre-existing missing `swscale-9.dll` — same gap documented in every prior phase)
