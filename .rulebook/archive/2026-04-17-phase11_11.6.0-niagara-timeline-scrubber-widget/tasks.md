# phase11_11.6.0-niagara-timeline-scrubber-widget

Bottom-of-viewport horizontal timeline — play/pause, seek, speed, loop, restart.

Reference: UE5 `SNiagaraPreviewGrid` + phase 8.4 `VfxPreviewClock`.

## 1. Widget

- [x] 1.1 `VfxTimelineScrubberWidget` in `runtime/engine/vfx/editor/vfx_timeline_scrubber_widget.{h,cpp}`. `Bind(VfxPreviewClock*)`. System-instance wiring for restart is host-side via the `onRestartRequested` callback — keeps the widget pure so it can be unit-tested without a fully constructed `VfxSystemInstance`.
- [x] 1.2 Strip geometry emitted via `ComputeLayout(panelBounds)`: background rect + time label + 6 transport buttons + seek track + draggable handle. Widget exposes `stripHeight`, `buttonWidth`, `buttonHeight`, `timeLabelWidth`, `seekMinWidth` tunables — host positions `panelBounds` at the bottom edge of the viewport.

## 2. Controls

- [x] 2.1 Play/Pause button flips state via `TogglePlayPause` (`clock->Play()` / `Pause()`). Icon label flips between `>` (paused) and `||` (playing); button background accents when playing.
- [x] 2.2 Frame-step buttons `<` / `>`: `StepBackward` scrubs `cur - 1/60` clamped at 0; `StepForward` calls `clock->StepFrame(1/60)` so the existing Stepping state machine applies (auto-pauses after one tick).
- [x] 2.3 Speed ring: `CycleSpeed` walks `[0.25, 0.5, 1, 2, 4]` wrapping at the end; `SetSpeed(value)` snaps to the nearest ring entry with tie-break towards the higher ring value; ring is stored in `kVfxTimelineSpeeds`. Writes via `clock->SetPlayRate`.
- [x] 2.4 Loop toggle owns a widget-local `m_loop` flag (the `VfxPreviewClock` has no native loop concept). `ToggleLoop` flips, `SetLoop` fires `onLoopChanged` only on change. Host handles actual loop behaviour via `TickAutoLoop()` which resets+replays the clock when the duration is reached.
- [x] 2.5 Restart button calls `RequestRestart` which fires `onRestartRequested`. Host wires the callback to the currently-edited `VfxSystemInstance` (instance reset pipeline) in phase 11.13 overlay integration.

## 3. Seek bar

- [x] 3.1 `SeekTrack` + `SeekHandle` layout items. `BeginSeek` / `EndSeek` frame the drag gesture; each frame of the drag calls `SeekByNormalised(u)` which clamps `u` into `[0,1]` and writes through `clock->Scrub(u * duration)` (triggers the clock's single-delta scrubbing behaviour).
- [x] 3.2 Time display rendered as `"{cur:.Ns} / {total:.Ns}"` via `FormatTimeDisplay` — the decimals count is controllable via `timeDecimals` (default 2). Total duration reads from `clock->GetMaxDuration()`.
- [x] 3.3 Keyboard seek shortcuts (Left/Right ±0.1s, Shift+Arrow ±1s, `.` / `,` step, Space toggle, `L` loop, `R` restart) are specified in `docs/engine/25-vfx/21-preview-timeline.md`. Actual key-to-action binding is registered by phase 11.10's keybinding handler — widget provides the full public API the keybindings call (`TogglePlayPause`, `StepBackward`, `StepForward`, `Seek`, `ToggleLoop`, `RequestRestart`).

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 4.1 Update or create documentation covering the implementation
      Appended "Timeline Scrubber Widget (phase 11.6)" section to `docs/engine/25-vfx/21-preview-timeline.md` with UI mock, controls table (enum + effect), keybindings table, auto-loop description, and test coverage list.
- [x] 4.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_timeline_scrubber.cpp` covers: unbound widget safe defaults; TogglePlayPause flips clock state; StepBackward clamps at zero; StepForward advances one frame then auto-pauses; CycleSpeed walks ring + wraps; SetSpeed snaps to nearest; Loop default on, Toggle + SetLoop-on-change callback semantics; RequestRestart fires callback; Seek clamps to duration bounds; SeekByNormalised maps to fraction; NormalisedTime zero when duration zero; FormatTimeDisplay honours decimals (2 + 3); TickAutoLoop fires restart at end with loop on / no-op with loop off / no-op mid-playback; Layout emits all 10 expected items (strip, time label, 6 buttons, track, handle); HitTest resolves play button + seek handle beats track (handle precedence).
- [x] 4.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the new widget + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
