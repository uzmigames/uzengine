# Phase 7-7.9.0 - VFX Spline Data Interface

Catmull-Rom spline DI with GPU sampling for path-following particle behaviors.

Reference: UE5 UNiagaraDataInterfaceSpline

## 1. CPU model

- [ ] 1.1 VfxSpline struct with Catmull-Rom control points + tension parameter.
- [ ] 1.2 Arc-length reparameterisation table for uniform-speed traversal.

## 2. GPU upload

- [ ] 2.1 StructuredBuffer<float4> of control points + spline count in emitter CB.
- [ ] 2.2 Bindless SRV registration.

## 3. HLSL helper

- [ ] 3.1 Create shaders/runtime/vfx/flipbook_spline.hlsli with SampleSpline(splineIdx, t, outPos, outTan).
- [ ] 3.2 Arc-length-corrected sampling.

## 4. Module and demo

- [ ] 4.1 SplineFollowModule pulls particles toward SampleSpline(normalizedAge).
- [ ] 4.2 Demo: Energy Tether preset showing particles tracking a spline.

## 5. Docs and tests

- [ ] 5.1 Documentation with HLSL API + arc-length behavior.
- [ ] 5.2 Tests: Catmull-Rom interpolation accuracy at control points, tangent continuity.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
