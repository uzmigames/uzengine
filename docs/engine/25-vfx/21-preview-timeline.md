# VFX preview timeline

Scrubbable preview clock for timing-critical effect authoring (impact → flash
→ debris → smoke). Replaces the wall-clock delta fed to `VFXSystem::Tick`
with an author-controlled delta so iterating on short sequences is 5–10×
faster than watching real-time playback loops.

Reference: UE5 `SNiagaraSystemViewportToolBar` + `FNiagaraTimelineController`.

## Overview

The clock is a small header-only state machine in
`engine/vfx/editor/vfx_preview_clock.h` owned by `VfxEditorOverlay`. Every
frame the editor shell calls
`overlay.SimulationDeltaTime(wallDt)` — the return value is the delta that
drives the simulation tick.

```cpp
const float wallDt = frameTimer.GetDeltaSeconds();
const float simDt  = overlay.SimulationDeltaTime(wallDt);
vfxSystem.Tick(simDt);
```

When Playing at 1× this is a pass-through; every other state substitutes a
clock-driven delta.

## States

| State       | `Tick(wallDt)` returns            | Intended UI affordance              |
|-------------|-----------------------------------|-------------------------------------|
| `Playing`   | `wallDt * playRate`               | ▶ button highlighted                |
| `Paused`    | `0`                               | ⏸ button highlighted                |
| `Stepping`  | `stepDt`, then auto-`Paused`      | ⏭ one-shot advance                  |
| `Scrubbing` | `target − prev`, then auto-`Paused`| slider drag                         |
| `Frozen`    | `0` (same as Paused)              | ❄ explicit freeze (distinct icon)   |

Transitions are idempotent: calling `Play()` while playing is a no-op.
`StepFrame(dt)` and `Scrub(target)` both reset to Paused after exactly one
`Tick`, so the caller can never receive the same delta twice.

## API

```cpp
VfxPreviewClock clock;                         // defaults: Playing, 1×, max 10s
clock.Pause();                                 // ⏸
clock.StepFrame(1.0f / 60.0f);                 // ⏭ one fixed tick then ⏸
clock.Scrub(2.5f);                             // ← slider jumps to 2.5s
clock.SetPlayRate(0.25f);                      // quarter-speed preview
clock.SetMaxDuration(30.0f);                   // expand scrubber range
clock.Reset();                                 // back to t=0 Paused

const float dt = clock.Tick(wallDt);           // produce the sim dt
```

Keyboard shortcuts, slider pixels, and timeline visuals live in the editor
shell — the clock itself is UI-framework-agnostic so it can be driven from
ImGui, the native overlay, or a unit test.

## Fixed-timestep determinism

`VFXSystem::Tick` consumes a single scalar dt, so feeding it the
clock-provided delta (including scrub jumps) keeps the simulation
bit-for-bit deterministic across identical scrub sequences. The clock never
splits a scrub jump across multiple `Tick` calls — the whole delta is
reported once and the state snaps back to Paused — so an author dragging
the scrubber cannot cause the engine's fixed-timestep accumulator to
accumulate out-of-order sub-ticks. `DeterministicAcrossIdenticalSequences`
and `ScrubDeltaIntegratesIntoFixedAccumulator` verify this property.

## Integration checklist

1. The editor shell owns the `VfxEditorOverlay` instance.
2. Each frame: `vfxSystem.Tick(overlay.SimulationDeltaTime(wallDt))`.
3. Bind keyboard shortcuts (suggested: `Space` → Play/Pause toggle,
   `.` → StepFrame, `,` → Reset) to `overlay.PreviewClock()` methods.
4. Render a timeline widget using
   `overlay.PreviewClock().GetCurrentTime()` /
   `GetMaxDuration()` / `GetState()` — the widget itself is editor-specific.

## Tests

`runtime/tests/engine/vfx/test_preview_clock.cpp` covers: default state,
Playing with play-rate scaling, negative wall-dt clamp, rate clamp,
Paused/Frozen zero-dt, StepFrame advance + auto-pause, default-dt step,
Scrub clamp at max duration, Scrub negative target, Scrub single-delta
emission, Reset clears pending step, deterministic sequence across two
clocks, scrub delta integrates cleanly into a fixed-timestep accumulator.
