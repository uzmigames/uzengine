# Lighting block B, item 5c — the sky capture's sun was the length of the illuminance vector

Rulebook task `phase0_lighting-block-b-05c-sky-capture-intensity` (proposal, design, spec
`sky-capture`). Branch `feat/sponza-gi-parity`, 2026-10-08. Block B.5b's finding, taken at the user's
decision: it changes the ambient of every atmosphere scene, Sponza's calibrated look included.

## Before

- `atmosphere_renderer.cpp` handed both capture paths a scalar `sunIntensity = skySunColor.Length()`
  (the length of the illuminance vector: 17.32 for the reference's white (10, 10, 10)), while the sky
  pass takes the per-channel illuminance (`skyCB.sunIlluminance`, 10 per channel) and the aerial
  perspective its x component. The capture shader multiplied the per-channel scattering by that scalar:
  √3 × the sky pass for a white sun, grey for a tinted one. Every image-based term samples the capture
  (the prefiltered specular, the sky SH, the environment-MIS estimate, the GI probes' sky), so metals
  reflected a sky √3 brighter than the one behind them and the sky's share of the indirect light was
  √3 too high.
- Measured on build ea13ab21 (`test-sky-capture/before`, `scripts/rendering/test_sky_capture.py --before`,
  1080p): the dumped environment against the mirrored view's sky along the same rays reads a ratio
  of 0.579 (p10 0.574, p90 0.582; by elevation 0-5 0.575, 5-10 0.575, 10-20 0.578, 20-30 0.579, 30-45 0.580); the chrome floor's sky class against
  F × the mirrored view reads a median relative error of 0.714 (ratio 1.714); on the warm-sun
  variant (sun colour (1.0, 0.7, 0.4)) the per-channel ratios are r 0.778, g 0.543, b 0.309, i.e. the
  per-channel illuminance over its length (10, 7, 4) / 12.85.

## What changed

- `sky_cubemap_cs.hlsl`: the sun illuminance per channel (`sunIllumR/G/B`, 12 root constants) in the
  in-scatter and the ground term, as the sky pass scatters it; no sun disc, as before.
- `SkyCubemapCaptureRootConstants` 10 → 12 DWORDs, `SkyCubemapCapturePSRootConstants` 8 → 10 (its
  shader is a stub; the layout follows), the asserts, the counts and the layout test.
- `atmosphere_renderer.cpp`: the three components from `skySunColor` in both paths.
- `scripts/rendering/reflection_reference.py`: the warm-sun variant; `scripts/rendering/test_sky_capture.py` (S1-S5).

## Gates (`scripts/rendering/test_sky_capture.py`)

| Gate | Criterion | Result |
|---|---|---|
| S1 | the capture is the sky pass: dumped environment vs the mirrored view sky along the same rays, ratio median in [0.95, 1.05], p10-p90 in [0.90, 1.10] | PASS: median 1.002 (p10 0.994, p90 1.007) over 619733 px; environment total luminance 61928.8 |
| S2 | the chrome floor reflects the sky: floor pixels whose reflected ray sees the sky match F x the mirrored view (median relative error <= 0.15) | PASS: median error 0.011 (ratio 0.989, p10 0.984, p90 0.997) over 1097887 px |
| S3 | a coloured sun: the ratio sky pass / capture per channel within [0.95, 1.05] on the warm-sun variant | PASS: r 0.999, g 0.997, b 0.991 over 346577 px (luminance median 0.997) |
| S4 | Sponza: direct light identical to the build before the item; the indirect light darkens, never brightens (diffuse_indirect ratio median within [0.5, 1.0]) | PASS: positions 0, direct 0; diffuse_indirect ratio 0.894; colour ratio 0.902 over 402472 lit px |
| S5 | control: the chrome floor under the PMREM is bit-identical to the build before the item (specular_indirect max |diff| = 0) | PASS: specular max |diff| 0, colour max |diff| 0 |

**Numbers (after, 1080p).** The capture against the sky pass (mirrored view, 619733 sky px): ratio median 1.002 (p10 0.994, p90 1.007; by elevation 0-5 0.995, 5-10 0.995, 10-20 1.001, 20-30 1.004, 30-45 1.005); the environment's total luminance 61928.8 (before 107250). Chrome floor, sky class (1097887 px): median relative error 0.011, ratio 0.989 (p10 0.984, p90 0.997). Warm sun (346577 sky px): per-channel ratios r 0.999, g 0.997, b 0.991 (luminance 0.997). Sponza Camera C (frame 120) against the build before: positions 0, direct 0; `diffuse_indirect` ratio median 0.894 (p10 0.817, p90 0.985), `specular_indirect` ratio 0.988; colour: 30893 px changed, ratio median 0.902, median luminance 0.0016 → 0.0015, mean signed delta -0.0163. PMREM floor: specular max |diff| 0, colour max |diff| 0. Harness 5/5.

## The look of Sponza

Camera A (the atrium, the fog project with SSR off, frame 60) against block B.5's capture of build 9f1be63f (the
lighting path of that view is unchanged by gate until this item): the direct light is identical (max |diff| 0),
`diffuse_indirect` reads 0.928 x at the median (p10 0.685, p90 1.003: the sky-lit floor and walls darken, the
sun-bounce areas do not), the colour luminance 0.985 x at the median over 528 539 lit pixels. Montage:
`test-sky-capture/sky_capture_sponza_camera_a.jpg`.

The sky's share of Sponza's indirect light drops by 1/√3 (the sun colour (1.0, 0.98, 0.95) × 10 has
length 16.9 against 9.8 per channel); the bounce from sun-lit surfaces does not change, so the
indirect light darkens unevenly (the `diffuse_indirect` ratio's p10-p90 above). The direct light is
bit-identical. The montage (`test-sky-capture/sky_capture_montage.jpg`) shows Camera C before and
after beside the reference floors; re-tuning the Sponza calibration (sky light intensity, exposure) is
the user's call, not part of this item. **Taken (2026-10-08, `lighting-sponza-recalibration.md`):** the
SkyLight's intensity and indirect-lighting intensity at √3 in the three Sponza scene files (the indirect
one is inert with GI on; corrected the same day, see that document); `diffuse_indirect` at
1.37 × (Camera A, frame 60) / 1.63 × (Camera C) of the pre-fix level, which the mesh-decal commit's
occluder removal dominates (that document's second amendment).

## Still open

- The capture's 16-step march against the sky pass's multi-scattering: they agree in shape to ~1 %
  (block B.5b); the residual sits inside S1's band.
- The capture has no sun disc (the analytic sun carries it): a mirror shows no sun in the sky it
  reflects, by design.
- The pixel-shader capture variant is a stub (black); its root-constant layout follows the compute one.
