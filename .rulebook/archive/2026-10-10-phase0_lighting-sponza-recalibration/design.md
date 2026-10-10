# Design: Sponza's recalibration

## Before
The SkyLight of `sponza.uzscene` (the user's project), `sponza-gi-camera-c.qa.uzscene` (the Camera C
harness project) and `sponza-fog.uzscene` (the Camera A harness project): `intensity 1.0`,
`indirect_lighting_intensity 1.0`; the capture's √3 gave the sky-lit terms their level.

## After
- `intensity √3` and `indirect_lighting_intensity √3`. No path reads both knobs. `intensity` (`skyLightIntensity`) multiplies the prefiltered specular and the
  environment-MIS estimate (`skyTintSpecular`), the GI probes' sky (`skyLightColor × skyLightIntensity` in
  the radiance cache and the DDGI tracers), the final gather's and the SDF reflections' sky hits
  (`skyMultiplier`) and the height fog's sky colour (the raw SH L0 × `skyLightIntensity`,
  `frame_atmosphere_passes.cpp`). `indirect_lighting_intensity` scales the SH coefficients (`shScale`,
  `frame_constants.cpp`), which only the SH fallback diffuse reads (`skyTintDiffuse = skyColor`, without
  the intensity) and the inert light probe volume's seed. The capture's √3 reached all of those through
  the cubemap, so the faithful restoration puts √3 in both. With GI on (both Sponza projects) the radiance
  cache answers every pixel and the final gather replaces its irradiance, so the indirect knob is inert
  there (Q4); it restores the SH-fallback level in GI-off configurations.
  (Amendment 3 corrects the earlier text, which kept the indirect knob at 1 on a wrong reading.)
- The direct light, the sun's shadows and the visible sky are untouched; the split-sum and SH-frame
  fixes of the same day stay (the recalibration restores the sky share only).
- The level against the pre-fix captures is not restored and is not meant to be any more: the mesh-decal
  commit removed the atrium-spanning dirt-decal mesh from the distance-field occluders, which had
  darkened the GI by 43 % (the proposal's second amendment).

## Harness
`scripts/rendering/test_sponza_recalibration.py`: Camera A (the fog project, SSR off, frame 60) and Camera C (the
QA project, frame 120). The level (Q2, Q3) against the captures of the build before the sky fix, the
fix-alone captures reported for the record; the identity (Q1) against this binary with the scene before
the edit (the split-sum item's captures), because the pre-fix captures predate the mesh winding repair,
the mesh decals and the split-sum item.
