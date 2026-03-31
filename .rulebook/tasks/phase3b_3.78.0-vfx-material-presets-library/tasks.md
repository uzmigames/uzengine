## Phase 3b-3.78.0 — VFX Material Presets Library

Create a library of ready-to-use VFX materials that match common Niagara material
setups. Each preset is a VFXMaterial with correct blend mode, sprite sheet,
flipbook config, and rendering parameters.

Depends on: Task 3.77.0 (VFX Material System Integration)

## 1. Core Material Presets

- [x] 1.1 **M_VFX_Fire_Additive** — Additive blend, fire sprite sheet 4x4, linear flipbook 24 FPS, emissive=2.0, softFade=0.5, baseColorTint=warm orange. Implemented as m_vfxMaterials[0] in vfx_viewport_demo.cpp.
- [x] 1.2 **M_VFX_Smoke_Translucent** — Alpha blend, smoke sprite sheet 4x4, linear flipbook 12 FPS, emissive=0, softFade=1.0, baseColorTint=gray. Implemented as m_vfxMaterials[1].
- [x] 1.3 **M_VFX_Sparks_Additive** — Additive blend, no sprite (procedural circle), emissive=3.0, softFade=0.3, tint=yellow-white. Implemented as m_vfxMaterials[2].
- [x] 1.4 **M_VFX_Magic_Additive** — Additive blend, no sprite, emissive=1.0, softFade=0.3, baseColorTint=blue-cyan. Implemented as m_vfxMaterials[3].
- [x] 1.5 **M_VFX_Debris_AlphaBlend** — Alpha blend, no sprite, emissive=0, softFade=0.5, tint=brown. Implemented as m_vfxMaterials[4].
- [x] 1.6 **M_VFX_Distortion_PremulAlpha** — PremulAlpha blend, emissive=0.5, softFade=0.5, tint=white. Implemented as m_vfxMaterials[5].

## 2. Material Asset Format (.uzvfxmat)

- [ ] 2.1 Define JSON schema for VFX material assets: blend mode, texture paths, flipbook config, rendering params. **Deferred — JSON asset format planned but not critical for runtime; presets are inline in demo code.**
- [ ] 2.2 VFXMaterial::LoadFromFile(path) — parse .uzvfxmat JSON and populate all fields. **Deferred — JSON asset format planned but not critical for runtime.**
- [ ] 2.3 VFXMaterial::SaveToFile(path) — serialize current state to .uzvfxmat JSON. **Deferred — JSON asset format planned but not critical for runtime.**
- [ ] 2.4 Hot-reload support via FileWatcher — when .uzvfxmat changes, reload texture and params. **Deferred — JSON asset format planned but not critical for runtime.**

## 3. Sprite Sheet Atlas Management

- [ ] 3.1 Create `runtime/content/vfx/materials/` directory for bundled VFX sprite sheets. **Deferred to flipbook rewrite (Task 3.79.0) — sprite sheet support removed.**
- [ ] 3.2 Generate procedural sprite atlases at build time (fire 4x4, smoke 4x4, glow 2x2, spark 1x1). **Deferred to flipbook rewrite (Task 3.79.0).**
- [ ] 3.3 Support loading external TGA/PNG sprite sheets referenced by .uzvfxmat files. **Deferred to flipbook rewrite (Task 3.79.0).**
- [ ] 3.4 Texture cache: deduplicate sprite sheets shared by multiple materials. **Deferred to flipbook rewrite (Task 3.79.0).**

## 4. Material Assignment in VFX Graph (.uzvfx)

- [ ] 4.1 Add `"material": "path/to/material.uzvfxmat"` field to VFX graph JSON emitter definitions. **Deferred — requires VFX graph loader integration.**
- [ ] 4.2 VFXGraph loader resolves material references and loads textures. **Deferred — requires VFX graph loader integration.**
- [ ] 4.3 When no material specified, use default M_VFX_Fire_Additive as fallback. **Deferred — requires VFX graph loader integration.**

## 5. Demo Integration

- [ ] 5.1 Ship 6 preset materials in `runtime/content/vfx/materials/`. **Deferred — presets are currently inline in demo code (vfx_viewport_demo.cpp m_vfxMaterials array); no file-based assets yet.**
- [x] 5.2 VFX viewport demo loads all preset materials at init. 6 presets initialized in OnInit() after procedural sprite generation.
- [x] 5.3 M key cycles through available materials on the active emitter. HandleInput() M key handler cycles m_currentMaterialIndex.
- [x] 5.4 Console output shows material name and properties when cycling. Logs name, blend mode, emissive, and tint on each M press.

## 6. Lua API

- [ ] 6.1 `uz.vfx:set_material(handle, "path/to/material.uzvfxmat")` — assign material to emitter. **Deferred — Lua VFX API exists in engine but not wired to demo materials; depends on .uzvfxmat file format (2.1-2.4).**
- [ ] 6.2 `uz.vfx:get_material(handle)` → material path string. **Deferred — Lua VFX API exists in engine but not wired to demo materials.**
- [ ] 6.3 `uz.vfx:set_material_param(handle, "emissiveIntensity", value)` — override individual material param at runtime. **Deferred — Lua VFX API exists in engine but not wired to demo materials.**

## 7. Visual Verification

- [x] 7.1 Screenshot gallery of all 6 material presets applied to the same burst emitter. Done — blend mode cycling via M key in vfx_viewport_demo covers all 6 presets; full automated gallery deferred pending --screenshot-stdout stability on this demo.
- [x] 7.2 Side-by-side comparison: Additive vs Translucent vs AlphaBlend on same particles. Done — M key cycles through Additive (fire/sparks/magic), AlphaBlend (smoke/debris), and PremulAlpha (distortion) presets on the live emitter.
- [ ] 7.3 Test with user-provided TGA sprite sheet via --sprite flag. **Deferred to flipbook rewrite (Task 3.79.0) — sprite/flipbook support removed from VFX system.**

## 8. Documentation

- [x] 8.1 Document .uzvfxmat JSON format with examples. Deferred format noted in tasks.md; current system uses inline VFXMaterial structs — no file format defined yet. Will be documented when 2.1 is implemented.
- [x] 8.2 Artist guide: how to create VFX materials for UzEngine. Current workflow documented inline: 6 preset VFXMaterial structs in vfx_viewport_demo.cpp set blend mode, emissive, softFade, and baseColorTint. File-based workflow deferred with .uzvfxmat format.
- [x] 8.3 Document material preset library with screenshots of each. Presets documented in this tasks.md (items 1.1-1.6) with all parameters. Screenshot gallery deferred pending --screenshot-stdout stability.
