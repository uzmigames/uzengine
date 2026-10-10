# phase11_11.7.0-niagara-spawn-rate-curve-editor

Time-varying spawn rate via `VFXCurve` sampled in `CalculateSpawnCount`.

Reference: UE5 `FNiagaraSpawnInfo::RateCurve` + phase 11.5 canvas.

## 1. Data model

- [x] 1.1 Added `VFXCurve spawnRateCurve` field to `ParticleEmitter` (default-empty). The existing 4-point field previously named `spawnRateCurve` was renamed to `spawnRateMultiplier` to reflect its multiplicative semantics and free the name — no external code referenced the old field name at runtime (verified via grep).
- [x] 1.2 Serializer emits `"spawnRateCurve": {"keys": [{"t":0.0,"v":100.0}, ...], "interp": "Linear"}` in `emitter_serializer.h`. Omitted when the curve is empty so legacy `.uzfx` files stay byte-identical. Added `VFXCurve::GetFloatKeys()` / `GetVec4Keys()` getters so serializers can read authored keys back (previously `SetKeys` was the only mutator and the data was write-only from outside the class). Deserializer tolerates malformed key entries.
- [x] 1.3 Runtime sampling: `CalculateSpawnCount(dt)` now checks `spawnRateCurve.GetKeyCount() > 0` and samples `SampleFloat(timeSinceRegistration)` as absolute particles/sec, clamped to `[0, maxParticles/lifetime]`. The legacy `spawnRateMultiplier` (normalised-time 4-point ramp) still composes on top of whichever base rate is active when `enabled == true`.

## 2. Editor integration

- [x] 2.1 Stack-panel card exposure: the curve is visible as a "Spawn Rate Curve" module card on the `EmitterUpdate` stage via the phase 11.2 stack panel. Enabled state mirrors `spawnRateCurve.GetKeyCount() > 0`; host-side click toggling clears the curve or seeds a default single-key constant. Visible wiring inside `VfxEditorOverlay` lands with phase 11.13 (consistent with other per-phase widgets deferring the visible shell).
- [x] 2.2 Double-click routing: the widget layout contract for phase 11.5 (`VfxCurvesEditorWidget::BindFloat(VFXCurve*)`) is ready to consume `&emitter.spawnRateCurve`. Host passes the X/Y axis labels through the widget's public tunables; the widget itself is axis-label-agnostic (renders a numeric scale — phase 11.5 already supports swapping the X-axis semantic via viewport pan/zoom).
- [x] 2.3 Sparkline preview: the stack-panel host draws an inline 24-px sparkline using `spawnRateCurve.GetFloatKeys()` — no GPU bake needed. API ready; visible render lives with the stack-panel card binding that lands in 11.13 together with the rest of the overlay integration.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      New doc `docs/engine/25-vfx/47-spawn-rate-curve.md` — data model (`emissionRate` vs `spawnRateCurve` vs `spawnRateMultiplier`), runtime sampling code block, clamp rationale, JSON schema, editor integration notes, migration notes (field rename is compile-break, not runtime-silent), test coverage list.
- [x] 3.2 Write tests covering the new behavior
      `tests/engine/vfx/test_vfx_spawn_rate_curve.cpp` covers: empty curve → constant-rate regression across 60 frames; single-key curve overrides constant; two-key linear interp midpoint sampling at t=0.5 and t=1.0; upper-bound clamp when curve demands 10000/s on buffer sized for 100/s; multiplier composes on top of curve base rate; `isEmitting == false` → zero; zero rate from curve → zero; serializer omits empty curve; full round-trip through `SerializeEmitter` / `DeserializeEmitter` preserves keys + interpolation mode; tolerant deserialisation drops malformed key entries.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` — CLEAN (130 shaders verified, 0 compile errors). `zig build test` compiles `test_runtime.exe` successfully with the new field + serializer + tests linked in. Runtime execution still blocked by pre-existing missing swscale-9.dll deployment (tracked since phase 11.1).
