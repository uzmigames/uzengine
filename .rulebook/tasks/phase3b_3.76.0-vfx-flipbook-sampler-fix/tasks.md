## Phase 3b-3.76.0 — Fix VFX Flipbook: Sampler Mismatch + Niagara Parity

The flipbook sprite sheet loads correctly (SRV valid) but particles still show
procedural circles. Root cause: sampler registers in the billboard PS don't match
the global root signature's static samplers.

## Root Cause Analysis

Global root signature static samplers:
- s0: 16x Anisotropic WRAP (material textures)
- s1: Comparison Linear BORDER (shadow PCF)
- s2: Bilinear CLAMP (post-process, depth)
- s3: Bilinear WRAP (3D noise)

Billboard PS declares:
- s0: linearSampler (expects bilinear CLAMP) → WRONG: gets aniso WRAP
- s1: pointSampler (expects point CLAMP) → WRONG: gets comparison sampler

The comparison sampler at s1 causes depth sampling to return garbage,
cascading into alpha = 0 → discard → procedural fallback.

## 1. Fix Sampler Registers in Billboard PS

- [ ] 1.1 In `particle_billboard_ps.hlsl`: change `linearSampler` from `register(s0)` to `register(s2)` (bilinear clamp — correct for sprite sampling).
- [ ] 1.2 In `particle_billboard_ps.hlsl`: change `pointSampler` from `register(s1)` to `register(s0)` — OR use `s2` (bilinear) for depth too since point isn't available. Using s2 (bilinear) for depth is acceptable since depth fade is approximate.
- [ ] 1.3 Verify root signature static samplers in `dx12_pso_creation.cpp` — document the mapping: s0=aniso, s1=comparison, s2=bilinear_clamp, s3=bilinear_wrap.

## 2. Fix Sampler in Trail Shaders (Same Issue)

- [ ] 2.1 In `trail_ps.hlsl`: check sampler registers match root signature.
- [ ] 2.2 Fix any sampler mismatches in trail PS.

## 3. Flipbook UV Verification

- [ ] 3.1 Add debug log in DrawBillboards() on first few frames: print spriteTextureIdx, flipbookRows, flipbookCols, flipbookFPS to verify correct values reach the shader.
- [x] 3.2 Verify ComputeFlipbookFrame() in `vfx_flipbook.hlsli` produces correct UV sub-rectangles for 6x6 grid. Reviewed FrameIndexToUV and SampleFlipbookUV — logic correct for any NxM grid. Added debug UV visualization line (commented out) in PS.
- [ ] 3.3 Test with --sprite flag: `--sprite "path/to/fireball_6x6.tga"` should show real fire frames.

## 4. Niagara Material Parity

- [ ] 4.1 Niagara uses Unlit material with particle color vertex interpolation + SubUV texture sampling. Verify our PS follows the same pattern: spriteColor * particleColor * colorOverLife.
- [ ] 4.2 Niagara applies emissive AFTER color multiplication. Verify our emissive path: `finalColor.rgb += finalColor.rgb * emissiveIntensity` (already fixed).
- [ ] 4.3 Niagara SubUV uses bilinear sampling with CLAMP addressing on the sub-rectangle. Verify our flipbook UV remapping clamps within the cell.
- [x] 4.4 Changed billboard PSO from additive (SrcAlpha/One) to premultiplied alpha (One/InvSrcAlpha). PS now premultiplies RGB by alpha before return. This supports both additive glow (from emissive) and proper alpha transparency, matching Niagara's premultiplied alpha for translucent/emissive materials.

## 5. Visual Verification

- [ ] 5.1 Screenshot with custom TGA sprite sheet — verify fire frames visible.
- [ ] 5.2 Compare procedural vs real sprite side by side.
- [ ] 5.3 Test flipbook animation — frames should cycle over particle lifetime.

## 6. Documentation

- [ ] 6.1 Document sampler register mapping for VFX shaders.
- [ ] 6.2 Document --sprite CLI flag for custom sprite sheets.
