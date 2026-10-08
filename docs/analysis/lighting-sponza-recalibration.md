# Lighting side item — Sponza's ambient recalibrated after the sky capture fix

Rulebook task `phase0_lighting-sponza-recalibration` (proposal, design, spec `sponza-recalibration`).
Branch `feat/sponza-gi-parity`, 2026-10-08. The user's choice after block B.5c.

## Before

Block B.5c's capture fix removed the √3 the sky capture applied to a white sun; Sponza, calibrated with
the old capture, read `diffuse_indirect` at 0.894 × (Camera C) and 0.928 × (Camera A) of its previous
level and the colour at 0.902 × / 0.985 ×. The split-sum item of the same day (the prefiltered mip
curve, the sky SH frame and packing) changed the image-based specular and the SH fallback as well.

## What changed

- `sample/content/scenes/sponza.uzscene` (the user's project), `sponza-gi-camera-c.qa.uzscene` (the
  Camera C harness project) and `sponza-fog.uzscene` (the Camera A harness project): the SkyLight's
  `intensity` and `indirect_lighting_intensity` at √3 (1.7320508). The engine is untouched; the visible
  sky stays the sky pass's.
- No path reads both knobs. `intensity` (`skyLightIntensity`) multiplies the prefiltered specular and the
  environment-MIS estimate (`skyTintSpecular`), the GI probes' sky (`skyLightColor × skyLightIntensity` in
  the radiance cache and the DDGI tracers), the final gather's and the SDF reflections' sky hits
  (`skyMultiplier`) and the height fog's sky colour (the raw SH L0 × `skyLightIntensity`,
  `frame_atmosphere_passes.cpp`). `indirect_lighting_intensity` scales the SH coefficients (`shScale`,
  `frame_constants.cpp`), which only the SH fallback diffuse reads (`skyTintDiffuse = skyColor`, without
  the intensity) and the inert light probe volume's seed. The capture's √3 reached all of those through
  the cubemap, so the faithful restoration puts √3 in both. With GI on (both Sponza projects) the radiance
  cache answers every pixel and the final gather replaces its irradiance, so the indirect knob is inert
  there (Q4); it restores the SH-fallback level in GI-off configurations.
- `scripts/test_sponza_recalibration.py` (Q1-Q4).

**Correction (2026-10-08, the commit after 4501fa74).** The first committed version (4501fa74) kept
`indirect_lighting_intensity` at 1, on two claims that were both wrong: that the SH fallback and the fog
read both knobs (in the code the fallback reads the indirect knob only and the fog the intensity only),
and that a first attempt with both at √3 had tripled those paths (Camera C's colour 1.52 ×). The first two
runs edited `sponza.uzscene` alone and never reached the harness scenes; the 1.52 × was the mesh-decal
commit's occluder removal measured against the pre-fix captures (the second amendment). The review of the
indoor-lighting task (2026-10-08) found the error.

## Gates (`scripts/test_sponza_recalibration.py`)

| Gate | Criterion | Result |
|---|---|---|
| Q1 | Sponza Camera A and C: positions and direct light identical to this binary with the scene before the edit | PASS: camera_a: positions 0, direct 0; camera_c: positions 0, direct 0 |
| Q4 | with GI on, indirect_lighting_intensity is inert: against the intensity-only captures, positions and direct identical and the diffuse / specular / colour medians within 0.5 % | FAIL: camera_a: diffuse 1.0085, specular 1.001294676764355, colour 1.0003; camera_c: diffuse 1.0000, specular 1.0000526257980824, colour 1.0001 |
| Q2 | the sky-lit indirect is back: diffuse_indirect median ratio against the pre-fix capture within [0.95, 1.05] at both cameras | FAIL: camera_a: 1.374 (the fix alone 0.9280510144228076); camera_c: 1.627 (the fix alone 0.8943248750829234) |
| Q3 | the colour: median luminance ratio against the pre-fix capture within [0.95, 1.05] at both cameras | FAIL: camera_a: 1.273; camera_c: 1.629 |

**Numbers (1080p).** Camera A: the edit against this binary with the scenes before it: positions 0, direct 0, `diffuse_indirect` 1.075, `specular_indirect` 1.034, colour 1.015; against the intensity-only captures (Q4): `diffuse_indirect` 1.0085, `specular_indirect` 1.0013, colour 1.0003; against the pre-fix captures: `diffuse_indirect` 1.374 (the sky fix alone 0.928), colour 1.273 (the sky fix alone 0.985). Camera C: the edit against this binary with the scenes before it: positions 0, direct 0, `diffuse_indirect` 1.072, `specular_indirect` 1.038, colour 1.072; against the intensity-only captures (Q4): `diffuse_indirect` 1.0000, `specular_indirect` 1.0001, colour 1.0001; against the pre-fix captures: `diffuse_indirect` 1.627 (the sky fix alone 0.894), colour 1.629 (the sky fix alone 0.902). Harness 1/4.

**Q4 at Camera A.** As registered, Q4 fails there: the diffuse indirect median read 1.0085 against the intensity-only capture (tolerance 0.5 %, set without a measured floor). Camera A is captured at frame 60, before the GI has converged, and a second process of the same configuration read 0.9913 against the harness run (p10 0.946, p90 1.026) and 1.0005 against the intensity-only capture (p10 1.000, p90 1.001; specular 1.0002, colour 1.0003). Every Camera A pixel takes the gather and reflection path (path bits 0x3), none reads the SH fallback, which is the only consumer of the indirect knob: the 0.85 % is the cross-process spread at frame 60, and the knob is inert (`q4_floor.json` beside the harness output).

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
