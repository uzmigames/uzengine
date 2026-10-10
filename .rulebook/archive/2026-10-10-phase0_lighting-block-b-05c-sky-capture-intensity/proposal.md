# The sky cubemap capture's sun: the length of the illuminance vector instead of the illuminance

## Why

Block B.5b measured the environment the sampler builds from against the sky the frame renders along
the same directions and found the capture √3 brighter at every elevation, with the shape identical
(ratio 0.578, p10-p90 0.574-0.582). The cause is one line per capture path: `atmosphere_renderer.cpp`
sets the capture's scalar `sunIntensity` to `skySunColor.Length()`, the length of the illuminance
vector (17.32 for the reference's white (10, 10, 10)), while the sky pass takes the per-channel
illuminance (`skyCB.sunIlluminance`, 10 per channel) and the aerial perspective its x component. For a
coloured sun the scalar cannot match the sky pass at all (the capture is grey where the sky is tinted).
Every image-based term samples the capture: the prefiltered specular, the sky SH, the environment-MIS
estimate, the GI probes' sky (`g_skyRadianceSrv` in the radiance cache and the DDGI tracers). So
metals reflected a sky √3 brighter than the one behind them, and the sky's share of Sponza's indirect
light was √3 too high. The user decided to take the fix (2026-10-08) knowing that it changes the ambient
of every atmosphere scene, Sponza's calibrated look included; the change is measured and shown.

## What changes

- The capture shaders (`sky_cubemap_cs.hlsl`, the pixel-shader fallback) take the sun illuminance per
  channel (three root constants in place of the scalar) and scatter it per channel exactly as the sky
  pass does; the ground term too.
- `SkyCubemapCaptureRootConstants` 10 → 12 DWORDs, `SkyCubemapCapturePSRootConstants` 8 → 10;
  `atmosphere_renderer.cpp` fills the three components from `skySunColor`.
- No sun disc in the capture, as before (the analytic sun carries the disc).
- Evidence: `scripts/rendering/test_sky_capture.py` (S1-S5) on the reflection reference (the dumped environment
  against the mirrored view's sky; the chrome floor against the mirrored view), its warm-sun variant
  (`reflection-reference-warm`, sun colour (1.0, 0.7, 0.4)), the PMREM floor and Sponza.

## Gates (pre-registered)

S1 the capture is the sky pass: the dumped environment against the mirrored view's sky pixels along
the same rays, ratio median within [0.95, 1.05] and p10-p90 within [0.90, 1.10] (before 0.578). S2 the
chrome floor reflects the sky: block B.5b's E1, median relative error ≤ 0.15 (before 0.714). S3 a
coloured sun: on the warm-sun variant the per-channel ratio sky pass / capture lies within
[0.95, 1.05] for r, g and b. S4 Sponza: positions and direct light identical to the capture of the build
before the item; the indirect light darkens, never brightens (`diffuse_indirect` ratio median within
[0.5, 1.0]); the colour change reported with a montage for the look decision. S5 control: the chrome
floor under the PMREM (no atmosphere capture) bit-identical to the build before. Reported: the
environment's total luminance before / after, the Sponza colour statistics, `test_sky_ibl_rebake.py`
(block A.5's guard of the capture's re-bake).

## Impact
- Affected specs: sky-capture (new)
- Affected code: `runtime/shaders/runtime/atmosphere/sky_cubemap_cs.hlsl`, the pixel-shader capture
  variant, `runtime/engine/renderer/passes/atmosphere/sky_cubemap_capture_pass.{h,cpp}`,
  `sky_cubemap_capture_ps_pass.{h,cpp}`, `runtime/engine/renderer/atmosphere/atmosphere_renderer.cpp`;
  `scripts/rendering/reflection_reference.py` (warm variant), `scripts/rendering/test_sky_capture.py`
- Breaking change: YES for the look of atmosphere scenes: the sky's share of every image-based term
  drops by 1/√3 (white sun); Sponza's calibration may need a re-tune, which is the user's call
- User benefit: reflections and the sky-lit ambient match the sky the frame shows, per channel.

## Out of scope

Re-tuning Sponza's look (the user decides after seeing the montage); the sun disc in the capture (kept
out: the analytic sun carries it); the capture's 16-step march against the sky pass's multi-scattering
(they agree to 1 % in shape, block B.5b); the other B.5b findings (`sphere.uzmesh` inside-out, the
split-sum level on rough metals).
