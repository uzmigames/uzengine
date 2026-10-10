# Design: depth prepass

## Before
`depth_prepass.cpp` bound the per-frame (b1) and per-object (b2) buffers and drew every mesh with
`shadow_depth_vs.hlsl`, whose clip position is `mul(worldPos, shadowViewProj)` with `shadowViewProj`
in `ShadowCB` at b4: unbound in this pass, so the transform was whatever b4 held. Its pipeline tested
`LessEqual` against a clear of `kDepthFar` = 0.0 (reversed-Z: nearer is greater), which rejects every
fragment. The GBuffer then cleared the depth again. Three independent reasons for an empty pass; the
GBuffer's clear hid the other two since reversed-Z was introduced.

## After
- `PipelineLibrary::GetGBufferDepthOnly(bindless)`: the GBuffer pipeline (`mesh_vs.hlsl`, back-face
  culling, the standard vertex layout) with no pixel shader and no colour targets, `Greater` with
  depth writes. `GetGBufferEqual(bindless)`: the GBuffer pipeline with `Equal` and no depth write.
  Both outside the permutation cache (plain members), both warmed up with the GBuffer pipelines.
- `GBufferPassDesc::depthOnly`: the pass declares only the depth write, clears the depth, begins a
  render pass with no colour attachments, binds the depth-only pipeline and the same per-frame and
  per-object constants as the colour pass, and skips masked, material-graph and translucent draws
  (they are not in the prepass, so they must test `Greater` later). The pass is named `DepthPrepass`
  in the graph (the manifest key stays). `GBufferPassDesc::keepDepth`: no depth clear, the depth
  attachment loads, `desc.pipeline` is the `Equal` pipeline; masked and material-graph pipelines are
  unchanged (`Greater`, writes), so a masked or graph object nearer than a standard one overwrites it
  and one farther is rejected, in either draw order.
- Why the depths are the same bits: both passes run `mesh_vs.hlsl` with the same `PerFrameCB`
  (jittered `viewProj`) and the same `PerObjectCB` (`model`), so `mul(mul(p, model), viewProj)` is
  evaluated by the same instructions; `Equal` then passes exactly the fragments the prepass wrote.
  A different vertex shader (the old shadow one) could not have given that, even with a bound matrix.
- `frame_geometry.cpp`: with `m_depthPrepassEnabled` (and no instancing, which is never active) the
  depth-only GBuffer pass is registered where the old prepass was, and the colour pass gets
  `keepDepth` and the `Equal` pipeline. The Hi-Z stays after the colour pass (block B.5): the prepass
  depth lacks masked and graph geometry.
- `r.DepthPrepass` defaults to the measured cheaper configuration (Z3). On the GPU the item was
  measured on, the GBuffer pixel shader is cheap enough that a 48-wall overdraw stack costs 0.18 ms:
  the prepass is expected to lose on Sponza and to win on the stack; the gates record both.
- Removed: `depth_prepass.{h,cpp}`, `GetDepthPrepass` / `GetDepthPrepassInstanced`, their warmup
  lines and `test_depth_prepass.cpp` (it tested the descriptor's defaults).

## Harness
`scripts/rendering/test_depth_prepass.py`: Sponza Camera C (QA configuration) with the prepass on, off and at
the default; the overdraw stack and the masked wall (`scripts/rendering/lights_reference.py`) on and off;
Z1/Z1b identity (positions, direct light), Z2 early-Z on the stack, Z3 the default, Z4 Sponza against
the committed capture.
