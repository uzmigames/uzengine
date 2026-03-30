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

- [ ] 1.1 In `runtime/engine/rhi/dx12/dx12_pso_creation.cpp`: add a 5th static sampler at register s4:
      Filter=MIN_MAG_MIP_LINEAR, Address=CLAMP, no comparison. This is dedicated for sprite sheets.
- [ ] 1.2 In `particle_billboard_ps.hlsl`: declare `SamplerState spriteSampler : register(s4)`.
      Use s4 for ALL sprite texture sampling (flipbook + single-frame). Keep s0 for depth.
- [ ] 1.3 Rebuild runtime (`cd runtime && zig build`) to update root signature.
- [ ] 1.4 Verify: sprite samples no longer bleed across atlas cell boundaries.

## 2. Move SubUV Computation to Vertex Shader (Niagara Pattern)

- [ ] 2.1 In `particle_billboard_vs.hlsl`: compute SubUV coords in VS, not PS.
      Read `flipbookRows`, `flipbookCols`, `flipbookFPS`, `flipbookTotalFrames` from VFXBillboardCB.
      Compute:
      ```
      float frameFloat = max(age, 0) * flipbookFPS;
      float subImageLerp = frac(frameFloat);
      uint frame0 = (uint)frameFloat % flipbookTotalFrames;
      uint frame1 = (frame0 + 1) % flipbookTotalFrames;
      float2 invSize = float2(1.0/flipbookCols, 1.0/flipbookRows);
      float2 uv0 = float2(frame0 % flipbookCols, frame0 / flipbookCols) * invSize + texcoord * invSize;
      float2 uv1 = float2(frame1 % flipbookCols, frame1 / flipbookCols) * invSize + texcoord * invSize;
      ```
      Pass uv0, uv1, subImageLerp to PS via interpolants.
- [ ] 2.2 Add new VS outputs: `float2 subUV0 : TEXCOORD6`, `float2 subUV1 : TEXCOORD7`,
      `float subUVLerp : TEXCOORD8`. When flipbook is inactive (rows*cols <= 1), set
      subUV0 = texcoord, subUV1 = texcoord, subUVLerp = 0.
- [ ] 2.3 In PS: remove ALL flipbook computation. Just sample at input.subUV0 and input.subUV1,
      lerp by input.subUVLerp. Much simpler PS code.

## 3. Fix Blend Mode Output (Niagara Exact Match)

- [ ] 3.1 **Additive** PSO: SrcColor=One, DstColor=One, SrcAlpha=Zero, DstAlpha=InvSrcAlpha.
      PS output: `float4(emissiveRGB, opacity)`. The RGB adds directly. Alpha preserves background.
- [ ] 3.2 **Translucent** PSO: SrcColor=SrcAlpha, DstColor=InvSrcAlpha, SrcAlpha=Zero, DstAlpha=InvSrcAlpha.
      PS output: `float4(emissiveRGB, opacity)`. Standard alpha blend.
- [ ] 3.3 **AlphaComposite** PSO: SrcColor=One, DstColor=InvSrcAlpha, SrcAlpha=Zero, DstAlpha=InvSrcAlpha.
      PS output: `float4(emissiveRGB * opacity, opacity)`. Premultiplied alpha.
- [ ] 3.4 Update all 3 PSO blend state configs in `vfx_viewport_vfx.cpp` Init() to match above.

## 4. Fix PS Material Output (Niagara Unlit Pattern)

- [ ] 4.1 For Unlit particles (default), the PS output formula is:
      ```
      float4 texSample = lerp(tex.Sample(s4, subUV0), tex.Sample(s4, subUV1), subUVLerp);
      float3 emissive = texSample.rgb * particleColor.rgb * colorLifeScale.rgb * emissiveIntensity;
      float opacity = texSample.a * particleColor.a * colorLifeScale.a;
      ```
      When channel mask > 0: extract single channel as both emissive intensity AND opacity.
- [ ] 4.2 Apply `spriteChannelMask` BEFORE the material computation:
      ```
      if (channelMask == 0) { emissive = texSample.rgb; opacity = texSample.a; }
      else { float ch = texSample[channelMask-1]; emissive = float3(ch,ch,ch); opacity = ch; }
      ```
      Then multiply by particle color: `emissive *= particleColor.rgb; opacity *= particleColor.a;`
- [ ] 4.3 For RGB-only textures (alpha=1 everywhere): with Additive blend, RGB is the light
      contribution and alpha=1 means full contribution. The black (0,0,0) pixels contribute nothing
      because One*0 = 0. This is why Niagara fire works with RGB-only textures + Additive.
- [ ] 4.4 Remove soft particle fade for Additive mode — soft fade makes no visual sense for
      additive particles (they add light, not block it).

## 5. Fix Texture Loading

- [ ] 5.1 Load sprite textures as **sRGB** (true) for color textures — `TextureLoader::Load(device, cmd, path, true)`.
      Currently loaded as linear which makes colors washed out.
- [ ] 5.2 When PNG has no alpha channel, the RHI should create RGBA8 with alpha=1.
      Verify this is the default behavior of TextureLoader for 3-channel PNGs.
- [ ] 5.3 Log texture format/channels on load: width, height, channels, hasAlpha, sRGB.

## 6. Preset Tuning for Fire Sprite Sheet

- [ ] 6.1 Simple Burst with fire sprite: Additive blend (mode 1), channelMask=0 (RGBA),
      emissive=1.0, size=1.0, spread=PI, lifetime=1.5-3.0, gravity=(-3 Z), 300 particles.
      Color over life: warm white→orange→transparent.
- [ ] 6.2 With RGB-only fire PNG: Additive blend makes black pixels invisible (add 0 light).
      Bright pixels add fire color. No alpha needed — this is how Niagara fire works.
- [ ] 6.3 With Green-channel mode (mask=2): green channel drives BOTH luminance and opacity.
      Bright green = bright opaque fire, dark green = dim transparent.

## 7. Demo Controls

- [ ] 7.1 Shift+M cycles channel mask (RGBA/R/G/B/A) — already implemented.
- [ ] 7.2 M cycles material presets — already implemented.
- [ ] 7.3 Add J key: cycle sRGB on/off for sprite texture (reloads texture).
- [ ] 7.4 Add K key: toggle soft fade on/off for quick comparison.

## 8. Visual Verification

- [ ] 8.1 Screenshot: fire sprite with Additive blend — flames should GLOW on dark background.
      Use `--bg-preset 2` (black background) if available, or dark time-of-day.
- [ ] 8.2 Screenshot: fire sprite with Additive blend on bright sky — flames barely visible
      (expected with additive on bright). Switch to Translucent for bright backgrounds.
- [ ] 8.3 Screenshot comparison: procedural circle vs fire sprite side by side.
- [ ] 8.4 Screenshot: Green channel mode vs RGBA mode — both should show flame shapes.

## 9. Documentation

- [ ] 9.1 Document the particle material rendering pipeline: VS SubUV → PS material → blend.
- [ ] 9.2 Document sprite sheet requirements: format, channels, sRGB, sizes.
- [ ] 9.3 Document blend mode selection guide: when to use Additive vs Translucent vs AlphaComposite.
- [ ] 9.4 Update Niagara parity analysis with verified implementation notes.
