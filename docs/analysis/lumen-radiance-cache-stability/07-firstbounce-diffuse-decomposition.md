# First-bounce diffuse shortfall decomposition (#2 under-lit-indirect lever)

Render-backed decomposition (2026-11-02, _evidence/gi/firstbounce-diffuse/report.md) of why the GI gather's
first-bounce DIFFUSE was ~0.65x of a diffuse-only-NEE path tracer, with EXACT DXR geometry (not an SDF loss).

## Factoring (0.611 = 0.789 x 0.879 x 0.880)
- x0.789 (48%): the PT reference's OWN receiver GGX lobe - the "diffuse-only" PT still sampled the 50/50
  GGX+diffuse receiver BSDF, so ~21% of its "diffuse first bounce" is glossy reflection a diffuse gather must
  not carry. NOT a gather defect; the correct diffuse-vs-diffuse target is ~0.774.
- x0.870 (28%, biggest FIXABLE): gi_hit_material.hlsli fetched the hit albedo with a BILINEAR CLAMP sampler at
  Sponza's TILED UVs -> hits read the texture edge texel (floor_01 0.67x, arch 0.66x, brick 0.78x). All GI
  consumers include the header, so the probe feed + reflections + surface cache read the same depressed albedo.
- receiver-side remainder: AO on the indirect x0.933, G-buffer albedo (absent dirt decals) x0.967,
  diffuse-energy term x0.974.

Eliminated with numbers: per-mesh aliasing (one draw per mesh), black rays, origin shift (0 cm), normal-map
hemisphere (+1.2%), sky-at-hit, a missing pi / 0.3 scale (r.GI.Intensity=1).

## Fix (committed, gated DEFAULT OFF)
r.GI.HitMaterial.WrapUv: under #ifdef UZ_GI_HIT_UV_WRAP the hit-albedo fetch uses the shared root signature's
bilinear WRAP static sampler (s3). OFF byte-identical; ON lifts the gather diffuse E/pi 0.880 -> 1.014
(x1.152, ~PT parity, predicted 1.012), pixel 0.774 -> 0.906. The correction EXPOSES a pre-existing hit-side
excess on ~7 ROIs already >1x the PT (not created here); two residue ROIs (gallery_r, wall_l_low) stay
under-lit from a separate untouched cause -> default OFF. Evidence: _evidence/gi/hit-uv-wrap/report.md.
