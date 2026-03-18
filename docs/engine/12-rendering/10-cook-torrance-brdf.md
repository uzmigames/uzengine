# Cook-Torrance BRDF — PBR Lighting Model

> **Status**: Implemented (Milestone 1.2)
> **Source**: `runtime/shaders/runtime/mesh_ps.hlsl`
> **Depends on**: PBR Material System (§06), Forward PBR Pass (§08)
> **References**: "Real Shading in Unreal Engine 4" (Karis 2013), "Physically Based Rendering" (Pharr et al.)

## Purpose

The Cook-Torrance BRDF (Bidirectional Reflectance Distribution Function) is a physically-based lighting model that describes how light reflects from a surface. It combines:

1. **Fresnel effect** — surface reflectance depends on viewing angle
2. **Microfacet geometry** — tiny surface imperfections scatter light
3. **Energy conservation** — diffuse + specular reflectance ≤ 1

The result is realistic, consistent, and predictable lighting that works across all material types (metal, plastic, cloth, skin).

## High-Level Formula

```
Lo(v) = ∫ BRDF(l, v) * Li(l) * (n·l) dl

BRDF = kD * (c/π) + kS * (D*G*F) / (4*(n·v)*(n·l))
```

Where:

- **Lo(v)** = outgoing radiance in view direction v
- **BRDF** = split into diffuse (kD) and specular (kS) lobes
- **c** = albedo color
- **D** = normal distribution function (GGX)
- **G** = geometry masking/shadowing function (Smith)
- **F** = Fresnel reflectance
- **n·l, n·v** = cosines of normal and light/view directions

## Microfacet Model — GGX (Trowbridge-Reitz)

### Normal Distribution Function (NDF)

The GGX NDF describes the statistical distribution of microfacet normals on the surface. A rougher surface has a wider distribution (broader specular lobe); a smoother surface has a tighter distribution (sharper peak).

#### Mathematical Definition

```
D(h) = α² / (π * ((n·h)² * (α² - 1) + 1)²)
```

Where:

- **h** = half-vector between view and light directions: h = normalize(v + l)
- **n·h** = dot(n, h) ∈ [0, 1], cosine between normal and half-vector
- **α** = roughness parameter (alpha) = perceptual_roughness²

#### Intuition

- **α = 0.01** (very smooth) → D is a tight, bright peak near n·h ≈ 1 → mirror-like highlights
- **α = 0.5** (medium rough) → D is moderate width → visible but spread specular
- **α = 1.0** (very rough) → D is very flat → diffuse-like appearance

#### HLSL Implementation

```hlsl
float DistributionGGX(float NdotH, float alpha) {
    float a2    = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}
```

**Why this formulation?**

- Avoids division by zero (denom ≥ 1)
- Pre-square alpha: more efficient than `sqrt()` in shader
- Mathematically equivalent to Beckmann NDF but less sensitive to numerical precision

### Roughness Remapping

The material stores **perceptual roughness** (0–1, intuitive for artists), but the shader remaps it to alpha:

```cpp
// C++ side: material stores perceptual roughness
float roughness = 0.5f;

// HLSL side: remap to alpha
float alpha = roughness * roughness;  // Squared for Disney convention
```

**Why the square?** Perceptual roughness is nonlinear — artists expect roughness 0.5 to look "matte mid-tone", not pure diffuse. Squaring it creates a perceptually linear relationship.

### Clamping Minimum Roughness

To prevent the specular lobe from collapsing to a delta function:

```hlsl
static const float MIN_ROUGHNESS = 0.04;

float alpha = max(roughness * roughness, MIN_ROUGHNESS * MIN_ROUGHNESS);
```

Without this clamp:
- Very low roughness → NDF spike → potential NaN/Inf from division
- Metallic surfaces would become perfectly reflective (unrealistic)

**Typical MIN_ROUGHNESS**: 0.04 (roughness 0.2), allowing mirror-like highlights but preventing singularities.

## Geometry Function — Smith GGX

### Conceptual Overview

The geometry function **G** represents the fraction of microfacets visible from both the view and light directions simultaneously. Rough surfaces self-shadow more (lower G); smooth surfaces self-shadow less (higher G).

The **Smith form** factors G into two independent terms:

```
G(l, v) = G1(l) * G1(v)
```

Each G1 represents visibility in one direction (either light or view).

### Schlick-GGX Single-Direction Term

```hlsl
float GeometrySchlickGGX(float NdotV, float alpha) {
    // k remapping for direct (analytic) lighting per Karis 2013
    float k = alpha / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
```

**Parameters**:

- **NdotV** = saturate(dot(n, direction)) where direction is either light or view
- **alpha** = roughness²
- **k** = remapping factor for direct lighting (Karis 2013 convention)

**Behavior**:

- NdotV → 0 (grazing angle) → G1 → 0 (fully shadowed)
- NdotV → 1 (surface normal to direction) → G1 → 1 (fully visible)

### Smith Combined Form

```hlsl
float GeometrySmith(float NdotV, float NdotL, float alpha) {
    return GeometrySchlickGGX(NdotV, alpha) * GeometrySchlickGGX(NdotL, alpha);
}
```

**Meaning**: Multiply the visibility from the view direction by the visibility from the light direction. If either is blocked, the surface contribution is reduced.

### Critical: Vis_SmithJointApprox Input (Roughness⁴)

> **IMPORTANT (v0.2.35)**: As of 2026-03-18, the Smith visibility function has been corrected to match UE5 exactly. This was the root cause of over-bright specular highlights on ALL surfaces.

The visibility term in deferred lighting must receive **alpha² (roughness⁴)**, not alpha (roughness²):

```hlsl
// UE5 ShadingModels.ush:215 — correct form
float alpha = max(roughness * roughness, MIN_ROUGHNESS * MIN_ROUGHNESS);
float alphaSquared = alpha * alpha;  // THIS is what goes to Vis_SmithJointApprox
float Vis = Vis_SmithJointApprox(NdotL, NdotV, alphaSquared);
```

**Why the fourth power?** The Smith visibility formula encodes roughness² inside its denominator. If you pass alpha (which is already roughness²), the function applies the squaring a second time, resulting in roughness⁴. This causes the visibility to collapse too much, making specular highlights 2–3 stops too bright.

**Visual impact if incorrect:**
- Specular highlights appear 2–3 stops (4–8x) brighter than they should be
- Highlights are too narrow and too peaked (lack diffuse-like spread)
- Low-roughness metals look over-saturated and artificial
- All surfaces appear hyper-reflective

**Fix applied in v0.2.35:**
- `runtime/shaders/runtime/include/pbr_common.hlsli` — `Vis_SmithJointApprox()` now documents the roughness⁴ requirement
- `runtime/shaders/runtime/include/lighting.hlsli` — deferred lighting compute now passes `Pow4(roughness)` (i.e., `alpha²`)
- `runtime/shaders/runtime/postprocess/ssr_resolve_cs.hlsl` — SSR resolve updated to use the same visibility term
- @source UE5 ShadingModels.ush:215, BRDF.ush:340–360

### Visual Example

```
Smooth surface (α = 0.05):
  G ≈ 1 (minimal self-shadowing) → bright specular highlight

Rough surface (α = 0.5):
  G ≈ 0.3–0.7 (moderate self-shadowing) → darker, broader highlight

Very rough surface (α = 1.0):
  G ≈ 0.1–0.3 (strong self-shadowing) → almost invisible highlight
```

## Fresnel Effect — Schlick Approximation

### Mathematical Definition

The Fresnel effect describes how reflectance varies with viewing angle. At normal incidence (facing the surface head-on), reflectance is low (mostly refracted). At grazing angles, nearly all light is reflected.

```
F(θ) = F0 + (1 - F0) * (1 - cos θ)⁵
```

Where:

- **F0** = Fresnel reflectance at normal incidence (0° angle of incidence)
- **θ** = angle between the view direction and half-vector
- **cos θ** = v·h (view-to-half dot product)

### HLSL Implementation

```hlsl
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}
```

**Parameters**:

- **cosTheta** = dot(v, h), clamped to [0, 1]
- **F0** = base reflectance at normal incidence

### F0 Selection (Material Dependent)

The key to material variety is choosing the correct F0:

#### Dielectrics (Plastic, Stone, Wood, Ceramic)

```cpp
float3 F0 = float3(0.04, 0.04, 0.04);  // ~4% reflectance at normal incidence
```

Most non-metal materials have F0 ≈ 0.04 (matches IOR ≈ 1.5). This value is universal and produces correct results for plastics, ceramics, stone, skin, etc.

#### Metals

```cpp
float3 F0 = baseColor.rgb;  // Use albedo color as F0
```

Metals are tinted — they reflect different wavelengths differently. Gold has F0 ≈ (1, 0.84, 0), Copper ≈ (0.95, 0.64, 0.54), etc. Reuse the baseColor as the reflectance color.

### Metallic Interpolation

The **metallic** parameter blends between dielectric and metallic F0:

```hlsl
float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
```

- **metallic = 0** → pure dielectric (F0 = 0.04, has diffuse lobe)
- **metallic = 1** → pure metal (F0 = baseColor, no diffuse)

### Energy Conservation (kS + kD ≤ 1)

The Fresnel value **F** is the specular reflectance. The remaining energy goes to diffuse:

```hlsl
float3 kS = F;                      // Specular component
float3 kD = (1.0 - kS) * (1.0 - metallic);  // Diffuse component
```

**Why multiply by (1 - metallic)?** Metals have zero diffuse lobe (all light is reflected specularly). Dielectrics have both specular and diffuse.

- **Dielectric, grazing angle (F ≈ 1)** → kS ≈ 1, kD ≈ 0 (mostly specular, no diffuse)
- **Dielectric, normal angle (F ≈ 0.04)** → kS ≈ 0.04, kD ≈ 0.96 (mostly diffuse, some specular)
- **Metal, any angle** → kD = 0 (zero diffuse, always), kS = F (specular from F0 = baseColor)

## Full Pixel Shader Implementation

```hlsl
float4 main(PSInput input) : SV_Target {
    // ── Fetch material ──
    PBRMaterial mat = materials[materialIndex];
    float3 albedo = mat.baseColor.rgb;

    // ── Surface vectors ──
    float3 N = normalize(input.normalWS);
    float3 V = normalize(cameraPos.xyz - input.positionWS);
    float3 L = normalize(lightDir.xyz);  // Normalized direction toward light
    float3 H = normalize(V + L);         // Half-vector

    // ── Clamped dot products (avoid division by zero) ──
    float NdotL = max(dot(N, L), 0.001);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // ── PBR parameter preparation ──
    // Perceptual roughness → alpha (squared for Disney convention)
    float alpha = max(mat.roughness * mat.roughness,
                      MIN_ROUGHNESS * MIN_ROUGHNESS);

    // F0: reflectance at normal incidence
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, mat.metallic);

    // ── Cook-Torrance BRDF calculation ──
    float  D = DistributionGGX(NdotH, alpha);              // Normal distribution
    float  G = GeometrySmith(NdotV, NdotL, alpha);         // Geometry masking
    float3 F = FresnelSchlick(VdotH, F0);                  // Fresnel reflectance

    // Specular BRDF: D*G*F / (4 * n·v * n·l)
    float3 numerator = D * G * F;
    float  denominator = max(4.0 * NdotV * NdotL, 0.001);
    float3 specular = numerator / denominator;

    // ── Energy conservation ──
    float3 kS = F;                                          // Specular reflectance
    float3 kD = (1.0 - kS) * (1.0 - mat.metallic);         // Diffuse remainder

    // Lambertian diffuse: albedo / π (normalized for energy conservation)
    float3 diffuse = kD * albedo / PI;

    // ── Direct lighting ──
    // Outgoing radiance = BRDF * incoming radiance * cos(angle)
    float3 Lo = (diffuse + specular) * lightColor.xyz * NdotL;

    // ── Ambient lighting (constant approximation for now) ──
    float3 ambient = ambientColor.xyz * albedo * mat.ao;

    // ── Emissive contribution ──
    float3 emissive = mat.emissive.rgb * mat.emissive.a;

    // ── Combine ──
    float3 color = ambient + Lo + emissive;

    // ── Tonemap (Reinhard) + gamma correction ──
    // Reinhard tonemap: maps [0, ∞) → [0, 1) while preserving dark values
    color = color / (color + 1.0);

    // sRGB gamma correction (approximate 2.2 for R8G8B8A8_UNORM backbuffer)
    color = pow(color, 1.0 / 2.2);

    return float4(color, 1.0);
}
```

## SM6.6 vs SM5 Variants

### SM6.6 Bindless Path (Preferred)

```hlsl
// Root constants:
struct PushConstants {
    uint vertexBufferIndex;    // SRV index for vertex fetch
    uint materialBufferIndex;  // SRV index for material StructuredBuffer
};
ConstantBuffer<PushConstants> push : register(b0);

// In pixel shader:
StructuredBuffer<PBRMaterial> materials = ResourceDescriptorHeap[push.materialBufferIndex];
PBRMaterial mat = materials[materialIndex];
```

**Advantages**:

- No descriptor table switches (bindless → direct heap access)
- Lower CPU overhead
- More flexible material indexing
- Modern GPU feature (VG10+, NVIDIA Maxwell+, AMD GCN2+)

### SM5 Fallback Path

```hlsl
// Descriptor table at register(t0):
StructuredBuffer<PBRMaterial> materials : register(t0);

// In pixel shader:
PBRMaterial mat = materials[materialIndex];
```

**Trade-offs**:

- Requires descriptor table binding per-frame (CPU cost)
- Limited by descriptor table size
- Compatible with older GPUs (DX11 era)

**Shader selection at compile-time**:

```cpp
#ifdef UZ_BINDLESS
    // SM6.6 code path (above)
#else
    // SM5 code path (above)
#endif
```

## Engine Comparison

| Aspect | Unreal 5 | Godot 4 | **UzEngine 1.2** |
|--------|----------|---------|------------------|
| **NDF** | GGX (Disney remap) | GGX | **GGX (Disney, alpha²)** |
| **Geometry** | Smith GGX (k for direct) | Smith GGX | **Smith GGX (k = alpha/2)** |
| **Fresnel** | Schlick | Schlick | **Schlick (5th power)** |
| **F0 selection** | Metallic blend (0.04 → albedo) | Fixed per-material | **Metallic lerp (0.04 → albedo)** |
| **Roughness remap** | Square (perceptual) | Square | **Square (perceptual)** |
| **Min roughness clamp** | 0.016 (0.125 perceptual) | 0.04 | **0.04 (0.2 perceptual)** |
| **Diffuse** | Lambertian / PI | Lambertian / PI | **Lambertian / PI** |
| **Ambient** | IBL probes + SSAO | SSAO | **Constant (future: IBL)** |
| **Tonemap** | Filmic ACES | Reinhard | **Reinhard (simple)** |

## Mathematical Details — Why These Choices?

### Why Schlick-GGX k = alpha/2 for Direct Lighting?

The Smith GGX geometry function has different k values depending on the context:

- **Direct (analytic) lighting**: k = alpha/2 (Karis 2013 convention)
- **IBL (image-based lighting)**: k = alpha²/2 (UE4 convention)
- **Epic's "Disney" remake**: varies k by context

Our implementation uses k = alpha/2 for direct lighting, matching the industry standard from "Real Shading in Unreal Engine 4".

### Why Disney Roughness Remap (Squared)?

Perceptual roughness is subjective: artists think of roughness on a 0–1 scale where 0.5 is "typical plastic" and 1.0 is "sandpaper". The microfacet alpha parameter follows a different curve. Squaring roughness produces a perceptually linear relationship:

```
Perceptual Roughness → Alpha (Squared)
0.0 → 0.0 (perfect mirror)
0.2 → 0.04 (polished metal)
0.5 → 0.25 (typical plastic)
0.8 → 0.64 (weathered metal)
1.0 → 1.0 (sandpaper)
```

Without the square, roughness 0.1 would already look very dull (too conservative).

### Why Lambertian Diffuse / PI?

The diffuse lobe is normalized by π for **energy conservation**. The cosine term (n·l) is already baked into the lighting equation; the diffuse BRDF must not exceed the hemisphere integral to ensure total reflectance ≤ 1.

Lambertian / π integrates to 1 over the hemisphere, making it the correct normalization.

### Why Reinhard Tonemap?

Reinhard is simple and fast:

```
tone = color / (color + 1.0)
```

Maps [0, ∞) → [0, 1) while preserving dark values. More sophisticated tonemaps (ACES, Filmic) are better but require more instructions. For MVP, Reinhard is sufficient.

## Rules

1. **ALWAYS use perceptual roughness (0–1) in materials** — never use alpha directly
2. **ALWAYS clamp dot products away from zero** — prevent division by zero in G and specular calculation
3. **ALWAYS use Schlick-GGX k = alpha/2 for direct lighting** — standard convention
4. **ALWAYS saturate the half-vector dot products** — prevents NaN from pow() with negative input
5. **ALWAYS apply gamma correction before output** — R8G8B8A8_UNORM expects sRGB
6. **NEVER use raw D*G*F without the 4*n·v*n·l denominator** — violates energy conservation

## Future Enhancements

- **Normal maps** — perturb N before BRDF calculation
- **Roughness maps** — per-texel roughness variation
- **Image-based lighting (IBL)** — replace ambient constant with pre-convolved cubemap
- **Subsurface scattering** — modify diffuse term for translucent materials (skin, wax)
- **Clear coat** — dual specular lobe for car paint and varnish
- **Multiple directional lights** — loop over light array, accumulate Lo
- **Spot lights** — cone falloff in attenuation
- **Point lights** — distance attenuation with physically plausible falloff
