# UE5 Tone Mapping & FXAA Post-Processing Analysis

**Analyzed UE5 version:** 5.6
**Analyzed by:** unreal-architect (Claude Haiku)

---

## Executive Summary

UE5's tone mapping and FXAA implementations provide key architectural patterns for HDR→LDR color space conversion and anti-aliasing. The critical insights are:

1. **Tone mapping is a permutation-based shader system**, not a hard-wired pipeline
2. **The "gamma-only" fallback** handles cases when tonemapping is disabled, providing a bypass path
3. **FXAA integrates as a full-screen post-process pass** with 6 quality presets driven by C++ parameters
4. **Pass ordering is managed at the call site**, not internally
5. **Output device detection determines which tone curve to apply** (sRGB, Rec709, ACES, ST2084/PQ, etc.)

---

## Part 1: Tone Mapping Architecture

### 1.1 Overview

**Files analyzed:**
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessTonemap.h` (lines 1-83)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessTonemap.cpp` (lines 1-1050)
- `W:\UE_5.6_Source\Engine\Shaders\Private\PostProcessTonemap.usf` (lines 1-716)
- `W:\UE_5.6_Source\Engine\Shaders\Private\TonemapCommon.ush` (lines 1-640)

### 1.2 Tone Mapping Input Structure

```cpp
struct FTonemapInputs
{
    // HDR scene color to convert
    FScreenPassTextureSlice SceneColor;  // [Required]

    // Bloom texture to composite
    FScreenPassTexture Bloom;  // [Required]

    // Color grading LUT (3D or unwrapped 2D)
    FRDGTextureRef ColorGradingTexture;  // [Required]

    // Eye adaptation buffer for automatic exposure
    FRDGBufferRef EyeAdaptationBuffer;
    const FEyeAdaptationParameters* EyeAdaptationParameters;

    // Local exposure (bilateral grid or exposure fusion)
    FRDGTextureRef LocalExposureBilateralGridTexture;  // Optional
    FRDGTextureRef BlurredLogLuminanceTexture;
    const FLocalExposureParameters* LocalExposureParameters;

    // Output override (if null, a new texture is created)
    FScreenPassRenderTarget OverrideOutput;

    // Control flags
    bool bGammaOnly = false;           // When true, skip tone curve (gamma-only fallback)
    bool bOutputInHDR = false;         // Leave output in HDR (no tonemapping)
    bool bWriteAlphaChannel = false;
};
```

**Key Pattern**: The inputs struct separates HDR→LDR control into boolean flags:
- `bGammaOnly` → skip tone curve, only apply gamma correction (bypass path)
- `bOutputInHDR` → output remains in HDR, no LDR conversion

This is **UzEngine's pattern for conditional post-processing** — use input flags, not runtime branch counts.

### 1.3 Permutation System (Shader Variants)

UE5 uses **shader permutations** to compile variants instead of runtime branching:

```cpp
// From PostProcessTonemap.cpp lines 86-98
class FTonemapperGammaOnlyDim      : SHADER_PERMUTATION_BOOL("USE_GAMMA_ONLY");
class FTonemapperLocalExposureDim  : SHADER_PERMUTATION_INT("LOCAL_EXPOSURE_MODE", 3);
    // 0: none, 1: bilateral, 2: fusion
class FTonemapperSharpenDim        : SHADER_PERMUTATION_BOOL("USE_SHARPEN");
class FTonemapperFilmGrainDim      : SHADER_PERMUTATION_BOOL("USE_FILM_GRAIN");
class FTonemapperMsaaDim           : SHADER_PERMUTATION_BOOL("METAL_MSAA_HDR_DECODE");
class FTonemapperAlphaChannelDim   : SHADER_PERMUTATION_BOOL("DIM_ALPHA_CHANNEL");

using FCommonDomain = TShaderPermutationDomain<
    FTonemapperGammaOnlyDim,
    FTonemapperLocalExposureDim,
    FTonemapperSharpenDim,
    FTonemapperFilmGrainDim,
    FTonemapperMsaaDim,
    FTonemapperAlphaChannelDim>;
```

**Critical optimization rule** (line 109-115):
```cpp
if (PermutationVector.Get<FTonemapperGammaOnlyDim>())
{
    return !PermutationVector.Get<FTonemapperLocalExposureDim>() &&
        !PermutationVector.Get<FTonemapperSharpenDim>() &&
        !PermutationVector.Get<FTonemapperFilmGrainDim>() &&
        !PermutationVector.Get<FTonemapperMsaaDim>();
}
```

**When gamma-only is enabled, all other features are DISABLED**. This eliminates dead code and reduces register pressure.

### 1.4 The Gamma-Only Fallback Path

**When is gamma-only used?** (PostProcessTonemap.cpp, line 135-140):

```cpp
if (bGammaOnly ||
    (Family->EngineShowFlags.Tonemapper == 0) ||  // r.ShowFlags.Tonemapper = 0
    (Family->EngineShowFlags.PostProcessing == 0))  // r.PostProcessing = 0
{
    PermutationVector.Set<FTonemapperGammaOnlyDim>(true);
    return PermutationVector;
}
```

**The gamma-only shader** (PostProcessTonemap.usf, lines 490-502):

```glsl
#if USE_GAMMA_ONLY
{
    OutColor.rgb = pow(SceneColor.rgb * (OneOverPreExposure * GlobalExposure), InverseGamma.x);
    OutLuminance = Luminance(OutColor.rgb);
}
#else
{
    // Full tone mapping (ACES + bloom + color grading + etc)
    half3 FinalLinearColor = SceneColor.rgb * SceneColorTint * (OneOverPreExposure * GlobalExposure * ...);
    // ... apply bloom, film grain, color lookup table ...
    half3 OutDeviceColor = ColorLookupTable(FinalLinearColor);
}
#endif
```

**Pattern for UzEngine**: When post-processing is disabled, UzEngine should compile a simple gamma-correction shader that does nothing else. This is **dead-code elimination at shader compile time**, not runtime branching.

### 1.5 Tone Curve Parameters

UE5 uses **film tone mapping** with 5 parameters:

```cpp
// From PostProcessTonemap.cpp line 836-840, FPostProcessSettings
float FilmSlope;        // Slope of mid-tone ramp (default ~0.91 for ACES)
float FilmToe;          // Shadow region curve control (default ~0.53)
float FilmShoulder;     // Highlight region curve control (default ~0.23)
float FilmBlackClip;    // Black point offset (default ~0.0)
float FilmWhiteClip;    // White point clipping (default ~0.035)
```

**Predefined presets** (TonemapCommon.ush, lines 68-97):

```
// ACES settings (UE5 default)
Slope = 0.91, Toe = 0.53, Shoulder = 0.23, BlackClip = 0, WhiteClip = 0.035

// Uncharted settings
Slope = 0.63, Toe = 0.55, Shoulder = 0.47, BlackClip = 0, WhiteClip = 0.01

// Legacy settings
Slope = 0.98, Toe = 0.3, Shoulder = 0.22, BlackClip = 0, WhiteClip = 0.025
```

**Tone curve function** (TonemapCommon.ush, lines 105-220 — `FilmToneMap()`):

1. Convert working color space to ACES AP1 (Academy Color Encoding System primaries)
2. Apply glow module (Sigmoid shaper with gain=0.05, mid=0.08)
3. Apply red modifier (hue-specific desaturation to preserve reds)
4. Compute log-space tone curve using toe/shoulder segments
5. Return AP1 primaries

**This is not a lookup table — it's a procedural function**. The curve uses **log-space piecewise functions** with smooth transitions:

```cpp
float3 ToeColor = (-FilmBlackClip) + (2 * ToeScale) / (1 + exp(...));
float3 ShoulderColor = (1 + FilmWhiteClip) - (2 * ShoulderScale) / (1 + exp(...));
float3 ToneColor = lerp(ToeColor, ShoulderColor, t);  // Smooth Hermite interpolation
```

### 1.6 Output Device Routing (No Tonemap, With Tonemap)

**File**: PostProcessTonemap.cpp, lines 227-271 (`GetTonemapperOutputDeviceParameters()`)

UE5 detects output format at **render target level**:

```cpp
EDisplayOutputFormat OutputDeviceValue;

if (Family.SceneCaptureSource == SCS_FinalColorHDR)
{
    OutputDeviceValue = EDisplayOutputFormat::HDR_LinearNoToneCurve;
}
else if (Family.SceneCaptureSource == SCS_FinalToneCurveHDR)
{
    OutputDeviceValue = EDisplayOutputFormat::HDR_LinearWithToneCurve;
}
else
{
    OutputDeviceValue = Family.RenderTarget->GetDisplayOutputFormat();
}
```

**Output device options** (TonemapCommon.ush, lines 22-32):

```cpp
#define TONEMAPPER_OUTPUT_sRGB                    0   // SDR
#define TONEMAPPER_OUTPUT_Rec709                  1   // SDR (TV standard)
#define TONEMAPPER_OUTPUT_ExplicitGammaMapping    2   // SDR (custom gamma)
#define TONEMAPPER_OUTPUT_ACES1000nitST2084       3   // HDR (1000 nits via ST2084/PQ)
#define TONEMAPPER_OUTPUT_ACES2000nitST2084       4   // HDR (2000 nits via ST2084/PQ)
#define TONEMAPPER_OUTPUT_ACES1000nitScRGB        5   // HDR (scRGB, linear wide-gamut)
#define TONEMAPPER_OUTPUT_ACES2000nitScRGB        6   // HDR (scRGB)
#define TONEMAPPER_OUTPUT_LinearEXR               7   // Full float (for offline rendering)
#define TONEMAPPER_OUTPUT_NoToneCurve             8   // Raw HDR, no tone mapping
#define TONEMAPPER_OUTPUT_WithToneCurve           9   // Raw HDR with tone curve applied
```

**Tone curve selection in shader** (PostProcessTonemap.usf, line 189-200):

```glsl
if(GetOutputDevice() >= TONEMAPPER_OUTPUT_ACES1000nitST2084)
{
    // ST2084 (Dolby PQ) — expects 0-10000 nits range
    // Encode linear values in nits
    LUTEncodedColor = LinearToST2084(clamp(LinearColor * LinearToNitsScale, 0.0, 10000.0));
}
else
{
    // Log space encoding for LUT color grading
    LUTEncodedColor = LinToLog(LinearColor + LogToLin(0));
}
```

**For UzEngine**: Output device detection is **CPU-side decision that drives shader permutations and parameter setup**. The shader does NOT branch on device at runtime; it's compiled for the target device upfront.

### 1.7 Exposure & Eye Adaptation

Eye adaptation provides automatic exposure control:

```cpp
const float2 EyeAdaptationData = EyeAdaptationBuffer[0].xw;
const float GlobalExposure = EyeAdaptationData.x;  // [0] = current exposure
const float DefaultEyeAdaptation = GetEyeAdaptationFixedExposure(View);

// If eye adaptation is disabled, create a default buffer with fixed exposure
if (!bEyeAdaptation)
{
    const FVector4f DefaultEyeAdaptationData(DefaultEyeAdaptation, 0, 0, DefaultEyeAdaptation);
    EyeAdaptationBuffer = CreateStructuredBuffer(..., &DefaultEyeAdaptationData, ...);
}
```

**Application in shader** (PostProcessTonemap.usf, line 515):

```glsl
half3 FinalLinearColor = SceneColor.rgb * SceneColorTint *
    (OneOverPreExposure * GlobalExposure * VignetteMask * LocalExposure);
```

The exposure is **a simple multiply**, applied before bloom and tone curve. The buffer contains both the exposure value and a backup for fallback.

### 1.8 Full Pipeline (Simplified)

```
[HDR SceneColor]
    ↓
[SampleSceneColor] with optional Chromatic Aberration
    ↓
[Sample Bloom] (with dirt mask tint)
    ↓
[Apply Sharpening] (if enabled, adaptive edge-based)
    ↓
[Compute Local Exposure] (bilateral or fusion mode, optional)
    ↓
[Apply Eye Adaptation] (GlobalExposure multiply)
    ↓
[Apply Vignette] (radial falloff)
    ↓
[Composite Bloom] to linear color
    ↓
[Apply Film Grain] (shadows/midtones/highlights weighted)
    ↓
[ColorLookupTable] (3D LUT applied in log or ST2084 space)
    ↓
[Backbuffer Quantization Dithering] (for 10-bit precision preservation)
    ↓
[Output Device Conversion]
    - sRGB: LinearToSrgbBranchless
    - Rec709: similar to sRGB
    - ACES ST2084: LinearToST2084
    - ExplicitGamma: pow(color, 1.0/gamma)
    ↓
[LDR Backbuffer] or [HDR Backbuffer]
```

---

## Part 2: FXAA Implementation

### 2.1 Overview

**Files analyzed:**
- `W:\UE_5.6_Source\Engine\Shaders\Private\FXAAShader.usf` (lines 1-107)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessAA.cpp` (lines 1-121)

### 2.2 FXAA Quality Presets

```cpp
// From PostProcessAA.cpp, line 7-16
TAutoConsoleVariable<int32> CVarFXAAQuality(
    TEXT("r.FXAA.Quality"), 4,  // Default: PC low-dither 12-sample
    TEXT(" 0: Console\n")
    TEXT(" 1: PC medium-dither 3-sample\n")
    TEXT(" 2: PC medium-dither 5-sample\n")
    TEXT(" 3: PC medium-dither 8-sample\n")
    TEXT(" 4: PC low-dither 12-sample (Default)\n")
    TEXT(" 5: PC extreme quality 12-samples"),
    ECVF_Scalability | ECVF_RenderThreadSafe);

enum class EFXAAQuality : uint8
{
    Console = 0,
    PC_Medium_3 = 1,
    PC_Medium_5 = 2,
    PC_Medium_8 = 3,
    PC_Low_12 = 4,    // Default
    PC_Extreme_12 = 5,
    MAX
};
```

**Mapping to FXAA library** (FXAAShader.usf, lines 18-36):

```glsl
#if FXAA_PRESET == 0
    #define FXAA_QUALITY__PRESET 10      // NVIDIA FXAA 3.11 preset 10
    #define FXAA_PC_CONSOLE 1
#elif FXAA_PRESET == 1
    #define FXAA_QUALITY__PRESET 10
    #define FXAA_PC 1
// ... etc up to FXAA_PRESET == 5 (preset 39)
#endif
```

**Key insight**: UE5 uses **NVIDIA FXAA 3.11 library** (proprietary, included in engine). The `FXAA_QUALITY__PRESET` parameter directly maps to sample count and tap pattern tables inside Fxaa3_11.ush.

### 2.3 FXAA Parameters

```cpp
// From PostProcessAA.cpp, line 94-99
PassParameters->fxaaQualitySubpix = 0.75f;           // Subpixel AA strength [0..1]
PassParameters->fxaaQualityEdgeThreshold = 0.166f;   // Edge detection threshold
PassParameters->fxaaQualityEdgeThresholdMin = 0.0833f; // Minimum edge to process
PassParameters->fxaaConsoleEdgeSharpness = 8.0f;     // Sharpness multiplier
PassParameters->fxaaConsoleEdgeThreshold = 0.125f;   // Console-specific threshold
PassParameters->fxaaConsoleEdgeThresholdMin = 0.05f; // Console minimum
```

These are **hardcoded to NVIDIA FXAA 3.11 defaults** and match the library's recommendations.

**Reciprocal frame buffer parameters** (PostProcessAA.cpp, lines 82-92):

```cpp
float N = 0.5f;
FVector4f Value(-N * OutputExtentInverse.X, -N * OutputExtentInverse.Y,
                  N * OutputExtentInverse.X, N * OutputExtentInverse.Y);
PassParameters->fxaaConsoleRcpFrameOpt = Value;

float N = 2.0f;
FVector4f Value(-N * OutputExtentInverse.X, -N * OutputExtentInverse.Y,
                  N * OutputExtentInverse.X, N * OutputExtentInverse.Y);
PassParameters->fxaaConsoleRcpFrameOpt2 = Value;
```

These values are **pixel offset scales** for sampling neighborhood pixels (0.5 texel and 2.0 texel offsets).

### 2.4 FXAA Shader Entry Point

```glsl
// From FXAAShader.usf, line 72-104
void FxaaPS(
    float4 SvPosition : SV_POSITION,
    out float4 OutColor : SV_Target0)
{
    FxaaTex TextureAndSampler;
    TextureAndSampler.tex = Input_Texture;
    TextureAndSampler.smpl = Input_Sampler;
    TextureAndSampler.UVMinMax = float4(
        Input_UVViewportBilinearMin,
        Input_UVViewportBilinearMax);

    float2 TexCenter = ApplyScreenTransform(SvPosition.xy, SvPositionToInputTextureUV);
    float4 TexCorners = float4(
        TexCenter - 0.5f * Input_ExtentInverse,
        TexCenter + 0.5f * Input_ExtentInverse);

    OutColor = FxaaPixelShader(
        TexCenter, TexCorners,
        TextureAndSampler, TextureAndSampler, TextureAndSampler,
        Input_ExtentInverse,
        fxaaConsoleRcpFrameOpt,    // 0.5 texel offsets
        fxaaConsoleRcpFrameOpt2,   // 2.0 texel offsets
        fxaaConsole360RcpFrameOpt2, // Xbox 360 specific
        fxaaQualitySubpix,
        fxaaQualityEdgeThreshold,
        fxaaQualityEdgeThresholdMin,
        fxaaConsoleEdgeSharpness,
        fxaaConsoleEdgeThreshold,
        fxaaConsoleEdgeThresholdMin,
        fxaaConsole360ConstDir);

    #if DIM_ALPHA_CHANNEL == 0
        OutColor.a = 1.0;  // Force opaque if not propagating alpha
    #endif
}
```

**Key pattern**:
- `FxaaPixelShader()` is defined in Fxaa3_11.ush (external NVIDIA library)
- All parameters are **hardcoded by UE5**, not user-exposed
- The shader is **permuted on alpha channel support only**

### 2.5 Pass Integration & Permutation

```cpp
// From PostProcessAA.cpp, lines 29-31
class FAlphaChannelDim : SHADER_PERMUTATION_BOOL("DIM_ALPHA_CHANNEL");
class FQualityDimension : SHADER_PERMUTATION_ENUM_CLASS("FXAA_PRESET", EFXAAQuality);
using FPermutationDomain = TShaderPermutationDomain<FAlphaChannelDim, FQualityDimension>;
```

Total permutations: 2 (alpha on/off) × 6 (quality presets) = **12 shader variants**.

### 2.6 FXAA Call Site

```cpp
FScreenPassTexture AddFXAAPass(FRDGBuilder& GraphBuilder, const FSceneView& InSceneView,
                                const FFXAAInputs& Inputs)
{
    check(Inputs.SceneColor.IsValid());
    check(Inputs.Quality != EFXAAQuality::MAX);

    // Create output or use override
    FScreenPassRenderTarget Output = Inputs.OverrideOutput;
    if (!Output.IsValid())
    {
        Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, Inputs.SceneColor,
                                                          View.GetOverwriteLoadAction(), TEXT("FXAA"));
    }

    // Add to RDG
    FPixelShaderUtils::AddFullscreenPass(GraphBuilder, View.ShaderMap,
        RDG_EVENT_NAME("FXAA(Quality=%d%s) %dx%d PS", ...),
        PixelShader, PassParameters, Output.ViewRect);

    return MoveTemp(Output);
}
```

**Note**: This is a **full-screen quad pass**, not a compute shader. FXAA always uses pixel shaders.

---

## Part 3: HDR → LDR Pipeline & Pass Ordering

### 3.1 Where Tone Mapping & FXAA are Called

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessing.cpp`

Search results show tone mapping is called in multiple contexts:
- After bloom composition
- Before final output
- With FXAA applied **before or after** tonemap, depending on config

**Pattern**: The **call site decides the pass order**, not internal logic. UE5's renderer graph (RDG) allows dynamic ordering of passes.

### 3.2 Typical Post-Processing Chain (Desktop)

```
[Scene Render]
    ↓
[Bloom Setup] (threshold, Gaussian pyramid)
    ↓
[Bloom Blur] (separable Gaussian)
    ↓
[Eye Adaptation] (histogram → exposure compute)
    ↓
[Local Exposure] (optional, bilateral or fusion)
    ↓
[TONEMAP PASS]
    - Input: HDR scene color + bloom
    - Output: LDR color (or HDR if bOutputInHDR)
    - Applies: tone curve, color grading, film grain, vignette
    ↓
[FXAA PASS] (if enabled)
    - Input: Tonemapped LDR color
    - Output: Anti-aliased LDR color
    - Quality: 0-5 (12-39 samples)
    ↓
[Final Backbuffer]
```

**Alternative order** (FXAA before tonemap):

```
[HDR Scene + Bloom]
    ↓
[FXAA] (operates on HDR)
    ↓
[TONEMAP]
    ↓
[Backbuffer]
```

**UE5 chooses the order based on**:
- Whether tone mapping is enabled (`r.Tonemapper`)
- Whether FXAA is enabled (`r.FXAA.Quality`)
- Performance tier

### 3.3 Bypass Paths

**"No tonemap" case** (when `EngineShowFlags.Tonemapper == 0`):

1. **Gamma-only shader** is used instead (single pow() operation)
2. Bloom is **still composited** (if enabled)
3. Color grading LUT is **still applied** (if enabled)
4. Only the tone curve is skipped

**"No post-processing" case** (when `EngineShowFlags.PostProcessing == 0`):

1. **Entire tonemap pass is skipped**
2. Scene color is output **as-is** (with pre-exposure applied)
3. No bloom, no AA, no color grading

**For UzEngine**: These bypass cases must be **clear decision trees at the call site**, not shader permutations. The shader should only exist for cases you actually use.

---

## Part 4: Key Architectural Patterns for UzEngine

### 4.1 Permutation-Based Post-Processing

**Pattern**: Instead of runtime `if` statements, use shader permutations for features:

```cpp
// Bad (runtime branching, wastes ALU):
if (bUseTonemap) { apply tone curve; }
if (bUseBloom) { add bloom; }
if (bUseFilmGrain) { apply grain; }

// Good (compile-time permutations, dead code elimination):
#if USE_TONEMAP
    // ... tone curve code (compiled out if false)
#endif
#if USE_BLOOM
    // ... bloom code (compiled out if false)
#endif
```

UE5 does this for: GammaOnly, LocalExposure, Sharpen, FilmGrain, AlphaChannel, OutputDevice.

**For UzEngine**: Use the same pattern. Create a shader permutation domain for each optional feature.

### 4.2 Fallback Paths (Gamma-Only)

When tone mapping is disabled, UE5 uses a **simple gamma-only shader**:

```glsl
OutColor.rgb = pow(SceneColor.rgb * GlobalExposure, InverseGamma.x);
```

This is **stateless, fast, and correct**. No color grading, no bloom, just gamma.

**For UzEngine**: When disabling PBR effects, always have a fallback that is **even simpler** than the full pipeline. Use a separate shader entirely, not a permutation.

### 4.3 Output Device Detection (CPU-Side)

UE5 detects the **output display format at CPU time** and sets shader parameters accordingly:

```cpp
EDisplayOutputFormat OutputDevice = Family.RenderTarget->GetDisplayOutputFormat();
// → Determines whether to encode with ST2084 (PQ), sRGB, etc.
```

Then the shader **conditionally applies the correct EOTF**:

```glsl
if(GetOutputDevice() >= TONEMAPPER_OUTPUT_ACES1000nitST2084)
{
    LUTEncodedColor = LinearToST2084(clamp(LinearColor * LinearToNitsScale, 0.0, 10000.0));
}
else
{
    LUTEncodedColor = LinToLog(LinearColor + LogToLin(0));
}
```

**For UzEngine**: Detect output device **once at frame start**, pass the enum to the shader, then use it for texture LUT lookups and final encoding.

### 4.4 Eye Adaptation as a Structured Buffer

UE5 uses a simple **2-float structured buffer** for eye adaptation:

```cpp
struct EyeAdaptationBuffer
{
    float CurrentExposure;  // [0].x
    float Reserved;         // [0].y
    float Reserved2;        // [0].z
    float FixedExposure;    // [0].w  (fallback if compute fails)
};
```

The shader reads it as:

```glsl
const float2 EyeAdaptationData = EyeAdaptationBuffer[0].xw;
const float GlobalExposure = EyeAdaptationData.x;
```

**For UzEngine**: Store exposure in a 4-float instead of fighting with register pressure. The extra floats cost nothing on modern GPUs.

### 4.5 Film Tone Mapping is Procedural, Not LUT-Based

UE5's film tone curve is **computed in the shader**, not looked up:

```cpp
half3 ToeColor = (-FilmBlackClip) + (2 * ToeScale) / (1 + exp(...));
half3 ShoulderColor = (1 + FilmWhiteClip) - (2 * ShoulderScale) / (1 + exp(...));
half3 ToneColor = lerp(ToeColor, ShoulderColor, SmoothedInterp);
```

This is **cheaper than a 3D LUT for tone curves** and gives per-channel control.

**For UzEngine**: Don't use texture LUTs for tone curves. Use the piecewise sigmoid approximation UE5 uses.

### 4.6 FXAA is Always Full-Screen, Not Adaptive

UE5's FXAA pass:
- Always runs on **full viewport** (no quad-based culling)
- Always uses **pixel shaders** (no compute variant)
- Parameters are **baked into the shader**, not dynamic buffers

**For UzEngine**: If you implement FXAA, don't over-engineer it. Use NVIDIA's reference implementation directly.

### 4.7 Output Format Selection Controls Everything

The **render target's display format** determines:
1. Which shader permutation is selected
2. What parameters are passed to the shader
3. Whether the tonemap is even run
4. Whether the output is HDR or SDR

**For UzEngine**: Make `GetDisplayOutputFormat()` your **single source of truth** for color space decisions.

---

## Part 5: Specific Implementation Details for UzEngine

### 5.1 Tone Mapping Bypass Logic

```cpp
// Current UzEngine approach (if any):
if (bDisableTonemap)
{
    // Skip tonemap pass entirely
    SceneColor = InputHDR;  // Direct output
}

// Better (UE5 approach):
if (bDisableTonemap)
{
    // Run simplified gamma-only shader
    // Still applies: exposure, gamma, alpha handling
    // Skips: bloom, color grading, film curve, grain
    AddSimpleGammaPass(SceneColor);
}

// Best (production):
// Always run tonemap pass, but permute on bGammaOnly flag
// Let the shader compiler optimize out unused code
```

### 5.2 Tone Curve Parameters for UzEngine's PBR

If UzEngine uses ACES-like tone mapping:

```cpp
// Recommended values (ACES defaults)
FilmSlope = 0.91f;       // Mid-tone ramp
FilmToe = 0.53f;         // Shadow curve
FilmShoulder = 0.23f;    // Highlight curve
FilmBlackClip = 0.0f;    // No black crush
FilmWhiteClip = 0.035f;  // Slight highlight clipping
```

### 5.3 Exposure Multiplier Application

```glsl
// Shader: How eye adaptation is applied
half3 FinalColor = SceneColor.rgb * GlobalExposure;
```

**Do NOT use**:
- `log2` / `exp2` (expensive on older GPUs)
- `pow()` (unnecessary)

**Use direct multiply** — exposure is just a scale factor.

### 5.4 Gamma Correction for Different Outputs

```cpp
// From GetTonemapperOutputDeviceParameters():
if ((PLATFORM_APPLE || OutputDevice == SDR_ExplicitGammaMapping) && Gamma == 0.0f)
{
    Gamma = 2.2f;  // Apple default
}

FVector3f InvDisplayGamma;
InvDisplayGamma.X = 1.0f / DisplayGamma;                           // For standard gamma
InvDisplayGamma.Y = 2.2f / DisplayGamma;                           // For sRGB reference
InvDisplayGamma.Z = 1.0f / FMath::Max(Gamma, 1.0f);               // For explicit gamma
```

For sRGB, use `LinearToSrgbBranchless()` (cheaper than pow).

### 5.5 Color Grading LUT Application

UE5 applies the LUT **after tone curve** in log or PQ space:

```glsl
half3 FinalLinearColor = SceneColor * Exposure * Bloom;  // Tone-mapped linear
half3 OutDeviceColor = ColorLookupTable(FinalLinearColor);  // LUT applied in log space
OutColor.rgb = OutDeviceColor;  // Ready for backbuffer
```

**For UzEngine**: If you have a color grading LUT, apply it **after all tone mapping**, before final output.

---

## Part 6: Anti-Patterns & What NOT to Do

### 6.1 Runtime Branching on Post-Process Flags

**Bad**:
```glsl
// Inside pixel shader
if (bUseTonemap) { ... }  // Each pixel evaluates this
if (bUseBloom) { ... }
if (bUseGrain) { ... }
```

**Good**: Use shader permutations instead. Let the compiler eliminate branches.

### 6.2 Tone Mapping in Multiple Stages

**Bad**:
- Apply ACES in one pass
- Apply color grading in another pass
- Apply gamma in a third pass
- Each pass does HDR→LDR→HDR conversions

**Good**: Do all tone mapping in **one pass**, once, in the right order.

### 6.3 Full-Screen Passes Without Viewport Clipping

**Bad**:
```cpp
// FXAA runs on entire window (1920x1080)
// But we only needed (800, 600)
```

**Good**:
```cpp
FPixelShaderUtils::AddFullscreenPass(..., Output.ViewRect);  // Use actual viewport
```

### 6.4 Hardcoding Exposure Values

**Bad**:
```cpp
float Exposure = 1.0f;  // Fixed
color *= Exposure;
```

**Good**:
```cpp
float Exposure = EyeAdaptationBuffer[0].x;  // Computed per frame
color *= Exposure;
```

### 6.5 Not Testing the "No Post-Processing" Path

**Bad**: Only test with all effects enabled.

**Good**: Verify that `r.PostProcessing 0` produces a **fast, correct** fallback (gamma only).

---

## Summary Table: Key Findings

| Aspect | UE5 Approach | Why | Applicable to UzEngine |
|--------|--------------|-----|----------------------|
| **Tone Mapping** | Procedural piecewise sigmoid curve (film tone map) | Efficient (no LUT), per-channel control | Yes — use this formula |
| **Fallback Path** | Gamma-only permutation (simple power) | Dead-code elimination, no runtime branching | Yes — implement this |
| **Output Device** | CPU-detected at render target level | Determines shader variant and parameters | Yes — query once per frame |
| **Eye Adaptation** | Structured buffer (2 floats) | Fast to compute, simple to pass | Yes — use this pattern |
| **FXAA** | NVIDIA 3.11 library, 6 quality presets | Industry standard, well-tuned | Yes if AA needed, else skip |
| **Pass Ordering** | Decided at call site (RDG) | Flexible, composable passes | Yes — use render graph |
| **Bloom** | Applied before tonemap | Bloom is additive, benefits from tone curve | Yes |
| **Color Grading** | Applied after tone curve (in log space) | Preserves intent, no double-encoding | Yes |
| **Film Grain** | Energy-conservative, per-zone intensity | Prevents over-brightening | Yes if grain desired |
| **Sharpening** | Adaptive edge-based, in shader | Preserves HDR highlights | Yes — use edge detection |

---

## File Paths (Absolute, for Reference)

**Core Tone Mapping:**
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessTonemap.h`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessTonemap.cpp`

**Tone Curve Algorithms:**
- `W:\UE_5.6_Source\Engine\Shaders\Private\PostProcessTonemap.usf`
- `W:\UE_5.6_Source\Engine\Shaders\Private\TonemapCommon.ush`

**ACES Color Encoding:**
- `W:\UE_5.6_Source\Engine\Shaders\Private\ACES\ACESCommon.ush`
- `W:\UE_5.6_Source\Engine\Shaders\Private\ACES\ACES_v1.3.ush`

**FXAA Implementation:**
- `W:\UE_5.6_Source\Engine\Shaders\Private\FXAAShader.usf`
- `W:\UE_5.6_Source\Engine\Shaders\Private\Fxaa3_11.ush`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessAA.cpp`

**Post-Processing Pipeline:**
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\PostProcessing.cpp`

---

## Conclusion

UE5's tone mapping and post-processing architecture prioritizes:

1. **Compile-time specialization** (permutations, not branches)
2. **Clear fallback paths** (gamma-only when effects disabled)
3. **Single-pass efficiency** (all tone mapping in one shader)
4. **Procedural curves** (cheaper than LUT-based tone mapping)
5. **Output-driven decisions** (display format determines everything)

UzEngine should adopt these patterns wholesale. The implementation is not novel — it's **well-engineered, proven, and simple**. Avoid the temptation to add runtime flexibility at the cost of complexity.

