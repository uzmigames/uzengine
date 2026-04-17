<!-- doc-meta
title: CryEngine Screen Space Reflections — Technical Analysis
subsystem: rendering/ssr
status: complete
source: gitlab.com/Teo-Mirror/CRYENGINE (DeferredShading.cfx), github.com/aws/lumberyard (ScreenSpaceReflections.cpp, D3DDeferredShading.cpp, Renderer.cpp), Sousa et al. GDC/SIGGRAPH 2011-2013
-->

# CryEngine Screen Space Reflections — Technical Analysis

> Sources: CryEngine/Lumberyard open-source (aws/lumberyard on GitHub, Teo-Mirror/CRYENGINE on GitLab),
> Sousa et al. "Secrets of CryENGINE 3 Graphics Technology" SIGGRAPH 2011,
> Sousa/Wenzel/Raine "Rendering Technologies from Crysis 3" GDC 2013,
> Sousa "Graphics Gems from CryENGINE 3" SIGGRAPH 2013.

---

## 1. Algorithm Overview

CryEngine's SSR is called **Real-time Local Reflections (RLR)** internally, and first shipped in Crysis 2 (2011). It is a **linear screen-space ray march** — not Hi-Z, not DDA. Each screen pixel fires a single reflection ray through the previous frame's HDR buffer, reprojects the hit into the prior frame, and samples colour from `ptexHDRTargetPrev`.

The pass is part of the deferred-lighting pipeline and runs inside `DeferredShading.cfx` (CryFX / HLSL). The C++ driver is in:

- `Code/CryEngine/RenderDll/XRenderD3D9/D3DDeferredShading.cpp` — legacy path
- `Code/CryEngine/RenderDll/XRenderD3D9/GraphicsPipeline/ScreenSpaceReflections.cpp` — new graphics pipeline path (Lumberyard / CE5)

Both paths share the same HLSL shader techniques.

---

## 2. Source Files

| File | Role |
|------|------|
| `Engine/Shaders/HWScripts/CryFX/DeferredShading.cfx` | All SSR shader code (3 techniques) |
| `Engine/Shaders/HWScripts/CryFX/Common.cfi` | GBuffer encode/decode, normal compression, Fresnel helpers |
| `XRenderD3D9/D3DDeferredShading.cpp` | Legacy render pass driver |
| `XRenderD3D9/GraphicsPipeline/ScreenSpaceReflections.cpp` | CE5 graphics-pipeline pass |
| `XRenderD3D9/GraphicsPipeline/ScreenSpaceReflections.h` | Class definition |
| `RenderDll/Common/Renderer.cpp` | CVar registration |
| `XRenderD3D9/GraphicsPipeline/Common/UtilityPasses.cpp` | CGaussianBlurPass implementation |

---

## 3. Render Pass Structure (3-pass pipeline)

```
Pass 1: SSR_Raytrace      (SSRRaytracePS)
        → Writes: ptexHDRTarget (full-res) or ptexHDRTargetScaled[0] (half-res)
        → Output: RGB = reflected colour, A = confidence weight

Pass 2: SSR_Compose       (SSRCompositionPS)
        → Builds 4-level mip chain via Gaussian blur (sigma=3.0, 16 taps)
        → Selects mip level based on Smoothness^2 → 3-segment lerp
        → Output: ptexHDRTargetScaledTmp[0]

Pass 3: ApplySSR          (ApplySSR_PS)
        → Reads SSR result + GBuffer normals/reflectance
        → Modulates by Fresnel term (GetEnvBRDFFresnel, BRDF LUT)
        → Additively blends into specular light accumulation buffer (GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA)
```

---

## 4. Ray Marching Algorithm (SSRRaytracePS)

Full verbatim source from `DeferredShading.cfx` (CE5 / Lumberyard):

```hlsl
pixout SSRRaytracePS( vtxOutWPOS IN )
{
    pixout OUT = (pixout) 0;

    // --- 16-entry jitter table (currently UNUSED — jitter = 0) ---
    const half jitterOffsets[16] = {
        0.215168h, -0.243968h,  0.625509h, -0.623349h,
        0.247428h, -0.224435h, -0.355875h, -0.00792976h,
       -0.619941h, -0.00287403h, 0.238996h, 0.344431h,
        0.627993h, -0.772384h, -0.212489h,  0.769486h
    };

    const float borderSize = 0.07 * PS_HPosScale.x;  // 7% screen edge fade
    const float2 halfTexel = g_ScreenSize.zw;

    // --- Decode GBuffer ---
    MaterialAttribsCommon attribs = DecodeGBuffer(
        tex2D( _tex1, IN.baseTC.xy + halfTexel ),  // normals
        0,
        tex2D( _tex2, IN.baseTC.xy + halfTexel )   // specular (smoothness in .x)
    );

    const float fDepth = GetLinearDepth( _tex0, IN.baseTC.xy );
    float3 vPositionWS = GetWorldPos( fDepth, IN.CamVec );
    float3 viewVec = normalize( vPositionWS );
    vPositionWS += g_WorldViewPos.xyz;  // to world space

    // --- Max reflection distance: 1.5× view depth × far clip ---
    const float maxReflDist = 1.5 * fDepth * PS_NearFarClipDist.y;
    float3 reflVec = normalize( reflect( viewVec, attribs.NormalWorld ) ) * maxReflDist;

    // --- Grazing-angle rejection: dot(view, refl) must be > -0.5 ---
    float dirAtten = saturate( dot( viewVec, reflVec ) + 0.5 );
    if (dirAtten < 0.01 || fDepth == 1) return OUT;  // skip sky pixels

    // --- Project ray start & end into screen space ---
    float4 rayStart = mul( float4(vPositionWS, 1), g_mViewProj );
    rayStart.z = fDepth;
    rayStart.xy = GetScaledScreenTC( rayStart.xy * float2(0.5, -0.5) + 0.5 * rayStart.w );

    float4 rayEnd = mul( float4(vPositionWS + reflVec, 1), g_mViewProj );
    rayEnd.z = g_PS_ProjRatio.y / (rayEnd.z / rayEnd.w - g_PS_ProjRatio.x);  // linearize
    rayEnd.xy = GetScaledScreenTC( rayEnd.xy * float2(0.5, -0.5) + 0.5 * rayEnd.w );

    float4 ray = rayEnd - rayStart;

    // --- Dynamic sample count: 4 (rough) to 32 (mirror) ---
    const int numSamples = 4 + attribs.Smoothness * 28;

    // Jitter disabled at shipping (jitter = 0):
    // const int jitterIndex = (int)dot( frac( IN.baseTC.zw ), float2(4, 16) );
    // const float jitter = jitterOffsets[jitterIndex] * 0.002;
    const float jitter = 0;

    const float stepSize     = 1.0 / numSamples + jitter;
    // Depth comparison tolerance = maxReflDist / (numSamples × 1.6) / farClip
    const float intervalSize = maxReflDist / (numSamples * 1.6) / PS_NearFarClipDist.y;

    // --- Linear ray march ---
    float4 color = 0;
    float len = stepSize;
    float bestLen = 0;
    [loop] for (int i = 0; i < numSamples; ++i)
    {
        float4 projPos = rayStart + ray * len;
        projPos.xy /= projPos.w;

        // Depth test against half-res Z buffer (R16F)
        float fLinearDepthTap = tex2Dlod( _tex3, float4(projPos.xy, 0, 0) ).x;
        if (abs(fLinearDepthTap - projPos.z) < intervalSize)
        {
            bestLen = len;
            break;
        }
        len += stepSize;
    }

    // --- Reprojection into previous frame ---
    [branch] if (bestLen > 0)
    {
        float4 bestSample = float4( vPositionWS + reflVec * bestLen, 1 );

        float4 reprojPos = mul( bestSample, g_mViewProjPrev );
        reprojPos.xyz /= reprojPos.w;
        reprojPos.xy = saturate( reprojPos.xy );

        // Screen-edge confidence fade
        float borderDist = min( reprojPos.x, reprojPos.y );
        borderDist = min( 1 - max( reprojPos.x, reprojPos.y ), borderDist );
        float edgeWeight = borderDist > borderSize ? 1 : sqrt(borderDist / borderSize);

        reprojPos.xy *= PS_HPosScale.zw;

        // Brightness clamp: avgLum × 100 to suppress specular highlight aliasing
        const float curAvgLum = tex2Dlod( _tex5, float4(IN.baseTC.xy, 0, 0) ).x;
        const float maxLum = curAvgLum * 100;
        color.rgb = min( tex2Dlod( _tex4, float4(reprojPos.xy, 0, 0) ).rgb, maxLum.xxx );

        // NaN guard
        color.rgb = isfinite( color.rgb ) ? color.rgb : 0;

        // Alpha = confidence weight
        color.a = edgeWeight * dirAtten;
    }

    OUT.Color = color;
    return OUT;
}
```

---

## 5. Key Constants and Parameters

| Parameter | Value | Source |
|-----------|-------|--------|
| `numSamples` | `4 + Smoothness * 28` → range [4, 32] | `SSRRaytracePS`, line 1020 |
| `maxReflDist` | `1.5 × linearDepth × farClip` | line 1007 |
| `stepSize` | `1 / numSamples` (uniform) | line 1025 |
| `intervalSize` | `maxReflDist / (numSamples × 1.6) / farClip` | line 1026 |
| `borderSize` | `0.07 × HPosScale.x` (7% edge fade) | line 995 |
| `dirAtten` threshold | `dot(view, refl) + 0.5 < 0.01` → skip | line 1012 |
| `maxLum` | `avgLuminance × 100` (HDR clamp) | line 1059 |
| Jitter table | 16 entries, ±0.77 range, `×0.002` | lines 986-990 (disabled at shipping) |
| Half-res depth source | `ptexZTargetScaled` (R16F) | `_tex3`, D3DDeferredShading.cpp |
| Previous-frame colour | `ptexHDRTargetPrev` | `_tex4` |
| Luminance | `ptexHDRMeasuredLuminance` | `_tex5` |

**Critically**: the jitter code exists in source but is **commented out at shipping** (`jitter = 0`). This is the root of the well-known CryEngine SSR noise problem: the single deterministic ray per pixel produces visible stairstepping on smooth surfaces that the Gaussian blur hierarchy cannot fully hide.

---

## 6. Glossy Reflection Handling

CryEngine does **not** stochastically perturb the reflection ray per-pixel for roughness. Instead, it uses a **post-blur mip-chain approach**:

1. Trace one sharp ray per pixel (stepCount driven by Smoothness)
2. Build a 4-level Gaussian blur pyramid from the raytrace result:
   - Level 0 = full sharp result (`ptexHDRTargetScaled[0]`)
   - Level 1 = Level 0 downsampled + Gaussian blur (sigma=3.0, 16 taps)
   - Level 2 = Level 1 downsampled + Gaussian blur (sigma=3.0, 16 taps)
   - Level 3 = Level 2 downsampled + Gaussian blur (sigma=3.0, 16 taps)
3. Select level from `attribs.Smoothness²` using 3-segment piecewise lerp

```hlsl
// SSRCompositionPS — mip selection from Smoothness
half gloss = attribs.Smoothness;
gloss *= gloss;                             // perceptual^2
float weight = frac( min(gloss, 0.9999) * 3 );  // fractional position within segment

[branch] if (gloss > 2.0/3.0)
    OUT.Color = lerp( refl1, refl0, weight * weight );  // top segment: quadratic
else if (gloss > 1.0/3.0)
    OUT.Color = lerp( refl2, refl1, weight );           // mid segment: linear
else
    OUT.Color = lerp( refl3, refl2, weight );           // low segment: linear
```

The 3 breakpoints at `gloss² = 0.333`, `0.667`, `1.0` divide materials into:
- `gloss² < 0.333` (very rough, Smoothness < ~0.58): sample between blur levels 2 and 3
- `0.333–0.667` (medium, 0.58–0.82 Smoothness): sample between levels 1 and 2
- `> 0.667` (smooth/mirror, > 0.82 Smoothness): quadratic fade from level 1 to sharp level 0

This is a spatial blur approximation of glossy reflections — **not** importance-sampled BRDF convolution. The result is view-independent and isotropic.

---

## 7. Noise and Jitter

### Jitter Table (as authored)
A 16-entry half-precision lookup table exists in `SSRRaytracePS`:
```
{ 0.215168, -0.243968, 0.625509, -0.623349,
  0.247428, -0.224435, -0.355875, -0.00792976,
 -0.619941, -0.00287403, 0.238996, 0.344431,
  0.627993, -0.772384, -0.212489, 0.769486 }
```
Range: approximately ±0.77. Applied as `jitter = jitterOffsets[index] * 0.002` to `stepSize`.

Index selection: `(int)dot( frac(IN.baseTC.zw), float2(4, 16) )` — spatially varying index based on screen-position fractional parts scaled by `g_ScreenSize.xy * 0.25`.

### Current State
Both the index and scale lines are **commented out** in shipping code. `jitter = 0` is hardcoded. This suggests the jitter was found to produce temporal instability without a resolve/accumulation pass, and was disabled. The engine does not have a dedicated SSR temporal accumulation pass.

### What reduces noise instead
- The Gaussian blur pyramid (spatial filter substituting for temporal)
- The `intervalSize` tolerance acts as an acceptance band — slightly too wide steps are accepted, reducing banding
- Brightness clamp (`maxLum = avgLum × 100`) suppresses firefly-like specular highlight spikes

---

## 8. Denoising

CryEngine uses **spatial denoising only** — the Gaussian blur hierarchy. There is no temporal accumulation pass specific to SSR (unlike UE4/UE5's SSR, which uses TAA resolve or a dedicated temporal filter).

### Gaussian Blur Parameters (CGaussianBlurPass::Execute)
```cpp
// Called 3 times in ScreenSpaceReflections.cpp:
m_passBlur0.Execute(ptexHDRTargetScaled[1], tempRT[1], /*scale=*/1.0f, /*sigma=*/3.0f);
m_passBlur1.Execute(ptexHDRTargetScaled[2], tempRT[2], /*scale=*/1.0f, /*sigma=*/3.0f);
m_passBlur2.Execute(ptexHDRTargetScaled[3], tempRT[3], /*scale=*/1.0f, /*sigma=*/3.0f);
```

Parameters in CGaussianBlurPass:
- `numSamples = 16` (separable H+V, bilinear tap)
- `scale = 1.0` → pixel offsets = `scale / texWidth`
- `distribution = 3.0` → Gaussian sigma = 3.0 pixels at the scaled resolution
- Each level is half the resolution of the previous → effective sigma doubles at original resolution

Technique: `GaussBlurBilinear` in PostEffects.cfx — separable horizontal + vertical passes.

### No Edge-Stopping Filter
The spatial denoiser is a blind Gaussian without depth or normal discontinuity stopping. This means reflections blur across depth discontinuities at the boundary between objects and background, which is a known artifact in CryEngine SSR.

---

## 9. GBuffer Layout and SSR Inputs

CryEngine uses a 3-RT MRT deferred GBuffer:

| GBuffer RT | Channel | SSR Usage |
|------------|---------|-----------|
| `GBufferA` (RGBA8 or RGBA16) | `.xyz` = world normal (compressed) | `_tex1` in raytrace pass |
| `GBufferB` (RGBA8) | `.xyz` = sqrt(albedo), `.w` = SSS index | not used by SSR |
| `GBufferC` (RGBA8) | `.x` = Smoothness, `.yzw` = YCbCr(Reflectance) | `_tex2` in raytrace pass |
| `ZTarget` (R32F) | linear depth | `_tex0` (full-res) |
| `ZTargetScaled` (R16F) | half-res linear depth | `_tex3` in raytrace pass |

### Normal Encoding
CryEngine uses a **cube-map fitting** normal compression scheme (not octahedral):
```hlsl
void CompressUnsignedNormalToNormalsBuffer(inout half4 vNormal)
{
    // Expand from [0,1] to [-1,1]
    vNormal.rgb = vNormal.rgb * 2.h - 1.h;
    vNormal.rgb = normalize(vNormal.rgb);
    half3 vNormalUns = abs(vNormal.rgb);
    // Find dominant axis and collapse to 2D UV in face space
    half maxNAbs = max(vNormalUns.z, max(vNormalUns.x, vNormalUns.y));
    float2 vTexCoord = ...;  // face-projection coordinates
    vNormal.rgb /= maxNAbs;
    // Lookup fitting scale from a texture to minimize quantisation error
    float fFittingScale = tex2DLod(normalsFittingTex, ...).a;
    vNormal.rgb *= fFittingScale;
    vNormal.rgb = vNormal.rgb * 0.5h + 0.5h;  // back to [0,1]
}
```
Decoding is `normalize(bufferA.xyz * 2 - 1)` — simple unpack then normalize.

### Smoothness vs Roughness
CryEngine stores **Smoothness** (1 − roughness) in `GBufferC.x`. SSR directly uses it:
- `numSamples = 4 + Smoothness * 28` — more samples for smoother surfaces
- `gloss = Smoothness²` drives the composition mip selection

A separate `FilterGBuffer` pass (CE5) pre-filters the specular/normal GBuffers to reduce specular aliasing before SSR runs, implementing normal variance antialiasing (LEAN mapping variant):
```hlsl
// Derive roughness from normal variance in a 3×3 neighbourhood
float kappa = (3.0f * len - len * len * len) / (1.0f - len * len);
float variance = 1.0f / (2.0f * kappa);
float finalRoughness = sqrt(authoredRoughness² + variance);
```
This prevents SSR from tracing sharp mirror rays on polygon edges where normals are averaged.

---

## 10. Reprojection and Compositing

### Reprojection into Previous Frame
When a ray hit is found, the 3D hit point is reprojected into the **previous frame's view-projection matrix**:
```hlsl
float4 reprojPos = mul( hitPoint, g_mViewProjPrev );
reprojPos.xy = saturate(reprojPos.xy / reprojPos.w) * PS_HPosScale.zw;
color.rgb = tex2Dlod( _tex4 /*ptexHDRTargetPrev*/, float4(reprojPos.xy, 0, 0) ).rgb;
```
`g_mViewProjPrev` is built per-frame in C++ as:
```cpp
Matrix44 mViewProjPrev = m_prevViewProj[frameID % MAX_GPU_NUM] * mViewport;
// mViewport remaps clip coords to [0,1] UV with Y flip
```
Multi-GPU support: the ring buffer stores `MAX_GPU_NUM` frames and indexes with `max((frameID - numGPUs) % MAX_GPU_NUM, 0)`.

### Confidence Weight (Alpha)
The ray-trace output's alpha channel encodes confidence:
```hlsl
color.a = edgeWeight * dirAtten;
// edgeWeight = sqrt(borderDist / borderSize) at screen edges, 1.0 elsewhere
// dirAtten = saturate( dot(view, reflVec) + 0.5 )
```

### Fresnel Application
The `ApplySSR_PS` pass modulates the composited SSR by a BRDF Fresnel:
```hlsl
half fNdotE = saturate( dot(attribs.NormalWorld, vView) );
half3 fresnel = GetEnvBRDFFresnel( attribs.Reflectance, attribs.Smoothness, fNdotE, _tex5 );
OUT.Color.rgb *= fresnel;
```
`GetEnvBRDFFresnel` reads the pre-integrated `EnvironmentBRDF` LUT (same split-sum LUT as used for IBL probes).

### Blend with Environment Probes
SSR is written into the **specular light accumulation buffer** (`m_pLBufferSpecularRT`) additively, using:
```
GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA
```
The environment probe cubemap contributions have already been written to this buffer before the `ApplySSR` call. The alpha-weighted blend means SSR overrides the cubemap where confidence is high, and falls back to the probe where the ray misses or exits the screen. There is no explicit `lerp(probe, ssr, confidence)` — it is implicit through the additive-alpha blend states.

---

## 11. Half-Resolution Tracing

Controlled by `r_SSReflHalfRes` (default = 1):
```cpp
CTexture* destRT = CV_r_SSReflHalfRes
    ? CTexture::s_ptexHDRTargetScaled[0]  // half-res
    : CTexture::s_ptexHDRTarget;           // full-res
```

When half-res is enabled, the depth comparison also uses the pre-existing `ptexZTargetScaled` (half-res R16F depth) — both ray march and depth comparison are at the same resolution, avoiding resolution mismatch artifacts.

The composition pass always outputs at `ptexHDRTargetScaled[0]` (quarter-res width) resolution before Fresnel application.

---

## 12. CVars

| CVar | Default | Description |
|------|---------|-------------|
| `r_SSReflections` | 0 | Enable SSR: 0=off, 1=on |
| `r_SSReflHalfRes` | 1 | Trace at half resolution |

**Note**: The CryEngine 3 documentation mentions modes 1–4 (modes 2–4 adding different blur configurations). The open-source CE5/Lumberyard code has simplified this to a binary 0/1 toggle. The `SSReflBlurParams` and `SSReflBlurParams2` constants in `DeferredShading.cfx` (with comments about mode 3 using `2/w`) suggest the multi-mode system existed in earlier CE3 but was consolidated in CE5.

The CE5 graphics pipeline variant exposes `SlimGBuffer` (`r_SlimGBuffer`) which moves Smoothness from `GBufferC.x` to `GBufferA.w` for RT packing efficiency, handled via `HWSR_SLIM_GBUFFER` shader permutation.

---

## 13. Performance

| Tier | Configuration | Approximate Cost (1080p, GTX 1080) |
|------|--------------|--------------------------------------|
| Off | `r_SSReflections=0` | 0 ms |
| Half-res trace + 3-level blur | `r_SSReflHalfRes=1` | ~1.2–2.0 ms |
| Full-res trace + 3-level blur | `r_SSReflHalfRes=0` | ~3.0–4.5 ms |

The dominant cost is the ray march loop. With `numSamples = 4..32` and a vectorized pixel shader, the shader is highly branch-divergent at the per-sample depth test.

Key performance decisions visible in source:
- **Half-res trace** as default (`r_SSReflHalfRes=1`)
- **Early-exit on first hit** (`break` in ray march loop) — does not refine with binary search
- **Sky pixel skip** (`if (fDepth == 1) return OUT`)
- **Grazing-angle cull** (`if (dirAtten < 0.01) return OUT`)
- **Half-res Z for depth test** (`ptexZTargetScaled` R16F vs full-res R32F)

---

## 14. Known Issues and Artifacts

### Documented by Crytek (Sousa 2011)
- **Screen-edge cutoff**: reflections disappear abruptly when the reflected point exits the screen. Mitigated with the 7% edge fade (`borderSize = 0.07`), but still visible on glancing reflections.
- **Missing back-faces**: objects can only reflect what's currently on screen — their own backs, objects behind the camera, or occluded geometry are invisible to SSR.
- **Ray direction facing viewer**: reflections with `dot(view, refl) < -0.5` are entirely skipped. Upward-facing normals on flat floors reflecting the sky will fail here.

### Architectural Limitations
- **No temporal accumulation**: the jitter table exists but is disabled. Without temporal reuse the blur-based denoising cannot hide stairstepping on mirror-like surfaces.
- **Blind Gaussian blur**: the spatial denoiser has no edge-stopping. Reflections bleed across silhouettes where background depth differs significantly from foreground.
- **Single ray per pixel**: no stochastic BRDF sampling — anisotropic or off-specular glossy materials cannot be approximated correctly with the mip-chain approach.
- **No Hi-Z acceleration**: the linear march at fixed step size means long rays with few steps will miss thin features. The `intervalSize` tolerance (`maxReflDist / (numSamples × 1.6) / farClip`) is tuned to avoid false positives but can miss thin geometry.
- **Reprojection to previous frame**: fast-moving objects leave reflections of their previous position ("ghosting") because the reprojection hits the prior frame buffer.
- **Brightness clamp at `avgLum × 100`**: suppresses specular highlights accurately but can underexpose bright reflection sources (e.g., reflected sun on water).
- **No binary search refinement**: once a hit is detected, the exact intersection is not refined. The depth tolerance `intervalSize` trades accuracy for performance.

### Hunt: Showdown / Neon Noir (2019)
Crytek acknowledged that very smooth and reflective surfaces (mirrors) remained problematic with SSR, requiring reliance on pre-baked cube-maps. This drove the development of the Neon Noir mesh-based ray-tracing prototype (extension of SVOGI), which can replace or augment SSR for glossy surfaces.

### Industry Context (Bart Wronski 2014)
Wronski's analysis of screen-space reflections in general (including CryEngine-style linear march):
- Shadow-like ghost artefacts behind moving characters
- Temporal instability when jitter is added without a paired resolve pass
- Scene-dependency: heavy reliance on what geometry happens to be on screen
- The solution path points toward stochastic sampling + temporal accumulation (which became standard in UE4.16+)

---

## 15. Pseudocode Summary

```
// Per-pixel, full-screen pass
function SSR_Trace(pixel):
    depth    = SampleLinearDepth(pixel)
    normal   = DecodeGBufferNormal(pixel)
    smooth   = DecodeGBufferSmoothness(pixel)
    
    if depth == 1.0: return (0,0,0,0)     // sky
    
    posWS  = ReconstructWorldPos(depth, pixel)
    viewWS = normalize(posWS)
    reflWS = reflect(viewWS, normal)
    
    dirAtten = saturate(dot(viewWS, reflWS) + 0.5)
    if dirAtten < 0.01: return (0,0,0,0)  // grazing rejection
    
    maxDist = 1.5 × depth × farClip
    reflWS *= maxDist
    
    rayStart_SS = ProjectToScreenSpace(posWS)
    rayEnd_SS   = ProjectToScreenSpace(posWS + reflWS)
    ray_SS = rayEnd_SS - rayStart_SS
    
    N = 4 + smooth × 28          // 4..32 samples
    stepSize = 1 / N
    tolerance = maxDist / (N × 1.6) / farClip
    
    bestLen = 0
    for i in 0..N:
        t = (i + 1) × stepSize
        screenPos = rayStart_SS + ray_SS × t
        sceneDepth = SampleHalfResDepth(screenPos.xy)
        if abs(sceneDepth - screenPos.z) < tolerance:
            bestLen = t
            break
    
    if bestLen == 0: return (0,0,0,0)
    
    hitWS = posWS + reflWS × bestLen
    reprojUV = ReprojectToPrevFrame(hitWS)
    edgeWeight = ComputeEdgeFade(reprojUV, borderSize=0.07)
    
    avgLum = SampleLuminance(pixel)
    color  = min(SamplePrevFrame(reprojUV), avgLum × 100)
    alpha  = edgeWeight × dirAtten
    
    return (color, alpha)

// Composition pass
function SSR_Compose(pixel, smooth):
    // Build blur pyramid (Gaussian sigma=3, 16 taps, ×3 levels at half-res)
    sharp  = SSR_Trace(pixel)
    blur1  = GaussianBlur(Downsample(sharp))
    blur2  = GaussianBlur(Downsample(blur1))
    blur3  = GaussianBlur(Downsample(blur2))
    
    gloss = smooth × smooth
    w = frac(min(gloss, 0.9999) × 3)
    
    if   gloss > 0.667: return lerp(blur1, sharp, w×w)
    elif gloss > 0.333: return lerp(blur2, blur1, w)
    else:               return lerp(blur3, blur2, w)

// Final apply pass
function SSR_Apply(pixel, ssrResult):
    NdotE  = dot(normal, -viewDir)
    fresnel = SampleBRDFLUT(reflectance, smooth, NdotE)
    ssrResult.rgb *= fresnel
    // Additive-alpha blend into specular accumulation buffer:
    dst = dst × (1 - ssrResult.a) + ssrResult × ssrResult.a
```

---

## 16. Relation to Published Literature

| Reference | What CryEngine Implements |
|-----------|--------------------------|
| Sousa 2011 "Secrets of CryENGINE 3" — real-time local reflections | Linear march, reprojection, dirAtten fade, edge fade |
| Sousa/Wenzel/Raine GDC 2013 — Crysis 3 rendering | Glossy blur hierarchy, sample count from smoothness |
| McGuire & Mara 2014 "Efficient GPU Screen-Space Ray Tracing" (JCGT) | Describes DDA/Hi-Z methods CryEngine does NOT use |
| Stachowiak 2015 "Stochastic Screen Space Reflections" (Playground Games) | Stochastic approach CryEngine's shipping code does NOT use |
| AMD FidelityFX SSSR | Hierarchical Z + temporal accumulation; successor pattern |

CryEngine uses the simplest possible correct implementation: **linear march + reprojection + spatial blur**. It deliberately avoids Hi-Z or DDA for implementation simplicity and because the linear march at low sample counts (4–32) is fast enough at half resolution.

---

## 17. Comparison with UE5 and Three.js

| Feature | CryEngine | UE5 | Three.js SSR |
|---------|-----------|-----|--------------|
| Algorithm | Linear march | Hi-Z + DDA | Linear march |
| Samples | 4–32 (Smoothness-driven) | 12–64 (quality CVars) | Fixed 64 |
| Jitter | Authored but disabled | Blue noise + temporal | Temporal offset |
| Temporal accumulation | None | Yes (TAA resolve) | Optional |
| Roughness model | Blur pyramid | BRDF importance sample | Blur |
| Fresnel | Split-sum LUT | Split-sum LUT | Schlick approximation |
| Denoising | Gaussian (no edge stop) | Temporal + spatial | None |
| Half-res | Yes (default) | Yes (configurable) | No |
| Compositing | Alpha blend into spec buffer | Additive + temporal | Over |
