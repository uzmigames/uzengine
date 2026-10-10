# Lighting block B, item 4c — the depth prepass that drew nothing

Rulebook task `phase0_lighting-block-b-04c-depth-prepass` (proposal, design, spec `depth-prepass`).
Branch `feat/sponza-gi-parity`, 2026-10-07. Block B.5's side finding ("the GBuffer discards the depth
prepass"), taken after the block closed.

## Before

- Block B.5 noticed that the GBuffer pass clears the depth-stencil before drawing, so the depth
  prepass bought no early-Z. Reading the pass for this item showed that it could not have produced a
  depth at all: it rendered with the shadow vertex shader, whose view-projection lives in `ShadowCB`
  at b4, a buffer the pass never bound (it bound the per-frame and per-object buffers only), and its
  pipeline tested `LessEqual` against the reversed-Z clear of 0.0, which no fragment passes. Three
  independent reasons for an empty pass; the GBuffer's clear hid the other two since reversed-Z.
- Measured on build 794b632f (`test-depth-prepass/before`): Sponza Camera C (QA configuration,
  1080p) with `r.DepthPrepass 1` spends 0.167 ms in `DepthPrepass` and leaves the GBuffer at 0.49 ms
  (0.52 ms without it, within noise); the G-buffer positions and the direct light are bit-identical
  with and without it (max |diff| 0). On a 48-wall overdraw stack drawn far to near the GBuffer reads
  0.174 ms with the prepass and 0.180 ms without: the pass changed nothing anywhere.

## What changed

- The prepass is the GBuffer pass in a depth-only mode (`GBufferPassDesc::depthOnly`): the GBuffer's
  own vertex shader and per-object constants, no pixel shader, no colour targets, reversed-Z
  `Greater` with depth writes (`PipelineLibrary::GetGBufferDepthOnly`), standard opaque draws only
  (masked, material-graph and translucent draws are skipped). Graph name `DepthPrepass`.
- The colour pass, when the prepass ran (`keepDepth`), does not clear the depth and draws standard
  opaque geometry with `Equal` and no depth write (`GetGBufferEqual`); masked and material-graph
  draws keep `Greater` with writes. The two passes evaluate the same `mul(mul(p, model), viewProj)`
  with the same constants, so `Equal` passes exactly the prepass's fragments.
- The old pass (`depth_prepass.cpp`), the `DepthPrepass` / `DepthPrepass_Instanced` pipelines and
  their test file are removed. The Hi-Z stays after the colour pass (block B.5): the prepass depth
  lacks masked and graph geometry.
- `r.DepthPrepass` defaults to the configuration the measurement favours (Z3).
- `scripts/rendering/lights_reference.py` gains `lights-reference-overdraw` (48 walls facing the camera, far to
  near) and `lights-reference-masked` (the mid wall as a Masked material, opacity 1);
  `scripts/rendering/test_depth_prepass.py` (Z1-Z4).

## Gates (`scripts/rendering/test_depth_prepass.py`)

| Gate | Criterion | Result |
|---|---|---|
| Z1 | identity (Sponza): with the prepass the G-buffer positions and the direct light are bit-identical to no prepass | PASS: positions max |diff| 0, direct 0, colour max |diff| 5.87e-05 (0 px > 1e-3), geometry 2073600 px |
| Z1b | identity (overdraw stack, masked wall): positions and direct light bit-identical with and without the prepass | PASS: overdraw: positions 0, direct 0, colour 0; masked: positions 0, direct 0, colour 0 |
| Z2 | the prepass depth is used: on the overdraw stack GBuffer(prepass on) <= 0.7 x GBuffer(prepass off) | PASS: GBuffer 0.054 ms with the prepass (0.019 ms) vs 0.180 ms without; frame 1.006 vs 1.128 ms |
| Z3 | the shipped default is the cheaper configuration on Sponza (prepass + GBuffer vs GBuffer alone; ties go to off) | PASS: prepass on 0.524 ms (prepass 0.008 + GBuffer 0.516) vs off 0.514 ms; cheaper off; default r.DepthPrepass 0 |
| Z4 | Sponza unchanged with the default: positions and direct_light identical to the committed capture; colour within the loading-state envelope | PASS: positions 0, direct 0, colour changed 3444 px (median rel 0.004; envelope 90656 px / 0.034) |

**Numbers (after, 1080p).** Sponza Camera C: prepass 0.008 ms + GBuffer 0.516 ms with the prepass (0.524 ms) against GBuffer 0.514 ms without (0.514 ms); frame 25.718 vs 25.494 ms; the cheaper configuration is "off" and the shipped default is r.DepthPrepass 0. Overdraw stack: prepass 0.019 ms + GBuffer 0.054 ms against GBuffer 0.180 ms without (frame 1.006 vs 1.128 ms). Identity with the prepass: Sponza positions max |diff| 0, direct 0, colour max |diff| 5.87e-05 (0 px above 1e-3); overdraw positions 0, direct 0; masked wall positions 0, direct 0. Sponza with the default against the committed capture: positions 0, direct 0, colour 3444 px changed (median relative 0.004). Harness 5/5.

## Still open

- The prepass's instanced path: instancing is never active (`useInstancing` is set false every frame),
  so the depth-only mode handles the per-draw path only; an instanced prepass needs the instanced
  GBuffer vertex shader without a pixel shader.
- Two-phase GPU occlusion culling (off by default) expects a Hi-Z from the prepass before the GBuffer;
  the Hi-Z stays after the colour pass (block B.5), so that flow is not restored by this item.
- A masked material with an alpha texture could not be tested from the sample content (no texture with
  an alpha channel is referenced by a mesh material); the masked wall uses opacity 1 and checks the
  pipeline routing, not the discard.
- Alpha-masked depth for the shadow passes (known-issues, block A.1) is unchanged.
