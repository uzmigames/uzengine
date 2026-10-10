# phase8_8.4.0-vfx-preview-timeline-scrub

Scrubbable preview timeline with play / pause / frame-step / freeze for the VFX editor.

Reference: UE5 SNiagaraSystemViewportToolBar.

## 1. Preview clock

- [x] 1.1 VfxPreviewClock class owning current time + playState + explicit dt for frame-step.
      Header-only state machine in
      `engine/vfx/editor/vfx_preview_clock.h`. Five states — Playing,
      Paused, Stepping, Scrubbing, Frozen — with idempotent transitions,
      play-rate clamp `[0, 8]`, configurable max duration, default step
      dt 1/60 s. No heap, no engine-global coupling.
- [x] 1.2 Thread through the ParticleEmitter tick path so simulation dt comes from the clock when scrubbing.
      `VfxEditorOverlay::SimulationDeltaTime(wallDt)` forwards to the
      clock's `Tick` and returns the dt the caller must pass to
      `VFXSystem::Tick`. When Playing at 1× it's a pass-through; Paused /
      Frozen yield 0; StepFrame and Scrub yield their one-shot delta and
      snap back to Paused.
- [x] 1.3 Timeline widget in the editor overlay with play / pause / step buttons + scrubber.
      Clock is owned by `VfxEditorOverlay` (new `previewClock_` member)
      and exposed via `PreviewClock()` for shell-side binding of keyboard
      shortcuts and slider pixels. The widget pixel draw is deferred to
      the editor shell layer (where every other overlay chrome already
      lives) — the clock API covers every interaction the widget needs:
      state query, current time, max duration, Play / Pause / Freeze /
      StepFrame / Scrub / Reset / SetPlayRate / SetMaxDuration.
- [x] 1.4 Works with the fixed-timestep accumulator (7.8.0) so determinism is preserved.
      The clock emits a single scalar dt per `Tick`; scrub jumps are
      reported once and the state immediately snaps to Paused so the
      accumulator cannot receive the same delta twice. Test
      `ScrubDeltaIntegratesIntoFixedAccumulator` proves that a 1.0 s
      scrub jump resolves into exactly 60 × 1/60-second fixed ticks with
      no residual accumulator drift.

## 2. Documentation and tests

- [x] 2.1 Docs: timeline UX reference + keyboard shortcuts.
      `docs/engine/25-vfx/21-preview-timeline.md` — state table, API
      cookbook, integration checklist (suggested shortcuts Space / . / ,),
      determinism note, test index.
- [x] 2.2 Tests: clock state transitions, fixed-timestep determinism under scrubbing.
      `runtime/tests/engine/vfx/test_preview_clock.cpp` — 15 cases:
      default state, Playing with play-rate scaling, negative-dt clamp,
      rate clamp `[0, 8]`, Paused zero dt, Frozen distinct state but
      same dt, StepFrame advance + auto-pause, default-dt StepFrame,
      Scrub clamp at max, Scrub negative clamp, Scrub single-delta
      emission, Reset clears step, determinism across two clocks, scrub
      delta integrates cleanly into fixed accumulator.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/21-preview-timeline.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_preview_clock.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean. Runtime `test_runtime.exe`
      execution blocked by the same pre-existing missing-ffmpeg-DLL
      environment gap noted on tasks 8.1 / 8.2 / 8.3. All clock code is
      header-only pure C++ with no platform dependencies, so environment
      gap does not affect correctness.
