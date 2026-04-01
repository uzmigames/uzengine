# Phase 4.5.0 — VFX Drag / Air Resistance

Velocity-dependent quadratic drag. Unlike constant damping, drag force scales with speed squared
(`F_drag = -k * |v|^2 * normalize(v)`), so fast particles decelerate much faster than slow ones.
This produces physically plausible arcs for projectiles, embers, and debris.

---

## 1. Constant Buffer — VFXUpdateCB

- [ ] 1.1 Add `float dragCoefficient` to `VFXUpdateCB` struct in `runtime/engine/vfx/vfx_viewport_vfx.h`. Place after the existing damping field. Default value `0.0f`. Comment: `// quadratic drag coefficient k in F=-k|v|^2*dir`.
- [ ] 1.2 Verify `VFXUpdateCB` total byte size is still a multiple of 16 (add `float _dragPad` or pack with a nearby field if alignment breaks). Update the byte-size comment if present.
- [ ] 1.3 Add `float dragCoefficient` to `VFXEmitterConfig` struct in the same header, default `0.0f`, matching field order with the CB.

## 2. CPU — CB Upload

- [ ] 2.1 In the function that fills `VFXUpdateCB` from `VFXEmitterConfig` (search for where `damping` or `VFXUpdateCB` is populated on CPU), add: `cb.dragCoefficient = config.dragCoefficient;`.
- [ ] 2.2 Confirm the upload path reaches the mapped constant buffer before `Dispatch()` is called for `particle_update_cs.hlsl`.

## 3. Shader — particle_update_cs.hlsl

- [ ] 3.1 Add `float dragCoefficient` to the `VFXUpdateCB` cbuffer declaration in `runtime/shaders/vfx/particle_update_cs.hlsl`, matching the CPU-side layout exactly.
- [ ] 3.2 Implement quadratic drag after any force accumulation but BEFORE position integration. Exact formula:
  ```hlsl
  /// @ref Witkin & Baraff 2001 "Physically Based Modeling" — quadratic drag
  /// @constants dragCoefficient=user (default 0.0), clamp prevents velocity reversal
  float speed = length(vel);
  if (speed > 1e-5f && dragCoefficient > 0.0f)
  {
      float dragMag = dragCoefficient * speed * speed;
      vel -= (vel / speed) * min(dragMag * dt, speed);
  }
  ```
- [ ] 3.3 Confirm the drag step executes every update tick for every alive particle, not only on spawn.

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
