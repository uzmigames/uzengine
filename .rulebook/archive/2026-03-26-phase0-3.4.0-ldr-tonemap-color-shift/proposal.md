# Proposal: phase0-3.4.0-ldr-tonemap-color-shift

## Why

The final LDR output has a severe color shift — the image appears with a red/brown/magenta
tint instead of matching the HDR scene colors. The HDR buffer looks correct (proper sky blue,
neutral ground, correct PBR material colors), but after tonemap the colors are distorted.

Visible in pass dump: `14-hdr.png` is correct, `24-final-ldr.png` has wrong colors.

## What Changes

### Investigate
1. Check ACES tonemap shader for channel swizzle or incorrect matrix
2. Check color grading LUT generation — the 3D LUT may have wrong mapping
3. Check auto-exposure output — exposure value may be extreme
4. Check if motion blur or CAS/FXAA is introducing the shift
5. Compare the LDR with tonemap disabled (passthrough) to isolate

## Impact
- Affected code: `tonemap_ps.hlsl`, `color_grading_lut_cs.hlsl`, possibly auto_exposure
- Visual impact: Correct final displayed colors
- No breaking changes
