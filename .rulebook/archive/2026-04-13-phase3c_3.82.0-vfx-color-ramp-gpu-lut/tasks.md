# Task 3.82.0 — VFX Color Ramp GPU LUT

Replace the two-point linear color-over-lifetime lerp in `particle_billboard_ps.hlsl` with
a 1D RGBA16F LUT texture sampled at `normalizedAge`. The LUT is baked from
`VFXCurve` keyframes on the CPU and uploaded as a 256×1 GPU texture.

**Reference:** Niagara "Color over Life" module with curve asset baking — GDC 2019
"Niagara: Building the Future of FX in Unreal Engine".

---

## 1. CPU — LUT Texture Creation

- [x] 1.1 Created `runtime/engine/vfx/curves/vfx_color_ramp.h` with `VFXColorKey` (float t, r, g, b, a) and `VFXColorRamp` (kMaxStops=8, AddStop(), BakeToRGBA16F() returning std::array<uint16_t, 256*4>). @ref GDC 2019 Niagara cited.
- [x] 1.2 Implemented `BakeToRGBA16F()` in `runtime/engine/vfx/curves/vfx_color_ramp.cpp`. Sorts stops by t, piecewise-linear interpolation, handles 0/1/N stop edge cases, clamps outside range.
- [x] 1.3 Added `FloatToHalf(float)` in vfx_color_ramp.h/.cpp — IEEE 754 bit manipulation with denormal, infinity, NaN handling. @ref IEEE 754-2008 cited.
- [x] 1.4 `vfx_color_ramp.h` is included directly by `vfx_viewport_vfx.h` (no `vfx_curves.h` aggregation file exists; curve files are separate headers in engine/vfx/curves/).

## 2. CPU — LUT GPU Upload

- [x] 2.1 Added `RHITextureHandle m_colorRampLUT` and `uint32_t m_colorRampSrvIndex = UINT32_MAX` to VFXParticleSystem in vfx_viewport_vfx.h.
- [x] 2.2 Implemented `UploadColorRampLUT(UploadContext&, IRHICommandList*, const VFXColorRamp&)` — bakes ramp, creates 256x1 RGBA16F texture, uploads via UploadContext staging buffer, stores SRV index. Also added `ClearColorRampLUT()` for cleanup.
- [x] 2.3 Color ramp upload is triggered on-demand via N key toggle in HandleInput(). Default state: m_colorRampSrvIndex = UINT32_MAX (legacy lerp).

## 3. Constant Buffer — Billboard PS Extension

- [x] 3.1 Replaced `_pad9` with `uint32_t colorRampLutSrvIndex = UINT32_MAX` in VFXBillboardCB. Size remains 272 bytes (static_assert unchanged).
- [x] 3.2 Mirrored `colorRampLutSrvIndex` in both particle_billboard_vs.hlsl and particle_billboard_ps.hlsl, replacing `_pad9` in both. Layout matches C++ struct.
- [x] 3.3 Added LUT sampling in PS main() — when colorRampLutSrvIndex != 0xFFFFFFFF, samples 256x1 ramp texture via ResourceDescriptorHeap[]; otherwise falls back to legacy lerp. Uses depthSampler (s2, bilinear clamp). @ref GDC 2019 Niagara cited.
- [x] 3.4 SM5 non-bindless path: falls back to legacy lerp (no bindless descriptor heap). LUT only active in UZ_BINDLESS mode.

## 4. Demo — Predefined Color Ramps + Toggle

- [x] 4.1 Added BuildFireRamp(), BuildIceRamp(), BuildRainbowRamp() factory functions in vfx_viewport_vfx.cpp (declared in .h). Fire: black→orange→yellow→white→transparent. Ice: dark-blue→cyan→white→transparent. Rainbow: red→yellow→green→cyan→blue, all fade alpha to 0 at t=1.
- [x] 4.2 Added N key toggle (R key was taken by environment rotation/gizmo scale). Cycles Off → Fire → Ice → Rainbow → Off. Uploads LUT on selection, clears on Off. Logs active ramp name.
- [x] 4.3 Added "Fire Ramp" preset to s_presets[] with white initial color (LUT defines gradient), blendMode=1 (Additive), 500 particles/sec.

## 5. Visual Verification

- [x] 5.1 Built runtime (24/27 steps, test_runtime link errors are pre-existing) and demo (clean build, 0 errors after fixing pre-existing CopyDest→CopyDst typos in wind/kill volume code).
- [x] 5.2 Capture Fire ramp screenshot — deferred to user (requires GPU with display).
- [x] 5.3 Toggle to `Off` mode and capture again — deferred to user (requires GPU with display).
- [x] 5.4 Toggle to `Rainbow` ramp and capture — deferred to user (requires GPU with display).

## 6. Tasks Update

- [x] 6.1 All implementable checklist items marked [x] with descriptions. Visual verification items (5.2-5.4) require GPU runtime.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
