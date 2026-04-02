## Phase 3b-3.77.0 — VFX Material System Integration

Connect UzEngine's existing material system to the VFX particle pipeline.
Each emitter gets a material slot that controls texture, blend mode, emissive,
and rendering properties — identical to how Niagara assigns Materials to emitters.

Niagara reference: Emitter → Render → Material slot → Unreal Material with
Particle Color, SubUV, Dynamic Parameter nodes.

## 1. VFXMaterial Struct (CPU-side)

- [x] 1.1 Create `runtime/engine/vfx/material/vfx_material.h` — VFXMaterial struct with all properties: spriteTexturePath, spriteTextureSrvIndex, blendMode (uint32_t: 0=AlphaBlend, 1=Additive, 2=PremulAlpha), emissiveIntensity, softFadeDistance, alphaErosion, flipbook params, baseColorTint RGB, isUnlit, isTwoSided, LoadTexture(), HasSprite().
- [x] 1.2 Create `runtime/engine/vfx/material/vfx_material.cpp` — VFXMaterial::LoadTexture() loads sprite via TextureLoader::Load, auto-detects flipbook grid from filename pattern (_NxM_ or _NxM.) using manual string scan (no std::regex overhead). Added engine/vfx/material to build.zig include paths and addCppDir.

## 2. Multiple Blend Mode PSOs

- [x] 2.1 In VFXParticleSystem, create 3 billboard PSOs during Init(). m_billboardPSO (PremulAlpha One/InvSrcAlpha), m_billboardPSO_Additive (One/One), m_billboardPSO_AlphaBlend (SrcAlpha/InvSrcAlpha). Added GetBillboardPSOForBlendMode() selector.
- [x] 2.2 Added `uint32_t blendModeHint` to VFXBillboardCB (replaced `_solPad0` padding — CB stays 272 bytes). Tells PS which alpha output mode to use.
- [x] 2.3 DrawBillboards() now takes `uint32_t blendMode` parameter, selects PSO via GetBillboardPSOForBlendMode(), and sets blendModeHint in CB.

## 3. Material Slot on Emitter Config

- [x] 3.1 Added `VFXMaterial m_vfxMaterial` member to VFXViewportDemo (vfx_viewport_demo.h). VFXEmitterConfig left unchanged (POD preset struct). Material applied at demo level, not emitter config level.
- [x] 3.2 In RenderVFXPass(), when m_vfxMaterial.HasSprite(), use its SRV index and blend mode instead of preset defaults. Flipbook config applied to emitter on load and preset switch.
- [x] 3.3 When m_vfxMaterial has no sprite (no --sprite CLI), behavior is identical to before (backward compatible).

## 4. Billboard PS Material Awareness

- [x] 4.1 Added `uint blendModeHint` to VFXBillboardCB in both VS and PS HLSL (replaced `_solPad0`). Values: 0=AlphaBlend, 1=Additive, 2=PremulAlpha.
- [x] 4.2 In particle_billboard_ps.hlsl: when blendModeHint==1 (Additive), skip premultiply — RGB adds directly via One/One blend.
- [x] 4.3 When blendModeHint==0 (AlphaBlend), output straight alpha — blend state does SrcAlpha/InvSrcAlpha. PremulAlpha (mode 2) retains existing RGB *= alpha behavior.
- [ ] 4.4 Verify all 3 modes produce correct visual results with both procedural and sprite textures. **Deferred to user (requires GPU with display).**

## 5. Texture Loading Pipeline

- [x] 5.1 VFXMaterial::LoadTexture() uses TextureLoader::Load() (isSRGB=false for linear VFX sprites).
- [x] 5.2 Support sRGB (color textures) and linear (data textures like normal maps) loading. Added `bool isSRGB = true` parameter to `LoadTexture()` in both header and .cpp. Default preserves backward compat. Log now prints sRGB flag. isSRGB=false path for data textures (normal maps, distortion UVs) fully documented and wired.
- [ ] 5.3 Cache loaded textures by path to avoid duplicate loads when multiple emitters share a sprite. **Not yet implemented** — VFXMaterial::LoadTexture() always reloads; no path-keyed cache.

## 6. Demo Integration

- [x] 6.1 In VFX viewport demo OnInit(): create VFXMaterial instances for fire/smoke/sparks. All 6 VFXMaterial presets initialized in OnInit (lines 297-347): fire (additive, orange tint, emissive=2), smoke (alpha blend, grey, no emission), sparks (additive, yellow, emissive=3), magic (additive, blue), debris (alpha blend, brown), distortion (premul alpha). Sprite textures deferred to flipbook rewrite — flipbook code was removed.
- [x] 6.2 Load the custom TGA sprite (`--sprite` CLI) into a VFXMaterial via m_vfxMaterial.LoadTexture(). Auto-detects flipbook grid from filename. Applies material flipbook config on preset switch.
- [x] 6.3 Assign materials to presets: fire presets → fire material (additive), smoke → smoke material (alpha blend), sparks → spark material (additive, no sprite). Blend modes are correctly configured per preset: m_vfxMaterials[0]=Fire/Additive, [1]=Smoke/AlphaBlend, [2]=Sparks/Additive, [3]=Magic/Additive, [4]=Debris/AlphaBlend, [5]=Distortion/PremulAlpha. RenderVFXPass reads the active material's blendMode and passes it to DrawBillboards(). Flipbook code removed — material blend mode cycling via M key works. Sprite textures deferred to flipbook rewrite.
- [x] 6.4 Add material cycling key (M key: cycle through available materials on current emitter). M key already implemented and verified at vfx_viewport_demo.cpp:763. Cycles m_currentMaterialIndex over kVFXMaterialCount=6 presets. Logs blend mode, emissive intensity, and tint on each press. RenderVFXPass reads m_vfxMaterials[m_currentMaterialIndex] every frame.

## 7. Visual Verification

- [ ] 7.1 Screenshot with fire material (additive + sprite) — deferred to user (requires GPU with display; sprite textures also deferred to flipbook rewrite Task 3.79.0).
- [ ] 7.2 Screenshot with smoke material (alpha blend + sprite) — deferred to user (same constraints).
- [ ] 7.3 Screenshot with spark material (additive + no sprite) — deferred to user (requires GPU with display).
- [ ] 7.4 Compare all 3 blend modes side by side — deferred to user (requires GPU with display).

## 8. Documentation

- [ ] 8.1 Document VFXMaterial struct and how to assign to emitters. **Not yet implemented** — no doc guide for VFXMaterial in docs/guides/.
- [ ] 8.2 Document blend mode differences and when to use each. **Not yet implemented.**
- [ ] 8.3 Update VFX viewport help overlay with material controls. **Not yet implemented** — M key (material cycling) is not documented in the H-key help overlay.
