# Design: mesh decals

## Before
A mesh whose material graph is `blendMode: Masked` goes through the G-buffer pass with the graph's PSO
(`MaterialPipeline::GetOrCreatePSO`: `BuildGBufferPipelineDesc` with no blending, depth writes,
back-face culling) and the pixel shader's alpha test (`gbuffer_ps.hlsl`: `if ((materialFlags & 3) == 1)
... if (alpha < alphaClipThreshold) discard`), the threshold being the loader's 0.3333 when the graph
says 0. Sponza's dirt quads therefore wrote opaque colour, normal, position and depth wherever the
mask exceeded a third, cast shadows (the shadow passes skip only blend modes ≥ 2) and cut everything
else.

## After
- The graph's `domain` (already parsed into `MaterialGraph::domain` and part of the content hash)
  selects the path: `MaterialDomain::Decal` on a mesh is a mesh decal.
- `MaterialPipeline::BuildGBufferPipelineDesc(..., meshDecal)`: for mesh decals the albedo target
  blends SrcAlpha / InvSrcAlpha on the colour and Zero / One on the alpha (the AO stays), the
  normal, position and emissive targets blend Zero / One (kept), `depthWrite = false`, the depth
  test as the G-buffer's (Greater), back-face culling as the graph says.
- `scene_sync.cpp`: when the resolved graph's domain is Decal, `materialFlags` bit 4 (0x10; bit 3 is
  the two-sided flag) is set and the blend bits cleared, so the translucent pass ignores the draw and
  the G-buffer pass owns it.
- `gbuffer_pass.cpp`: the command loop runs in two phases, the first skipping mesh decals and the
  second drawing only them (after every opaque and masked draw, over the finished depth); the
  depth-only pass skips them.
- `gbuffer_ps.hlsl` (material-graph path): mesh decals write `float4(albedo, opacity)` to the albedo
  target (the opacity is the blend factor; the alpha blend keeps the AO underneath), discard below
  1/255, and bypass the masked alpha test.
- `frame_shadows.cpp`, `shadow_pass.cpp`: the caster loops skip bit 4 (a decal is not an occluder);
  `frame_gi_passes.cpp`: out of the distance-field occluder set.
- `dirt_decal.uzmat`: `domain: Decal` (the blend mode stays Masked for the loader's sake; the
  routing is the domain's).

## Harness
`scripts/rendering/decal_reference.py` (the wall, the ramp quad, the control) and `scripts/rendering/test_mesh_decals.py`:
D1 the blend identity along the ramp (the control's wall colour, the ramp's opaque end), D2 the
positions on the wall plane and the wall's normal under the decal, D3 the direct light equal to the
control outside the footprint (no shadow band), D4 Sponza Camera A / C positions changed only inside
the decal meshes' screen bounds and the G-buffer cost, D5 the footprint's boundary contrast against
the build before (the montage for the look).
