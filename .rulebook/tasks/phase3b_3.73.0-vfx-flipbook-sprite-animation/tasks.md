## Phase 3b-3.73.0 — VFX Flipbook / Sprite Sheet Animation

Animate particles using sprite sheet (flipbook) textures. Each particle cycles
through sub-frames of an atlas texture based on age. Niagara reference: SubImage Index module.

## 1. Flipbook Runtime

- [ ] 1.1 Verify `vfx_flipbook.hlsli` helper functions work correctly: ComputeFlipbookFrame(), ComputeFlipbookFrameRandom(), SampleFlipbookUV().
- [ ] 1.2 Verify `particle_billboard_ps.hlsl` flipbook path is active when flipbookRows > 1 || flipbookCols > 1.
- [ ] 1.3 Test all 3 blend modes: None (snap), Linear (lerp between frames), Random (per-particle static frame).

## 2. Sprite Sheet Asset Pipeline

- [x] 2.1 Create procedural test sprite sheets at runtime (4x4 fire + 4x4 smoke) — generated in OnInit() as GPU textures. Fire: yellow-white core evolving to dim red. Smoke: dense white puffs to diffuse transparent gray.
- [x] 2.2 Add sprite texture SRV selection via `GetSpriteTextureForPreset()` — routes fire presets to fire sprite, smoke to smoke sprite, others to procedural circle (UINT32_MAX).
- [ ] 2.3 Load sprite sheet PNGs from `runtime/content/vfx/` directory if available.

## 3. Preset Updates

- [x] 3.1 Fire presets (Simple Burst 24 FPS, Fireball 30 FPS, Ring of Fire 20 FPS, Explosion Debris 24 FPS): 4x4 flipbook with fire frames, linear blend.
- [x] 3.2 Smoke Plume: 4x4 smoke puffs, linear blend, 12 FPS.
- [ ] 3.3 Magic Orb: 2x2 glow variations, random blend (per-particle static frame). Already configured (2x2, blendMode=2).
- [ ] 3.4 Sparks: no flipbook (single bright point). Already configured (1x1).

## 4. Procedural Sprite Generation

- [x] 4.1 Generate a 4x4 "fire" sprite sheet procedurally: each frame is a soft circle with varying radius, color shift from yellow-white to orange to dark red across frames, pseudo-noise edge turbulence for organic fire shape. 64x64 cells, 256x256 total RGBA8.
- [x] 4.2 Generate a 4x4 "smoke" sprite sheet: soft circles with increasing size (30%-100%) and decreasing opacity (0.9 to 0.1) per frame, white-to-gray brightness transition. 64x64 cells, 256x256 total RGBA8.
- [x] 4.3 Upload as RGBA8 textures via UploadContext staging buffer, store SRV indices (m_fireSpriteSrvIdx, m_smokeSpriteSrvIdx). Pass to DrawBillboards in RenderVFXPass.

## 5. Visual Verification

- [ ] 5.1 Screenshot with fire flipbook — particles should show animated flame frames.
- [ ] 5.2 Screenshot with smoke flipbook — puffs should evolve over lifetime.
- [ ] 5.3 Verify linear blend mode produces smooth frame transitions.

## 6. Documentation

- [ ] 6.1 Document flipbook format (rows, cols, totalFrames, FPS, blendMode).
- [ ] 6.2 Update VFX guide with sprite sheet authoring guidelines.
