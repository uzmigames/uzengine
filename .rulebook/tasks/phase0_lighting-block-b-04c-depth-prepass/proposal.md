# Depth prepass: a pass that drew nothing, made real and measured

## Why

Block B.5's side finding: the GBuffer pass clears the depth-stencil before drawing, so the depth
prepass (0.36 ms on Sponza at 1080p in that measurement) bought no early-Z. Reading the pass for
this item showed worse: the prepass could not have produced a depth at all. It rendered with the
shadow vertex shader, whose view-projection lives in `ShadowCB` at b4, a buffer the pass never
binds (it binds the per-frame and per-object buffers only), and its pipeline tested `LessEqual`
against the reversed-Z clear of 0.0, which no fragment passes. Measured before any change
(2026-10-07, build 794b632f, Sponza Camera C, QA configuration, 1080p): `r.DepthPrepass 1` costs
0.167 ms in the `DepthPrepass` pass and leaves the GBuffer at 0.49 ms (0.52 ms without it, noise);
the G-buffer positions and the direct light are bit-identical with and without it. On a 48-wall
overdraw stack drawn far to near the GBuffer reads 0.174 ms with it and 0.180 ms without.

## What changes

- The prepass becomes the GBuffer pass in a depth-only mode: the GBuffer's own vertex shader and
  per-object constants, no pixel shader, no colour targets, reversed-Z `Greater` with depth writes,
  standard opaque draws only (masked, material-graph and translucent draws are skipped).
- The GBuffer pass, when the prepass ran, keeps the depth (no clear) and draws standard opaque
  geometry with `Equal` and no depth write; masked and material-graph draws keep `Greater` with
  writes, as before. The clip-space depths of the two passes are the same bits because they come
  from the same shader and constants.
- The old pass (`depth_prepass.cpp`, the shadow-shader pipelines `DepthPrepass` /
  `DepthPrepass_Instanced`, their test file) is removed.
- `r.DepthPrepass` defaults to the configuration the measurements favour (Z3); the other stays one
  CVar away.
- Evidence: `scripts/test_depth_prepass.py` (Z1-Z4) on Sponza and on two reference scenes
  (`lights-reference-overdraw`, `lights-reference-masked`).

## Gates (pre-registered)

Z1 identity on Sponza: with the prepass the G-buffer positions and the direct light are bit-identical
to no prepass (the `Equal` test rejected no fragment); Z1b the same on the overdraw stack and on the
masked-wall scene; Z2 the prepass depth is used: on the overdraw stack GBuffer(prepass on) ≤ 0.7 ×
GBuffer(prepass off); Z3 the shipped default is the cheaper configuration on Sponza (prepass +
GBuffer against GBuffer alone, ties to off); Z4 Sponza unchanged with the default: positions and
direct light identical to the committed capture, colour within block B.6's loading-state envelope.

## Impact
- Affected specs: depth-prepass (new)
- Affected code: `runtime/engine/renderer/passes/geometry/gbuffer_pass.{h,cpp}`,
  `frame/frame_geometry.cpp`, `frame/frame_setup.cpp`, `scene/scene_renderer_init.cpp`,
  `core/pipeline/pipeline_geometry.cpp`, `pipeline_library.h`, `pipeline_library_impl.h`,
  `pipeline_lifecycle.cpp`; removed `passes/geometry/depth_prepass.{h,cpp}`,
  `runtime/tests/engine/renderer/test_depth_prepass.cpp`; `scripts/lights_reference.py` (two
  variants), `scripts/test_depth_prepass.py`
- Breaking change: NO (the image is identical by gate; the default may change by measurement)
- User benefit: a prepass that does what its name says, and no longer paying for one that does not.

## Out of scope

Two-phase GPU occlusion culling (off by default; its flow needs a Hi-Z from the prepass before the
GBuffer, which block B.5 moved after it), instanced draws in the depth-only mode (instancing is
never active: `useInstancing` is set false every frame), alpha-masked depth for shadows.
