## 1. Implementation

- [ ] 1.1 Implement white balance conversion — temperature (Kelvin) to RGB multiplier via Planckian locus approximation. Tint as green-magenta offset on the perpendicular axis. UE5 uses CIE D-illuminant + chromatic adaptation (Bradford matrix).
- [ ] 1.2 Implement lift/gamma/gain per zone — luminance-based zone weighting: shadows (luma < shadowsMax), highlights (luma > highlightsMin), midtones (in between). Per-zone: `output = gain * pow(max(0, input + offset), 1/gamma)`. Apply saturation per zone as lerp toward luminance.
- [ ] 1.3 Implement global adjustments — saturation (lerp to luma), contrast (pivot at 0.18 gray, `pow(color/0.18, contrast)*0.18`), gamma, gain, offset. Applied in order: saturation → contrast → gamma → gain → offset.
- [ ] 1.4 Implement blue correction — desaturate blue channel when luminance is low: `blue *= lerp(1, saturation, blueCorrection * (1-luma))`. UE5 default blueCorrection=0.6. Prevents oversaturated blue in dark scenes.
- [ ] 1.5 Implement 3D LUT generation compute shader (`lut_generate_cs.hlsl`) — dispatch 32×32×32 threads. Each thread: start with identity color (x/31, y/31, z/31), apply white balance → saturation → contrast → gamma → gain → offset → per-zone → blue correction → scene tint. Write to RGBA8 3D texture.
- [ ] 1.6 Implement LUT regeneration trigger — regenerate LUT only when color grading parameters change (dirty flag). Not per-frame. Mark dirty when PostProcessVolume settings change.
- [ ] 1.7 Implement LUT application in tonemap pass — after ACES tonemap + exposure: `gradedColor = lut3D.SampleLevel(linearSampler, tonemappedColor * (31.0/32.0) + 0.5/32.0, 0)`. Scale+offset maps [0,1] to LUT texel centers.
- [ ] 1.8 Implement external LUT import — load artist-provided 256×16 LUT strip (industry standard) and convert to 16×16×16 3D texture. Applied as override or blended with procedural LUT.
- [ ] 1.9 Add ColorGradingDesc to PostProcessStack — enabled/disabled, temperature, tint, globalSaturation/contrast/gamma/gain/offset, shadow/midtone/highlight zones, blueCorrection, expandGamut, sceneTint, optional external LUT path.
- [ ] 1.10 Create color grading demo — same scene rendered with 4 different presets: neutral, warm cinematic, cool sci-fi, desaturated horror. Side-by-side comparison. Interactive sliders for temperature/saturation/contrast.

## 2. Visual Verification

- [ ] 2.1 Screenshot: neutral settings — identical to no color grading (identity LUT)
- [ ] 2.2 Screenshot: warm preset (3200K, +saturation) — warm orange/golden cast
- [ ] 2.3 Screenshot: cool preset (10000K, +blue gain) — cold blue/cyan cast
- [ ] 2.4 Screenshot: desaturated (saturation=0) — perfect grayscale
- [ ] 2.5 Screenshot: shadow/highlight split — shadows tinted blue, highlights tinted orange
- [ ] 2.6 Screenshot: external LUT from Photoshop applied correctly
- [ ] 2.7 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document all color grading parameters with UE5 equivalents
- [ ] 3.2 Add artist workflow guide (temperature, LUT import from DaVinci/Photoshop)
- [ ] 3.3 Document LUT format specification (32³ RGBA8, 256×16 strip import)
