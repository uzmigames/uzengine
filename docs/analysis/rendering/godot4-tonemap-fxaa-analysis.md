# Godot 4 Tone Mapping & FXAA Analysis

## Executive Summary

Godot 4 implements tone mapping and FXAA in a **unified post-process shader pass** that runs on the render device via RenderingDevice. The architecture separates **tone mapper modes** (Linear, Reinhard, Filmic, ACES, AgX) via shader specialization flags, applies exposure/white-point controls uniformly, and integrates FXAA as an optional pre-tonemap antialiasing step.

Key insight: **The "no tonemap" / linear mode is not a bypass—it's a shader branch that returns color unchanged**, allowing exposure and BCS (brightness/contrast/saturation) controls to still apply to HDR values.

---

## 1. TONE MAPPING ARCHITECTURE

### 1.1 Shader Compilation Strategy

**File**: `E:/godot/servers/rendering/renderer_rd/effects/tone_mapper.cpp` (lines 38-106)

Godot uses **static specialization** for tone mapper modes:

```cpp
// Desktop version: 8 variants (4 modes × 2 multiview options)
Vector<String> tonemap_modes;
tonemap_modes.push_back("\n");                                           // Mode 0: NORMAL
tonemap_modes.push_back("\n#define USE_GLOW_FILTER_BICUBIC\n");         // Mode 1: GLOW + BICUBIC
tonemap_modes.push_back("\n#define USE_1D_LUT\n");                      // Mode 2: COLOR CORRECTION 1D LUT
tonemap_modes.push_back("\n#define USE_GLOW_FILTER_BICUBIC\n#define USE_1D_LUT\n"); // Mode 3: GLOW + LUT

tonemap_modes.push_back("\n#define USE_MULTIVIEW\n");                   // Modes 4-7: XR multiview variants
tonemap_modes.push_back("\n#define USE_MULTIVIEW\n#define USE_GLOW_FILTER_BICUBIC\n");
tonemap_modes.push_back("\n#define USE_MULTIVIEW\n#define USE_1D_LUT\n");
tonemap_modes.push_back("\n#define USE_MULTIVIEW\n#define USE_GLOW_FILTER_BICUBIC\n#define USE_1D_LUT\n");

// Mobile version: 8 variants with different feature set
// Uses SUBPASS rendering instead of texture reads on deferred renderers
```

The actual **tone mapper algorithm choice** is **runtime** via a push constant flag:

```cpp
enum TonemapMode { TONEMAPPER_LINEAR, TONEMAPPER_REINHARD, TONEMAPPER_FILMIC, TONEMAPPER_ACES, TONEMAPPER_AGX };
```

This decouples shader **compilation variants** (glow/LUT) from **algorithm selection** (tone curve).

### 1.2 Push Constant Layout

**Desktop** (112 bytes):
```cpp
struct TonemapPushConstant {
    float bcs[3];                      // 12 bytes - brightness, contrast, saturation
    uint32_t flags;                    //  4 bytes - bitset: USE_BCS, USE_GLOW, USE_AUTO_EXPOSURE, etc.

    float pixel_size[2];               //  8 bytes - 1/width, 1/height (for FXAA)
    uint32_t tonemapper;               //  4 bytes - which algorithm (LINEAR=0, REINHARD=1, FILMIC=2, ACES=3, AGX=4)
    float output_max_value;            //  4 bytes - output clamping (typically 1.0 for SDR, >1.0 for HDR)

    uint32_t glow_texture_size[2];     //  8 bytes - dimensions of bloom pyramid
    float glow_intensity;              //  4 bytes - bloom multiplier
    float glow_map_strength;           //  4 bytes - per-pixel bloom mask strength

    uint32_t glow_mode;                //  4 bytes - ADD/SCREEN/SOFTLIGHT/REPLACE/MIX
    float glow_levels[7];              // 28 bytes - per-mip weights for bloom (7 LOD levels)

    float exposure;                    //  4 bytes - scene exposure (EV scale)
    float white;                       //  4 bytes - white point (reference luminance for Reinhard/ACES)
    float auto_exposure_scale;         //  4 bytes - normalization factor for auto-exposure
    float luminance_multiplier;        //  4 bytes - global brightness scale (unused in most paths)

    vec4 tonemapper_params;            // 16 bytes - algorithm-specific parameters
};
```

**Mobile** (80 bytes): Optimized, drops some fields, uses specialization constants for tone mapper selection.

---

## 2. TONE MAPPING ALGORITHMS

**File**: `E:/godot/servers/rendering/renderer_rd/shaders/effects/tonemap.glsl`

### 2.1 Linear (No Tonemap)

**Lines 246-264**:
```glsl
vec3 apply_tonemapping(vec3 color) {
    if (params.tonemapper == TONEMAPPER_LINEAR) {
        return color;  // Passthrough: no tone curve applied
    }
    // ... other algorithms
}
```

**Behavior**: Returns color unchanged. **Exposure, white point, BCS, and sRGB conversion still apply**.

This is **NOT** a true bypass—it's:
1. Color scaled by `exposure * luminance_multiplier`
2. No tone curve applied
3. BCS applied (brightness, contrast, saturation)
4. sRGB encoding if FLAG_CONVERT_TO_SRGB is set
5. Clamped to `output_max_value`

### 2.2 Reinhard (Extended Formula)

**Lines 92-97**:
```glsl
vec3 tonemap_reinhard(vec3 color) {
    float white_squared = params.tonemapper_params.x;
    // Reinhard: color * (1 + color/white²) / (1 + color/output_max)
    return color * (1.0f + color / white_squared)
                 / (1.0f + color / params.output_max_value);
}
```

**Parameters**:
- `tonemapper_params.x` = `white_squared` (white point squared, e.g., 4.0 for white=2.0)
- `output_max_value` = upper luminance clamp (e.g., 1.0)

**Formula basis**: Reinhard's extended formula from https://doi.org/cjbgrt (Photographic Tone Reproduction for Digital Images)
- Produces characteristic "crushed blacks" if white² is too small
- More compression at bright end than dark end
- Parameter `white` (white_squared) controls knee sharpness

### 2.3 Filmic (Uncharted 2)

**Lines 99-115**:
```glsl
vec3 tonemap_filmic(vec3 color) {
    const float exposure_bias = 2.0f;
    const float A = 0.22f * exposure_bias * exposure_bias;  // ~0.88
    const float B = 0.30f * exposure_bias;                  // 0.60
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.01f;
    const float F = 0.30f;

    vec3 color_tonemapped = ((color * (A * color + C * B) + D * E)
                           / (color * (A * color + B) + D * F)) - E / F;

    return color_tonemapped / params.tonemapper_params.x;
}
```

**Formula**: Uncharted 2 curve (John Hable, GDC 2010)
- Numerator: `color * (A*color + C*B) + D*E` — cubic parabola (toe)
- Denominator: `color * (A*color + B) + D*F` — scaled denominator for normalized output
- Final division by `tonemapper_params.x` is the white point scaling

**Parameters**:
- `tonemapper_params.x` = white point (e.g., 11.2 in the original curve, Godot may use different scale)

**Characteristics**:
- Smooth "filmic" S-curve
- More highlight rolloff than Reinhard
- Slightly desaturates with linear compression
- Originally designed for 0–11.2 input range

### 2.4 ACES (Academy Color Encoding System)

**Lines 117-144**:
```glsl
vec3 tonemap_aces(vec3 color) {
    const float exposure_bias = 1.8f;

    // Rec. 709 → RRT (Reference Rendering Transform) — includes exposure bias
    const mat3 rgb_to_rrt = mat3(
        vec3(0.59719f * exposure_bias, 0.35458f * exposure_bias, 0.04823f * exposure_bias),
        vec3(0.07600f * exposure_bias, 0.90834f * exposure_bias, 0.01566f * exposure_bias),
        vec3(0.02840f * exposure_bias, 0.13383f * exposure_bias, 0.83777f * exposure_bias)
    );

    // RRT → ODT (Output Device Transform) — Rec. 709 back
    const mat3 odt_to_rgb = mat3(
        vec3(1.60475f, -0.53108f, -0.07367f),
        vec3(-0.10208f, 1.10813f, -0.00605f),
        vec3(-0.00327f, -0.07276f, 1.07602f)
    );

    color *= rgb_to_rrt;
    vec3 color_tonemapped = (color * (color + A) - B) / (color * (C * color + D) + E);
    color_tonemapped *= odt_to_rgb;

    return color_tonemapped / params.tonemapper_params.x;
}
```

**Key insight**: The matrices bake in **exposure bias** (×1.8) so the input range is effectively ×1.8 brighter than you might expect.

**Parameters**:
- `tonemapper_params.x` = white point scaling (Godot default often matches ACES reference white ~11.2, but scaled down)

**Adapted from**: BakingLab ACES implementation (MIT License) — closely matches ACES 1.0.3 ACES2065-1 → sRGB ODT

### 2.5 AgX (Eary Chow, Blender-compatible)

**Lines 179-228**:
```glsl
vec3 tonemap_agx(vec3 color) {
    // Combined Rec. 709 → Rec. 2020 + AgX inset
    const mat3 rec709_to_rec2020_agx_inset_matrix = mat3(
        vec3(0.544814746488245, 0.140416948464053, 0.0888104196149096),
        vec3(0.373787398372697, 0.754137554567394, 0.178871756420858),
        vec3(0.0813978551390581, 0.105445496968552, 0.732317823964232)
    );

    const mat3 agx_outset_rec2020_to_rec709_matrix = mat3(
        vec3(1.96488741169489, -0.299313364904742, -0.164352742528393),
        vec3(-0.855988495690215, 1.32639796461980, -0.238183969428088),
        vec3(-0.108898916004672, -0.0270845997150571, 1.40253671195648)
    );

    color = rec709_to_rec2020_agx_inset_matrix * color;
    color = allenwp_curve(color);  // Uses Reinhard-like shoulder + sigmoid toe
    color = min(vec3(params.output_max_value), color);
    color = agx_outset_rec2020_to_rec709_matrix * color;

    return color;
}
```

**Parameters**: Delegated to `allenwp_curve()` which uses `tonemapper_params[0..3]`:
- `params.tonemapper_params.x` = contrast
- `params.tonemapper_params.y` = toe slope
- `params.tonemapper_params.z` = shoulder slope
- `params.tonemapper_params.w` = shoulder width

**Characteristics**:
- Blender-compatible (uses same LUT-generation approach as Blender's AgX)
- Hybrid curve: Reinhard-like shoulder + sigmoid power-law toe
- Rec. 2020 working space before tone curve (broader color gamut)
- Clipping to `output_max_value` to prevent cyan fringing
- No `lusRGB.compensate_low_side` (Blender complexity omitted for performance)

---

## 3. EXPOSURE & WHITE POINT PIPELINE

**Main shader entry** (lines 854-894):

```glsl
void main() {
    // 1. Load source HDR color
    vec4 color = textureLod(source_color, uv_interp, 0.0f);
    color.rgb *= params.luminance_multiplier;  // Global brightness scale

    // 2. Apply exposure (EV scale, typically 1.0 = no change)
    float exposure = params.exposure;
    if (bool(params.flags & FLAG_USE_AUTO_EXPOSURE)) {
        // Read 1x1 average luminance texture, scale by auto_exposure_scale
        exposure *= 1.0 / (texelFetch(source_auto_exposure, ivec2(0,0), 0).r
                          * params.luminance_multiplier / params.auto_exposure_scale);
    }
    color.rgb *= exposure;

    // 3. Optional FXAA before tonemap (pre-tonemap antialiasing)
    if (bool(params.flags & FLAG_USE_FXAA)) {
        color.rgb = do_fxaa(color.rgb, exposure, uv_interp);
    }

    // 4. Apply bloom (glow) — pre-tonemap blends
    if (bool(params.flags & FLAG_USE_GLOW) && params.glow_mode != GLOW_MODE_SOFTLIGHT) {
        vec3 glow = gather_glow(source_glow, uv_interp) * params.glow_intensity;
        color.rgb = apply_glow(color.rgb, glow, params.white);
    }

    // 5. APPLY TONE MAPPING
    color.rgb = apply_tonemapping(color.rgb);
    // ^^ Uses params.tonemapper (enum) to select algorithm
    // ^^ All algorithms use params.white (white point) in their curve

    // 6. Post-tonemap bloom (SOFTLIGHT mode only)
    if (bool(params.flags & FLAG_USE_GLOW) && params.glow_mode == GLOW_MODE_SOFTLIGHT) {
        vec3 glow = gather_glow(source_glow, uv_interp) * params.glow_intensity;
        glow = apply_tonemapping(glow);  // Tone glow too for consistency
        color.rgb = apply_glow(color.rgb, glow, params.white);
    }

    // 7. BCS (Brightness, Contrast, Saturation)
    if (bool(params.flags & FLAG_USE_BCS)) {
        color.rgb *= params.bcs.x;  // Brightness (linear)

        color.rgb = linear_to_srgb(color.rgb);

        color.rgb = mix(vec3(0.5), color.rgb, params.bcs.y);  // Contrast (sRGB space)

        // Saturation: mix with desaturated value
        color.rgb = mix(vec3(dot(...)), color.rgb, params.bcs.z);
    }

    // 8. Color correction LUT (optional)
    if (bool(params.flags & FLAG_USE_COLOR_CORRECTION)) {
        color.rgb = apply_color_correction(color.rgb);  // 1D or 3D LUT
    }

    // 9. sRGB encoding (if not already done in step 7)
    if (bool(params.flags & FLAG_CONVERT_TO_SRGB)) {
        color.rgb = linear_to_srgb(color.rgb);
    }

    // 10. Debanding dither (optional)
    if (bool(params.flags & FLAG_USE_8_BIT_DEBANDING)) {
        color.rgb += screen_space_dither(...);
    }

    frag_color = color;
}
```

### Key Control Points:

| Parameter | Type | Range | Effect |
|-----------|------|-------|--------|
| `exposure` | float | -∞..∞ (EV scale) | Multiplies HDR input before tone curve. Typical: -2 to +2 |
| `white` (white point) | float | 0..∞ | Reinhard/Filmic/ACES use this for curve normalization. Typical: 1.0–4.0 |
| `output_max_value` | float | 0..∞ | Final clamp; limits tone curve output. Typical: 1.0 for SDR, 1.5–5.0 for EDR/HDR |
| `tonemapper_params[4]` | vec4 | Algorithm-specific | Tone curve shape parameters (white², contrast, toe, shoulder, etc.) |
| `bcs[3]` | vec3 | x: 0..∞, y: 0..2, z: 0..2 | Brightness multiplier, contrast (0.5=midtone mid, 1.0=neutral), saturation |

### HDR → LDR Pipeline with LINEAR Tone Mapper:

When `tonemapper = TONEMAPPER_LINEAR`:

1. **HDR input** (e.g., 0–16 range, from render pass)
2. **× exposure** → brightens/darkens
3. **Skip tone curve** (linear mode)
4. **+ glow** (bloom)
5. **BCS applied** (brightness, contrast, saturation)
6. **Clipped to output_max_value** (e.g., 1.0 for SDR display)
7. **sRGB encoded** (gamma 2.2)
8. **Written to LDR framebuffer** (8-bit or 10-bit)

**No clamping happens in the tone curve itself** — saturation happens only at final sRGB encoding. Bright HDR values beyond `output_max_value` are discarded (hard clipping).

---

## 4. FXAA IMPLEMENTATION

**File**: `E:/godot/servers/rendering/renderer_rd/shaders/effects/tonemap.glsl` (lines 469–836)

### 4.1 Integration Point

FXAA is **embedded in the tone mapper shader**, not a separate pass. Controlled by:

```cpp
tonemap.push_constant.flags |= p_settings.use_fxaa ? TONEMAP_FLAG_USE_FXAA : 0;
```

In shader:
```glsl
if (bool(params.flags & FLAG_USE_FXAA)) {
    color.rgb = do_fxaa(color.rgb, exposure, uv_interp);
}
```

**Applied**: Post-exposure, pre-tonemap (step 3 in pipeline above)

### 4.2 Algorithm: FXAA 3.11

Based on **NVIDIA FXAA 3.11** (Timothy Lottes, 2014), ported from Rendu (Simon Rodriguez, MIT License).

**Core steps**:

1. **Edge Detection** (lines 541–562):
   ```glsl
   float lumaUp    = rgb2luma(...);  // Perceived brightness via sqrt(dot(rgb, [0.299, 0.587, 0.114]))
   float lumaDown  = rgb2luma(...);
   float lumaLeft  = rgb2luma(...);
   float lumaRight = rgb2luma(...);
   float lumaCenter = rgb2luma(color);

   float lumaRange = max(lumaCenter, ...) - min(lumaCenter, ...);

   if (lumaRange < max(0.0312, lumaMax * 0.125)) {
       return color;  // Early exit: no edge
   }
   ```

   **Edge threshold**: `max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)`
   - `EDGE_THRESHOLD_MIN = 0.0312` (absolute)
   - `EDGE_THRESHOLD_MAX = 0.125` (relative to max luminance)

   **Luma function**: `sqrt(dot(rgb, [0.299, 0.587, 0.114]))` — simplified perceptual brightness (no gamma correction, just sqrt)

2. **Edge Direction Determination** (lines 569–580):
   ```glsl
   float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners)
                        + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0
                        + abs(-2.0 * lumaRight + lumaRightCorners);

   float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners)
                      + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0
                      + abs(-2.0 * lumaDown + lumaDownCorners);

   bool isHorizontal = (edgeHorizontal >= edgeVertical);
   ```

   **Purpose**: Determine if edge is horizontal or vertical (to search perpendicular to it)

3. **Gradient Estimation & Iteration** (lines 582–651):
   ```glsl
   float stepLength = isHorizontal ? params.pixel_size.y : params.pixel_size.x;

   float luma1 = isHorizontal ? lumaDown : lumaLeft;
   float luma2 = isHorizontal ? lumaUp : lumaRight;
   float gradient1 = luma1 - lumaCenter;
   float gradient2 = luma2 - lumaCenter;

   bool is1Steepest = abs(gradient1) >= abs(gradient2);

   float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

   // Search along the edge direction iteratively, stepping with QUALITY(i) multipliers
   // QUALITY(0)=1.0, QUALITY(5)=1.5, QUALITY(10)=2.0, QUALITY(11)=4.0, QUALITY(12)=8.0
   for (int i = 2; i < 12; i++) {
       if (!reached1) {
           uv1 -= vec3(offset * QUALITY(i), 0.0);
           lumaEnd1 = rgb2luma(...) - lumaLocalAverage;
           reached1 = abs(lumaEnd1) >= gradientScaled;
       }
       if (!reached2) {
           uv2 += vec3(offset * QUALITY(i), 0.0);
           lumaEnd2 = rgb2luma(...) - lumaLocalAverage;
           reached2 = abs(lumaEnd2) >= gradientScaled;
       }
   }
   ```

   **Strategy**: Expand search from center until luma gradient exceeds `0.25 * max_gradient`. Step size increases exponentially (1, 1, 2, 4, 8...) to trade off precision vs. cost.

4. **Sub-Pixel Blending** (lines 666–689):
   ```glsl
   float pixelOffset = -distanceFinal / edgeThickness + 0.5;

   float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
   float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
   float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * 0.75;  // SUBPIXEL_QUALITY=0.75

   float finalOffset = max(pixelOffset, subPixelOffsetFinal);

   // Sample final color at offset UV
   vec3 finalColor = textureLod(source_color, finalUv, 0.0).xyz * exposure;
   ```

   **Purpose**: Apply a second-order smoothing to handle sub-pixel details and reduce aliasing on shallow edges.

### 4.3 FXAA Parameters

**Compile-time constants** (not runtime-configurable):
```glsl
const float EDGE_THRESHOLD_MIN = 0.0312;     // Absolute edge minimum
const float EDGE_THRESHOLD_MAX = 0.125;      // Relative to max luma (12.5%)
const int ITERATIONS = 12;                   // Max search iterations
const float SUBPIXEL_QUALITY = 0.75;         // Sub-pixel offset weight (0–1)
```

**No runtime parameters** — FXAA is all-or-nothing via the `FLAG_USE_FXAA` flag.

### 4.4 Comparison to SMAA (Subpixel Morphological AA)

Godot also supports **SMAA** (separate from tone mapper), which uses:
- **Edge detection** → produce edge texture
- **Weight calculation** → lookup LUT-based weights (search & area textures)
- **Blending** → apply weights

SMAA is **higher quality but costlier** than FXAA (3-pass algorithm vs. 1-pass single pixel neighborhood).

---

## 5. POST-PROCESSING ROUTING & BYPASS LOGIC

### 5.1 When Tonemap is Disabled (LINEAR Mode)

Setting `environment.tonemap_mode = ENV_TONE_MAPPER_LINEAR`:

1. **Shader branch** (line 247-248):
   ```glsl
   if (params.tonemapper == TONEMAPPER_LINEAR) {
       return color;  // No tone curve
   }
   ```

2. **Pipeline still active**:
   - Exposure × applied ✓
   - FXAA applied ✓
   - Bloom applied ✓
   - BCS applied ✓
   - sRGB encoding applied ✓
   - Debanding applied ✓

3. **Result**: HDR → LDR with linear color response (no perceptual compression)

### 5.2 When FXAA is Disabled

Setting `use_fxaa = false`:

```cpp
tonemap.push_constant.flags &= ~TONEMAP_FLAG_USE_FXAA;
```

Shader skips the `do_fxaa()` call entirely (lines 873–876). **No performance cost** — the branch is compile-time constant-folded.

### 5.3 When Bloom is Disabled

Setting `use_glow = false`:

```cpp
tonemap.push_constant.flags &= ~TONEMAP_FLAG_USE_GLOW;
```

Glow gather & blending skipped (lines 878–889, 897–907).

### 5.4 Disable Everything (Linear Pass-Through)

Settings:
```cpp
TonemapSettings settings;
settings.tonemap_mode = ENV_TONE_MAPPER_LINEAR;
settings.use_fxaa = false;
settings.use_glow = false;
settings.use_bcs = false;
settings.convert_to_srgb = false;  // Output stays linear (still written as float or normalized int)
```

**Behavior**: Nearly a pass-through, with only:
- Exposure applied
- Clamping to `output_max_value`
- sRGB encoding (if final target requires it)

This is useful for **intermediate texture writing** or **linear-space rendering with no post-processing**.

---

## 6. ARCHITECTURE LESSONS FOR UZENGINE

### 6.1 Specialization vs. Runtime Branching

**Godot's choice**: Separate **compilation variants** for glow/LUT features (static), but **runtime branching** for tone mapper algorithm selection.

**Why split**:
- Glow requires texture reads that are spatially-dependent → compile-time optimization (mipmap paths, multiview paths)
- Tone mapper algorithm is **purely computational** (no texture-dependent branching) → safe to runtime branch

**UzEngine adaptation**:
```cpp
// In tone_pass.cpp
enum TonemapVariant {
    VARIANT_NORMAL,           // No glow, no LUT
    VARIANT_WITH_GLOW,        // Bloom pyramid
    VARIANT_WITH_COLOR_LUT,   // 3D color correction
    VARIANT_GLOW_AND_LUT,
    VARIANT_COUNT
};

// Compile 4 distinct shaders (variants)
// Each shader has runtime switch on: LINEAR, REINHARD, FILMIC, ACES, AGX

struct TonemapPushConstants {
    float exposure, white, output_max_value;
    uint32_t tonemapper_mode;  // 0–4
    float tonemapper_params[4];
    uint32_t flags;  // USE_FXAA, USE_BCS, etc.
    // ...
};
```

### 6.2 Integrated FXAA vs. Separate Pass

**Godot**: Single unified post-process pass (more cache-friendly, fewer render targets).

**Pros**:
- One framebuffer bind, one render pass
- FXAA reads source color at low cost (already in GPU cache)
- Fewer barriers/transitions

**Cons**:
- Can't easily insert other passes between FXAA and tonemap

**UzEngine adaptation**: Implement FXAA inside tone_pass shader rather than separate. If advanced AA (SMAA/TAA) needed, make it a separate optional pre-pass.

### 6.3 Exposure Controls

**Pattern**: Single unified `exposure` push constant applied uniformly:
```cpp
color.rgb *= exposure;  // Before any algorithmic processing
```

Then algorithms (Reinhard, ACES, AgX) use `white` point to normalize the curve. This decouples:
- **Exposure** = scene brightness adjustment (user-facing)
- **White point** = tone curve calibration (per-algorithm)

**UzEngine adaptation**: Don't bake exposure into individual tone curve parameters. Keep it separate:
```cpp
// tone_pass shader
color.rgb *= push_constants.exposure;
color.rgb = apply_tonemapper(color.rgb, push_constants.tonemapper_params);
```

### 6.4 Linear Mode Implementation

**Lesson**: "No tonemap" is not a bypass; it's a shader branch that skips the curve but **keeps all other post-processing**.

This allows:
- Linear color control via exposure + BCS
- Comparisons with/without tone curves by toggling a single enum
- No special-case code paths

**UzEngine adaptation**: Always route through tone mapper shader, even when LINEAR. Don't create a separate "no-op pass":
```glsl
// ✗ Wrong: separate bypass
if (use_tonemap) {
    renderpass_tonemap(...);
} else {
    // Copy pass or nothing
}

// ✓ Correct: single pass with algorithm selection
renderpass_tonemap(..., tonemapper_mode: LINEAR);
```

### 6.5 Auto-Exposure Integration

**Godot pattern**:
```cpp
float exposure = params.exposure;
if (use_auto_exposure) {
    exposure *= 1.0 / (exposure_texture[0,0] * scale);
}
color *= exposure;
```

**Single read** of pre-computed exposure value (1×1 texture from prior luminance pass). Allows dynamic exposure without per-frame branching.

**UzEngine adaptation**:
```cpp
// Compute 1x1 average luminance via compute shader
// Store in dedicated UAV[exposure_texture]
// Read as single texelFetch in tone pass
struct AutoExposureSettings {
    float target_luminance;
    float adaptation_speed;
    // ... (computed off-screen)
};
```

---

## 7. IMPLEMENTATION CHECKLIST FOR UZENGINE

### Required Files:

1. **`src/engine/renderer/post_process/tone_mapper.h/cpp`**
   - Struct `TonemapPushConstants` (mimic Godot's layout, but optimized for DX12)
   - Enum `TonemapMode` (LINEAR, REINHARD, FILMIC, ACES, AGX)
   - `class ToneMapper` with method `apply(RID src_color, RID dst_framebuffer, const TonemapSettings&)`

2. **`src/engine/renderer/shaders/post_process/tone_map.hlsl`**
   - All 5 tonemapping functions (Reinhard, Filmic, ACES, AgX, allenwp_curve)
   - FXAA 3.11 implementation (port from Godot)
   - Exposure, white-point, BCS, sRGB pipeline

3. **`src/engine/renderer/post_process/fxaa.h`** (optional, if separating FXAA)
   - Or embed directly in tone_map.hlsl

### Parameters to Expose (C++):

```cpp
struct TonemapSettings {
    TonemapMode mode = LINEAR;

    float exposure = 1.0f;          // EV scale
    float white_point = 1.0f;       // Tone curve reference
    float output_max = 1.0f;        // SDR clamp

    float brightness = 1.0f;        // BCS
    float contrast = 1.0f;
    float saturation = 1.0f;

    float tonemapper_params[4] = {0, 0, 0, 0};

    bool use_fxaa = true;
    bool use_auto_exposure = false;
    bool convert_to_srgb = true;

    RID glow_texture;               // Bloom
    float glow_intensity = 0.3f;

    RID color_correction_lut;       // 3D LUT
};
```

### Shader Compilation Strategy:

Compile once per variant (4–8 PSOs), not per algorithm:
```cpp
RhiComputePipelineDesc pso;
pso.shader = "tone_map.hlsl";
pso.defines.push_back("-DUSE_GLOW");    // Variant 1
pso.defines.push_back("-DUSE_COLOR_LUT"); // Variant 2
// etc.

// At runtime, push `tonemapper_mode` enum to choose algorithm
```

---

## 8. REFERENCES & CITATIONS

### Tone Mapping Papers:
1. Reinhard et al. (2002) — "Photographic Tone Reproduction for Digital Images"
   - https://doi.org/10.1145/566570.566575

2. Hable, J. (2010) — "Uncharted 2 Tone Mapping Curve" (GDC talk)
   - https://gdcvault.com/play/1012459

3. MJP (Multiple Importance Sampling) — ACES HLSL implementation
   - https://github.com/TheRealMJP/BakingLab

4. Eary Chow — AgX tonemapping curve (Blender-compatible)
   - https://github.com/EaryChow/AgX_LUT_Gen

### Antialiasing:
1. NVIDIA FXAA 3.11 — Timothy Lottes, 2014
   - http://developer.nvidia.com/

2. SMAA — Jorge Jimenez (Activision)
   - http://www.iryoku.com/smaa/

### Godot 4 Source:
- Tone mapper header: `E:/godot/servers/rendering/renderer_rd/effects/tone_mapper.h` (217 lines)
- Tone mapper impl: `E:/godot/servers/rendering/renderer_rd/effects/tone_mapper.cpp` (400 lines)
- Tone mapper shader: `E:/godot/servers/rendering/renderer_rd/shaders/effects/tonemap.glsl` (950 lines)
- Mobile version: `tonemap_mobile.glsl` (850 lines)
- SMAA: `E:/godot/servers/rendering/renderer_rd/effects/smaa.cpp`, `smaa_*.glsl`

---

## Summary Table

| Feature | Implementation | Control | Cost |
|---------|---|---|---|
| **Linear (no tonemap)** | Shader branch (return color) | `tonemapper = LINEAR` | 0 cycles, still applies exposure/BCS |
| **Reinhard** | Extended formula with white² | `white`, `output_max` | 5 ops (div, add, mul) |
| **Filmic** | Uncharted 2 cubic curve | `white` scaling param | 8 ops (mul, add, div) |
| **ACES** | RRT + ODT + tone curve | `white` scaling param | 12 ops (matmul, curve, matmul) |
| **AgX** | Rec. 2020 + allenwp curve | 4D params (contrast, toe, shoulder, width) | 18 ops (matmul, sigmoid, matmul) |
| **FXAA** | Edge detection + iterative search | `use_fxaa` flag | 30–50 texel reads (early exit <20 if no edges) |
| **Glow/Bloom** | Bicubic pyramid sample + blend modes | `glow_intensity`, `glow_mode` | 20–40 texel reads |
| **BCS** | Brightness × , contrast mix, saturation mix | `bcs[3]` | 5 ops (mul, sRGB encode, mix) |

---

**Generated**: 2026-03-13
**Source**: Godot 4 commit ~a79fde7 (March 2025)
