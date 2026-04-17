# phase11_11.6.0-niagara-timeline-scrubber-widget

Bottom-of-viewport horizontal timeline — play/pause, seek, speed, loop, restart.

Reference: UE5 `SNiagaraPreviewGrid` + phase 8.4 `VfxPreviewClock`.

## 1. Widget

- [ ] 1.1 `VfxTimelineScrubberWidget` in
      `runtime/engine/vfx/editor/vfx_timeline_scrubber_widget.{h,cpp}`.
      `Bind(VfxPreviewClock*, VfxSystemInstance*)`.
- [ ] 1.2 Bottom-anchored strip (~32 px tall) across the full viewport
      width, above the engine stats overlay.

## 2. Controls

- [ ] 2.1 Play/Pause button (left). Icon flips based on
      `clock->IsPlaying()`.
- [ ] 2.2 Frame-step buttons `<` / `>` advance one `(1/60s)` at a time
      while paused.
- [ ] 2.3 Speed selector: ring-cycle button (`0.25x / 0.5x / 1x / 2x / 4x`),
      writes to `clock->SetSpeed()`.
- [ ] 2.4 Loop toggle (icon: ↻ / ↺) writes `clock->SetLoop(bool)`.
- [ ] 2.5 Restart button (icon: ⟲) triggers `restartRequested = true` on
      the bound `VfxSystemInstance`.

## 3. Seek bar

- [ ] 3.1 Horizontal track with current-time indicator; user drag moves
      the indicator and calls `clock->SetSeekTime(t)` → triggers a
      deterministic replay from the system's rngSeed.
- [ ] 3.2 Time display (left): `"{cur:.2f}s / {total:.2f}s"` where total
      is the longest emitter lifetime × loopCount.
- [ ] 3.3 Keyboard shortcuts — left/right arrow seek ±0.1s, Shift+Arrow
      seek ±1s (registered with phase 9.3 keybinding handler in 11.10).

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
      Append UI mock + keybindings table to `docs/engine/25-vfx/21-preview-timeline.md`.
- [ ] 4.2 Write tests covering the new behavior
      Time display formatting, speed ring cycling, seek math,
      restart wiring.
- [ ] 4.3 Run tests and confirm they pass
      `zig build` clean; QA: fireball preset, play/pause/seek/loop all
      respond to clicks + keyboard.
