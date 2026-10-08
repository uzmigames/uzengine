# Design: Sponza's recalibration

## Before
The SkyLight of `sponza.uzscene` (the user's project), `sponza-gi-camera-c.qa.uzscene` (the Camera C
harness project) and `sponza-fog.uzscene` (the Camera A harness project): `intensity 1.0`,
`indirect_lighting_intensity 1.0`; the capture's √3 gave the sky-lit terms their level.

## After
- `intensity √3`: `skyLightIntensity` multiplies every sky-lit term: the prefiltered specular
  (`skyTintSpecular`), the environment-MIS estimate, the probes' sky radiance, the final gather's sky
  hits, the SH fallback (`skyTintDiffuse`) and the height fog's sky colour (SH L0 × intensity).
  `indirect_lighting_intensity` stays 1: it scales the SH coefficients, which the SH fallback and the
  fog read on top of `intensity`, so √3 on both tripled those paths (the first run: Camera C's colour
  1.52 × through the fog).
- The direct light, the sun's shadows and the visible sky are untouched; the split-sum and SH-frame
  fixes of the same day stay (the recalibration restores the sky share only).
- The level against the pre-fix captures is not restored and is not meant to be any more: the mesh-decal
  commit removed the atrium-spanning dirt-decal mesh from the distance-field occluders, which had
  darkened the GI by 43 % (the proposal's second amendment).

## Harness
`scripts/test_sponza_recalibration.py`: Camera A (the fog project, SSR off, frame 60) and Camera C (the
QA project, frame 120). The level (Q2, Q3) against the captures of the build before the sky fix, the
fix-alone captures reported for the record; the identity (Q1) against this binary with the scene before
the edit (the split-sum item's captures), because the pre-fix captures predate the mesh winding repair,
the mesh decals and the split-sum item.
