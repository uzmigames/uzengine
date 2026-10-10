# Clouds — the reference scenes, the harness and the baseline (2026-10-08)

For Rulebook task `phase0-3.1.0-volumetric-clouds-rewrite` (plan step 2 of its map; Phase 1.9's harness). Branch
`feat/sponza-gi-parity`, binary 7eee2d00 (after `phase0_clouds-noise-assets`: both noise volumes load).

## The scenes (`scripts/cloud_reference.py`)

A 1 km floor, sky atmosphere (sun illuminance 10), a sun at 40° elevation toward +X (SUN_DIR (0.766, 0, 0.643)),
a sky light at intensity 1, no height fog (`r.Fog 0`), one `volumetric_clouds` entity with every serializer key
written (coverage 0.45, extinction 0.04, wind 0, 64 primary / 6 light steps: the shader's caps). One project per
pose because uzengine.exe takes no camera arguments: `sun` (the sun at row ~282), `away`, `horizon` (pitch +8°, for
aerial perspective) and `above` (15 km up, a 100 m near plane so float32 ray reconstruction stays at 0.0005°).
Variants: `noclouds` (the entity removed: the sky mask and the controls), `steps32` / `steps16` (the banding dose).
1920×1080, TAA on (the clouds use the unjittered matrix), auto exposure off, exposure 1.

## The harness (`scripts/test_clouds.py`)

`--baseline` captures every pose and variant (14 captures of 300 frames: a dump of frame 181 with the `motion`
fields, screenshots 150-181, `gpu_ms` over frames 200-299 with no dump or screenshot); a default run captures into
`test-clouds/after` and compares. Metrics (formulas in the script's docstring): cost (every `gpu_ms` key starting
with "cloud", the frame, the delta against `noclouds`), the sky mask (relative luminance difference against
`noclouds`), banding (the power of detrended vertical ln Y profiles at 0.125-0.25 cycles/px, where 64 slices land;
`noclouds` as the negative control, 32 / 16 steps as the dose), temporal stability (per-pixel CoV over 32
screenshots, p95), silver lining (cloud edges over interiors 3-30° from the sun, the away pose as the control, the
sky behind them reported), aerial perspective (contrast and chromaticity against the sky per elevation band at
the horizon pose). Gates C1 (every capture completes, resolution, cloud passes present / absent as expected) and
C2 (every metric finite); later phases add theirs.

## The baseline

| Pose | Cloud passes (ms) | GPU frame (ms) | Cloud / clear fraction | Banding hf power | Temporal CoV p95 | Silver lining | AP falloff |
|---|---|---|---|---|---|---|---|
| sun | 5.33 | 6.03 (0.579 without clouds) | 0.917 / 0.0167 | 5.42e-06 (257 × the control) | 0.00257 | 0.388 | - |
| away | 5.7 | 6.34 (0.577 without clouds) | 0.869 / 0.0581 | 8.03e-06 (461 × the control) | 0.00318 | 0.401 | - |
| horizon | 3.6 | 4.31 (0.614 without clouds) | 0.955 / 0.0106 | 2.92e-05 (1.57e+03 × the control) | 0.00403 | - | 0.192 |
| above | 4.58 | 5.08 (0.498 without clouds) | 0.851 / 0.119 | 2.22e-05 (6.96 × the control) | 0.00227 | - | - |

Banding dose at the sun pose: 64 steps 5.42e-06, 32 steps 1.75e-05, 16 steps 8.14e-05 (monotone).
`test-clouds/baseline/clouds_montage.jpg` holds the four poses, their controls, the masks and the step crops.

**What the baseline shows.** The clouds cost 3.6-5.7 ms at 1080p (half-resolution march). They are flat white:
the cloud edges near the sun read 0.39 of the interiors (no silver lining: edges should be brighter there), the
same ratio as away from the sun (0.40), because the march has no multi-scattering octaves, phase-function
lobes per octave or powder term (Phase 2). The horizon pose keeps contrast against the sky down to 0.5° elevation
(falloff 0.19): distant clouds fade toward the ambient colour, not toward the aerial perspective (Phase 5). The
64-step banding is 257 × the no-cloud control and rises 3 × / 15 × at 32 / 16 steps (Phase 1's adaptive march
and jitter).

## Notes from the harness review

- The screenshot-based temporal metric under-reads cloud flicker 3-6 × (the tonemap's shoulder); an HDR version
  needs a small dump region over frames 150-181.
- The cloud mask has no transmittance: a "cloud" is a pixel whose luminance moved more than 25 % from the sky.
- `tracing_max_distance_km` is not wired (the shader hard-codes 50 km); the composite darkens and blue-tints clouds
  by up to about 5 % toward the frame corners.
