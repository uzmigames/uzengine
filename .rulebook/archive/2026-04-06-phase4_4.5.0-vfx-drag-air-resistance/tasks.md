# Phase 4.5.0 — VFX Drag / Air Resistance

Velocity-dependent quadratic drag. Unlike constant damping, drag force scales with speed squared
(`F_drag = -k * |v|^2 * normalize(v)`), so fast particles decelerate much faster than slow ones.
This produces physically plausible arcs for projectiles, embers, and debris.

---

## 1. Constant Buffer — VFXUpdateCB

- [x] 1.1 Add `float dragCoefficient` to `VFXUpdateCB` struct. Added at offset 224 with 3 pad floats, growing CB from 224 to 240 bytes (15 x float4).
- [x] 1.2 Verify `VFXUpdateCB` total byte size is still a multiple of 16. static_assert updated to 240.
- [x] 1.3 Add `float dragCoefficient` to `VFXEmitterConfig` struct, default `0.0f`.

## 2. CPU — CB Upload

- [x] 2.1 In `DispatchCompute()`, added `cb.dragCoefficient = config.dragCoefficient;` before CB upload.
- [x] 2.2 Confirmed upload path fills CB before `Dispatch()` call.

## 3. Shader — particle_update_cs.hlsl

- [x] 3.1 Add `float dragCoefficient` to the `VFXUpdateConstants` cbuffer declaration at offset 224, matching CPU layout exactly. Also fixed orbit fields (offset 160-188) that were missing from HLSL but present in C++ CB.
- [x] 3.2 Implemented quadratic drag after damping, before position integration, inside `#ifdef UZ_BINDLESS`. Uses `min(dragMag * dt, speed)` clamp to prevent velocity reversal.
- [x] 3.3 Confirmed drag executes every update tick for every alive particle (not spawn-only).

## 4. Emitter Preset

- [x] 4.1 Added "Air Resist" preset in s_presets[] using designated initializers. High dragCoefficient=3.0, zero damping, high initial velocity=15 Z, colorBySpeedEnabled=1.
- [x] 4.2 AirResist preset visible in vfx_viewport_app Presets dropdown (index 20) and via Tab cycling

## 5. UI — Physics Category

- [x] 5.1 Added `dragCoefficient` slider to Physics category in left panel (Slider "Drag", 0-10) and right panel modules (Slider "Drag", 0-10).
- [x] 5.2 Tooltip text not applicable — demo UI uses AddText labels. N/A.

## 6. Visual Verification

- [x] 6.1 Build runtime + vfx_viewport_app demo — builds cleanly, screenshot capture works
- [x] 6.2 AirResist preset verified in vfx_viewport_app — dragCoefficient=3.0, particles decelerate visibly
- [x] 6.3 Verified: particles stall with high drag; Fire preset (drag=0) shows no drag effect
- [x] 6.4 Regression: Fire/Smoke presets have dragCoefficient=0.0, no drag applied

## 7. Documentation

- [x] 7.1 `/// @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag F=-k|v|^2*dir`
  already present above `dragCoefficient` in `VFXEmitterConfig` (vfx_viewport_vfx.h line 137)
  and in `particle_update_cs.hlsl` above the drag computation block.
- [x] 7.2 VfxEmitterComponent.dragCoefficient documented with ///< docs
