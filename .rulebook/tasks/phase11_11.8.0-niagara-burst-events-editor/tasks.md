# phase11_11.8.0-niagara-burst-events-editor

Time-stamped spawn-burst array + timeline-marker editor.

Reference: UE5 `FNiagaraSpawnInfo::Burst`.

## 1. Data model

- [ ] 1.1 `VfxBurstEvent { float time; uint32_t count; }` in
      `runtime/engine/vfx/spawn/vfx_burst_event.h`.
- [ ] 1.2 `std::vector<VfxBurstEvent> bursts` field on `ParticleEmitter`,
      sorted by `time` (invariant maintained on add/remove/move).
- [ ] 1.3 JSON round-trip in `emitter_serializer.h`:
      `"bursts": [{"t": 0.2, "n": 300}, {"t": 0.4, "n": 150}]`.

## 2. Runtime spawning

- [ ] 2.1 Track `uint32_t burstCursor` on `ParticleEmitter` (index of
      next-unfired burst). Reset to 0 on `restartRequested`.
- [ ] 2.2 Each frame in `CalculateSpawnCount(dt)`, after the rate-curve
      spawn, advance `burstCursor` while
      `bursts[burstCursor].time <= timeSinceRegistration` and add the
      corresponding `count` to this frame's spawn total.

## 3. Editor integration

- [ ] 3.1 Extend 11.6 timeline scrubber to render burst markers —
      vertical ticks at each `burst.time`, height proportional to
      `log2(count)`.
- [ ] 3.2 Right-click timeline at X → "Add Burst Here" popup that asks
      for count (default 100).
- [ ] 3.3 Click an existing marker → popup editor (time + count fields
      + Delete button).

## 4. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/48-burst-events.md`.
- [ ] 4.2 Write tests covering the new behavior
      Sorted-insert invariant, burstCursor advancement, restart reset,
      JSON round-trip.
- [ ] 4.3 Run tests and confirm they pass
      `zig build` clean; QA: add bursts at 0.2s + 0.4s to an emitter,
      observe spawn spikes on timeline.
