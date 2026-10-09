# The post-process chain is a scaffold since bd4db199 (re-scoped 2026-10-09)

## Why

The first version of this task (2026-10-08) claimed that 7eea6b95 (2026-10-04) dropped the passes CAS_Sharpen,
ColorGradingLUT, DOF_Setup / DOF_Blur, FXAA, LDR_Blit, LensFlare and MotionBlur from
`frame_postprocess.cpp`. That was wrong. In 7eea6b95^ every one of them sat after an unconditional
`return; // Skip full pipeline (CAS, FXAA, motion blur, DOF)` (line 337) that the shader rewrite bd4db199
(2026-03-31) put at the end of a "Simplified pipeline" block; the SponzaTest reference branch has the same return
(line 339). 7eea6b95 deleted dead text. The passes' shaders are the rewrite's scaffolds: `motion_blur_cs`,
`motionblur_tilemax_cs`, `motionblur_neighbormax_cs`, `dof_setup_cs`, `dof_blur_cs`, `lensflare_cs` and
`color_grading_lut_cs` are one-line no-op kernels, and `cas_sharpen_ps` / `fxaa_ps` return their input. The frame
has been Bloom + Tonemap (+ TAA, SSR composite, region-dump readback when active) since 2026-03-31, the SponzaTest
image included. The error: counting `AddPass("...")` occurrences in 7eea6b95^ without reading the early return
above them. A faithful re-wiring would have displayed never-written textures (r.MotionBlur is on by default; the old
tonemap bound the LUT whenever it existed), so the restoration agent stopped and changed nothing that renders.

## What Changes

- Comments and CVar descriptions say what runs: the `frame_postprocess.cpp` header lists the passes that run, the
  closing note names the scaffold shaders and why re-wiring alone would break the frame; `r.MotionBlur`,
  `r.LensFlare*` and `r.CAS.Sharpness` are marked inert; `lens_flare_pass.cpp` states it is never dispatched.
- `SceneRenderer::SetLensFlareEnabled` (the Lua `uz.renderer.set_lens_flare_enabled`) sets `r.LensFlare` too:
  ApplyCVars re-read the member from the CVar every frame, so the Lua toggle never held.
- `scripts/test_postprocess_chain.py` re-scoped: I1 the pass inventory, I2 image identity.

## Gates

I1 no scaffold post-process pass is registered in any configuration (defaults, all off, TAA off, DOF on, lens flare
on). I2 the default and all-off frames equal the before captures within the cross-process floor.

## Impact
- Affected specs: postprocess-chain (new)
- Affected code: comments and CVar descriptions in `frame_postprocess.cpp`, `frame_lighting.cpp`,
  `lens_flare_pass.cpp`, `lua_renderer_api.cpp`; `SetLensFlareEnabled` moved to `scene_upscaler.cpp`
- Breaking change: NO (no rendering change)
- Open decision for the user: whether to implement motion blur, DOF, lens flare, colour grading, CAS or FXAA. Each
  needs its shader written before its pass is wired (the 7eea6b95^ wiring has defects: the motion blur read the raw
  jittered HDR at render resolution; the LUT was bound before it was generated and sampled after ACES; CVar names do
  not line up: ApplyCVars reads r.MotionBlur.Max / .Amount, registered as r.MotionBlur.MaxRadius and nothing).
