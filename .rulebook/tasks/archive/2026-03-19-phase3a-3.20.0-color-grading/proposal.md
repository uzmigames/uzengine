# Color Grading

## Summary

Implement Color Grading matching UE5's color correction system. Supports 3D LUT (Look-Up Table) application, per-channel lift/gamma/gain (shadows/midtones/highlights), white balance (temperature/tint), saturation, contrast, and tone curve adjustments. This is the "Instagram filter" of game rendering — transforms technically correct images into artistically styled ones.

## Motivation

Color grading is what separates "technically rendered" from "visually distinctive." Every AAA game has unique color identity: Cyberpunk's cyan/magenta, The Last of Us's desaturated green, Elden Ring's warm gold. Without color grading, all PBR renderers look identical.

## UE5 Reference Implementation

### Algorithm (UE5 PostProcessCombinedLUT.usf)
- **3D LUT generation**: UE5 generates a 32×32×32 3D texture on GPU from all color grading parameters. This LUT is baked once per settings change, then sampled per-pixel.
- **LUT application**: In tonemap pass, after ACES: `gradedColor = Texture3DSample(lutTexture, color.rgb)`. Single texture fetch applies ALL grading.
- **Parameter stacking**: Temperature → Tint → Saturation → Contrast → Lift/Gamma/Gain → Channel Mixer → Tone Curve → LUT overlay.

### UE5 Parameters (FPostProcessSettings — Color Grading)
| Parameter | UE5 Default | Description |
|-----------|-------------|-------------|
| **White Balance** | | |
| `WhiteTemp` | 6500 | Color temperature in Kelvin (warm=low, cool=high) |
| `WhiteTint` | 0 | Green-magenta shift (-50 to +50) |
| **Global** | | |
| `ColorSaturation` | (1,1,1,1) | Per-channel saturation (RGBA, A=master) |
| `ColorContrast` | (1,1,1,1) | Per-channel contrast |
| `ColorGamma` | (1,1,1,1) | Per-channel gamma |
| `ColorGain` | (1,1,1,1) | Per-channel gain (multiply) |
| `ColorOffset` | (0,0,0,0) | Per-channel offset (add) |
| **Shadows** | | |
| `ColorSaturationShadows` | (1,1,1,1) | Shadow saturation |
| `ColorContrastShadows` | (1,1,1,1) | Shadow contrast |
| `ColorGammaShadows` | (1,1,1,1) | Shadow gamma |
| `ColorGainShadows` | (1,1,1,1) | Shadow gain |
| `ColorOffsetShadows` | (0,0,0,0) | Shadow offset |
| `ShadowsMax` | 0.09 | Luminance threshold for shadows |
| **Midtones** | | |
| `ColorSaturationMidtones` | (1,1,1,1) | Midtone saturation |
| `ColorContrastMidtones` | (1,1,1,1) | Midtone contrast |
| `ColorGammaMidtones` | (1,1,1,1) | Midtone gamma |
| `ColorGainMidtones` | (1,1,1,1) | Midtone gain |
| `ColorOffsetMidtones` | (0,0,0,0) | Midtone offset |
| **Highlights** | | |
| `ColorSaturationHighlights` | (1,1,1,1) | Highlight saturation |
| `ColorContrastHighlights` | (1,1,1,1) | Highlight contrast |
| `ColorGammaHighlights` | (1,1,1,1) | Highlight gamma |
| `ColorGainHighlights` | (1,1,1,1) | Highlight gain |
| `ColorOffsetHighlights` | (0,0,0,0) | Highlight offset |
| `HighlightsMin` | 0.5 | Luminance threshold for highlights |
| `HighlightsMax` | 1.0 | Highlight max luminance |
| **Misc** | | |
| `BlueCorrection` | 0.6 | Desaturate blues under low luminance |
| `ExpandGamut` | 1.0 | Expand BT.709 gamut toward BT.2020 |
| `SceneColorTint` | White | Scene-wide color multiply |
| External LUT | None | Optional external 3D LUT texture override |

### UE5 LUT Generation Pipeline
1. Start with neutral 32×32×32 identity LUT
2. Apply white balance (temperature → chromatic adaptation matrix)
3. Apply global saturation, contrast, gamma, gain, offset
4. Apply per-zone (shadows/midtones/highlights) adjustments weighted by luminance
5. Apply tone curve (ACES or custom)
6. Apply external LUT if present (artist baked in DaVinci/Photoshop)
7. Write final 3D texture — sampled per-pixel in tonemap pass

## UzEngine Implementation Plan

### Shader Files
- `lut_generate_cs.hlsl` — Compute: generate 32×32×32 3D LUT from all color grading parameters
- `color_grading.hlsli` — Include: white balance, lift/gamma/gain, channel math helpers

### Integration
- LUT is generated once when parameters change (not per-frame)
- Tonemap pass samples LUT: `color = lutTexture.SampleLevel(linearSampler, color.rgb * lutScale + lutOffset, 0)`
- LUT covers [0,1] range in SDR, or extended range for HDR

### Constant Buffer (for LUT generation)
```hlsl
cbuffer ColorGradingParams : register(b2) {
    float  temperature;         // 6500 Kelvin default
    float  tint;                // 0 default (-50 to +50)
    float4 globalSaturation;    // (1,1,1,1) — RGBA, A=master
    float4 globalContrast;      // (1,1,1,1)
    float4 globalGamma;         // (1,1,1,1)
    float4 globalGain;          // (1,1,1,1)
    float4 globalOffset;        // (0,0,0,0)
    float4 shadowSaturation;    // Shadow zone adjustments
    float4 shadowGamma;
    float4 shadowGain;
    float  shadowsMax;          // 0.09
    float4 midtoneSaturation;   // Midtone zone adjustments
    float4 midtoneGamma;
    float4 midtoneGain;
    float4 highlightSaturation; // Highlight zone adjustments
    float4 highlightGamma;
    float4 highlightGain;
    float  highlightsMin;       // 0.5
    float  highlightsMax;       // 1.0
    float4 sceneTint;           // White default
    float  blueCorrection;      // 0.6
    float  expandGamut;         // 1.0
};
```

## Impact

- **Shaders**: 1 compute shader + 1 HLSLI include
- **Textures**: 1× 32×32×32 RGBA8 3D texture (LUT, 128KB)
- **Integration**: Modified tonemap pass (add LUT sample)
- **Performance**: LUT generation ~0.02ms (once on change), LUT sample ~0.01ms per frame
- **VRAM**: 128KB

## Breaking Changes

None — extends tonemap pass. Default LUT is identity (no change).

## Acceptance Criteria

1. Warm temperature (3200K) produces orange/warm cast, cool (10000K) produces blue
2. Desaturating to 0 produces grayscale image
3. Shadows/midtones/highlights adjustments affect only their luminance zone
4. External LUT imported from Photoshop/DaVinci applies correctly
5. Default parameters (all neutral) produce identical output to no color grading
6. Demo: same scene with different color grades (warm/cool/desaturated/cinematic)
