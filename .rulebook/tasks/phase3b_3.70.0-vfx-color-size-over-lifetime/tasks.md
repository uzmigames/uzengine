## Phase 3b-3.70.0 — VFX Color & Size Over Lifetime Curves

Wire the existing VFX curve system into the GPU particle pipeline so particles
change color and size as they age. Uses analytical shader evaluation (no LUT textures).

## 1. CB & Config Fields (Analytical Approach)

- [x] 1.1 Add sizeOverLife fields (start, peak, end, peakTime) and colorOverLife fields (startRGBA, endRGBA) to VFXEmitterConfig in vfx_viewport_vfx.h.
- [x] 1.2 Add matching fields to VFXBillboardCB struct (sizeOverLife 16 bytes + colorLifeStart/End 32 bytes). Updated static_assert to 256 bytes.
- [x] 1.3 Default curves: sizeOverLife start=1.0 peak=1.0 end=0.0 peakTime=0.3; colorOverLife start=(1,1,1,1) end=(1,1,1,0).

## 2. Shader Integration

- [x] 2.1 Added sizeOverLife and colorOverLife fields to VFXBillboardCB in both particle_billboard_vs.hlsl and particle_billboard_ps.hlsl (identical layout).
- [x] 2.2 In particle_billboard_vs.hlsl: evaluate piecewise-linear size curve at normalizedAge = age/lifespan, multiply with particleSize * globalSizeMultiplier.
- [x] 2.3 In particle_billboard_ps.hlsl: evaluate color curve as lerp(colorLifeStart, colorLifeEnd, normalizedAge), multiply with input.color.

## 3. CPU Integration

- [x] 3.1 Pass all 12 new fields from VFXEmitterConfig through VFXBillboardCB in DrawBillboards().

## 4. Preset Updates

- [x] 4.1 Simple Burst: sizeStart=1.0, sizePeak=1.2, sizeEnd=0.0, peakTime=0.2, colorStart=(1,1,1,1), colorEnd=(1,0.5,0.1,0).
- [x] 4.2 Smoke Plume: sizeStart=0.5, sizePeak=2.0, sizeEnd=2.5, peakTime=0.3, colorStart=(0.6,0.6,0.6,0.8), colorEnd=(0.3,0.3,0.3,0).
- [x] 4.3 Sparks: sizeStart=1.0, sizePeak=1.0, sizeEnd=0.1, peakTime=0.0, colorStart=(1,1,0.8,1), colorEnd=(1,0.3,0,0).
- [x] 4.4 Magic Orb: sizeStart=0.8, sizePeak=1.0, sizeEnd=0.3, peakTime=0.5, colorStart=(0.3,0.7,1,1), colorEnd=(0.1,0.3,1,0).
- [x] 4.5 Explosion Debris: sizeStart=1.0, sizePeak=1.5, sizeEnd=0.2, peakTime=0.1, colorStart=(1,0.8,0.3,1), colorEnd=(0.5,0.1,0,0).
- [x] 4.6 Fireball: sizeStart=0.8, sizePeak=1.3, sizeEnd=0.5, peakTime=0.2, colorStart=(1,0.6,0.1,1), colorEnd=(0.8,0.2,0.05,0.3).

## 5. Demo & Visual Verification

- [ ] 5.1 Build and run vfx_viewport demo, verify color/size gradients visible on all presets.
- [ ] 5.2 Screenshot capture for visual confirmation.

## 6. Documentation

- [ ] 6.1 Update VFX docs with color/size over lifetime parameter descriptions.
