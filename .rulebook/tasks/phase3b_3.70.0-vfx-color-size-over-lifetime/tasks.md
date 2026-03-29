## Phase 3b-3.70.0 — VFX Color & Size Over Lifetime Curves

Wire the existing VFX curve system into the GPU particle pipeline so particles
change color and size as they age. Niagara reference: ColorOverLife / SizeOverLife modules.

## 1. Curve GPU Upload

- [ ] 1.1 Add `RHITextureHandle m_colorCurveLUT` and `m_sizeCurveLUT` to VFXParticleSystem — 1D textures (256x1 RGBA16F for color, R16F for size) baked from VFXCurve keyframes.
- [ ] 1.2 Implement `BakeCurveToLUT(VFXCurve, device, cmd)` — evaluates curve at 256 uniform samples, uploads to 1D texture via staging buffer. Returns SRV index.
- [ ] 1.3 Add default curves: color = white→transparent (alpha 1→0), size = 1.0→0.3 (shrink at end of life).

## 2. Shader Integration

- [ ] 2.1 Add `uint colorCurveSrvIndex` and `uint sizeCurveSrvIndex` to VFXBillboardCB (b2). UINT32_MAX = no curve (use flat particle color/size).
- [ ] 2.2 In `particle_billboard_vs.hlsl`: sample size curve at `normalizedAge = age / lifespan`, multiply with `particleSize * globalSizeMultiplier`.
- [ ] 2.3 In `particle_billboard_ps.hlsl`: sample color curve at `normalizedAge`, multiply with `input.color` (particle base color × curve color).
- [ ] 2.4 Both shaders must gracefully skip curve sampling when SRV index is UINT32_MAX.

## 3. CPU Integration

- [ ] 3.1 Add `VFXCurve colorOverLife` and `VFXCurve sizeOverLife` fields to VFXEmitterConfig.
- [ ] 3.2 Upload curve LUTs during VFXParticleSystem::Init() or when preset changes.
- [ ] 3.3 Pass SRV indices through VFXBillboardCB in DrawBillboards().

## 4. Preset Updates

- [ ] 4.1 Fire Fountain / Fireball: orange→yellow→dark red (color), 0.8→1.2→0.3 (size grows then shrinks).
- [ ] 4.2 Smoke Plume: white(0.6)→gray(0.3)→transparent (color), 0.5→2.0 (expand over life).
- [ ] 4.3 Sparks: white-yellow→orange→dark (color), 1.0→0.1 (shrink rapidly).
- [ ] 4.4 Magic Orb: blue→cyan→white→transparent (color), 1.0→0.5 (gentle shrink).
- [ ] 4.5 Simple Burst: warm white→transparent (color), 1.0→0.0 (fade and shrink).

## 5. Demo & Visual Verification

- [ ] 5.1 Add curve toggle key (Shift+9/0: color curve on/off, Shift+Tab override).
- [ ] 5.2 Screenshot capture at frame 100 for each preset — verify visible color/size gradients.
- [ ] 5.3 Compare with/without curves: particles should look significantly more natural with curves.

## 6. Documentation

- [ ] 6.1 Update VFX viewport help overlay (H key) with curve controls.
- [ ] 6.2 Document curve format in `docs/guides/vfx-curves.md`.
