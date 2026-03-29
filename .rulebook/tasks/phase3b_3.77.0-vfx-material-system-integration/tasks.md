## Phase 3b-3.77.0 — VFX Material System Integration

Connect UzEngine's existing material system to the VFX particle pipeline.
Each emitter gets a material slot that controls texture, blend mode, emissive,
and rendering properties — identical to how Niagara assigns Materials to emitters.

Niagara reference: Emitter → Render → Material slot → Unreal Material with
Particle Color, SubUV, Dynamic Parameter nodes.

## 1. VFXMaterial Struct (CPU-side)

- [ ] 1.1 Create `runtime/engine/vfx/material/vfx_material.h` — VFXMaterial struct:
  - `std::string spriteTexturePath` — path to sprite sheet (TGA/PNG)
  - `uint32_t spriteTextureSrvIndex = UINT32_MAX` — loaded SRV index
  - `EBlendMode blendMode = EBlendMode::Additive` — from material_types.h
  - `float emissiveIntensity = 1.0f`
  - `float softFadeDistance = 0.5f`
  - `float alphaErosion = 0.0f`
  - `float alphaClipThreshold = 0.0f` — for masked mode
  - `uint32_t flipbookRows = 1, flipbookCols = 1, flipbookTotalFrames = 1`
  - `float flipbookFPS = 0.0f`
  - `uint32_t flipbookBlendMode = 0` — 0=None, 1=Linear, 2=Random
  - `float baseColorTintR/G/B = 1.0f` — material-level color tint
  - `bool isUnlit = true` — VFX particles default unlit
  - `bool isTwoSided = true` — billboards always double-sided
- [ ] 1.2 Create `runtime/engine/vfx/material/vfx_material.cpp` — VFXMaterial::LoadTexture(device, cmd) loads sprite from path, stores SRV index. Auto-detects flipbook grid from filename pattern (e.g. `_6x6_` → rows=6, cols=6).

## 2. Multiple Blend Mode PSOs

- [x] 2.1 In VFXParticleSystem, create 3 billboard PSOs during Init(). m_billboardPSO (PremulAlpha One/InvSrcAlpha), m_billboardPSO_Additive (One/One), m_billboardPSO_AlphaBlend (SrcAlpha/InvSrcAlpha). Added GetBillboardPSOForBlendMode() selector.
- [x] 2.2 Added `uint32_t blendModeHint` to VFXBillboardCB (replaced `_solPad0` padding — CB stays 272 bytes). Tells PS which alpha output mode to use.
- [x] 2.3 DrawBillboards() now takes `uint32_t blendMode` parameter, selects PSO via GetBillboardPSOForBlendMode(), and sets blendModeHint in CB.

## 3. Material Slot on Emitter Config

- [ ] 3.1 Add `VFXMaterial* material = nullptr` to VFXEmitterConfig (optional — null = use inline params like today).
- [ ] 3.2 When material is set, DrawBillboards() reads emissive/softFade/flipbook/sprite from the material instead of VFXEmitterConfig fields.
- [ ] 3.3 When material is null, behavior is identical to current (backward compatible).

## 4. Billboard PS Material Awareness

- [x] 4.1 Added `uint blendModeHint` to VFXBillboardCB in both VS and PS HLSL (replaced `_solPad0`). Values: 0=AlphaBlend, 1=Additive, 2=PremulAlpha.
- [x] 4.2 In particle_billboard_ps.hlsl: when blendModeHint==1 (Additive), skip premultiply — RGB adds directly via One/One blend.
- [x] 4.3 When blendModeHint==0 (AlphaBlend), output straight alpha — blend state does SrcAlpha/InvSrcAlpha. PremulAlpha (mode 2) retains existing RGB *= alpha behavior.
- [ ] 4.4 Verify all 3 modes produce correct visual results with both procedural and sprite textures.

## 5. Texture Loading Pipeline

- [ ] 5.1 VFXMaterial::LoadTexture() uses existing TextureLoader::Load() for TGA/PNG sprite sheets.
- [ ] 5.2 Support sRGB (color textures) and linear (data textures like normal maps) loading.
- [ ] 5.3 Cache loaded textures by path to avoid duplicate loads when multiple emitters share a sprite.

## 6. Demo Integration

- [ ] 6.1 In VFX viewport demo OnInit(): create VFXMaterial instances for fire/smoke/sparks.
- [ ] 6.2 Load the custom TGA sprite (`--sprite` CLI) into a VFXMaterial.
- [ ] 6.3 Assign materials to presets: fire presets → fire material (additive), smoke → smoke material (alpha blend), sparks → spark material (additive, no sprite).
- [ ] 6.4 Add material cycling key (M key: cycle through available materials on current emitter).

## 7. Visual Verification

- [ ] 7.1 Screenshot with fire material (additive + sprite) — bright glowing flames.
- [ ] 7.2 Screenshot with smoke material (alpha blend + sprite) — soft translucent puffs.
- [ ] 7.3 Screenshot with spark material (additive + no sprite) — bright streaks.
- [ ] 7.4 Compare all 3 blend modes side by side.

## 8. Documentation

- [ ] 8.1 Document VFXMaterial struct and how to assign to emitters.
- [ ] 8.2 Document blend mode differences and when to use each.
- [ ] 8.3 Update VFX viewport help overlay with material controls.
