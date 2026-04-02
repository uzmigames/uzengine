## Phase 3b-3.79.0 — VFX Flipbook Niagara Parity (Complete Rewrite)

Rewrite the flipbook/SubUV system from scratch following Niagara's architecture.
Current implementation has bugs (green channel hack, sampler issues, texture not
visible). This task replaces it with a correct, production-quality implementation.

Reference: UE5 Niagara SubUV system — SubImageIndex module, ParticleSubUV material
expression, NiagaraSpriteVertexFactory. See docs/analysis/particles/ for full analysis.

## 1. Fix Core UV Computation (CPU → GPU Data Flow)

- [ ] 1.1 SubUV index MUST be a float (not uint frame index). Compute on CPU each frame:
      `subImageIndex = normalizedAge * (totalFrames - 1)`. The integer part = current frame,
      fractional part = blend factor to next frame. Pass as float in the particle update CB.
- [ ] 1.2 Add `float subImageIndex` to VFXUpdateCB or compute in the update compute shader.
      In particle_update_cs.hlsl: write `subImageIndex = (age / lifespan) * (totalFrames - 1)`
      to a new particle attribute channel (float per particle).
- [ ] 1.3 Add a new SoA channel: `m_particleBuffers[7]` = subImageIndices (float, stride=4).
      Create in Init(), zero-init in UploadInitialFreeList(), pass SRV to billboard VS.

## 2. Fix Billboard VS — Read SubUV Index Per Particle

- [ ] 2.1 Add `uint subImageIndexBufferIdx` to VFXBillboardConstants (b0 root constants).
      The VS reads the per-particle subImageIndex float from this SRV buffer.
- [ ] 2.2 In particle_billboard_vs.hlsl: read `float subImageIndex = subImageIndices[particleID]`.
      Pass to PS as interpolant (add to VSOutput struct: `float subImageIndex : TEXCOORD6`).
- [ ] 2.3 Remove flipbook computation from PS entirely — the VS provides the index.

## 3. Fix Billboard PS — Correct Atlas UV Remapping

- [ ] 3.1 In particle_billboard_ps.hlsl: receive `subImageIndex` from VS interpolant.
      Compute atlas UV from subImageIndex + input.texcoord:
      ```
      uint frameIndex = (uint)subImageIndex;
      float blendFactor = frac(subImageIndex);
      uint col0 = frameIndex % flipbookCols;
      uint row0 = frameIndex / flipbookCols;
      float2 frameSize = 1.0 / float2(flipbookCols, flipbookRows);
      float2 uv0 = float2(col0, row0) * frameSize + input.texcoord * frameSize;
      ```
- [ ] 3.2 For linear blend: compute next frame UV the same way with `frameIndex + 1`.
      `spriteColor = lerp(sample(uv0), sample(uv1), blendFactor)`.
- [x] 3.3 Add `uint spriteChannelMask` to VFXBillboardCB: 0=RGBA (use full texture), 1=R as alpha,
      2=G as alpha, 3=B as alpha, 4=A as alpha. Default=0 (full RGBA).
      Implemented ApplyChannelMask() helper in PS, replaced _solPad1 with spriteChannelMask in
      VFXBillboardCB (C++ header, VS, PS). All 3 sampling paths now use ApplyChannelMask().
- [x] 3.4 Add `uint32_t spriteChannelMask = 0` to VFXEmitterConfig. Pass through CB.
      Added field to VFXEmitterConfig after blendMode. DrawBillboards() passes config.spriteChannelMask to billboardCB.
- [x] 3.5 Add channel mask cycling key in demo (Shift+M: cycle RGBA/R/G/B/A).
      Added Shift+M handler before M key handler. M key now only fires without shift. Help overlay updated.
- [x] 3.6 Remove the hardcoded green-channel-only hack from current PS code.
      Replaced all 3 `float4(1,1,1, rawSample.g)` hacks with `ApplyChannelMask(rawSample, spriteChannelMask)`.
      Fire presets default to spriteChannelMask=2 (Green), custom --sprite defaults to Green, user cycles with Shift+M.

## 4. Fix Texture Sampling

- [ ] 4.1 Use sampler s0 (anisotropic wrap) for sprite texture — this works and is what
      the mesh_ps uses. The WRAP addressing handles flipbook UV sub-rectangles correctly
      because the UVs are always in [0,1] after remapping to the sub-rectangle.
- [ ] 4.2 For depth buffer sampling in soft fade, also use s0 with SampleLevel(0).
- [ ] 4.3 Verify: when no sprite texture (UINT32_MAX), use the existing procedural circle.
      When sprite IS bound, sample it normally (full RGBA).

## 5. Fix Blend Modes (Niagara Reference)

- [ ] 5.1 **Additive** (fire, sparks): SrcBlend=SrcAlpha, DstBlend=One. NOT One/One.
      Niagara's additive uses SrcAlpha so the alpha channel controls how much light is added.
      Fix the Additive PSO blend state.
- [ ] 5.2 **Translucent** (smoke): SrcBlend=SrcAlpha, DstBlend=InvSrcAlpha. Standard alpha blend.
      The PS outputs straight (non-premultiplied) color.
- [ ] 5.3 **AlphaComposite** (default Niagara): SrcBlend=One, DstBlend=InvSrcAlpha.
      The PS premultiplies RGB by alpha before output. This is the most versatile mode.
- [ ] 5.4 Update the billboard PS blend mode branching:
      - Additive (mode 1): output `float4(rgb * alpha, alpha)` — SrcAlpha does the rest
      - Translucent (mode 0): output `float4(rgb, alpha)` — straight alpha
      - AlphaComposite (mode 2): output `float4(rgb * alpha, alpha)` — premultiplied

## 6. Fix SubUV in Spawn/Update Compute Shaders

- [ ] 6.1 In particle_spawn_cs.hlsl: initialize `subImageIndex = 0.0` for new particles.
- [ ] 6.2 In particle_update_cs.hlsl: compute `subImageIndex = (age / lifespan) * (totalFrames - 1)`.
      Write to the new channel. The totalFrames comes from a new CB field.
- [ ] 6.3 Add `uint flipbookTotalFrames` to VFXUpdateCB so the compute shader knows the frame count.

## 7. Test with Real Sprite Sheet

- [ ] 7.1 Load the PNG flipbook: `runtime/content/vfx/fireball_6x6.png` (6x6 grid).
      Auto-detect grid from filename via VFXMaterial.
- [ ] 7.2 Verify: particles show flame shapes from the texture, NOT circles.
- [ ] 7.3 Verify: flames animate over particle lifetime (frame 0 at birth → frame 35 at death).
- [ ] 7.4 Verify: linear blend produces smooth transitions between adjacent frames.
- [ ] 7.5 Test with additive blend — flames should glow, not darken the scene.

## 8. Visual Verification

- [ ] 8.1 Screenshot at frame 60 with fire preset + PNG sprite — visible flame shapes.
- [ ] 8.2 Screenshot comparison: procedural circles vs sprite sheet side by side.
- [ ] 8.3 Test all presets with M key material cycling — fire/smoke use sprites, sparks don't.

## 9. Documentation

- [ ] 9.1 Document the SubUV data flow: CPU subImageIndex → GPU channel → VS → PS → UV remap.
- [ ] 9.2 Document supported sprite sheet formats and naming conventions.
