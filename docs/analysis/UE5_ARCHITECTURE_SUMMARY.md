# UE5.6 Rendering Architecture - Quick Reference

**Analysis Date**: 2026-03-13
**Source**: W:\UE_5.6_Source (Full UE5.6)
**Full Analysis**: `UE5_RENDERING_SYSTEMS_ANALYSIS.md` (822 lines)

---

## System Summary Table

| System | Key Implementation | Default Values | Format |
|--------|-------------------|-----------------|--------|
| **BRDF** | GGX NDF + Smith G + Burley diffuse | D=GGX, G=Smith, F=Schlick | N/A |
| **G-Buffer MRT** | 5 targets (A,B,C,D,E + Velocity) | 3-5 MSAA | PF_11G11B10, PF_A8R8G8B8, etc |
| **Shadows** | 4 cascades, 2K per cascade | 2048×2048, 16-tap PCF | D32F atlas |
| **IBL Specular** | 256×256 base, 9 mips | Mip count: 9, Quality: GGX filtered | TextureCube |
| **IBL Irradiance** | SH3 or 32×32 cubemap | SH3 RGB (9 coefficients) | RG16F LUT |
| **BRDF LUT** | Split-sum preintegration | 256×256 (NoV, Roughness) | R16G16F |
| **Tone Mapping** | ACES v1.3 film stock | Slope=0.91, Toe=0.53, Shoulder=0.23 | Log-space sigmoid |
| **Bloom** | 6-stage Gaussian pyramid | Intensity=0.675, Threshold=-1.0 | 1/2, 1/4, ... 1/64 res |
| **TAA** | Halton jitter + variance clipping | 0.5 pixel jitter, 4-frame pattern | Vel buffer RG16F |
| **DoF** | Bokeh (1K LUT) / Circle / Gaussian | Bokeh shape customizable | Depth-based CoC |
| **Auto Exposure** | Histogram (64-bin) or downsampling | LowPercent=75, HighPercent=95 | EV-based adaptation |
| **Contact Shadows** | Screen-space ray cast | 8 steps, 0.2 unit length | Per-light |

---

## G-Buffer Layout (5 MRT)

### Slot A: Scene Color + Normal.Z
- Format: PF_FloatR11G11B10 (HDR)
- RGB: Scene color (11:11:10 float)
- A: Normal.Z (10-bit, reconstructed)

### Slot B: BaseColor + AO
- Format: PF_A8R8G8B8 (sRGB)
- RGB: BaseColor (8:8:8)
- A: AmbientOcclusion (8-bit)

### Slot C: Normal.XY + Metallic + Roughness
- Format: PF_A8R8G8B8
- RG: Octahedral normal (2×8 bits)
- B: Metallic (0-1 quantized)
- A: Roughness (0-1 quantized)

### Slot D: Custom Material Data
- Format: PF_A8R8G8B8
- Usage: Subsurface color, cloth params, etc.

### Slot E: Precomputed Shadows (optional)
- Format: PF_A8R8G8B8
- RGBA: Shadow map channel masks

### Velocity (separate RT, optional)
- Format: PF_G16R16F
- RG: Motion vector (pixels/frame)

---

## BRDF Equations

### Normal Distribution (D)
```glsl
D_GGX = a² / (π * ((NoH * a² - NoH) * NoH + 1)²)
a = roughness * roughness
```

### Fresnel (F)
```glsl
F_Schlick = F0 + (1 - F0) * (1 - VoH)^5
```

### Visibility (G)
```glsl
Vis_Smith = 0.5 / (NoL * sqrt(NoV² - NoV² * a²) + a² + NoV * sqrt(NoL² - NoL² * a²) + a²)
```

### Diffuse
```glsl
f_diffuse_Burley = c / π * (1 + (FD90 - 1) * (1 - NoV)^5) * (1 + (FD90 - 1) * (1 - NoL)^5)
FD90 = 0.5 + 2 * VoH² * roughness
```

### IBL Integration (Split-Sum)
```glsl
f_IBL = (F0 * A + F_Fresnel_bias * B) * prefiltered_specular
EnvBRDF_LUT[NoV, roughness] → (A, B) scale-bias pair
```

---

## Shadow System Details

### Cascade Configuration
| Cascade | Resolution | Typical Distance |
|---------|------------|------------------|
| 0 | 2048×2048 | 0-10m (near) |
| 1 | 2048×2048 | 10-50m |
| 2 | 2048×2048 | 50-200m |
| 3 | 2048×2048 | 200m+ (far) |

### PCF Filter
- Type: Poisson disk
- Sample count: 16 taps
- Penumbra radius: Distance-scaled (0.5-4 pixels)
- Bias: Slope-scale + fixed offset

### Contact Shadows
- Algorithm: Screen-space ray cast
- Steps: 8 per light
- Length: 0.2 units (configurable)
- Used for: Occlusion under objects (feet, props)

---

## Lighting Architecture

### Light Types
```cpp
ELightType {
    Directional,  // Sun/moon, cascaded shadows
    Point,        // Omnidirectional, dual-paraboloid shadows
    Spot,         // Cone + falloff, perspective shadows
    Rect          // Area rectangle, LTC importance sampling
}
```

### Attenuation
- Formula: `1 / (1 + (distance / radius)²)`
- Cutoff: Hard radius boundary with soft fade
- Optional: IES texture profiles, custom light functions

### Clustering
- **Clustered forward+**: 16×16 tiles, per-tile light list
- **Max lights**: ~256 per tile (simple), unlimited with culling
- **Sorting**: By type, function, shadowing, complexity

---

## IBL Processing Pipeline

### 1. Input Cubemap Capture
- Resolution: 256×256 per face (or custom)
- Format: RGB16F or RGB32F (HDR)
- Source: Sky, reflection probes, or precomputed

### 2. Specular Prefiltering
```
for mip in [0, 8]:
    roughness = mip / 8
    for each texel:
        integrate 1K samples of GGX lobe
        weighted by importance sampling PDF
```
- **Mip 0**: Sharpest (roughness ≈ 0)
- **Mip 8**: Diffuse (roughness ≈ 1)

### 3. Irradiance Computation
- **SH projection**: 3rd-order (9 coefficients per channel)
- **Alternative**: 32×32 cubemap via multi-sampling
- **Cost**: Offline precompute or ~200ms compute shader

### 4. BRDF LUT Generation
```
for (NoV in [0, 256]):
    for (roughness in [0, 256]):
        integrate F·G term via Monte Carlo
        store (scale, bias) coefficients
```
- **Format**: R16G16F (scale in R, bias in G)
- **Resolution**: 256×256 (standard)
- **Precomputed offline** in most engines

---

## Post-Processing Pipeline Order

1. **G-Buffer Pass** — Write all material data
2. **Lighting Pass** — Read G-Buffer, compute direct + indirect
3. **Tone Mapping** — ACES film stock curve
4. **Color Grading** — LUT blending (shadows/midtones/highlights)
5. **Bloom** — 6-stage Gaussian downsampling + compose
6. **Auto Exposure** — Histogram or luminance adapt
7. **TAA** — Temporal reprojection + variance clipping
8. **DoF** — Bokeh/Circle/Gaussian blur
9. **Motion Blur** — Velocity buffer sampling
10. **SSAO/SSR** — Screen-space ambient/specular (if enabled)

---

## Default Parameters Cheat Sheet

### Bloom (SOG)
```
Intensity: 0.675
Threshold: -1.0 (no threshold)
SizeScale: 4.0
Filter 1 (1/2):   Size=0.3,  Tint=0.3465
Filter 2 (1/4):   Size=1.0,  Tint=0.138
Filter 3 (1/8):   Size=2.0,  Tint=0.1176
Filter 4 (1/16):  Size=10.0, Tint=0.066
Filter 5 (1/32):  Size=30.0, Tint=0.066
Filter 6 (1/64):  Size=64.0, Tint=0.061
```

### Film Tone Mapper
```
Slope: 0.91       // Film curve slope
Toe: 0.53         // Shadow bend
Shoulder: 0.23    // Highlight bend
BlackClip: 0.0    // Black compression
WhiteClip: 0.035  // White compression
```

### Auto Exposure
```
Method: Histogram (64-bin) or Basic
MinBrightness: 0.0 (EV stops)
MaxBrightness: 2.0
LowPercent: 75.0  // Dark emphasis
HighPercent: 95.0 // Bright emphasis
SpeedUp: 2.0 f-stops/sec
SpeedDown: 1.0 f-stops/sec
Bias: 0.0 (log2 offset)
```

### Color Grading (per zone)
```
Shadows:    ShadowsMax = 0.09
Highlights: HighlightsMin = 0.5, HighlightsMax = 1.0
Per zone:
  - Saturation: [0.0, 2.0]
  - Contrast: [0.0, 2.0]
  - Gamma: [0.01, 2.0]
  - Gain: [0.0, 2.0]
  - Offset: [-1.0, 1.0]
```

---

## Performance Targets (60 FPS @ 1440p)

| System | Budget (ms) | Note |
|--------|-----------|------|
| G-Buffer Pass | 1.5 | Deferred write |
| Lighting Pass | 2.0 | Clustered deferred |
| Tone + LUT | 0.3 | Post-process |
| Bloom | 0.8 | 6-stage pyramid |
| TAA | 0.5 | Temporal reprojection |
| SSAO | 0.5 | Optional, half-res |
| SSR | 0.7 | Optional, Hi-Z trace |
| **Total Graphics** | **~6.3 ms** | 16.67 ms frame budget |

---

## Key Differences from SimpleForward Renderers

| Feature | UE5 Deferred | SimpleForward |
|---------|-------------|----------------|
| **Lighting** | Deferred (lights don't branch on materials) | Forward (light × material × shadow) |
| **Shadows** | Cascaded atlas (all lights share) | Per-light shadow map |
| **BRDF** | Split-sum IBL (preintegrated) | Screen-space or hack approximation |
| **G-Buffer** | 5+ MRTs (bandwidth-heavy) | None (forward writes directly) |
| **Scaling** | 100+ dynamic lights easy | 8-16 lights before pain |
| **Quality** | Film-industry standard (ACES) | Basic tone mapping |
| **Temporal** | TAA mandatory for quality | Can use MSAA |

---

## Patterns UzEngine Should Adopt

1. **Abstraction**: RHI layer (DX12/Vulkan/Metal) — don't hardcode format names
2. **Clustering**: Forward+ for scalability to 100+ lights
3. **Preintegration**: Compute BRDF/specular mips offline, ship as assets
4. **Temporal**: TAA for effective 2K quality from 1440p input
5. **Material dispatch**: Enum in G-Buffer (SHADINGMODELID_*), not switch-on-type
6. **Screen-space**: Depth pyramid (Hi-Z) for efficient effects (SSR, AO)
7. **Adaptive**: Exposure + local contrast for cinematic look
8. **Deferred**: For PC/console, forward+ for mobile

---

## File References

Full path analysis in: `UE5_RENDERING_SYSTEMS_ANALYSIS.md`

**Key source files**:
- `W:\UE_5.6_Source\Engine\Shaders\Private\BRDF.ush` (600+ lines)
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredShadingCommon.ush` (500+ lines)
- `W:\UE_5.6_Source\Engine\Shaders\Private\DeferredLightingCommon.ush` (400+ lines)
- `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\GBufferInfo.h` (336 lines)
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Scene.h` (1000+ lines)
- `W:\UE_5.6_Source\Engine\Shaders\Private\TonemapCommon.ush` (250+ lines)

