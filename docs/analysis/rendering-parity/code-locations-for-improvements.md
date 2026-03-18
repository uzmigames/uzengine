# UzEngine Rendering — Code Locations for Next Improvements

**Purpose**: Exact file paths and line numbers where rendering improvements need to be implemented.

**Format**: Feature → Files to Create/Modify → Specific Changes

---

## IMMEDIATE FIXES (Days 1-3)

### 1.1 Fix GTAO GPU Hang

**Problem**: GTAO compute shader crashes on dispatch.

**Files to Debug**:
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\gtao_pass.cpp` — Add RenderDoc logging
- `e:\UzEngine\runtime\shaders\runtime\postprocess\gtao_cs.hlsl` — Check for infinite loops
- `e:\UzEngine\runtime\engine\renderer\core\pipeline_library.cpp` — Verify compute root signature

**Changes**:
1. In `gtao_pass.cpp`, before dispatch, log:
   ```cpp
   UZ_LOG(Renderer, Info, "GTAO dispatch: depthSrv=%u, normalSrv=%u, width=%u, height=%u",
          constants.depthSrvIndex, constants.normalSrvIndex, constants.outputWidth, constants.outputHeight);
   ```

2. In `gtao_cs.hlsl`, add bounds checking:
   ```hlsl
   uint x = dispatchThreadID.x;
   uint y = dispatchThreadID.y;
   if (x >= outputWidth || y >= outputHeight) return; // Early exit
   ```

3. Check `constant_buffers.h` line 71-82 for GTAORootConstants alignment.

---

### 1.2 Complete PCSS Blocker Search

**Files to Modify**:
- `e:\UzEngine\runtime\shaders\runtime\include\shadow_common.hlsli` — Add blocker search function
- `e:\UzEngine\runtime\shaders\runtime\include\pcss.hlsli` — Update PCSS constants

**Changes**:

In `shadow_common.hlsli` (after line 100), add:
```hlsl
/// Blocker search for PCSS — finds average depth of occluders.
/// @source UE5 ShadowFilteringCommon.ush:180-220
float SearchBlockers(float2 shadowCoords, float compareDepth,
                     float searchRadius, uint numSamples,
                     Texture2D<float> shadowMap, SamplerState samplerState) {
    float blockerCount = 0;
    float avgBlockerDepth = 0;

    for (uint i = 0; i < min(numSamples, 32u); ++i) {
        float2 offset = poissonDisk[i] * searchRadius;
        float2 sampleCoord = shadowCoords + offset;

        // Clamp to atlas bounds (avoid sampling next cascade)
        sampleCoord = clamp(sampleCoord, float2(0.0, 0.0), float2(1.0, 1.0));

        float sampleDepth = shadowMap.Sample(samplerState, sampleCoord).r;

        if (sampleDepth < compareDepth) {  // Occluder
            avgBlockerDepth += sampleDepth;
            blockerCount += 1.0;
        }
    }

    return blockerCount > 0 ? avgBlockerDepth / blockerCount : compareDepth;
}
```

In `deferred_lighting_ps.hlsl` (around line 400-450 where PCF is called), update:
```hlsl
// Before: fixed Poisson kernel
float shadowFactor = SampleShadowPCF(...);

// After: variable penumbra via PCSS
float blockerDepth = SearchBlockers(shadowCoord, pixelDepth,
                                    searchRadius, 32,
                                    shadowMap, shadowSampler);
float penumbraRadius = (pixelDepth - blockerDepth)
                     * tanLightSourceAngle
                     * maxSoftKernelSize;
float shadowFactor = SampleShadowPCF(shadowCoord, pixelDepth,
                                     penumbraRadius, shadowMap, shadowSampler);
```

---

### 1.3 Add Film Grain, Vignette, Chromatic Aberration

**Files to Modify**:
- `e:\UzEngine\runtime\shaders\runtime\postprocess\tonemap_ps.hlsl` — Add post-FX
- `e:\UzEngine\runtime\engine\renderer\core\constant_buffers.h` — Add PerFrameCB fields

**Changes**:

In `constant_buffers.h` (in PerFrameCB struct, line 109 before padding):
```cpp
float    filmGrainIntensity = 0.02f;    ///< Film grain strength (0-1).
float    vignetteStrength = 0.5f;       ///< Vignette darkening (0-1).
float    chromaticAberrationAmount = 0.01f; ///< CA fringing strength.
float    _pad_postfx = 0.f;             ///< Padding to 16-byte boundary.
```

In `tonemap_ps.hlsl` (after ACES tonemapping, before sRGB conversion):
```hlsl
// ── Post-Process Effects ──
float3 color = toneMapResult;

// Film grain
float2 grainUv = uv + frac(timeParams.x * 1.3);
float noise = frac(sin(dot(grainUv, float2(12.9898, 78.233))) * 43758.5453);
color += (noise - 0.5) * 2.0 * filmGrainIntensity;

// Vignette
float2 vignetteUv = uv - 0.5;
float vignette = length(vignetteUv);
vignette = smoothstep(0.7, 0.3, vignette);
color *= lerp(1.0, vignette, vignetteStrength);

// Chromatic aberration
float3 caColor = float3(
    hdrColor.Sample(s0, uv + float2(chromaticAberrationAmount, 0)).r,
    hdrColor.Sample(s0, uv).g,
    hdrColor.Sample(s0, uv - float2(chromaticAberrationAmount, 0)).b
);
// Optional: blend with original
// color = lerp(color, caColor, 0.5);

// Final sRGB
float3 srgb = LinearToSRGB(color);
```

---

## PHASE 2 IMPLEMENTATIONS (Days 4-20)

### 2.1 Temporal Anti-Aliasing (TAA)

**Files to Create**:
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\taa_pass.h` (new)
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\taa_pass.cpp` (new)
- `e:\UzEngine\runtime\shaders\runtime\postprocess\taa_cs.hlsl` (new)

**Files to Modify**:
- `e:\UzEngine\runtime\engine\renderer\scene_renderer.h` — Add m_taaPing, m_taaPong, m_taaPass
- `e:\UzEngine\runtime\engine\renderer\scene_renderer.cpp` — Initialize + execute TAA pass
- `e:\UzEngine\runtime\engine\renderer\camera\camera.h` — Add jitter to projection

**Key Changes**:

In `camera.h` (add to Camera class):
```cpp
void SetJitter(float x, float y) {
    m_jitterX = x;
    m_jitterY = y;
}
Vec2 GetJitter() const { return {m_jitterX, m_jitterY}; }

float m_jitterX = 0.f;
float m_jitterY = 0.f;
```

In `scene_renderer.cpp` RenderFrame():
```cpp
// Before rendering:
float jitterOffsets[4][2] = {
    {-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f}
};
uint jitterIdx = frameIndex % 4;
m_camera->SetJitter(jitterOffsets[jitterIdx][0] / width,
                    jitterOffsets[jitterIdx][1] / height);

// After lighting, before bloom:
if (m_taaPass) {
    m_taaPass->Execute(cmdList, currentFrameColor, historySrv,
                       width, height, frameIndex);
}
```

In `taa_cs.hlsl` (compute shader):
```hlsl
// Read current frame + history
float3 current = currentColor[pixel];
float3 history = historyColor[pixel];

// Reproject: compute previous-frame screen position
float4 worldPos = ... // From depth + invViewProj
float4 prevScreenPos = mul(worldPos, prevViewProj);
prevScreenPos.xy /= prevScreenPos.w;
prevScreenPos.xy = prevScreenPos.xy * 0.5 + 0.5;

// Sample 3x3 neighborhood for clamping
float3 colorMin = current, colorMax = current;
for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
        colorMin = min(colorMin, currentColor[pixel + int2(x, y)]);
        colorMax = max(colorMax, currentColor[pixel + int2(x, y)]);
    }
}

// Clamp history to neighborhood
float3 clampedHistory = clamp(history, colorMin, colorMax);

// Exponential filter (0.8 weight on history = smoother)
float3 result = lerp(current, clampedHistory, 0.8);
outputColor[pixel] = float4(result, 1.0);
```

---

### 2.2 Screen-Space Reflections (SSR)

**Files to Create**:
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\ssr_pass.h` (new)
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\ssr_pass.cpp` (new)
- `e:\UzEngine\runtime\shaders\runtime\postprocess\ssr_cs.hlsl` (new)

**Files to Modify**:
- `e:\UzEngine\runtime\engine\renderer\scene_renderer.h` — Add m_ssrPass
- `e:\UzEngine\runtime\engine\renderer\scene_renderer.cpp` — Execute SSR after deferred lighting
- `e:\UzEngine\runtime\shaders\runtime\lighting\deferred_lighting_ps.hlsl` — Composite SSR result

**Key Changes**:

In `ssr_cs.hlsl`:
```hlsl
// Per-pixel ray-march through Hi-Z pyramid
float3 rayDir = reflect(V, N);
float2 rayScreenPos = screenPos;
float rayDepth = depth;
float stepSize = 1.0;
float4 hitResult = float4(0, 0, 0, 0); // rgba8(color, confidence)

for (int step = 0; step < 32; ++step) {
    // Ray-march in screen space
    rayScreenPos += rayDir.xy * stepSize;
    rayDepth = rayDir.z * stepSize;

    // Sample Hi-Z pyramid to accelerate
    uint mipLevel = clamp(uint(log2(stepSize)), 0, 4);
    float sampleDepth = hizPyramid.mips[mipLevel].Load(int3(rayScreenPos * resolution >> mipLevel, 0)).r;

    if (rayDepth >= sampleDepth) {
        // Hit! Refine with binary search (4 steps)
        hitResult = SampleSceneColor(rayScreenPos, roughness);
        break;
    }
    stepSize *= 1.3; // Exponential step
}

outputSSR[pixel] = hitResult;
```

In `deferred_lighting_ps.hlsl` (after final lighting):
```hlsl
// Read SSR result
float4 ssrResult = ssrTexture[pixel]; // Loaded via descriptor
float3 ssrColor = ssrResult.rgb;
float confidence = ssrResult.a;

// Composite: blend SSR with IBL
float3 iblReflection = sampleIBL(...);
float3 reflectionFinal = lerp(iblReflection, ssrColor, confidence);

// Apply to specular term
sceneColor += reflectionFinal * F * (1 - roughness);
```

---

### 2.3 Contact Shadows

**Files to Modify**:
- `e:\UzEngine\runtime\shaders\runtime\include\shadow_common.hlsli` — Add contact shadow function
- `e:\UzEngine\runtime\shaders\runtime\lighting\deferred_lighting_ps.hlsl` — Call contact shadow

**Key Changes**:

In `shadow_common.hlsli`:
```hlsl
/// Ray-march toward light to detect contact occlusion.
/// @source UE5 ShadowProjectionPixelShader.usf:300-350
float ComputeContactShadow(float3 worldPos, float3 N, float3 L,
                           Texture2D<float> depthBuffer,
                           float4x4 viewProj,
                           float contactDistance) {
    float shadowFactor = 1.0;
    float rayLength = 0;
    float stepSize = contactDistance / 16.0;

    for (int step = 0; step < 16; ++step) {
        float3 rayPos = worldPos + L * rayLength;
        float4 rayScreenPos = mul(float4(rayPos, 1), viewProj);
        rayScreenPos.xy /= rayScreenPos.w;
        rayScreenPos.xy = rayScreenPos.xy * 0.5 + 0.5;
        #ifdef DX12
        rayScreenPos.y = 1 - rayScreenPos.y; // Flip Y
        #endif

        // Bounds check
        if (rayScreenPos.x < 0 || rayScreenPos.x > 1 ||
            rayScreenPos.y < 0 || rayScreenPos.y > 1) break;

        float sampleDepth = depthBuffer.Sample(s0, rayScreenPos.xy).r;
        if (rayScreenPos.z > sampleDepth + 0.001) {
            shadowFactor = 0.0;
            break;
        }
        rayLength += stepSize;
    }
    return shadowFactor;
}
```

In `deferred_lighting_ps.hlsl` (in shadow accumulation loop):
```hlsl
// After computing directional light shadow:
float pcfShadow = SampleShadowPCF(...);
float contactShadow = ComputeContactShadow(worldPos, N, L, depthBuffer, viewProj, 10.0); // 10 cm contact distance
float finalShadow = pcfShadow * contactShadow;
```

---

### 2.4 Motion Blur

**Files to Create**:
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\motion_blur_pass.h` (new)
- `e:\UzEngine\runtime\shaders\runtime\postprocess\motion_blur_ps.hlsl` (new)

**Files to Modify**:
- `e:\UzEngine\runtime\engine\renderer\scene_renderer.cpp` — Execute motion blur after tonemap
- `e:\UzEngine\runtime\engine\renderer\core\constant_buffers.h` — Add motionBlurStrength

**Key Changes**:

In `motion_blur_ps.hlsl`:
```hlsl
// Read velocity (from velocity texture, populated by GBuffer pass)
float2 velocity = velocityBuffer.Sample(s0, uv).rg;

// Gather-blur along velocity vector
float3 color = hdrColor.Sample(s0, uv);
int numSamples = max(1, int(length(velocity) * 100.0)); // Scale by velocity magnitude
numSamples = min(numSamples, 16); // Cap at 16 samples

for (int i = 1; i < numSamples; ++i) {
    float t = float(i) / float(numSamples);
    float2 sampleUv = uv + velocity * t * motionBlurStrength;
    color += hdrColor.Sample(s0, sampleUv);
}

color /= float(numSamples);
outputColor = float4(color, 1.0);
```

---

### 2.5 Decals

**Files to Create**:
- `e:\UzEngine\runtime\engine\ecs\graphics\decal_component.h` (new)
- `e:\UzEngine\runtime\engine\renderer\passes\geometry\decal_pass.h` (new)
- `e:\UzEngine\runtime\engine\renderer\passes\geometry\decal_pass.cpp` (new)
- `e:\UzEngine\runtime\shaders\runtime\geometry\decal_ps.hlsl` (new)

**Files to Modify**:
- `e:\UzEngine\runtime\engine\renderer\scene_renderer.cpp` — Iterate decals, call decal pass

**Key Changes**:

In `decal_component.h`:
```cpp
struct DecalComponent : public Component {
    Vec3 position;
    Quat rotation;
    Vec3 scale;
    uint32_t materialIndex = 0;
    float blendStrength = 1.0f;
};
```

In `decal_pass.hlsl`:
```hlsl
// Fullscreen pass that reads/writes G-Buffer
float4 albedoAO = gBufferAlbedo[pixel];
float4 normalMat = gBufferNormal[pixel];

// Apply decal blend (simplified)
float4 decalAlbedo = decalTexture.Sample(s0, screenUv);
float4 decalBlend = decalBlendMask.Sample(s0, screenUv);

// Blend: decalBlend.r = albedo blend, .g = normal, .b = roughness
albedoAO = lerp(albedoAO, decalAlbedo, decalBlend.r * blendStrength);

gBufferAlbedo[pixel] = albedoAO;
gBufferNormal[pixel] = normalMat;
```

---

## PHASE 3 POLISH (Days 23-30)

### 3.1 Emissive Maps

**Files to Modify**:
- `e:\UzEngine\runtime\engine\renderer\core\constant_buffers.h` — Add emissiveIndex to PerObjectCB
- `e:\UzEngine\runtime\shaders\runtime\geometry\mesh_ps.hlsl` — Sample emissive
- `e:\UzEngine\runtime\shaders\runtime\lighting\deferred_lighting_ps.hlsl` — Add emissive to HDR

**Key Changes**:

In `constant_buffers.h` (PerObjectCB struct, after metallicIndex):
```cpp
uint32_t emissiveIndex;     ///< SRV heap index for emissive map (0 = black fallback)
float    emissiveIntensity; ///< Emission multiplier (0-10).
```

In `mesh_ps.hlsl`:
```hlsl
// In GBuffer output
float3 emissive = 0;
if (materialParams.emissiveIndex != UINT32_MAX) {
    emissive = GetTexture(materialParams.emissiveIndex).Sample(s0, uv).rgb;
}

// Store in pass-through (use a separate emissive RT, or pack into existing)
outputEmissive = float4(emissive * materialParams.emissiveIntensity, 1.0);
```

In `deferred_lighting_ps.hlsl`:
```hlsl
float3 sceneColor = dirLightResult + pointLightResult + iblResult;
sceneColor += emissive; // Add before bloom threshold
```

---

### 3.2 Color Grading LUT

**Files to Create**:
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\color_grading_pass.h` (new)
- `e:\UzEngine\runtime\shaders\runtime\postprocess\color_grading_ps.hlsl` (new)

**Files to Modify**:
- `e:\UzEngine\runtime\engine\renderer\scene_renderer.cpp` — Load neutral LUT, apply in tonemap

**Key Changes**:

In `color_grading_ps.hlsl`:
```hlsl
// 3D trilinear LUT sampling
float3 color = toneMapResult; // Input from tonemap
float3 colorLut = color * 31.0; // Scale to 32x32x32 LUT coordinates
uint3 lutCoord = uint3(colorLut);

// Trilinear interpolation (simplified - use lerp for full quality)
float3 lut0 = lutTexture[lutCoord].rgb;
float3 lut1 = lutTexture[lutCoord + 1].rgb;
float3 lutColor = lerp(lut0, lut1, frac(colorLut.x));

outputColor = float4(lutColor, 1.0);
```

---

## VERIFICATION CHECKLIST

After each feature is implemented, verify:

- [ ] Code compiles without warnings
- [ ] GPU driver doesn't report errors (PIX/RenderDoc check)
- [ ] Visual looks correct (compare with UE5 reference if available)
- [ ] Performance impact < 5% frame time increase
- [ ] Toggle/disable option works (can disable feature at runtime)
- [ ] Tasks.md updated with completion status

---

## FILE STRUCTURE SUMMARY

```
e:\UzEngine\runtime\engine\renderer\
├── passes/
│   ├── postprocess/
│   │   ├── taa_pass.h/cpp          (NEW: TAA)
│   │   ├── ssr_pass.h/cpp          (NEW: Screen-space reflections)
│   │   ├── motion_blur_pass.h/cpp  (NEW: Motion blur)
│   │   ├── color_grading_pass.h    (NEW: Color grading LUT)
│   │   └── (existing: bloom, gtao, auto_exposure)
│   ├── geometry/
│   │   ├── decal_pass.h/cpp        (NEW: Decals)
│   │   └── (existing: gbuffer, depth_prepass)
│   └── (existing: atmosphere, lighting, debug)
├── scene_renderer.h/cpp
│   └── Add m_taaPass, m_ssrPass, m_motionBlurPass, m_decalPass
└── core/
    ├── constant_buffers.h
    │   └── Add emissiveIndex, filmGrainIntensity, etc.
    └── (existing: CBRingAllocator, PipelineLibrary)

e:\UzEngine\runtime\shaders\runtime\
├── postprocess/
│   ├── taa_cs.hlsl           (NEW)
│   ├── ssr_cs.hlsl           (NEW)
│   ├── motion_blur_ps.hlsl   (NEW)
│   ├── color_grading_ps.hlsl (NEW)
│   └── tonemap_ps.hlsl       (MODIFY: add grain, vignette, CA)
├── include/
│   ├── shadow_common.hlsli   (MODIFY: add blocker search, contact shadow)
│   └── (existing: pbr_common, math_util)
├── lighting/
│   ├── deferred_lighting_ps.hlsl (MODIFY: composite SSR, contact shadows)
│   └── (existing: mesh shaders)
└── geometry/
    └── decal_ps.hlsl         (NEW)

e:\UzEngine\runtime\engine\ecs\
└── graphics/
    └── decal_component.h     (NEW)
```

---

**Document Version**: 1.0
**Date**: 2026-03-17
**Last Updated By**: unreal-architect
