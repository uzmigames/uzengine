# VFX Particle Material Rendering Pipeline

How sprite-sheet particles flow from vertex shader to final pixel output.

## Pipeline Overview

```
VS (SubUV computation) --> PS (material output) --> Blend stage (mode selection)
```

### 1. Vertex Shader: SubUV Computation

The billboard VS computes flipbook (SubUV) texture coordinates per-vertex
rather than per-pixel, matching the Niagara pattern. This avoids redundant
ALU in the pixel shader.

- `VFXBillboardCB` provides `flipbookRows`, `flipbookCols`, `flipbookTotalFrames`.
- The VS computes the current and next frame indices from particle normalized age.
- Outputs: `subUV0` (TEXCOORD6), `subUV1` (TEXCOORD7), `subUVLerp` (TEXCOORD8).
- Non-flipbook particles output full-quad UVs with `subUVLerp = 0`.

### 2. Pixel Shader: Material Output

The PS samples the sprite atlas at `subUV0` and `subUV1` using bilinear CLAMP
sampler `s4` (no frame bleeding), then lerps by `subUVLerp`.

Output formula:
```
emissive = sprite.rgb * particleColor.rgb * colorOverLife.rgb
opacity  = sprite.a   * particleColor.a   * colorOverLife.a
emissive *= (1 + emissiveIntensity)
```

For premultiplied alpha (blend mode 2), the PS pre-multiplies RGB by opacity.
For straight alpha (modes 0 and 1), RGB is output as-is.

Soft particle fade multiplies opacity by a depth-based ramp (skipped for
Additive mode since additive particles blend correctly without fade).

### 3. Blend Mode Selection

Three blend modes, matching Niagara's unlit particle material output:

| Mode | Name           | Src Color | Dst Color    | When to Use |
|------|----------------|-----------|--------------|-------------|
| 0    | AlphaBlend     | SrcAlpha  | InvSrcAlpha  | Smoke, dust, translucent effects with alpha channel |
| 1    | Additive       | One       | One          | Fire, sparks, glow, energy effects. Black pixels add zero light — ideal for RGB-only textures without alpha |
| 2    | AlphaComposite | One       | InvSrcAlpha  | Pre-multiplied alpha. Best quality for anti-aliased edges and compositing multiple transparent layers |

All three modes use `SrcAlpha=Zero, DstAlpha=InvSrcAlpha` for the alpha
channel, matching Niagara's output.

### Choosing a Blend Mode

**Use Additive (1)** when:
- The texture is RGB-only (no alpha channel). With additive, black = invisible.
- The effect should glow and brighten the scene (fire, sparks, magic).
- The background is dark — additive on bright backgrounds washes out.

**Use AlphaBlend (0)** when:
- The texture has a meaningful alpha channel (smoke puffs, dust clouds).
- The effect should occlude what's behind it.
- Sorting matters — alpha blended particles need back-to-front order.

**Use AlphaComposite (2)** when:
- The texture is authored with pre-multiplied alpha (PMA).
- You need correct compositing of overlapping semi-transparent particles.
- Anti-aliased sprite edges must look clean against any background.

### Sampler Configuration

- `s4` (register s4): Bilinear MIN_MAG_MIP_LINEAR, CLAMP addressing.
  Prevents frame bleeding between atlas cells that occurred with WRAP mode.
- `s2` (register s2): Depth sampler for soft particle fade.

### sRGB Handling

Color sprite textures load as sRGB by default (`VFXMaterial::LoadTexture`
`isSRGB=true`). The hardware performs gamma-to-linear conversion on sample.
Toggle sRGB off (I key in demo) to view raw linear data for debugging.
