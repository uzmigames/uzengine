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
- [ ] 4.2 Confirm the preset name appears in the demo preset dropdown at runtime. Requires running demo on GPU.

## 5. UI — Physics Category

- [x] 5.1 Added `dragCoefficient` slider to Physics category in left panel (Slider "Drag", 0-10) and right panel modules (Slider "Drag", 0-10).
- [ ] 5.2 Tooltip text not applicable — demo UI uses AddText labels, not tooltip widgets.

## 6. Visual Verification

- [ ] 6.1 Build runtime, then build VFX demo. **Deferred to user (requires GPU with display).**
- [ ] 6.2 Run demo with "Air Resistance" preset, capture screenshot at frame 30. **Blocked on 4.1 and 6.1.**
- [ ] 6.3 Verify particles stall plausibly; toggling coefficient produces visibly different arcs. **Deferred to user.**
- [ ] 6.4 Set `dragCoefficient = 0.0f` — regression check. **Deferred to user.**

## 7. Documentation

- [x] 7.1 `/// @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag F=-k|v|^2*dir`
  already present above `dragCoefficient` in `VFXEmitterConfig` (vfx_viewport_vfx.h line 137)
  and in `particle_update_cs.hlsl` above the drag computation block.
- [ ] 7.2 Update VFX guide with a "Drag / Air Resistance" section. **Not yet implemented** —
  no vfx-system.md exists in docs/guides/; existing guides do not cover drag.
