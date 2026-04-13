## Phase 3b-3.80.0 — VFX Particle Material Rendering Pipeline (Niagara Parity)

Complete rewrite of how particles render with sprite sheet materials. Based on
exhaustive analysis of UE5 Niagara's NiagaraSpriteVertexFactory, ParticleSubUV,
and material blend pipeline. See docs/analysis/vfx/niagara-sprite-rendering-pipeline.md.

### Root Cause of Current Failure
1. Sampler s0 = Anisotropic WRAP causes frame bleeding between atlas cells
2. PNG without alpha channel + AlphaBlend = invisible (alpha=1 makes everything opaque/black)
3. For RGB-only textures, Niagara uses Unlit Additive where RGB IS the output (no alpha needed)
4. SubUV coords should be computed in VS and passed to PS (not computed per-pixel in PS)

## 1. Add Static Sampler s4 — Bilinear CLAMP for Sprites

- [x] 1.1 In `runtime/engine/rhi/dx12/dx12_pso_creation.cpp`: add a 5th static sampler at register s4:
      Filter=MIN_MAG_MIP_LINEAR, Address=CLAMP, no comparison. Array size 4->5, NumStaticSamplers 4->5.
- [x] 1.2 In `particle_billboard_ps.hlsl`: declare `SamplerState spriteSampler : register(s4)`.
      s4 for sprite sampling, s2 (depthSampler) for depth. Removed s0 usage from PS.
- [x] 1.3 Rebuild runtime (`cd runtime && zig build`) to update root signature.
- [ ] 1.4 Verify: sprite samples no longer bleed across atlas cell boundaries.

## 2. Move SubUV Computation to Vertex Shader (Niagara Pattern)

- [x] 2.1 In `particle_billboard_vs.hlsl`: compute SubUV coords in VS. Added flipbook frame
      computation after billboard expansion, reads flipbook CB params, outputs atlas UVs.
- [x] 2.2 Add new VS outputs: `float2 subUV0 : TEXCOORD6`, `float2 subUV1 : TEXCOORD7`,
      `float subUVLerp : TEXCOORD8`. Dead particles and non-flipbook path set defaults.
- [x] 2.3 In PS: removed ALL flipbook computation and vfx_flipbook.hlsli include. PS now just
      samples at input.subUV0/subUV1 and lerps by input.subUVLerp.

## 3. Fix Blend Mode Output (Niagara Exact Match)

- [x] 3.1 **Additive** PSO: SrcColor=One, DstColor=One, SrcAlpha=Zero, DstAlpha=InvSrcAlpha.
- [x] 3.2 **Translucent** PSO: SrcColor=SrcAlpha, DstColor=InvSrcAlpha, SrcAlpha=Zero, DstAlpha=InvSrcAlpha.
- [x] 3.3 **AlphaComposite** PSO: SrcColor=One, DstColor=InvSrcAlpha, SrcAlpha=Zero, DstAlpha=InvSrcAlpha.
- [x] 3.4 Updated all 3 PSO blend states in `vfx_viewport_vfx.cpp` Init() — alpha channel now Zero/InvSrcAlpha on all modes (Niagara match).

## 4. Fix PS Material Output (Niagara Unlit Pattern)

- [x] 4.1 PS output formula rewritten: emissive = sprite.rgb * color * colorLife, opacity = sprite.a * color.a * colorLife.a, emissive *= (1+emissiveIntensity). Blend-mode-aware output (premul for mode 2, straight for 0/1).
- [x] 4.2 ApplyChannelMask applied to raw lerped sample before material computation.
- [x] 4.3 RGB-only textures with Additive blend work correctly — black pixels add zero light.
- [x] 4.4 Soft particle fade skipped for Additive mode (blendModeHint != 1 guard).

## 5. Fix Texture Loading

- [x] 5.1 Verified: `VFXMaterial::LoadTexture()` already defaults `isSRGB=true` in its signature (vfx_material.h:107),
      which overrides `TextureLoader::Load`'s default of `false`. Color textures load as sRGB correctly.
- [x] 5.2 Verified: `TextureLoader::Load` calls `stbi_load(..., 4)` which forces 4-channel RGBA output.
      stb_image fills alpha=255 for RGB-only sources. 3-channel PNGs produce RGBA8 with alpha=1. No gap.
- [x] 5.3 Added `originalChannels` field to `TextureLoadResult` (texture_loader.h). Propagated in
      `TextureLoader::Load` and `LoadFromMemory`. VFXMaterial log now prints width, height, hasAlpha, sRGB.

## 6. Preset Tuning for Fire Sprite Sheet

- [x] 6.1 Simple Burst with fire sprite: Additive blend (mode 1), channelMask=0 (RGBA),
      emissive=1.0, size=1.0, spread=PI, lifetime=1.5-3.0, gravity=(-3 Z), 300 particles.
      Color over life: warm white→orange→transparent.
      Updated spread=3.14159, lifetime=1.5-3.0, colorLifeStart=(1,0.95,0.8,1) warm white,
      colorLifeEnd=(1,0.4,0.05,0) orange->transparent. Both .cpp and .json presets updated.
- [x] 6.2 With RGB-only fire PNG: Additive blend makes black pixels invisible (add 0 light).
      Bright pixels add fire color. No alpha needed — this is how Niagara fire works.
      Added comment in Simple Burst preset explaining RGB-only + Additive behavior.
- [x] 6.3 With Green-channel mode (mask=2): green channel drives BOTH luminance and opacity.
      Bright green = bright opaque fire, dark green = dim transparent.
      Added comment in Simple Burst preset explaining green-channel mode behavior.

## 7. Demo Controls

- [ ] 7.1 Shift+M cycles channel mask (RGBA/R/G/B/A) — already implemented.
- [ ] 7.2 M cycles material presets — already implemented.
- [x] 7.3 Add I key: toggle sRGB on/off for sprite texture (J was taken by lit-particles toggle).
      Added m_spriteSRGB member + Key::I handler in vfx_viewport_input.cpp.
- [x] 7.4 Add U key: toggle soft fade on/off for quick comparison (K was taken by kill-volume toggle).
      Added m_softFadeEnabled member + Key::U handler; sets softFadeDistance to 0/0.5 on all emitters.

## 8. Visual Verification

- [ ] 8.1 Screenshot: fire sprite with Additive blend — flames should GLOW on dark background.
      Use `--bg-preset 2` (black background) if available, or dark time-of-day.
- [ ] 8.2 Screenshot: fire sprite with Additive blend on bright sky — flames barely visible
      (expected with additive on bright). Switch to Translucent for bright backgrounds.
- [ ] 8.3 Screenshot comparison: procedural circle vs fire sprite side by side.
- [ ] 8.4 Screenshot: Green channel mode vs RGBA mode — both should show flame shapes.

## 9. Documentation

- [x] 9.1 Document the particle material rendering pipeline: VS SubUV → PS material → blend.
      Created docs/guides/vfx-particle-material-pipeline.md with full pipeline overview.
- [ ] 9.2 Document sprite sheet requirements: format, channels, sRGB, sizes.
- [x] 9.3 Document blend mode selection guide: when to use Additive vs Translucent vs AlphaComposite.
      Included in vfx-particle-material-pipeline.md "Choosing a Blend Mode" section.
- [ ] 9.4 Update Niagara parity analysis with verified implementation notes.

## 10. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 10.1 Update or create documentation covering the implementation
- [ ] 10.2 Write tests covering the new behavior
- [ ] 10.3 Run tests and confirm they pass
