# Proposal: phase16_vfx-scalability-hardware-validation

## Why

`phase16_vfx-scalability-cvars-sequencer` shipped the full CPU surface
of the scalability + sequencer feature: five `r.VFX.*` CVars, the
VFXSystem CVar sampling + budget propagation, per-emitter `isPaused`
and `timeScale` fields, the `Seek(targetTime)` API, the
`FindEmitterByName` resolver, the public C API
(`uz_vfx_seek` / `pause` / `resume` / `set_time_scale`), and the
matching `uz.vfx:freeze` / `set_time_scale` / `seek` / `find_by_name`
Lua bindings. Validated by 19 unit tests on `MockRHIDevice`.

Three items in that task explicitly require a DX12-capable host to
validate behaviour against the live sim/render pipeline — they were
deferred under the original task and land here:

1. The deterministic `Seek` warmup loop inside
   `SceneRenderer::DispatchVFXSimulation`. Today, calling `Seek(t)`
   resets the emitter and sets `pendingSeekTime`; the inline sim path
   needs to consume that field by dispatching
   `ceil(t / fixedDt)` micro-ticks of the spawn + update CS before
   the regular tick, then calling `ClearPendingSeek`. The loop is
   straightforward but its correctness depends on multi-dispatch
   barrier ordering, which only makes sense to verify on hardware.
2. Integration test: seek a deterministic fire effect to 2.5s and
   compare alive counts + sample particle positions against a
   continuous sim that ran for 2.5 simulated seconds (item 1.11 from
   the parent task).
3. `r.VFX.SimulatePaused = 1` freezes alive particles — the
   screenshot captured at frame N must be byte-identical (or
   PSNR > 50dB) to frame N+10's screenshot when paused (item 1.12).
4. `r.VFX.MaxTotalParticles = 1000` reduces alive count from a
   pre-seeded 20'000-particle emitter within 1 frame (item 1.13).

## What Changes

### Seek warmup loop

- Inside `DispatchVFXSimulation`, after the early-out for
  paused / no-GPU-sim cases, walk the emitter list once more for
  `pendingSeekTime > 0`. For each such emitter:
  * Compute `numTicks = ceil(pendingSeekTime / kSeekFixedDt)`
    (`kSeekFixedDt = 1.0f / 60.0f`, declared next to the existing
    group-size constants).
  * In a loop run the same spawn / update / indirect-args CS triplet
    that the regular tick runs, with `dt = kSeekFixedDt`. Reuse the
    existing CB-fill helpers — only the dt / frameNumber inputs
    change.
  * After the loop, call `emitter->ClearPendingSeek()` so subsequent
    frames take the regular path.
- Document the budget cost: 2.5s of warmup at 60 Hz = 150 sub-ticks =
  potentially several ms on a single frame. Provide a CVar
  `r.VFX.Seek.MaxTicksPerFrame` (default 256) that splits long seeks
  across frames if the budget is exceeded — the emitter remains
  unfinished until next frame resumes the warmup.

### Integration tests (require DX12 device)

- `test_vfx_seek_determinism` — spawn a flame emitter, run continuous
  for 2.5s, capture alive count + first-N positions; reset, call
  `Seek(2.5f)`, run one frame to consume the warmup, capture again,
  assert equal up to bitwise (deterministic) tolerance.
- `test_vfx_simulate_paused_freezes_particles` — render at frame N,
  set CVar 1, render 10 frames, screenshot at N+10, PSNR vs N must
  exceed 50 dB.
- `test_vfx_max_total_particles_caps_alive_count` — pre-seed 20 000
  alive, set `r.VFX.MaxTotalParticles = 1000`, render one frame,
  assert `VFXSystem::GetTotalAliveParticles() <= 1000` within 5
  frames (allowing the GPU readback ring to converge).

### Documentation

- Append to `docs/engine/25-vfx/100-scalability-cvars.md` with
  measured timings on the `Seek` warmup loop at 1k / 4k / 16k
  particle capacities so authors can budget cinematic scrubs
  accurately.

## Impact

- Affected specs: VFX scalability surface (existing).
- Affected code:
  - `runtime/engine/renderer/frame/frame_vfx_inline.cpp` — Seek
    warmup loop, MaxTicksPerFrame budget split.
  - CVar registry — register `r.VFX.Seek.MaxTicksPerFrame`.
  - New: `runtime/tests/engine/vfx/test_vfx_seek_determinism.cpp`,
    `test_vfx_simulate_paused_freezes_particles.cpp`,
    `test_vfx_max_total_particles_caps_alive_count.cpp`.
  - `docs/engine/25-vfx/100-scalability-cvars.md` — perf section.
- Breaking change: NO. Default behaviour is unchanged when no `Seek`
  is queued; CVar defaults preserve current rendering.
- User benefit: cinematic timeline integration with deterministic
  scrubbing on real hardware, regression tests that catch sim-path
  drift, an explicit warmup-cost budget for shipping titles.

## Dependencies

- Builds on `phase16_vfx-scalability-cvars-sequencer` (already
  archived).
- Depends on access to a DX12-capable host for the integration tests.
