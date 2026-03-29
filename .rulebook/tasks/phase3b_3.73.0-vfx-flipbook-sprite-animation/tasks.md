## Phase 3b-3.73.0 — VFX Flipbook / Sprite Sheet Animation

Animate particles using sprite sheet (flipbook) textures. Each particle cycles
through sub-frames of an atlas texture based on age. Niagara reference: SubImage Index module.

## 1. Flipbook Runtime

- [ ] 1.1 Verify `vfx_flipbook.hlsli` helper functions work correctly: ComputeFlipbookFrame(), ComputeFlipbookFrameRandom(), SampleFlipbookUV().
- [ ] 1.2 Verify `particle_billboard_ps.hlsl` flipbook path is active when flipbookRows > 1 || flipbookCols > 1.
- [ ] 1.3 Test all 3 blend modes: None (snap), Linear (lerp between frames), Random (per-particle static frame).

## 2. Sprite Sheet Asset Pipeline

- [ ] 2.1 Create procedural test sprite sheets at runtime (4x4 numbered frames, 8x8 fire animation frames) — generate in Init() as GPU textures.
- [ ] 2.2 Add `uint32_t spriteTextureSrvIndex` to VFXEmitterConfig to support per-preset sprite textures.
- [ ] 2.3 Load sprite sheet PNGs from `runtime/content/vfx/` directory if available.

## 3. Preset Updates

- [ ] 3.1 Fire Fountain / Fireball: 4x4 flipbook with fire frames, linear blend, 30 FPS.
- [ ] 3.2 Smoke Plume: 4x4 smoke puffs, linear blend, 15 FPS.
- [ ] 3.3 Magic Orb: 2x2 glow variations, random blend (per-particle static frame).
- [ ] 3.4 Sparks: no flipbook (single bright point).

## 4. Procedural Sprite Generation

- [ ] 4.1 Generate a 4x4 "fire" sprite sheet procedurally: each frame is a soft circle with varying radius, color shift from yellow to orange to red across frames.
- [ ] 4.2 Generate a 4x4 "smoke" sprite sheet: soft circles with increasing size and decreasing opacity per frame.
- [ ] 4.3 Upload as RGBA8 textures via staging buffer, store SRV indices.

## 5. Visual Verification

- [ ] 5.1 Screenshot with fire flipbook — particles should show animated flame frames.
- [ ] 5.2 Screenshot with smoke flipbook — puffs should evolve over lifetime.
- [ ] 5.3 Verify linear blend mode produces smooth frame transitions.

## 6. Documentation

- [ ] 6.1 Document flipbook format (rows, cols, totalFrames, FPS, blendMode).
- [ ] 6.2 Update VFX guide with sprite sheet authoring guidelines.
