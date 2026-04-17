<!-- doc-meta -->
# UE5 Glass/Transmission & Metallic Material Analysis

**Authors**: UE5 5.6 source analysis  
**Date**: 2026-04-04  
**Scope**: Transmission/glass shading model, metallic encoding, specular parameter  

---

## 1. TRANSMISSION/GLASS IN UE5's DEFERRED PIPELINE

### 1.1 Shading Model: SHADINGMODELID_THIN_TRANSLUCENT

**Location**: `Engine/Shaders/Private/ShadingModels.ush:945-946`

```hlsl
case SHADINGMODELID_THIN_TRANSLUCENT:
    return DefaultLitBxDF( GBuffer, N, V, L, Falloff, NoL, AreaLight, Shadow );
```

**Key Finding**: Thin translucent materials **reuse the DefaultLitBxDF shader** (same as regular PBR lit materials). There is NO separate transmission shading in the deferred pass. The transmission is handled through **dual-blend rendering in a FORWARD/TRANSLUCENT pass**, not in deferred lighting.

### 1.2 Thin Translucent Implementation: Dual-Blend Forward Pass

**Location**: `Engine/Shaders/Private/ThinTranslucentCommon.ush`

UE5 uses **dual-blend OIT (Order-Independent Transparency)** for glass/thin translucent materials:

```hlsl
void AccumulateThinTranslucentModel(
    inout float3 DualBlendSurfaceLuminancePostCoverage,
    inout float3 DualBlendSurfaceTransmittancePreCoverage,
    inout float DualBlendSurfaceCoverage,
    FMaterialPixelParameters MaterialParams,
    FGBufferData GBuffer,
    float3 DiffuseColor,
    float3 SpecularColor,
    float3 EmissiveColor,
    float TopMaterialCoverage)
{
    const float3 N = MaterialParams.WorldNormal;
    const float3 V = MaterialParams.CameraVector;
    
    // Fresnel at both entry and exit surfaces
    const float3 FresnelRatio = F_Schlick(GBuffer.SpecularColor, NoV);
    
    // Transmittance through the glass (absorption model)
    float3 TransmittanceColor = GetThinTranslucentMaterialOutput0(MaterialParams);
    float PathLength = rcp(NoV);  // Account for viewing angle
    float3 NegativeAbsorptionCoefficient = log(TransmittanceColor);
    float3 BottomMaterialTransmittance = exp(NegativeAbsorptionCoefficient * PathLength);
    
    // Apply Fresnel twice (entry + exit)
    Transmittance = (1 - FresnelRatio) * BottomMaterialTransmittance * (1 - FresnelRatio);
    
    // Output for dual-blend blending
    DualBlendSurfaceLuminancePostCoverage = SurfaceColor * DualBlendSurfaceCoverage;
    DualBlendSurfaceTransmittancePreCoverage = Transmittance;
}
```

**Materials flow**:
1. **Deferred pass** (BasePass): If material is `SHADINGMODELID_THIN_TRANSLUCENT`, GBuffer is written with regular lit properties (albedo, normal, metallic, roughness)
2. **Translucent forward pass** (after deferred lighting): `AccumulateThinTranslucentModel()` reads the material and accumulates with dual-blend OIT
3. **No refraction distortion** in this shader — pure transparency + absorption model

### 1.3 GBuffer Storage for Thin Translucent

**Location**: `ShadingModelsMaterial.ush:12-34`

The thin translucent shading model stores **no special CustomData** (unlike subsurface or clear coat):

```cpp
void SetGBufferForShadingModel(
    inout FGBufferData GBuffer,
    inout FMaterialPixelParameters MaterialParameters,
    FPixelMaterialInputs PixelMaterialInputs,
    const float Opacity,
    const half3 BaseColor,
    const half  Metallic,
    const half  Specular,
    const float Roughness,
    const float Anisotropy,
    // ... (subsurface params ignored for thin_translucent)
    const uint ShadingModel)
{
    GBuffer.WorldNormal = MaterialParameters.WorldNormal;
    GBuffer.BaseColor = BaseColor;
    GBuffer.Metallic = Metallic;           // Stored (R8 in GBufferB.r)
    GBuffer.Specular = Specular;           // Stored (R8 in GBufferB.g)
    GBuffer.Roughness = Roughness;         // Stored (R8 in GBufferB.b)
    GBuffer.Anisotropy = Anisotropy;
    GBuffer.ShadingModelID = ShadingModel;
    // CustomData = 0 for thin translucent (no extra channels used)
}
```

**No CustomData encoding** — thin translucent relies on material editor inputs at render time.

### 1.4 Material Inputs for Glass

**Material system inputs**:
- `GetThinTranslucentMaterialOutput0(MaterialParams)` — **TransmittanceColor** (RGB, the color you see when looking through glass)
- `GetThinTranslucentMaterialOutput1(MaterialParams)` — **SurfaceCoverage** (opacity, 0-1)
- Standard PBR inputs: BaseColor, Metallic, Specular, Roughness, Normal

**Typical glass material**:
- TransmittanceColor: `(1.0, 1.0, 1.0)` for clear glass, or `(0.9, 0.95, 0.98)` for slight tint
- Metallic: `0.0` (glass is dielectric)
- Specular: `0.5` (standard dielectric, F0 = 0.04)
- Roughness: `0.0-0.1` (clear glass is very smooth)
- Normal: flat/smooth for optical clarity

---

## 2. METALLIC MATERIAL ENCODING IN GBUFFER

### 2.1 Metallic Precision: Half (R8)

**Location**: `DeferredShadingCommon.ush:333-387` (FGBufferData struct)

```cpp
struct FGBufferData {
    // ... other fields ...
    half Metallic;           // 0..1, line 346
    half Specular;           // 0..1, line 348
    float4 CustomData;       // line 350
    half Roughness;          // 0..1, line 359
    // ...
};
```

**Storage**: `half` = **16-bit float** (R16F precision)

**GBuffer Layout** (PC deferred):
| Channel | Format | Data | Bits |
|---------|--------|------|------|
| GBufferB.r | R8_UNORM | Metallic | 8 |
| GBufferB.g | R8_UNORM | Specular | 8 |
| GBufferB.b | R8_UNORM | Roughness | 8 |
| GBufferB.a | R8_UNORM | AO/ShadingModelID | 8 |

**Location**: `DeferredShadingCommon.ush:551-553` (encode)

```hlsl
OutGBufferB.r = GBuffer.Metallic;
OutGBufferB.g = GBuffer.Specular;
OutGBufferB.b = GBuffer.Roughness;
```

**Location**: `DeferredShadingCommon.ush:648-650` (decode)

```hlsl
GBuffer.Metallic  = InGBufferB.r;
GBuffer.Specular  = InGBufferB.g;
GBuffer.Roughness = InGBufferB.b;
```

### 2.2 Example: Metallic Sphere (metallic=1.0, roughness=0.4)

**Encoding**:
- `Metallic = 1.0` → `OutGBufferB.r = 1.0` → writes `255` in R8 channel
- `Roughness = 0.4` → `OutGBufferB.b = 0.4` → writes `102` in R8 channel (0.4 * 255)

**Decoding** (in deferred lighting pass):
- `GBuffer.Metallic = InGBufferB.r` → reads back `1.0` ✓
- `GBuffer.Roughness = InGBufferB.b` → reads back `0.4` ✓

**Precision loss**: R8 quantization = ±1/256 ≈ ±0.004 (acceptable for visual quality)

### 2.3 Metallic in Shading: ComputeF0

**Location**: `ShadingCommon.ush:114-151`

```hlsl
// Dielectric Fresnel for non-metallic
half DielectricSpecularToF0(half Specular)
{
    return half(0.08f * Specular);  // F0 = 0.08 * Specular input
}

// Compute F0 based on metallic parameter
half3 ComputeF0(half Specular, half3 BaseColor, half Metallic)
{
    // Metallic = 0: use dielectric F0 (0.08 * Specular)
    // Metallic = 1: use BaseColor as F0 (metallic response)
    // Metallic = 0.5: blend between both
    return lerp(DielectricSpecularToF0(Specular).xxx, BaseColor, Metallic);
}
```

**Example: Metallic Sphere with F0 calculation**

| Property | Value | Calculation |
|----------|-------|-------------|
| Metallic | 1.0 | — |
| Specular | 0.5 | — |
| BaseColor | (0.8, 0.8, 0.8) | gray metal |
| **F0** | (0.8, 0.8, 0.8) | `lerp(0.08*0.5, 0.8, 1.0)` = BaseColor |

Non-metallic example:

| Property | Value | Calculation |
|----------|-------|-------------|
| Metallic | 0.0 | — |
| Specular | 0.5 | — |
| BaseColor | (0.8, 0.5, 0.3) | orange plastic |
| **F0** | (0.04, 0.04, 0.04) | `lerp(0.08*0.5, _, 0.0)` = (0.04, 0.04, 0.04) |

---

## 3. SPECULAR PARAMETER IN UE5

### 3.1 Specular Input Range & Default

**Location**: Material editor input

- **Range**: 0.0 to 1.0
- **Default**: 0.5
- **Physical interpretation**: Dielectric reflectivity control

### 3.2 F0 Computation Formula: 0.08 * Specular

**Location**: `ShadingCommon.ush:116`

```hlsl
return half(0.08f * Specular);
```

**F0 Mapping**:
| Specular Input | F0 Value | Material | Note |
|---|---|---|---|
| 0.0 | 0.0 | No reflection | Unphysical |
| 0.5 | 0.04 | Dielectric (standard) | Most materials (plastic, fabric, concrete) |
| 1.0 | 0.08 | High-reflectivity dielectric | Water, polished ceramics |

**Physical basis**: Most non-metallic materials have F0 ≈ 0.02–0.08 depending on density. UE5 uses 0.08 as the "max dielectric" and scales it with the Specular slider. The factor **0.08** is chosen empirically for artist control.

### 3.3 Energy Conservation & Usage

The Specular parameter **only affects non-metallic materials**:

```hlsl
// When Metallic = 0 (dielectric)
F0 = lerp(0.08 * Specular, BaseColor, 0)
   = 0.08 * Specular

// When Metallic = 1 (metal)
F0 = lerp(0.08 * Specular, BaseColor, 1)
   = BaseColor  // Specular parameter ignored
```

**Practical defaults**:
- **Glass**: Specular = 0.5, F0 = 0.04 (standard dielectric)
- **Water**: Specular = 1.0, F0 = 0.08 (higher reflectivity)
- **Plastic**: Specular = 0.5, F0 = 0.04
- **Metal**: Specular = ignored, F0 = BaseColor (typically 0.3–0.9 for RGB color)

---

## 4. PRACTICAL RECOMMENDATIONS FOR UZENGINE

### 4.1 Glass Sphere in Deferred Renderer

**Problem**: UzEngine uses deferred rendering. Glass requires transparency (can't be opaque).

**Solution A: Hybrid Approach (Recommended for Demo)**

1. **Separate forward pass for translucent objects**
   - Keep deferred pipeline for opaque/masked materials
   - Add a translucent/forward pass that runs AFTER deferred lighting
   - Render glass spheres with dual-blend blending (using additive + multiplicative blend factors)

2. **Material setup for glass sphere**:
   ```cpp
   // In C++
   MaterialComponent material;
   material.albedo = float3(0.8, 0.8, 0.8);     // slight tint
   material.metallic = 0.0;
   material.specular = 0.5;                      // F0 = 0.04
   material.roughness = 0.01;                    // very smooth
   material.transmission_color = float3(1.0, 1.0, 1.0);  // clear
   material.opacity = 1.0;
   ```

3. **Shader implementation**:
   ```hlsl
   // glass_ps.hlsl
   float3 F0 = 0.08 * material.specular;  // 0.04 for glass
   
   // Fresnel-Schlick
   float3 F = F0 + (1.0 - F0) * pow(1.0 - NoV, 5.0);
   
   // Specular reflection
   float3 spec = D_GGX(roughness, NoH) * F * V_SmithJointApprox(NoV, NoL, roughness);
   
   // Transmittance (from background)
   float3 transmittance = (1.0 - F) * (1.0 - F);  // Fresnel at both surfaces
   
   // Output for dual-blend
   float3 surface_color = spec;
   float3 transmittance_color = transmittance;
   ```

### 4.2 Faking Glass in Deferred (High Specular + Environment)

If a separate translucent pass is not feasible:

1. **Use high specular (Specular = 1.0, F0 = 0.08)**
2. **Very low roughness (0.01–0.05)**
3. **High metallic = 0 to keep F0 as dielectric**
4. **Albedo = dark gray (0.1, 0.1, 0.1)** to minimize diffuse
5. **Rely on environment reflections** (IBL) for glass appearance
6. **Result**: Looks like polished metal, but close enough for a showcase

**Limitation**: No transmission (can't see through to background). Only reflections visible.

### 4.3 GBuffer Encoding for UzEngine

**Adopt UE5's layout**:
| Target | Field | Precision | Format |
|--------|-------|-----------|--------|
| GBufferB.r | Metallic | R8 | UNORM |
| GBufferB.g | Specular | R8 | UNORM |
| GBufferB.b | Roughness | R8 | UNORM |
| GBufferB.a | AO / ShadingID | R8 | UNORM |

**Do NOT pack metallic+roughness into 16-bit channels** — UE5 uses separate R8 channels for clarity and artist control.

### 4.4 F0 Formula: 0.08 * Specular

**Implement exactly as UE5**:

```cpp
// In constant buffer
float f0_scale = 0.08f;  // Hardcoded, matches UE5

// In shader
float f0_dielectric = f0_scale * specular;  // 0.08 * 0.5 = 0.04 for glass
float3 f0_metal = base_color;

float3 f0 = lerp(f0_dielectric.xxx, f0_metal, metallic);
```

**Do NOT change the 0.08 constant** — it's part of UE5's material authoring contract. Artists expect Specular=0.5 to yield F0=0.04.

### 4.5 Clear Coat for Glass Layering (Optional)

UE5's clear coat shading model could be used for glass with additional layers:

```hlsl
// Clear coat layer (glass surface)
float clear_coat = 1.0;
float clear_coat_roughness = 0.01;

// Bottom layer (tinted material)
float3 base_color_tinted = lerp(base_color, float3(1, 1, 1), 0.2);
```

Not needed for a simple glass sphere, but useful for car paint or protective coatings.

---

## 5. COMPARISON: UE5 vs UZENGINE

| Feature | UE5 (Deferred) | UzEngine (Current) | Recommendation |
|---------|---|---|---|
| Glass rendering | Forward/translucent pass + dual-blend OIT | Deferred only | Add translucent pass |
| Metallic storage | R8 GBufferB.r | Unknown | Adopt R8 format |
| Specular formula | F0 = 0.08 * Specular | Unknown | Adopt 0.08 factor |
| Thin translucent shading | Custom shader + material inputs | N/A | Implement ThinTranslucent pass |
| Fresnel at glass | F_Schlick using GBuffer.SpecularColor | Unknown | Adopt Schlick with F0 computation |

---

## 6. FILES TO REFERENCE (UE5 Source)

| File | Lines | Content |
|------|-------|---------|
| `ShadingModels.ush` | 945–946 | Thin translucent case (uses DefaultLitBxDF) |
| `ThinTranslucentCommon.ush` | 10–75 | Dual-blend transmission shader |
| `ShadingCommon.ush` | 114–151 | DielectricSpecularToF0 + ComputeF0 formulas |
| `ShadingModelsMaterial.ush` | 12–34 | GBuffer encoding for thin translucent (no CustomData) |
| `DeferredShadingCommon.ush` | 333–387 | FGBufferData struct (metallic, specular, roughness as half) |
| `DeferredShadingCommon.ush` | 551–553 | Encode metallic/specular/roughness to R8 GBufferB |
| `DeferredShadingCommon.ush` | 648–650 | Decode metallic/specular/roughness from GBufferB |

---

## 7. KEY TAKEAWAYS FOR MATERIAL SHOWCASE

1. **Glass sphere is NOT deferred-only** — UE5 uses a separate forward/translucent pass
2. **Metallic storage is R8 per channel** — not packed, independent quantization
3. **Specular parameter controls F0 for dielectrics**: F0 = 0.08 * Specular (standard: 0.5 → 0.04)
4. **Thin translucent uses dual-blend OIT** — not physically-based refraction, but perceptually correct
5. **For UzEngine showcase**: Either add a translucent pass, or fake glass with high specular + environment reflections

