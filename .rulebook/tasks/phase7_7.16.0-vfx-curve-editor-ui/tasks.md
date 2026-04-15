# Phase 7-7.16.0 - VFX Curve Editor UI

Hermite-curve editor with tangent modes for VFX parameters. Backs .uzcurve asset format.

Reference: UE5 SNiagaraCurveEditor.cpp

## 1. Curve data

- [ ] 1.1 VfxCurve: variable-length key list (time, value, inTangent, outTangent, tangentMode).
- [ ] 1.2 Evaluator EvaluateCurve(curve, t) returning float or float4.

## 2. UI widget

- [ ] 2.1 Draw curve spline in a bounded panel.
- [ ] 2.2 Drag keys to move, Shift+click to add, Alt+drag tangent handles.
- [ ] 2.3 Zoom/pan on time axis.

## 3. Asset format

- [ ] 3.1 .uzcurve JSON serializer.
- [ ] 3.2 Reference by path from emitter config.

## 4. Migration

- [ ] 4.1 Promote existing triplet fields (sizeOverLife start/peak/end) to single-key curves on first save.
- [ ] 4.2 Backward-compat loader still accepts triplets.

## 5. Docs and tests

- [ ] 5.1 Artist guide with tangent-mode reference.
- [ ] 5.2 Tests: Hermite interpolation continuity, tangent-mode correctness, JSON round-trip.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
