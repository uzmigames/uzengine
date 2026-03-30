# Task 3.82.0 — VFX Color Ramp GPU LUT

Replace the two-point linear color-over-lifetime lerp in `particle_billboard_ps.hlsl` with
a 1D RGBA16F LUT texture sampled at `normalizedAge`. The LUT is baked from
`VFXCurve` keyframes on the CPU and uploaded as a 256×1 GPU texture.

**Reference:** Niagara "Color over Life" module with curve asset baking — GDC 2019
"Niagara: Building the Future of FX in Unreal Engine".

---

## 1. CPU — LUT Texture Creation

- [ ] 1.1 Create `runtime/engine/vfx/curves/vfx_color_ramp.h`. Define `struct VFXColorKey`
  with `float t` (normalized [0,1]) and `float r, g, b, a`. Define `struct VFXColorRamp`
  with `std::vector<VFXColorKey> keys` and a method
  `[[nodiscard]] std::array<uint16_t, 256*4> BakeToRGBA16F() const` that evaluates
  the piecewise-linear ramp at 256 evenly-spaced t values and returns raw RGBA16F data
  (use `std::bit_cast` or a small `float_to_half()` helper). Include
  `/// @ref Niagara 2019 "Color over Life" — 256-sample LUT bake` on the class.
- [ ] 1.2 Implement `BakeToRGBA16F()` in `runtime/engine/vfx/curves/vfx_color_ramp.cpp`.
  Sort keys by `t` on entry. For each sample `i` (0..255), compute `t = i / 255.0f`,
  find the bracketing keys, lerp `r,g,b,a` linearly, convert to half-float. If there
  are 0 keys return all-white. If 1 key, repeat it across all samples.
- [ ] 1.3 Add a `float_to_half(float v)` helper (IEEE 754 bit-manipulation, no
  external library). Cite `/// @ref IEEE 754-2008 half-precision conversion`.
- [ ] 1.4 Add `VFXColorRamp` to `runtime/engine/vfx/curves/vfx_curves.h` (existing file
  for `VFXCurve`). If `vfx_curves.h` doesn't already include color ramps, add an
  `#include "vfx_color_ramp.h"` at the bottom.

## 2. CPU — LUT GPU Upload

- [ ] 2.1 In `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`, add to `VFXParticleSystem`
  (or the demo's particle system equivalent): `RHITextureHandle m_colorRampLUT` and
  `uint32_t m_colorRampSrvIndex = UINT32_MAX`.
- [ ] 2.2 Add method `bool UploadColorRampLUT(IRHIDevice* device, IRHICommandList* cmd,
  const VFXColorRamp& ramp)` to the particle system in
  `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`. Implementation:
  - Call `ramp.BakeToRGBA16F()` to get 256×4 half-float data.
  - Create a `Texture2D` via `IRHIDevice::CreateTexture2D(256, 1, Format::RGBA16F,
    TextureUsage::ShaderResource, "VFXColorRampLUT")`.
  - Upload via staging buffer copy (same pattern used for other textures in the demo).
  - Obtain SRV index from the descriptor heap and store in `m_colorRampSrvIndex`.
- [ ] 2.3 Call `UploadColorRampLUT()` during emitter initialization when a non-default
  ramp is set. For the default case (no ramp), leave `m_colorRampSrvIndex = UINT32_MAX`
  so the PS falls back to the legacy two-point lerp.

## 3. Constant Buffer — Billboard PS Extension

- [ ] 3.1 In `VFXBillboardCB` in `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`,
  replace one existing `_pad*` field with `uint32_t colorRampLutSrvIndex` (UINT32_MAX
  = disabled). Update the `static_assert` size check — size must remain 272 bytes.
- [ ] 3.2 Mirror `colorRampLutSrvIndex` in the HLSL `VFXBillboardCB` cbuffer in
  `runtime/shaders/runtime/vfx/particle_billboard_ps.hlsl`, replacing the matching
  `_pad*` field. Layout must stay identical to the C++ struct.
- [ ] 3.3 In the PS `main()` function, after `normalizedAge` is computed, add:
  ```hlsl
  float4 colorLifeScale;
  if (colorRampLutSrvIndex != 0xFFFFFFFF) {
      Texture2D<float4> rampTex = ResourceDescriptorHeap[colorRampLutSrvIndex];
      colorLifeScale = rampTex.SampleLevel(depthSampler, float2(normalizedAge, 0.5f), 0);
  } else {
      colorLifeScale = lerp(colorLifeStart, colorLifeEnd, normalizedAge);
  }
  ```
  Add `/// @ref Niagara 2019 — LUT sample at normalizedAge replaces linear lerp` above
  the block.
- [ ] 3.4 For the SM5 non-bindless path: declare
  `Texture2D<float4> colorRampLUT : register(t5);` (one slot after the depth buffer)
  and sample it with the same guard condition.

## 4. Demo — Predefined Color Ramps + Toggle

- [ ] 4.1 Add `BuildFireRamp()`, `BuildIceRamp()`, `BuildRainbowRamp()` factory
  functions in `runtime/demos/vfx_viewport/vfx_viewport_vfx.cpp`, each returning a
  `VFXColorRamp`:
  - Fire: black(0) → orange(0.3) → yellow(0.6) → white(0.85) → transparent(1.0).
  - Ice: dark-blue(0) → cyan(0.4) → white(0.8) → transparent(1.0).
  - Rainbow: red(0) → yellow(0.25) → green(0.5) → cyan(0.75) → blue(1.0).
  All ramps should smoothly fade alpha to 0 at t=1.
- [ ] 4.2 Add keyboard toggle `'R'` in `runtime/demos/vfx_viewport/vfx_viewport_demo.cpp`
  to cycle through: `Off (legacy lerp)` → `Fire` → `Ice` → `Rainbow` → back to `Off`.
  On each cycle: call `UploadColorRampLUT()` with the selected ramp and update
  `VFXBillboardCB.colorRampLutSrvIndex`. Print the active ramp name to the ImGui overlay.
- [ ] 4.3 Add a `"Fire Ramp"` VFX preset in the presets array using the fire ramp LUT
  and `blendMode = 1` (Additive).

## 5. Visual Verification

- [ ] 5.1 Build runtime, then `runtime/demos/vfx_viewport/`. Fix all errors.
- [ ] 5.2 Capture Fire ramp screenshot:
  `vfx_viewport.exe --screenshot-stdout --screenshot-frame 8 --frames 12`
  View via Read tool. Verify particles show black→orange→yellow→white gradient over
  their lifetime, not the legacy start/end color lerp.
- [ ] 5.3 Toggle to `Off` mode and capture again. Verify fallback to the existing
  two-point linear lerp (no regression).
- [ ] 5.4 Toggle to `Rainbow` ramp and capture. Verify visible color cycling.

## 6. Tasks Update

- [ ] 6.1 Mark all checklist items `[x]` in this file as they are completed.
  Add a one-line note per item describing what was done.
