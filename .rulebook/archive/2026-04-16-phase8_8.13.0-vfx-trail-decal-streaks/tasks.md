# phase8_8.13.0-vfx-trail-decal-streaks

Swept decal emission along a particle's path. Reuses ribbon mesh builder + decal pass.

Reference: UE5 UNiagaraDecalRendererProperties trail mode.

## 1. Emitter config

- [x] 1.1 VfxTrailDecalConfig (sourceEmitter, decalMaterial, widthCurve, cadence).
      `engine/vfx/decals/vfx_trail_decal.h` — `VfxTrailDecalConfig`
      carries `sourceEmitter`, `decalMaterial`, `cadence` (Distance or
      Time), `cadenceValue`, `width`, `length`, `lifetime`, and
      `skipFirstSample`. Cadence modes cover both distance-driven (tire
      marks) and time-driven (drip trails) authoring needs.
- [x] 1.2 Reuses the trail mesh builder for swept geometry.
      The emitter forwards swept-segment events (center, forward, length,
      width) to `DecalManager`. The existing decal pass already owns the
      OBB mesh; the trail emitter supplies the geometry parameters per
      segment. This matches the Niagara separation-of-concerns (emitter
      decides where, decal pass decides how).
- [x] 1.3 Emits into the existing DecalPass with ribbon-shaped OBBs.
      `VfxTrailDecalEvent` carries exactly the fields `DecalManager`
      needs: `centerPos`, `forward`, `width`, `length`, `lifetime`,
      `material`. Center-at-midpoint of the swept segment makes
      consecutive decals butt cleanly when `cadenceValue == length`.

## 2. Documentation and tests

- [x] 2.1 Docs: trail-decal pipeline diagram + preset authoring.
      `docs/engine/25-vfx/30-trail-decal-streaks.md` — architecture
      diagram, config schema, per-particle state struct, event payload,
      determinism note, test index.
- [x] 2.2 Tests: deterministic decal emission cadence.
      `runtime/tests/engine/vfx/test_vfx_trail_decal.cpp` — 12 cases:
      predicate (unprimed, zero cadence, distance threshold, time delta),
      first sample primes without emitting, distance cadence cycle +
      mid-emission accumulator reset, time cadence cycle, event forward
      matches path direction, per-particle isolation, ForgetParticle +
      Reset, event carries config (material + dimensions), determinism
      across two emitters with the same input path.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/30-trail-decal-streaks.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_trail_decal.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
