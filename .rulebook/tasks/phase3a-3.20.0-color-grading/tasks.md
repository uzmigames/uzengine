## 1. Implementation

- [x] 1.1 Implement white balance conversion — Planckian locus + D-illuminant + Bradford CAT, implemented in color_grading_lut_cs.hlsl WhiteBalance() function. Matches UE5 TonemapCommon.ush:586-611 exactly.
- [x] 1.2 Implement lift/gamma/gain per zone — Nuke-style 3-zone (shadow/midtone/highlight) with luminance-based smoothstep weighting. Implemented in color_grading_lut_cs.hlsl ColorCorrectAll(). Matches UE5 PostProcessCombineLUTs.usf:105-140.
- [x] 1.3 Implement global adjustments — saturation/contrast/gamma/gain/offset per UE5 ColorCorrect(). Contrast pivots at 0.18. Implemented in color_grading_lut_cs.hlsl. Matches UE5 PostProcessCombineLUTs.usf:88-102.
- [x] 1.4 Implement blue correction — AP0-space blue correction matrix before tonemap + inverse after tonemap. Matches UE5 PostProcessCombineLUTs.usf:256-279. BlueCorrection=0.6 default.
- [x] 1.5 Implement 3D LUT generation compute shader (`color_grading_lut_cs.hlsl`) — full UE5 CombineLUTsCommon parity: LogToLin decode, white balance (Planckian+D-illuminant+Bradford CAT), sRGB->AP1, gamut expansion, Nuke-style 3-zone color correction (shadow/mid/highlight), blue correction+uncorrection, FilmToneMap, ColorCorrection polynomial, color scale+overlay, gamma, sRGB encode, /1.05 normalization. 8x8x8 thread groups for 32^3 volume texture.
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
