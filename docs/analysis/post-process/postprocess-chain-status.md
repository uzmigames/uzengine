# The post-process chain — what runs (2026-10-09)

Rulebook task `phase0_postprocess-chain-restore` (re-scoped). Branch `feat/sponza-gi-parity`.

## What runs

Bloom and Tonemap, straight to the backbuffer, plus the TAA / temporal upscaler, the SSR composite and
copy-prev-colour, and the region-dump readback when they are active. Nothing else: no CAS, FXAA, motion blur, DOF,
lens flare or colour-grading LUT pass is registered, and their CVars are labelled inert.

## Correction of 2026-10-08

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

## Measured (`scripts/rendering/test_postprocess_chain.py`)

- Inventory (I1, PASS): in defaults, all off, TAA off, DOF on and lens flare on, the frame times only the passes above
  (before and after the change).
- Identity (I2): the reflection reference's default and all-off frames are bit-identical across the change, and
  across Debug vs ReleaseFast builds. Sponza's frame 120 reads mean 0.88 / max 5 code values
  against the ReleaseFast before capture, with a same-binary Debug floor of mean 0.34 / max
  6: the async mesh-SDF bake (block B-03) advances with CPU speed, so the GI state at a fixed frame
  depends on the build configuration. Compare Sponza captures only within one build configuration.

## To implement an effect

Write its shader first (each is a scaffold), then wire its pass; the 7eea6b95^ wiring is not a template (the motion
blur read the raw jittered HDR at render resolution; the LUT was bound before it was generated and sampled after
ACES; ApplyCVars reads r.MotionBlur.Max / .Amount while the registered names are r.MotionBlur.MaxRadius and none).
Other casualties of bd4db199's return: two-phase occlusion culling never engages (previous Hi-Z fields written only
in the dead section; r.GpuCull=1 only), the PassInspector post targets are not registered, the ctx.*Active flags
and the *SrvForTonemap chain are computed and unread, SetTaaEnabled has the setter defect fixed here for the lens flare.
