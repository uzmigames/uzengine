# Lighting side item — Sponza's ambient recalibrated after the sky capture fix

Rulebook task `phase0_lighting-sponza-recalibration` (proposal, design, spec `sponza-recalibration`).
Branch `feat/sponza-gi-parity`, 2026-10-08. The user's choice after block B.5c.

## Before

Block B.5c's capture fix removed the √3 the sky capture applied to a white sun; Sponza, calibrated with
the old capture, read `diffuse_indirect` at 0.894 × (Camera C) and 0.928 × (Camera A) of its previous
level and the colour at 0.902 × / 0.985 ×. The split-sum item of the same day (the prefiltered mip
curve, the sky SH frame) changed the image-based specular and the SH fallback as well.

## What changed

- `sample/content/scenes/sponza.uzscene` (the user's project), `sponza-gi-camera-c.qa.uzscene` (the
  Camera C harness project) and `sponza-fog.uzscene` (the Camera A harness project): the SkyLight's
  `intensity` at √3 (1.7320508); `indirect_lighting_intensity` stays 1. (The first two runs edited
  `sponza.uzscene` alone and measured an unedited scene: the ratios against this binary read 1.000.) `intensity` multiplies every sky-lit term: the prefiltered
  specular and the environment-MIS estimate (`skyTintSpecular`), the GI probes' sky
  (`skyLightColor × skyLightIntensity` in the radiance cache and the DDGI tracers), the final gather's
  sky hits (`skyMultiplier`), the SH fallback (`skyTintDiffuse`) and the height fog's sky colour
  (`SH L0 × skyLightIntensity`). `indirect_lighting_intensity` scales the SH coefficients, which the
  SH fallback and the fog read on top of `intensity`: the first attempt with both at √3 tripled those
  two paths (Camera C's colour 1.52 × through the fog, Camera A's diffuse 1.24 ×). The engine is
  untouched; the visible sky stays the sky pass's.
- `scripts/test_sponza_recalibration.py` (Q1-Q3).

## Gates (`scripts/test_sponza_recalibration.py`)

| Gate | Criterion | Result |
|---|---|---|
| Q1 | Sponza Camera A and C: positions and direct light identical to this binary with the scene before the edit | PASS: camera_a: positions 0, direct 0; camera_c: positions 0, direct 0 |
| Q2 | the sky-lit indirect is back: diffuse_indirect median ratio against the pre-fix capture within [0.95, 1.05] at both cameras | FAIL: camera_a: 1.358 (the fix alone 0.9280510144228076); camera_c: 1.627 (the fix alone 0.8943248750829234) |
| Q3 | the colour: median luminance ratio against the pre-fix capture within [0.95, 1.05] at both cameras | FAIL: camera_a: 1.253; camera_c: 1.629 |

**Numbers (1080p).** Camera A: against the pre-fix capture, `diffuse_indirect` ratio 1.358 (p10 1.074, p90 2.355; the fix alone 0.928); `specular_indirect` ratio 1.211 (the fix alone 0.983); colour ratio 1.253 (the fix alone 0.985), median luminance 0.0035 → 0.0051. Against this binary with the scene before the edit: positions 0, direct 0, `diffuse_indirect` ratio 1.054, `specular_indirect` ratio 1.032, colour ratio 1.010. Camera C: against the pre-fix capture, `diffuse_indirect` ratio 1.627 (p10 1.413, p90 1.862; the fix alone 0.894); `specular_indirect` ratio 1.131 (the fix alone 0.988); colour ratio 1.629 (the fix alone 0.902), median luminance 0.0016 → 0.0027. Against this binary with the scene before the edit: positions 0, direct 0, `diffuse_indirect` ratio 1.072, `specular_indirect` ratio 1.038, colour ratio 1.072. Harness 1/3.

## The level against the build before the sky fix is not the target any more

The chain of Camera C captures (pre-fix → the sky fix alone → the mesh winding repair → the mesh decals →
the split-sum item → now) located the level change between the sky fix and now in the mesh-decal commit
(513e2983): `diffuse_indirect` × 1.69 (Camera C) / × 1.39 (Camera A), uniform over the frame (every
region of an 8 × 4 grid between 1.5 and 2.2 ×). Sponza's dirt-decal mesh (`decals_1st_floor1_dirt_decal_0`,
7036 vertices, bounds spanning the atrium: 12 × 27 × 16 m) had been a Masked opaque occluder in the
distance field (`r.GI.MeshSdf.TwoSided 1`, 128³); the experiment on the current binary (the material
back to the Surface domain, Camera C) returns the diffuse indirect to 0.571 × and the specular to
0.34 × of the decal build. So the pre-fix look included a bogus occluder darkening the GI by 43 %, and
"within 5 % of the pre-fix level" (Q2, Q3) is no longer the right target: the √3 restores the sky share
the capture fix removed, its own effect is measured against this binary's captures of the scene before
the edit, and the level against the pre-fix captures is reported with the decal occluder's share.

The √3 is kept: it is the factor the capture removed from the authored sky, and the user asked for it;
whether Sponza now reads too bright is a visual call on the montage (`sponza_recalibration_montage.jpg`:
the pre-fix frame, the sky fix alone, now).

## Still open

- The other atmosphere scenes keep the corrected level (the reference scenes on purpose); any scene
  calibrated with the old capture carries the same 1/√3 and can take the same `intensity`.
- Thin open "sticker" meshes in the two-sided mesh SDF: the dirt-decal mesh's 128³ field darkened the
  whole atrium's GI by 43 % while it was Masked. Any Masked mesh of that shape does the same; the mesh
  SDF bake has no guard for it (an item for the GI).
- The pre-fix references predate the mesh winding repair, the mesh decals and the split-sum item; the
  identity (Q1) is therefore measured against this binary with the scene before the edit, and the
  level (Q2, Q3) against the pre-fix captures, whose medians those items barely move (the split-sum
  item's Sponza colour ratios 1.000 / 1.001).
