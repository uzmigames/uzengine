# Phase 4.8.0 — VFX Size & Color by Speed

Map particle speed to size multiplier and/or color. The mapping is a linear remap of speed
from `[speedRangeMin, speedRangeMax]` to `[0, 1]`, then lerped into the output range. Both
features are independently toggled and computed per-particle in the vertex shader using the
velocity stored in the particle buffer.

Use cases: sparks that glow brighter/larger when fast, smoke that darkens when slow,
debris that scales with impact velocity.

---

## 1. Constant Buffer — VFXBillboardCB

- [x] 1.1 Added `sizeBySpeedEnabled` (uint), `sizeBySpeedMin` (float), `sizeBySpeedMax` (float), `speedRangeMax` (float) to VFXBillboardCB. Repurposed `_pad5`-`_pad8` slots (no size change).
- [x] 1.2 VFXBillboardCB total size remains 288 bytes (multiple of 16). static_assert passes.
- [x] 1.3 Added matching fields to VFXEmitterConfig: `sizeBySpeedEnabled=0`, `sizeBySpeedMin=0.5f`, `sizeBySpeedMax=2.0f`, `speedRangeMax=10.0f`.

## 2. CPU — CB Upload

- [x] 2.1 In `DrawBillboards()`, mapped all new fields: `sizeBySpeedEnabled`, `sizeBySpeedMin`, `sizeBySpeedMax`, `speedRangeMax`.
- [x] 2.2 Confirmed fill runs before every billboard draw call.

## 3. Shader — particle_billboard_vs.hlsl

- [x] 3.1 Added all new fields to `VFXBillboardCB` cbuffer declaration, matching CPU layout exactly.
- [x] 3.2 Speed computed via `length(particleVel)`, normalized t via `saturate(speed / max(speedRangeMax, 0.001))`.
- [x] 3.3 Size-by-speed applied after size-over-life, before corner expansion: `finalSize *= lerp(sizeBySpeedMin, sizeBySpeedMax, t)`. Guarded by `sizeBySpeedEnabled != 0`.
- [ ] 3.4 Color-by-speed not implemented (requires additional CB fields: colorBySpeedEnabled, colorBySpeedLow, colorBySpeedHigh). Deferred to separate sub-task.
- [x] 3.5 When `sizeBySpeedEnabled = 0`, the branch is skipped, producing identical output.

## 4. UI — New "Dynamic" Category

- [ ] 4.1 Add a new ImGui collapsing header `"Dynamic"` in the VFX inspector (after `"Physics"` and `"Rendering"` categories).
- [ ] 4.2 Inside the Dynamic category:
  ```cpp
  ImGui::SeparatorText("Speed Range");
  ImGui::SliderFloat("Speed Min (cm/s)", &config.speedRangeMin, 0.0f, 2000.0f);
  ImGui::SliderFloat("Speed Max (cm/s)", &config.speedRangeMax, 0.0f, 5000.0f);

  ImGui::SeparatorText("Size by Speed");
  ImGui::Checkbox("Enable##SizeBySpeed", (bool*)&config.sizeBySpeedEnabled);
  ImGui::SliderFloat("Size at Min Speed", &config.sizeBySpeedMin, 0.0f, 5.0f);
  ImGui::SliderFloat("Size at Max Speed", &config.sizeBySpeedMax, 0.0f, 5.0f);

  ImGui::SeparatorText("Color by Speed");
  ImGui::Checkbox("Enable##ColorBySpeed", (bool*)&config.colorBySpeedEnabled);
  ImGui::ColorEdit4("Color at Min Speed", &config.colorBySpeedLow.x);
  ImGui::ColorEdit4("Color at Max Speed", &config.colorBySpeedHigh.x);
  ```
- [ ] 4.3 Guard size-by-speed sliders with `if (config.sizeBySpeedEnabled)` and color-by-speed pickers with `if (config.colorBySpeedEnabled)` so disabled controls are greyed out.

## 5. Visual Verification

- [ ] 5.1 Build runtime then demo.
- [ ] 5.2 Size-by-speed test — emit particles with varying velocities, enable size-by-speed with `sizeBySpeedMin=0.5, sizeBySpeedMax=3.0, speedRangeMin=0, speedRangeMax=400`. Verify fast particles visibly larger.
  ```
  vfx_demo.exe --screenshot-stdout --screenshot-frame 15 --frames 20
  ```
- [ ] 5.3 Color-by-speed test — `colorBySpeedLow=(0,0,1,1)` (blue), `colorBySpeedHigh=(1,0,0,1)` (red). Screenshot must show a clear color gradient correlated with particle speed.
- [ ] 5.4 Both disabled — output must match pre-task reference.

## 6. Documentation

- [ ] 6.1 Add `/// @ref` comments in the shader explaining speed remap formula.
- [ ] 6.2 Update VFX guide with a "Size & Color by Speed" section covering typical parameter ranges for common effects (sparks: high speedRangeMax ~600; smoke: low ~50).
