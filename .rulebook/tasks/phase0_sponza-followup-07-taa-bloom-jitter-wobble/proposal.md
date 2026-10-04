# Proposal: phase0_sponza-followup-07-taa-bloom-jitter-wobble

## Why

User report (2026-09-08, right after `phase0_sponza-followup-06` enabled `r.TAA` in the Sponza
project): "a mesh está balançando quando a câmera chega perto o suficiente". **P** logged the
pose (-223.3, -599.6, 576.6) → (-418.6, -1577.7, 648.7): the first-floor balustrade corner, a
wall ~40 cm from the camera filling the left half of the frame.

Reproduced with a **static** camera at that pose (`scripts/sponza-closeup-camera.*`): with TAA on,
consecutive frames differ (mean |Δ| 1.06 / 2.45 / 1.54 /255, 0.25–1.11 % of the pixels changing by
more than 40); with TAA off they are stable (0.00 %). The difference map is an edge map of the
near geometry; a pixel on the cornice edge reads 180 → 188 → **31** → 179 across four frames while
the unjittered render gives a constant 194; along a scanline the edge moves by 6–8 px in one frame.
The per-region grid shows TAA accumulating normally on the far wall (noise 10× below the TAA-off
level) and not at all on the near wall.

Eliminated first (static, TAA on, frame 300 → 301, baseline 1.06 / 0.25 %): GTAO, final gather, SDF
reflections, all three, the resolve's anti-flicker cap and variance clip, bloom, CAS, motion blur —
every result within 0.84–1.33 / 0.18–0.29 %. A 1-D model of the resolve puts its own oscillation at
≤ 2/255.

Cause (`frame_setup.cpp`, identical in SponzaTest): the Halton jitter was added to
`m[2][0]` / `m[2][1]` of the **combined view-projection** as if it were the projection matrix. In a
projection matrix that row multiplies view-space z (= clip w) and the result is a uniform NDC
translation; in the view-projection it multiplies the vertex's **world z**, so the shift is
`jitter · world_z / w` — for the wall at w ≈ 40 cm and z ≈ 576 cm about **14× the jitter (±7 px)**,
for the atrium wall at 15 m about 0.2 px. The resolve un-jitters by (jitterX, jitterY) pixels and
its history clamp then follows the displaced current frame: near surfaces shake while the camera
stands still, far surfaces converge. That is exactly "sways when the camera gets close enough".

## What Changes

- `runtime/engine/renderer/frame/taa_jitter.h`: `ApplyTaaJitter(viewProj, jx, jy, W, H)` folds the
  NDC translation into the view-projection through its w column (`VP[i][0] += jNdc.x · VP[i][3]`,
  same for y). `frame_setup.cpp` uses it. Every vertex now moves by exactly (jx, jy) render pixels;
  depth is unchanged. The DLSS / FSR2 / TSR upscalers receive the same pixel jitter and were
  affected the same way.
- Hygiene found on the way, kept: the TAA-path Bloom read `m_hdrSceneColorSrv` (jittered scene
  colour) instead of the TAA output; it now reads `tonemapInputSrv` at output resolution. On its
  own this did **not** change the wobble (`r.Bloom=0` control identical), but bloom must be built
  from the displayed image.
- Tests: gtest `TaaJitter.*` (`runtime/tests/engine/renderer/test_taa_jitter.cpp`: uniform pixel
  shift for near/far/floor/ceiling points, identity at zero jitter, and the legacy formula's
  depth dependence documented); `scripts/test_taa_bloom_input.ps1` (11 checks). QA scenes
  `scripts/sponza-closeup-camera.*` (static) and `scripts/sponza-closeup-approach.*` (2 cm/frame).

## Impact
- Affected specs: none
- Affected code: `runtime/engine/renderer/frame/frame_setup.cpp`, `runtime/engine/renderer/frame/taa_jitter.h` (new),
  `runtime/engine/renderer/frame/frame_postprocess.cpp`, `runtime/tests/engine/renderer/test_taa_jitter.cpp` (new),
  `scripts/test_taa_bloom_input.ps1`, `scripts/sponza-closeup-camera.*`, `scripts/sponza-closeup-approach.*`
- Breaking change: NO
- User benefit: TAA jitter is a true sub-pixel jitter at every depth — no shaking of near geometry,
  and the AA itself now works as designed close up
