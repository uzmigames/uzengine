# Phase 4.8.0 — VFX Size & Color by Speed

Map particle speed to size multiplier and/or color. The mapping is a linear remap of speed
from `[speedRangeMin, speedRangeMax]` to `[0, 1]`, then lerped into the output range. Both
features are independently toggled and computed per-particle in the vertex shader using the
velocity stored in the particle buffer.

Use cases: sparks that glow brighter/larger when fast, smoke that darkens when slow,
debris that scales with impact velocity.

---

## 1. Constant Buffer — VFXBillboardCB

- [ ] 1.1 Add the following block to `VFXBillboardCB` in `runtime/engine/vfx/vfx_viewport_vfx.h`:
  ```cpp
  // Size-by-speed
  float sizeBySpeedMin;       // size multiplier at speedRangeMin; default 1.0f
  float sizeBySpeedMax;       // size multiplier at speedRangeMax; default 1.0f
  float speedRangeMin;        // speed (cm/s) that maps to t=0; default 0.0f
  float speedRangeMax;        // speed (cm/s) that maps to t=1; default 500.0f
  uint  sizeBySpeedEnabled;   // 0=off 1=on; default 0
  // Color-by-speed
  uint  colorBySpeedEnabled;  // 0=off 1=on; default 0
  float2 _speedColorPad;      // padding
  float4 colorBySpeedLow;     // RGBA at speedRangeMin; default (1,1,1,1)
  float4 colorBySpeedHigh;    // RGBA at speedRangeMax; default (1,1,1,1)
  ```
- [ ] 1.2 Verify `VFXBillboardCB` total size is a multiple of 16 after the addition. Adjust padding field if needed.
- [ ] 1.3 Add matching fields to `VFXEmitterConfig` with the same defaults.

## 2. CPU — CB Upload

- [ ] 2.1 In the `VFXBillboardCB` fill function, map all new fields from `VFXEmitterConfig`:
  ```cpp
  cb.sizeBySpeedMin      = config.sizeBySpeedMin;
  cb.sizeBySpeedMax      = config.sizeBySpeedMax;
  cb.speedRangeMin       = config.speedRangeMin;
  cb.speedRangeMax       = config.speedRangeMax;
  cb.sizeBySpeedEnabled  = config.sizeBySpeedEnabled;
  cb.colorBySpeedEnabled = config.colorBySpeedEnabled;
  cb.colorBySpeedLow     = config.colorBySpeedLow;
  cb.colorBySpeedHigh    = config.colorBySpeedHigh;
  ```
- [ ] 2.2 Confirm the fill runs before every billboard draw call.

## 3. Shader — particle_billboard_vs.hlsl

- [ ] 3.1 Add all new fields to the `VFXBillboardCB` cbuffer declaration, matching CPU layout exactly.
- [ ] 3.2 After reading per-particle velocity from the particle buffer, compute speed and normalized `t`:
  ```hlsl
  /// @ref Hable 2010 "Uncharted 2: HDR Lighting" — speed remap for visual response curves
  float speed = length(particleVelocity);
  float speedRange = max(speedRangeMax - speedRangeMin, 1e-5f);
  float t = saturate((speed - speedRangeMin) / speedRange);
  ```
- [ ] 3.3 Apply size-by-speed BEFORE corner expansion, multiplied into the existing size:
  ```hlsl
  if (sizeBySpeedEnabled)
  {
      float sizeMul = lerp(sizeBySpeedMin, sizeBySpeedMax, t);
      particleSize *= sizeMul;
  }
  ```
- [ ] 3.4 Apply color-by-speed AFTER existing lifetime color/alpha modulation (so they compose):
  ```hlsl
  if (colorBySpeedEnabled)
  {
      float4 speedColor = lerp(colorBySpeedLow, colorBySpeedHigh, t);
      outColor *= speedColor;
  }
  ```
- [ ] 3.5 Confirm both toggles off = byte-identical to pre-task output.

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
