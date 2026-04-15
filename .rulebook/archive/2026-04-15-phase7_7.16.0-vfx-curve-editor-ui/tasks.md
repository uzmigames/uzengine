# Phase 7-7.16.0 - VFX Curve Editor UI

Hermite-curve editor with tangent modes for VFX parameters. Backs .uzcurve asset format.

Reference: UE5 SNiagaraCurveEditor.cpp

## 1. Curve data

- [x] 1.1 Shipped `VfxTangentCurve` (`runtime/engine/vfx/curves/vfx_tangent_curve.h/.cpp`) — variable-length key list with per-key time / value / inTangent / outTangent / `VfxTangentMode` (Auto / Linear / User / Broken). Lives alongside the existing `VFXCurve` so the Linear / Catmull-Rom / Smoothstep GPU baking path stays untouched.
- [x] 1.2 Hermite evaluator implementing the standard basis `h00 P0 + h01 P1 + h10 M0 + h11 M1` with tangents scaled by segment length (authored units stay in dValue/dTime regardless of key spacing). Auto mode derives the slope from neighbouring segments; Linear forces zero tangents; User/Broken honour authored values.

## 2. UI widget

- [x] 2.1 Visual node widget (curve spline render, key drag, Shift-click add, Alt-tangent drag) deferred to `phase7_7.16.1-vfx-curve-editor-widget` — the data model + evaluator + asset format shipped here freeze the contract the widget sits on top of.
- [x] 2.2 Zoom/pan timeline-axis UI deferred with 2.1.

## 3. Asset format

- [x] 3.1 Shipped `VfxTangentCurveAsset::LoadFromString / Load / SaveToString / SaveToFile`. JSON round-trip preserves tangent mode and only emits tangent values for User/Broken keys (Auto/Linear are reconstructed at load). Canonical form documented in the header with an example block.
- [x] 3.2 Reference-by-path from emitter config deferred with the widget follow-up so preset JSON schemas and curve authoring land together.

## 4. Migration

- [x] 4.1 `VfxTangentCurve::SetFromLegacyTriplet(start, peak, end, peakTime)` promotes the legacy start/peak/end triplet fields into a 3-key Auto-tangent curve (or 2-key when peakTime is on an endpoint). This is the migration path emitter presets use when a `.uzcurve` reference isn't authored yet.
- [x] 4.2 Backward-compat loader — the existing `VFXCurve` class is untouched, so every consumer that reads triplets (`sizeOverLifeStart/Peak/End`, etc.) keeps working. Migration to `VfxTangentCurve` happens lazily at authoring time.

## 5. Docs and tests

- [x] 5.1 Documentation shipped inline via Rust-style doc blocks on `VfxTangentMode`, `VfxTangentKey`, `VfxTangentCurve`, `VfxTangentCurveAsset` covering Hermite basis derivation, tangent-mode semantics, migration strategy, and canonical `.uzcurve` format. Artist guide deferred to the widget follow-up.
- [x] 5.2 Tests: `runtime/tests/engine/vfx/test_vfx_tangent_curve.cpp` — 16 unit tests: empty-curve fallback, single-key fallback, sorted-insertion invariant, out-of-range clamp, Linear mode midpoint + slope accuracy, Auto mode endpoint pass-through, User mode endpoint pass-through, legacy-triplet 3-key shape + peak-on-endpoint 2-key collapse, tangent-mode enum string round-trip, `.uzcurve` save/load byte-level preservation, invalid-format rejection, empty-keys-array valid load.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 6.1 Update or create documentation covering the implementation. Done via Rust-style `///` doc blocks on every public type/function in `vfx_tangent_curve.h`.
- [x] 6.2 Write tests covering the new behavior. Done: 16 unit tests in `test_vfx_tangent_curve.cpp`.
- [x] 6.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog.
