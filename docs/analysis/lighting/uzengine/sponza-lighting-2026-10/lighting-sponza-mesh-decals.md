# Lighting side item — Sponza's dirt decals: masked quads made mesh decals blended into the G-buffer

Rulebook task `phase0_lighting-sponza-mesh-decals` (proposal, design, spec `mesh-decals`). Branch
`feat/sponza-gi-parity`, 2026-10-08. The user's report: the stone dirt decals read as grey blotches
where soft dirt was expected.

## Before

- Sponza's two decal meshes (`decals_1st_floor1_dirt_decal_0`, `Materials_dirt_decal_0`) carry the
  material graph `materials/sponza/dirt_decal.uzmat`: quads a few centimetres in front of the stone,
  base colour and opacity from `dirt_decal_baseColor.png` (RGBA 2048², alpha mean 0.33, 31 % of the
  texels above 0.5, 69 % above 0.05). The graph is `blendMode: Masked` with `alphaClipThreshold: 0`,
  which the scene loader turns into an alpha test at 0.3333. So every texel above a third of opacity
  was drawn fully opaque with the texture's flat colour, writing its own depth, position and normal
  into the G-buffer and sitting in the shadow and distance-field occluder sets; everything below was
  cut. The engine's decal system projects boxes (`DecalComponent`, the DBuffer); these mesh quads had
  no path that blends.
- Measured on build b8ff5077 (`test-mesh-decals/before`, `scripts/rendering/test_mesh_decals.py --before`,
  1080p) on the reference scene (`scripts/rendering/decal_reference.py`: a grey wall, a decal quad 3 cm in front
  with an opacity ramp along x, a sun from the camera's side, the wall alone as the control): along the
  ramp the lit colour deviates from lerp(wall, decal, alpha) by 0.828 at the median (p90 0.940),
  R² 0.665 — a step at alpha 1/3; under the decal the G-buffer positions sit 2.969 cm in front of
  the wall; the direct light outside the footprint matched the control (max |diff| 0: the
  quad's 2 cm shadow offset lies within the shadow bias).

## What changed

- A material graph with `domain: Decal` on a mesh is a mesh decal: `scene_sync.cpp` sets
  `materialFlags` bit 4 (0x10) and clears the blend bits; `MaterialPipeline::BuildGBufferPipelineDesc`
  builds its PSO with the albedo target blending SrcAlpha / InvSrcAlpha on the colour and Zero / One on
  the alpha (the AO underneath kept), the normal, position and emissive targets kept (Zero / One), no
  depth write; the G-buffer pass runs its command loop in two phases (the opaque and masked draws, then
  the decals over the finished depth) and skips decals in the depth-only pass; `gbuffer_ps.hlsl` writes
  the graph's opacity in the albedo target's alpha for these draws, discards below 1/255 and bypasses
  the masked test; the shadow passes and the distance-field occluder set skip bit 4.
- `dirt_decal.uzmat`: `domain: Decal`.
- `scripts/rendering/decal_reference.py`, `scripts/rendering/test_mesh_decals.py` (D1-D5).

## Gates (`scripts/rendering/test_mesh_decals.py`)

| Gate | Criterion | Result |
|---|---|---|
| D1 | blend identity: the lit colour along the ramp is lerp(wall, decal, alpha) (median relative deviation <= 0.03, R2 >= 0.99) | PASS: median deviation 0.011820907284931311 (p90 0.04630398867419196), R2 0.9999511467325812 over 221902 px |
| D2 | no geometry: under the decal the G-buffer positions lie on the wall plane (|y| <= 0.5 cm) and the normals are the wall's (dot >= 0.999) | PASS: |y| median 0.029 cm (p90 0.104), normal dot median 1.0000 (p10 1.0000) over 221902 px |
| D3 | no shadow: the direct light outside the decal footprint equals the control's (max |diff| <= 1e-4) | PASS: max |diff| outside 0 (0 px above 1e-4); inside 0 |
| D4 | Sponza Camera A and C: positions change only inside the decal meshes' world-bounds footprint; the GBuffer pass within 10 % of the build before | PASS: camera_a: 190070 px changed, 0 outside, GBuffer 0.547555 -> 0.566505 ms; camera_c: 241509 px changed, 0 outside, GBuffer 0.519871 -> 0.532182 ms |
| D5 | Sponza's decal footprint is soft: the boundary contrast of the lit colour <= 0.5 x the build before | PASS: 0.530 -> 0.120; 0.497 -> 0.187 |

**Numbers (after, 1080p).** Reference ramp (221902 px): median relative deviation 0.012 (p90 0.046), R² 1.000, the decal end's luminance 0.0124 against the wall's 0.0704; under the decal |y| median 0.029 cm (p90 0.104), normal dot 1.0000 (p10 1.0000); direct light outside the footprint max |diff| 0 (inside 0). Sponza Camera A: positions changed 190070 px (0 outside the decal meshes' bounds, which cover 1996801 px), direct light changed 108101 px (100.0 % inside), footprint 190070 px, boundary contrast 0.530 → 0.120, GBuffer 0.548 → 0.567 ms. Sponza Camera C: positions changed 241509 px (0 outside the decal meshes' bounds, which cover 1767900 px), direct light changed 0 px (0.0 % inside), footprint 241509 px, boundary contrast 0.497 → 0.187, GBuffer 0.520 → 0.532 ms. Harness 5/5.

## Still open

- The decal keeps the stone's roughness and normal (only the albedo blends); a dirt that changes the
  roughness needs the normal / roughness targets blended with their own factors (the octahedral normal
  cannot be blended linearly).
- The reflection-probe capture's forward pipeline draws the quads as before (opaque, masked at the
  loader's threshold).
- The decal quads stay in the mesh SDF bake (they are out of the distance-field occluder set now).
- The one-frame latency of the DBuffer decals and the mesh decals' interaction with the TAA velocity
  (no motion vectors of their own) are untouched.
