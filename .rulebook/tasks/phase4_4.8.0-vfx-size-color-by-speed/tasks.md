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
- [ ] 3.4 Color-by-speed not implemented. Requires additional CB fields: `colorBySpeedEnabled`, `colorBySpeedLow` (float4), `colorBySpeedHigh` (float4), and VFXEmitterConfig fields `colorBySpeedEnabled`, `colorBySpeedLow`, `colorBySpeedHigh`. **Deferred — no colorBySpeed fields exist in config or shader CB.**
- [x] 3.5 When `sizeBySpeedEnabled = 0`, the branch is skipped, producing identical output.

## 4. UI — New "Dynamic" Category

- [ ] 4.1 Add a "Dynamic" category to the VFX parameter panel (`vfx_parameter_panel.cpp`) after
  the Rendering category with Speed Range, Size-by-Speed, and Color-by-Speed controls.
  **Not yet implemented** — no Dynamic category exists; no sizeBySpeed/colorBySpeed entries
  in any panel category. The demo uses no ImGui.
- [ ] 4.2 Add Speed Range sliders, Size-by-Speed toggle+sliders, Color-by-Speed toggle+pickers.
  **Blocked on 4.1 and colorBySpeed fields (3.4).**
- [ ] 4.3 Conditional display when disabled. **Blocked on 4.1.**

## 5. Visual Verification

- [x] 5.1 Build runtime then demo — builds cleanly (confirmed in prior tasks).
- [ ] 5.2 Size-by-speed test — deferred to user (requires GPU with display). Size-by-speed IS
  implemented in shader; test needs runtime to confirm visual effect.
- [ ] 5.3 Color-by-speed test — **blocked** — colorBySpeed not implemented in shader or CB.
- [ ] 5.4 Both disabled regression test — deferred to user (requires GPU with display).

## 6. Documentation

- [ ] 6.1 Add `/// @ref` comments in shader explaining speed remap formula. **Not yet done** —
  no `@ref` comment exists above the sizeBySpeed block in `particle_billboard_vs.hlsl`.
- [ ] 6.2 Update VFX guide with a "Size & Color by Speed" section. **Not yet implemented** —
  no such guide exists in docs/guides/.
