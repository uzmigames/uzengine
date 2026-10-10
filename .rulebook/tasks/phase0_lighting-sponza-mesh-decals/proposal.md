# Sponza's dirt decals: masked quads drawn as grey blotches, made mesh decals blended into the G-buffer

## Why

The user reported (2026-10-08) that Sponza's stone dirt decals read as grey blotches where soft dirt
was expected. Sponza carries two decal meshes (`decals_1st_floor1_dirt_decal_0`,
`Materials_dirt_decal_0`) with the material graph `materials/sponza/dirt_decal.uzmat`: a quad set
a few centimetres in front of the stone, base colour and opacity from `dirt_decal_baseColor.png`
(RGBA, alpha mean 0.33, 31 % of the texels above 0.5, 69 % above 0.05: a soft mask). The material is
`blendMode: Masked` with `alphaClipThreshold: 0`, which the scene loader turns into an alpha test at
0.3333 ("a threshold of 0 would never discard anything"). So every texel above a third of opacity is
drawn fully opaque with the texture's flat grey-brown colour, writing its own depth, position and
normal into the G-buffer, and casting shadows; everything below is cut: hard-edged blotches, no soft
dirt, and the stone's own detail replaced under them. The engine's decal system (`DecalComponent`,
the DBuffer) projects boxes, not meshes, so these quads had no path that blends.

## What changes

- Mesh decals: a material graph with `domain: Decal` on a mesh renders into the G-buffer after the
  opaque and masked draws with alpha blending on the albedo (SrcAlpha / InvSrcAlpha for the colour,
  the AO in the alpha channel kept), the normal, position and emissive targets untouched (Zero / One
  blend), no depth write, back-face culling. The pixel shader writes the graph's opacity in the
  albedo target's alpha for these draws and discards only opacity below 1/255; the masked alpha test
  does not apply to them.
- `MaterialPipeline::BuildGBufferPipelineDesc` gains the mesh-decal variant (blend and depth states);
  `scene_sync.cpp` marks the draw (`materialFlags` bit 4, 0x10; bit 3 is the two-sided flag; the blend
  bits cleared); the G-buffer pass
  draws the marked commands in a second phase and skips them in the depth-only pass; the shadow
  passes skip them (a decal is not an occluder).
- `dirt_decal.uzmat`: `domain: Decal`.
- Evidence: `scripts/test_mesh_decals.py` (D1-D5) on a reference scene (`scripts/decal_reference.py`:
  a grey wall, a decal quad 1 cm in front with a generated alpha-ramp texture, a grazing sun, the
  wall alone as the control) and on Sponza Camera A / C.

## Gates (pre-registered)

D1 blending identity: along the decal's alpha ramp the lit colour is lerp(wall, decal, alpha)
(the wall from the control capture, the decal from the ramp's opaque end): median relative deviation
≤ 0.03 and the fit's R² ≥ 0.99 over the ramp (before: a step at alpha 0.333). D2 the decal writes no
geometry: over its footprint the G-buffer positions lie on the wall plane (|Δ| ≤ 0.5 cm) and the
normals are the wall's (before: 1 cm in front). D3 no shadow: the wall's direct light with the decal
equals the control's everywhere (max |diff| ≤ 1e-4; before: the quad's shadow band beside it). D4
Sponza Camera A and C: positions change only inside the decal meshes' screen bounds (reported with
the direct-light changes: the quads' former shadows and self-shading go); the G-buffer pass cost
within 10 % of the build before. D5 Sponza's decal footprint is soft: the mean gradient magnitude of
the lit colour across the footprint's boundary ≤ 0.5 × the build before (reported with the montage).

## Impact
- Affected specs: mesh-decals (new)
- Affected code: `runtime/engine/renderer/material/material_pipeline.{h,cpp}`,
  `runtime/engine/renderer/scene/scene_sync.cpp`,
  `runtime/engine/renderer/passes/geometry/gbuffer_pass.cpp`,
  `runtime/engine/renderer/frame/frame_shadows.cpp`, `runtime/shaders/runtime/gbuffer/gbuffer_ps.hlsl`;
  `sample/content/materials/sponza/dirt_decal.uzmat`; `scripts/decal_reference.py`,
  `scripts/test_mesh_decals.py`
- Breaking change: NO for other content (only `domain: Decal` graphs on meshes take the new path);
  Sponza's decals change by design
- User benefit: soft dirt on the stone, lit and shadowed with the stone, no grey blotches.

## Out of scope

The DBuffer decal system (box projections) is untouched; a decal's roughness / normal blending (the
dirt keeps the stone's), the SDF and GI treatment of the decal quads (they stay in the distance
field as thin geometry), the reflection-probe capture's forward pipeline (draws the quads as before).
