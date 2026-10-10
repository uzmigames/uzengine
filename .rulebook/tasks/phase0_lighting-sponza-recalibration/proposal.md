# Sponza's ambient after the sky capture fix: the √3 made explicit in the scene

## Why

Block B.5c fixed the sky cubemap capture (the sun scaled by the length of the illuminance vector,
√3 × the per-channel value of a white sun). Every image-based term then dropped its sky share by
1/√3, and Sponza, calibrated with the old capture, read `diffuse_indirect` at 0.894 × (Camera C) and
0.928 × (Camera A) of its previous level, colour at 0.902 × / 0.985 ×. The user asked for the previous
ambient level back (2026-10-08).

## What changes

- `sample/content/scenes/sponza.uzscene`: the SkyLight's `intensity` and `indirect_lighting_intensity`
  go from 1.0 to √3 (1.7320508), so the sky's share of the prefiltered specular, the sky SH, the
  environment-MIS estimate and the GI probes' sky returns to the calibrated level with the factor
  written where it can be seen, instead of hidden in the capture.
- Nothing in the engine changes; the visible sky stays the sky pass's.
- Evidence: `scripts/test_sponza_recalibration.py` (Q1-Q3) against the captures of the build before the
  sky fix.

## Amendment (2026-10-08, after the first run)

The first run set both `intensity` and `indirect_lighting_intensity` to √3 and read Camera C's colour at
1.52 × and Camera A's diffuse at 1.24 ×: the SH fallback is multiplied by both (`shScale` on the
coefficients, `skyTintDiffuse` in the shader) and the height fog's sky colour by SH L0 × `intensity`, so
those paths were tripled. The indirect-lighting intensity stays 1. And the pre-fix captures predate the
mesh winding repair, the mesh decals and the split-sum item, which move positions and direct light where
they apply, so Q1 (the identity) is measured against this binary's captures of the scene before the edit
(the split-sum item's Sponza captures); Q2 / Q3 keep the pre-fix captures as the level reference.

## Amendment 2 (2026-10-08, after the second run): the target level and the scene files

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

The harness projects load `sponza-gi-camera-c.qa.uzscene` (Camera C) and `sponza-fog.uzscene` (Camera A),
not `sponza.uzscene` (the user's project): the first two runs measured an unedited scene (the ratios
against this binary's captures read 1.000). The √3 goes into the SkyLight `intensity` of all three.

## Amendment 3 (2026-10-08, the review of the indoor-lighting task): Amendment 1 was wrong

Amendment 1's two claims do not hold. No path reads both knobs: the SH fallback reads
`indirect_lighting_intensity` only (`shScale` on the coefficients; `skyTintDiffuse = skyColor`, without
the intensity) and the height fog reads `intensity` only (the raw SH L0 × `skyLightIntensity`). And the
first run never measured the knobs: it edited `sponza.uzscene` alone while the harness loads the other two
scenes (Amendment 2), so its 1.52 × / 1.24 × were the mesh-decal commit's occluder removal against the
pre-fix captures. The faithful restoration puts √3 in both knobs; with GI on the indirect knob is inert.
Gate Q4, registered before the run: against the captures with √3 in `intensity` only, positions and
direct light identical and the medians of the diffuse indirect, the specular indirect and the colour
within 0.5 %.

## Gates (pre-registered)

Q1 positions and direct light identical to the build before the sky fix (the sky light touches
neither). Q2 `diffuse_indirect` median ratio against the pre-fix capture within [0.95, 1.05] at Camera
A and C (the fix alone: 0.928 / 0.894). Q3 colour median luminance ratio within [0.95, 1.05] at both
cameras. Reported: the specular ratios, the montage (pre-fix, fix alone, recalibrated).

## Impact
- Affected specs: sponza-recalibration (new)
- Affected code: `sample/content/scenes/sponza.uzscene`; `scripts/test_sponza_recalibration.py`
- Breaking change: NO (content; Sponza returns to its calibrated level)
- User benefit: Sponza looks as it did, with the sky's factor in the scene file.

## Out of scope

Other atmosphere scenes (their ambient stays at the corrected level; the reference scenes carry the
corrected physics on purpose); a re-tune beyond the √3.
