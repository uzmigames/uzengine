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

- [ ] 4.1 Add an "Air Resistance" preset entry in the preset list (wherever existing presets are defined — search for `"Smoke"` or `"Fire"` strings in the VFX demo or config). Set: `dragCoefficient = 0.5f`, `damping = 0.0f` (no constant damping), standard lifetime/size defaults.
- [ ] 4.2 Confirm the preset name appears in the demo preset dropdown at runtime.

## 5. UI — Physics Category

- [ ] 5.1 In the VFX inspector/panel UI code (search for `"Physics"` ImGui category or where the damping slider is drawn), add an ImGui slider for `dragCoefficient`:
  ```cpp
  ImGui::SliderFloat("Drag Coefficient", &config.dragCoefficient, 0.0f, 5.0f, "%.3f");
  ```
- [ ] 5.2 Add a tooltip: `"Quadratic air resistance: F = -k * speed^2 * dir. Higher values decelerate fast particles more aggressively."`.

## 6. Visual Verification

- [ ] 6.1 Build runtime, then build VFX demo (`cd runtime && zig build && cd demos/vfx_demo && zig build`).
- [ ] 6.2 Run demo with "Air Resistance" preset, capture screenshot at frame 30:
  ```
  vfx_demo.exe --screenshot-stdout --screenshot-frame 30 --frames 35
  ```
- [ ] 6.3 Visually verify: particles launched upward curve and stall plausibly; toggling `dragCoefficient` from 0 to 2.0 produces visibly different arc lengths.
- [ ] 6.4 Set `dragCoefficient = 0.0f` — behavior must be identical to no drag (regression check).

## 7. Documentation

- [ ] 7.1 Add `/// @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag F=-k|v|^2*dir` doc comment above the `dragCoefficient` field in `VFXEmitterConfig`.
- [ ] 7.2 Update `docs/guides/vfx-system.md` (or equivalent VFX guide) with a "Drag / Air Resistance" section describing the formula, parameter range, and typical use cases.
