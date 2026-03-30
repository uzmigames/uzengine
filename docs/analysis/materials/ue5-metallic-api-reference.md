<!-- doc-meta
title: UE5 Metallic & IBL API Reference
project: UzEngine
created: 2026-03-23
keywords: API, function signatures, metallic, IBL, UE5 source
-->

# UE5 Metallic & IBL API Reference

## HLSL Shader Functions

### Color Decomposition

**File**: `Engine/Shaders/Private/BRDF.ush`

```hlsl
float3 ComputeF0(float Specular, float3 BaseColor, float Metallic)
{
    return lerp(Specular.xxx * 0.08, BaseColor, Metallic);
}
```

**Parameters**:
- `Specular`: Dielectric F0 (typically 0.5 → represents ~0.04 F0)
- `BaseColor`: Diffuse color (for diffuse) and metal F0 (for specular)
- `Metallic`: [0, 1] blend factor

**Returns**: F0 value to use in Fresnel calculation

**Usage** (DeferredShadingCommon.ush:750):
```hlsl
GBuffer.SpecularColor = ComputeF0(GBuffer.Specular, GBuffer.BaseColor, GBuffer.Metallic);
GBuffer.DiffuseColor = GBuffer.BaseColor - GBuffer.BaseColor * GBuffer.Metallic;
```

---

### Diffuse BRDF

**File**: `Engine/Shaders/Private/BRDF.ush`

```hlsl
float3 Diffuse_Lambert(float3 DiffuseColor)
{
    return DiffuseColor * (1.0 / PI);
}
```

**Parameters**:
- `DiffuseColor`: Albedo to diffuse-shade (should be 0 for metals)

**Returns**: Diffuse coefficient

**Applied in**: `DeferredLightingCommon.ush:420`
```hlsl
float3 LightingDiffuse = Diffuse_Lambert(GBuffer.DiffuseColor) * Lighting;
```

---

### Specular BRDF (GGX Cook-Torrance)

**File**: `Engine/Shaders/Private/BRDF.ush`

```hlsl
float3 SpecularBRDF(FBxDFContext Context)
{
    float D = D_GGX(Context.Roughness, Context.NoH);
    float Vis = Vis_SmithJointApprox(Context.Roughness, Context.NoV, Context.NoL);
    float3 F = F_Schlick(Context.SpecularColor, Context.VoH);
    return (D * Vis) * F;
}
```

**Parameters** (via FBxDFContext struct):
- `Roughness`: Material roughness [0, 1]
- `SpecularColor`: F0 value (metal or dielectric)
- `NoV`, `NoL`, `NoH`: Dot products with normal
- `VoH`: Dot product view-to-half-vector

**Returns**: Specular contribution

---

## C++ Deferred Rendering Functions

### Decompose GBuffer (CPU Side)

**File**: `Renderer/Private/DeferredShadingRenderer.cpp`

When reading from GBuffer texture:
```cpp
FGBufferData GBuffer = GetGBufferData(UV);
// GBuffer.Metallic is read from GBufferB.r
float3 DiffuseColor = GBuffer.BaseColor - GBuffer.BaseColor * GBuffer.Metallic;
float3 SpecularColor = ComputeF0(GBuffer.Specular, GBuffer.BaseColor, GBuffer.Metallic);
```

**Critical fields in FGBufferData**:
- `BaseColor`: RGB stored in GBufferA
- `Metallic`: R channel of GBufferB
- `Roughness`: G channel of GBufferB
- `WorldNormal`: Packed into GBufferA.a or GBufferC

**Source file**: `Engine/Shaders/Private/DeferredShadingCommon.ush` (struct FGBufferData)

---

### Sky Light Existence Check

**File**: `Renderer/Private/IndirectLightRendering.cpp:1987-1991`

```cpp
const bool bSkyLight = Scene->SkyLight
    && (Scene->SkyLight->ProcessedTexture || Scene->SkyLight->bRealTimeCaptureEnabled)
    && !Scene->SkyLight->bHasStaticLighting;
```

**This determines if sky light indirect pass runs**:
- `Scene->SkyLight`: Pointer to FSkyLightSceneProxy (can be nullptr)
- `ProcessedTexture`: Captured cubemap texture
- `bRealTimeCaptureEnabled`: Real-time sky capture active
- `bHasStaticLighting`: Excludes pre-baked sky light

**When FALSE**: No sky light contribution applied (zero for all materials)

---

### Apply Sky Light & Reflections

**File**: `Renderer/Private/IndirectLightRendering.cpp:2146-2159`

```cpp
void FDeferredShadingSceneRenderer::RenderDeferredReflectionsAndSkyLighting(
    FRDGBuilder& GraphBuilder,
    const FSceneTextures& SceneTextures,
    FLumenSceneFrameTemporaries& LumenFrameTemporaries,
    TArray<FRDGTextureRef>& DynamicBentNormalAOTextures)
{
    // ...
    const bool bSkyLight = Scene->SkyLight && Scene->SkyLight->ProcessedTexture;

    if (bSkyLight || bReflectionEnv) {
        AddSkyReflectionPass(...);  // This is where sky/reflections apply
    }
}
```

**This function is SEPARATE from deferred light passes**:
- Called AFTER all direct lights are accumulated
- Only runs if `bSkyLight` or reflection environment exists
- If both are false, **zero indirect lighting** is applied

**Key call hierarchy**:
```
DeferredShadingRenderer::Render()
  → RenderLights()  ← Direct lighting (sun, points, spots)
  → RenderDeferredReflectionsAndSkyLighting()  ← Indirect (sky, reflections)
       → AddSkyReflectionPass()  ← Sky light texture applied here
```

---

### Reflection Capture Query

**File**: `Renderer/Private/ReflectionEnvironment.cpp`

```cpp
bool ShouldDoReflectionEnvironment(const FScene* Scene, const FEngineShowFlags& EngineShowFlags)
{
    return Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num() > 0
        && EngineShowFlags.ReflectionEnvironment;
}
```

**When FALSE**: No reflection capture contribution (zero for specular)

---

## Shader Passes (GPU Pipeline)

### Pass 1: Deferred Light Pixel Shader

**File**: `Engine/Shaders/Private/DeferredLightPixelShaders.usf:230-399`

```hlsl
void DeferredLightPixelMain(
    float4 InScreenPosition : TEXCOORD0,
    float2 ScreenUV : TEXCOORD0,
    float3 ScreenVector : TEXCOORD1,
    float4 SVPos : SV_POSITION,
    out float4 OutColor : SV_Target0)
{
    // Read GBuffer
    FScreenSpaceData ScreenSpaceData = GetScreenSpaceData(InputParams.ScreenUV);
    FGBufferData GBuffer = ScreenSpaceData.GBuffer;

    // Read light
    FDeferredLightData LightData = InitDeferredLightFromUniforms(CURRENT_LIGHT_TYPE);

    // Compute lighting (uses DiffuseColor and SpecularColor derived from GBuffer.Metallic)
    float4 Radiance = GetDynamicLighting(DerivedParams.TranslatedWorldPosition,
        DerivedParams.CameraVector, GBuffer, AmbientOcclusion, LightData,
        LightAttenuation, Dither, uint2(InputParams.PixelPos), SurfaceShadow);

    OutColor += Radiance;
}
```

**Called once per light** (directional, point, spot)
- Reads GBuffer at pixel
- Computes direct diffuse + specular
- Accumulates to scene color

**For metallic=1.0**: DiffuseColor=0, so only specular contributes

---

### Pass 2: Sky Light Pass (Indirect)

**File**: `Renderer/Private/CompositionLighting/...`

**Conceptual code** (not exact source):
```cpp
// In AddSkyReflectionPass (line 2146)
if (bSkyLight) {
    // Apply sky light diffuse (SH) + specular reflections
    // Uses SpecularColor for reflections (metal or dielectric)
    // Uses DiffuseColor for diffuse (0 for metals!)
}
```

**Key insight**: This pass is **only called if bSkyLight is true**
- If no sky light → this pass doesn't run
- If no reflection captures → no reflection pass
- **Result**: Zero indirect lighting for metals

---

## Data Flow: From GBuffer to Lighting

```
GBuffer (written by Base Pass)
  ├─ BaseColor (RGB)
  ├─ Metallic (R channel of GBufferB)
  ├─ Normal (GBufferA.a or GBufferC)
  └─ Roughness (G channel of GBufferB)
    ↓
Deferred Light Pixel Shader
  ├─ Reads GBuffer
  ├─ Computes DiffuseColor = BaseColor * (1 - Metallic)
  ├─ Computes SpecularColor = ComputeF0(Metallic, BaseColor)
  ├─ Applies Direct Light:
  │  ├─ Diffuse: Diffuse_Lambert(DiffuseColor)
  │  └─ Specular: SpecularBRDF(SpecularColor, Roughness)
  └─ Outputs to SceneColor (additive per light)
    ↓
Sky Light Pass (if bSkyLight)
  ├─ Reads GBuffer again
  ├─ Recomputes DiffuseColor, SpecularColor
  ├─ Applies Indirect Light:
  │  ├─ Diffuse: Sky SH * DiffuseColor
  │  └─ Specular: Sky/Probe reflection * SpecularColor
  └─ Adds to SceneColor (additive)
    ↓
Final SceneColor = Direct + Indirect (if available)
```

---

## Key Conditions for Zero Indirect Lighting

All of these result in **zero indirect lighting** for ALL materials (including metals):

```cpp
// Scene->SkyLight == nullptr
→ Sky light pass doesn't run

// Scene->SkyLight->ProcessedTexture == nullptr (no cubemap)
→ Sky light pass doesn't run

// Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num() == 0
→ No reflection probes

// Scene->SkyLight->bHasStaticLighting == true
→ Sky light skipped (assumed pre-baked)

// ViewFamily.EngineShowFlags.ReflectionEnvironment == false
→ Reflection environment disabled in editor/console
```

**Result**: Metallic surfaces = purely specular, no environmental light = black when highlight absent

---

## Verification: Check if Sky Light Exists

```cpp
// To check in your code:
const UWorld* World = GetWorld();
if (World && World->Scene) {
    FScene* Scene = (FScene*)World->Scene;

    bool bHasSkyLight = Scene->SkyLight
        && Scene->SkyLight->ProcessedTexture;

    if (!bHasSkyLight) {
        // This is why metals are black!
        UE_LOG(LogWarning, Warning, TEXT("No sky light in scene"));
    }
}
```

---

## Related Reading

- **Metallic property documentation**: `Engine/Classes/Materials/MaterialExpressionMaterialParameter.h`
- **GBuffer layout**: `Engine/Shaders/Private/DeferredShadingCommon.ush` (FGBufferData struct)
- **BRDF functions**: `Engine/Shaders/Private/BRDF.ush`
- **Sky light implementation**: `Renderer/Classes/SkyLightSceneProxy.h`
- **Reflection environment**: `Renderer/Private/ReflectionEnvironment.cpp`

