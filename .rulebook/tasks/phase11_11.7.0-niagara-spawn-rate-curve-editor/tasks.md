# phase11_11.7.0-niagara-spawn-rate-curve-editor

Time-varying spawn rate via `VFXCurve` sampled in `CalculateSpawnCount`.

Reference: UE5 `FNiagaraSpawnInfo::RateCurve` + phase 11.5 canvas.

## 1. Data model

- [ ] 1.1 Add `VFXCurve spawnRateCurve` (default-empty) to
      `ParticleEmitter` in `runtime/engine/vfx/components/particle_emitter.h`.
- [ ] 1.2 Serialize/deserialize in `emitter_serializer.h` as JSON:
      `"spawnRateCurve": {"keys": [{"t": 0.0, "v": 100.0}, ...]}`
      (missing key = empty curve = constant-rate behaviour).
- [ ] 1.3 Runtime sampling: when `spawnRateCurve.KeyCount() > 0`,
      `CalculateSpawnCount(dt)` uses
      `spawnRateCurve.SampleFloat(timeSinceRegistration)` in place of
      the constant `emissionRate`. Clamp sampled rate to
      `[0, maxParticles / lifetime]` for safety.

## 2. Editor integration

- [ ] 2.1 In 11.2 stack panel, the Emitter Spawn stage shows a
      "Spawn Rate Curve" module card. Enabled state mirrors
      `spawnRateCurve.KeyCount() > 0`; disabling clears keys.
- [ ] 2.2 Double-click the card → open phase 11.5 curves editor bound
      to `spawnRateCurve`, with X-axis labelled "Emitter Time (s)" and
      Y-axis "Particles/sec".
- [ ] 2.3 Inline preview: tiny sparkline of the curve rendered inside
      the module card (~24 px tall) so authors see the shape at a
      glance without opening the full editor.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 3.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/47-spawn-rate-curve.md`.
- [ ] 3.2 Write tests covering the new behavior
      Constant rate regression (no curve), single-key curve,
      multi-key linear interp, clamp at emitter end.
- [ ] 3.3 Run tests and confirm they pass
      `zig build` clean; QA: ramp spawn from 0→500/s over 2 seconds.
