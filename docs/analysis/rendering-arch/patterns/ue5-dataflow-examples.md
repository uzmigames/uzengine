<!-- doc-meta: ue5-dataflow-examples, Data flow traces for complex UE5 rendering systems -->

# UE5 Data Flow Examples — Complete Pipeline Traces

**Purpose**: Trace complete data flow for complex features from component → CB → shader → GPU.

---

## Example 1: Exponential Height Fog

### Scenario

User enables volumetric fog in scene with parameters:
- Fog density: 0.02
- Fog height falloff: 0.2
- Inscattering color: (0.7, 0.8, 0.9)

Trace data flow from scene component to final pixel output.

### Stage 1: Game Thread (Component Definition)

**File**: `Engine/Classes/Components/ExponentialHeightFogComponent.h`

```cpp
class UExponentialHeightFogComponent : public UActorComponent
{
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FogDensity = 0.02f;  // ← User sets this
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FogHeightFalloff = 0.2f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLinearColor InscatteringColor = FLinearColor(0.7, 0.8, 0.9);
    
    // Called when property changes in editor
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
    {
        // Mark scene as needing fog update
        GetWorld()->Scene->UpdateFog();
    }
};
```

**Lifecycle**: Component exists throughout level lifetime.

### Stage 2: Scene Proxy (Render Thread Mirror)

**File**: `Engine/Source/Runtime/Renderer/Private/FogRendering.cpp`

```cpp
class FFogSceneInfo
{
public:
    // Copied from game thread (snapshot each frame)
    float FogDensity;
    float FogHeightFalloff;
    FLinearColor InscatteringColor;
    
    // Called from render thread (1-frame delayed)
    void UpdateFromComponent(const UExponentialHeightFogComponent* Component)
    {
        FogDensity = Component->FogDensity;            // 0.02
        FogHeightFalloff = Component->FogHeightFalloff; // 0.2
        InscatteringColor = Component->InscatteringColor; // (0.7, 0.8, 0.9)
    }
};
```

**When Updated**: Each frame, render thread calls `UpdateFromComponent()`.

### Stage 3: Constant Buffer Population

**File**: `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp`

**Function**: `FSceneRenderer::SetupFogConstants()`

```cpp
struct FFogConstants
{
    float FogDensity;            // 0.02 (float = 4 bytes)
    float FogHeightFalloff;      // 0.2  (float = 4 bytes)
    float FogHeightOffset;       // 0.0  (float = 4 bytes)
    float Padding0;              // padding
    FLinearColor InscatteringColor; // (0.7, 0.8, 0.9, 1.0) (4×float = 16 bytes)
    // Total: 32 bytes
};

void FSceneRenderer::SetupFogConstants(FRHICommandList& RHICmdList)
{
    FFogConstants FogCB;
    FogCB.FogDensity = Scene->FogInfo->FogDensity;
    FogCB.FogHeightFalloff = Scene->FogInfo->FogHeightFalloff;
    FogCB.InscatteringColor = Scene->FogInfo->InscatteringColor;
    
    // Update GPU buffer
    FRHIUploadData UploadData = RHICmdList.LockStructuredBuffer(
        FogConstantBufferRef, 0, sizeof(FFogConstants), EResourceLockMode::RLM_WriteOnly
    );
    FMemory::Memcpy(UploadData.GetDataPtr(), &FogCB, sizeof(FFogConstants));
    RHICmdList.UnlockStructuredBuffer(UploadData);
}
```

**When Called**: Early in frame (before all deferred passes).

### Stage 4: Shader Binding

**File**: `Engine/Shaders/Private/ExponentialHeightFogCommon.ush`

```hlsl
// Constant buffer definition (must match C++ struct exactly)
cbuffer FogCB : register(b3)
{
    float FogDensity;
    float FogHeightFalloff;
    float FogHeightOffset;
    float Padding0;
    float4 InscatteringColor;
};

// Fog computation function
float3 GetExponentialFogColor(float WorldZ)
{
    // Use CB values
    float FogFalloff = exp2(-FogHeightFalloff * (WorldZ - FogHeightOffset));
    float FogAlpha = saturate(FogDensity * FogFalloff);
    
    // Inscattering: fog color modulated by density
    float3 FogColor = InscatteringColor.rgb;
    
    return FogColor * FogAlpha;
}
```

### Stage 5: Deferred Light Pass Integration

**File**: `Engine/Shaders/Private/DeferredLightingPixelShader.hlsl`

```hlsl
float4 main(FScreenSpaceData ScreenSpaceData, uint PixelCoord : SV_Position) : SV_Target
{
    // ... compute lighting ...
    float3 HDRColor = ComputeLighting(...);
    
    // Apply fog
    float WorldZ = GetWorldZFromDepth(ScreenSpaceData.Depth);
    float3 FogColor = GetExponentialFogColor(WorldZ);
    
    // Blend fog with scene
    HDRColor = lerp(HDRColor, FogColor, FogColor.a);
    
    return float4(HDRColor, 1.0);
}
```

### Stage 6: Output to Screen

**GPU Rasterizer**:
1. Interpolate PixelCoord per pixel
2. Execute pixel shader
3. Output HDRColor with fog applied
4. Write to HDR render target

### Summary Table

| Stage | File | Class/Function | Data |
|-------|------|-----------------|------|
| 1. Component | ExponentialHeightFogComponent.h | UExponentialHeightFogComponent | FogDensity=0.02, FogHeightFalloff=0.2, InscatteringColor=(0.7,0.8,0.9) |
| 2. Proxy | FogRendering.cpp | FFogSceneInfo::UpdateFromComponent() | Copy from game thread to render thread |
| 3. CB | SceneRendering.cpp | SetupFogConstants() | Pack into FFogConstants struct, lock CB, memcpy |
| 4. Shader | ExponentialHeightFogCommon.ush | GetExponentialFogColor() | Read from cbuffer FogCB, compute falloff |
| 5. Integration | DeferredLightingPixelShader.hlsl | main() | Apply fog to final HDR color |
| 6. GPU | GPU Rasterizer | (implicit) | Render to screen with fog applied |

---

## Example 2: Reflection Probe Capture & IBL

### Scenario

Designer places reflection probe in scene.
Probe captures environment every frame.
IBL uses captured cubemap for reflections + ambient.

### Stage 1: Component Creation

**File**: `Engine/Classes/Engine/ReflectionCapture.h`

```cpp
class AReflectionCapture : public AActor
{
public:
    UPROPERTY(EditAnywhere)
    float CaptureDistance = 3000.0f;  // Capture radius (cm)
    
    UPROPERTY(EditAnywhere)
    bool bCapture = false;  // Enable capture
    
    // Capture texture (render target)
    class UTextureCube* CaptureTexture = nullptr;  // 128×128 cubemap
};
```

### Stage 2: Probe Registration in Scene

**File**: `Engine/Source/Runtime/Engine/Private/World.cpp`

```cpp
void AReflectionCapture::BeginPlay()
{
    Super::BeginPlay();
    
    // Register with scene
    GetWorld()->Scene->AddReflectionCapture(this);
}

// Scene maintains list of active probes
class FScene
{
    TArray<AReflectionCapture*> ReflectionCaptures;
};
```

### Stage 3: Capture Rendering (Render Thread)

**File**: `Engine/Source/Runtime/Renderer/Private/ReflectionCapture.cpp`

```cpp
void FSceneRenderer::CaptureReflectionProbe(AReflectionCapture* Probe)
{
    // For each of 6 cube faces
    for (int Face = 0; Face < 6; ++Face)
    {
        // Setup view matrix (camera looking at face direction)
        FMatrix ViewMatrix = GetCubeFaceViewMatrix(Probe->GetActorLocation(), Face);
        FMatrix ProjectionMatrix = GetCubeFaceProjection(Probe->CaptureDistance);
        
        // Render scene to cubemap face
        // (same as normal scene rendering, but smaller viewport)
        RenderScene(
            ViewMatrix, 
            ProjectionMatrix,
            Probe->CaptureTexture,  // Render target
            Face
        );
    }
    
    // Convolve cubemap to irradiance (optional, for SH)
    ConvolveIrradiance(Probe->CaptureTexture);
}
```

### Stage 4: IBL Constant Buffer Update

**File**: `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp`

```cpp
struct FIBLConstants
{
    uint ReflectionProbeCount;           // Number of active probes
    float3 ReflectionProbePositions[MAX_PROBES];  // World positions
    float ReflectionProbeRadii[MAX_PROBES];       // Capture radii
    uint ReflectionProbeTextureIndices[MAX_PROBES]; // Index into texture array
};

void FSceneRenderer::UpdateIBLConstants(FRHICommandList& RHICmdList)
{
    FIBLConstants IBLData;
    IBLData.ReflectionProbeCount = Scene->ReflectionCaptures.Num();
    
    for (int i = 0; i < IBLData.ReflectionProbeCount; ++i)
    {
        AReflectionCapture* Probe = Scene->ReflectionCaptures[i];
        IBLData.ReflectionProbePositions[i] = Probe->GetActorLocation();
        IBLData.ReflectionProbeRadii[i] = Probe->CaptureDistance;
        IBLData.ReflectionProbeTextureIndices[i] = Probe->CaptureTexture->TextureArrayIndex;
    }
    
    RHICmdList.UpdateDynamicBuffer(IBLConstantBufferRef, &IBLData, sizeof(IBLData));
}
```

### Stage 5: Deferred Light Pass (IBL Sampling)

**File**: `Engine/Shaders/Private/ReflectionCommon.ush`

```hlsl
cbuffer IBLConstants : register(b4)
{
    uint ReflectionProbeCount;
    float3 ReflectionProbePositions[MAX_PROBES];
    float ReflectionProbeRadii[MAX_PROBES];
    uint ReflectionProbeTextureIndices[MAX_PROBES];
};

// Texture array of cubemaps
TextureCubeArray ReflectionProbes : register(t10);

float3 SampleIBLReflection(float3 WorldPos, float3 Normal, float Roughness)
{
    float3 ViewDir = normalize(CameraPos - WorldPos);
    float3 ReflectionDir = reflect(-ViewDir, Normal);
    
    // Find closest probe
    float ClosestDist = 1e10;
    uint ClosestProbeIdx = 0;
    
    for (uint i = 0; i < ReflectionProbeCount; ++i)
    {
        float Dist = distance(WorldPos, ReflectionProbePositions[i]);
        
        if (Dist < ReflectionProbeRadii[i] && Dist < ClosestDist)
        {
            ClosestDist = Dist;
            ClosestProbeIdx = i;
        }
    }
    
    // Sample cubemap from closest probe
    uint ProbeTextureIdx = ReflectionProbeTextureIndices[ClosestProbeIdx];
    uint Mip = uint(Roughness * 5);  // 6 mips for roughness
    
    float3 ReflectionColor = ReflectionProbes.SampleLevel(
        SamplerLinear,
        float4(ReflectionDir, ProbeTextureIdx),
        Mip
    ).rgb;
    
    return ReflectionColor;
}

// In deferred light pass
float3 SpecularIBL = SampleIBLReflection(WorldPos, Normal, Roughness);
```

### Stage 6: Final Composite

```hlsl
float4 main(...) : SV_Target
{
    // ... base lighting ...
    float3 HDRColor = BaseLight + SpecularIBL;
    return float4(HDRColor, 1.0);
}
```

### Data Flow Diagram

```
Actor (Game)
  └─ AReflectionCapture
     ├─ ActorLocation (world position)
     ├─ CaptureDistance (radius)
     └─ CaptureTexture (cubemap reference)
        │
        ├─ Render Thread: Scene maintains list of probes
        │  └─ CaptureReflectionProbe() renders 6 faces
        │
        ├─ Constant Buffer: IBLConstants
        │  ├─ ReflectionProbeCount = 1
        │  ├─ ReflectionProbePositions[0] = ActorLocation
        │  ├─ ReflectionProbeRadii[0] = CaptureDistance
        │  └─ ReflectionProbeTextureIndices[0] = TextureArrayIndex
        │
        └─ Shader: Deferred Light Pass
           └─ SampleIBLReflection()
              ├─ Find closest probe
              ├─ Sample cubemap mip (based on roughness)
              └─ Return specular contribution
```

---

## Example 3: CSM Shadow Maps with PCSS

### Scenario

Directional light casts shadows on terrain.
4 cascades, PCSS for soft shadows.

### Stage 1: Light Component

```cpp
class ADirectionalLight : public AActor
{
public:
    UPROPERTY(EditAnywhere)
    float Intensity = 1000.0f;  // Lux
    
    UPROPERTY(EditAnywhere)
    FLinearColor LightColor = FLinearColor::White;
    
    UPROPERTY(EditAnywhere)
    float CascadeCount = 4;  // 4 cascades
    
    UPROPERTY(EditAnywhere)
    float ShadowMapResolution = 2048;  // Per cascade
};
```

### Stage 2: Cascade Setup

**File**: `Engine/Source/Runtime/Renderer/Private/ShadowSceneRenderer.cpp`

```cpp
struct FCSMConstants
{
    FMatrix LightViewProjection[4];  // One per cascade
    float4 CascadeDepthRanges[4];     // Near/far z per cascade
    uint CascadeCount;
    float ShadowMapBias;
    float PCSSSearchRadius;
    float PCSSFilterRadius;
};

void FSceneRenderer::SetupCSMConstants(FRHICommandList& RHICmdList, const ADirectionalLight* Light)
{
    FCSMConstants CSMData;
    CSMData.CascadeCount = Light->CascadeCount;  // 4
    CSMData.ShadowMapBias = 0.00048f;  // Depth bias
    CSMData.PCSSSearchRadius = 2.0f;   // Blocker search area (cm)
    CSMData.PCSSFilterRadius = 1.0f;   // PCF kernel size (cm)
    
    // Compute cascade matrices
    for (int i = 0; i < 4; ++i)
    {
        float CascadeStart = i * 500.0f;      // Cascade 0: 0-500cm
        float CascadeEnd = (i+1) * 500.0f;    // Cascade 1: 500-1000cm, etc.
        
        FMatrix LightViewMatrix = GetLightViewMatrix(Light);
        FMatrix LightProjection = GetOrthographicProjection(
            CascadeStart,
            CascadeEnd,
            2048, 2048  // Frustum size
        );
        
        CSMData.LightViewProjection[i] = LightViewMatrix * LightProjection;
        CSMData.CascadeDepthRanges[i] = FVector4(CascadeStart, CascadeEnd, 0, 0);
    }
    
    RHICmdList.UpdateDynamicBuffer(CSMConstantBufferRef, &CSMData, sizeof(CSMData));
}
```

### Stage 3: Shadow Depth Rendering

**File**: `Engine/Shaders/Private/ShadowDepthVS.hlsl`

```hlsl
cbuffer CSMConstants : register(b0)
{
    float4x4 LightViewProjection[4];
    float4 CascadeDepthRanges[4];
    uint CascadeCount;
};

struct VS_Output
{
    float4 Position : SV_Position;
    float Depth : DEPTH;
};

VS_Output main(VS_Input Input, uint InstanceID : SV_InstanceID)
{
    VS_Output Output;
    
    // Determine which cascade this instance belongs to
    uint CascadeIdx = InstanceID / (65536 / CascadeCount);  // Rough estimate
    
    // Transform to light space (for this cascade)
    float4 WorldPos = float4(Input.Position, 1.0);
    Output.Position = mul(WorldPos, LightViewProjection[CascadeIdx]);
    
    // Output depth for shadow comparison
    Output.Depth = Output.Position.z / Output.Position.w;
    
    return Output;
};
```

### Stage 4: Shadow Depth Texture Binding

**File**: `Engine/Source/Runtime/Renderer/Private/ShadowSceneRenderer.cpp`

```cpp
void FSceneRenderer::RenderShadowMaps(FRHICommandList& RHICmdList)
{
    // Create/bind shadow depth textures (one per cascade)
    FRDGTextureRef ShadowDepthTextures[4];
    
    for (int i = 0; i < 4; ++i)
    {
        FRDGTextureDesc DepthDesc;
        DepthDesc.Extent = FIntPoint(2048, 2048);
        DepthDesc.Format = PF_DepthStencil;
        DepthDesc.Usage = TexCreate_DepthStencilTargetable;
        
        ShadowDepthTextures[i] = Builder.CreateTexture(DepthDesc, TEXT("ShadowDepth_Cascade_0") + i);
    }
    
    // Render each cascade
    for (int i = 0; i < 4; ++i)
    {
        // Set render target
        RHICmdList.SetRenderTarget(ShadowDepthTextures[i], nullptr);
        RHICmdList.ClearDepthStencil(1.0f, 0);
        
        // Render scene geometry (opaque only)
        DrawMeshes(RHICmdList, EDepthDrawingMode::Shadow, i);
    }
    
    // Bind all 4 textures to texture array for sampling
    for (int i = 0; i < 4; ++i)
    {
        ShadowDepthTextureArray[i] = ShadowDepthTextures[i];
    }
}
```

### Stage 5: PCSS in Deferred Light Pass

**File**: `Engine/Shaders/Private/DeferredLightingPixelShader.hlsl`

```hlsl
// Shadow depth texture array
Texture2DArray ShadowDepthTextures : register(t11);

float PCSSCalculatePenumbra(float ReceiverZ, uint CascadeIdx)
{
    // Step 1: Find blockers (shadow map samples at search radius)
    float SearchRadius = 2.0;  // cm in world space
    float BlockerCount = 0;
    float BlockerAverageZ = 0;
    
    for (int i = -2; i <= 2; ++i)
    for (int j = -2; j <= 2; ++j)
    {
        float2 SampleUV = CurrentPixelUV + float2(i, j) * SearchRadius / 2048;
        float ShadowDepth = ShadowDepthTextures.Sample(SamplerPoint, float3(SampleUV, CascadeIdx)).r;
        
        // If depth is closer than receiver: it's a blocker
        if (ShadowDepth < ReceiverZ - 0.00048)  // Bias
        {
            BlockerCount += 1.0;
            BlockerAverageZ += ShadowDepth;
        }
    }
    
    if (BlockerCount > 0)
    {
        BlockerAverageZ /= BlockerCount;
    }
    else
    {
        return 0.0;  // No blockers = no shadow
    }
    
    // Step 2: Estimate penumbra size
    float LightDistance = 1e10;  // Assume very far (orthographic light)
    float Penumbra = (ReceiverZ - BlockerAverageZ) / BlockerAverageZ * LightDistance;
    
    return saturate(Penumbra);
}

float PCSSCalculateShadow(float3 WorldPos, float3 Normal, uint CascadeIdx)
{
    // Transform to light space
    float4 LightSpacePos = mul(float4(WorldPos, 1.0), LightViewProjection[CascadeIdx]);
    float2 ShadowMapUV = LightSpacePos.xy / LightSpacePos.w;
    float ReceiverZ = LightSpacePos.z / LightSpacePos.w;
    
    // Get penumbra size
    float Penumbra = PCSSCalculatePenumbra(ReceiverZ, CascadeIdx);
    
    // Step 3: Percentage-closer filtering (adaptive kernel)
    float FilterRadius = Penumbra * 1.0;  // 1cm base filter
    float Shadow = 0.0;
    float SampleCount = 16.0;
    
    for (int i = 0; i < 16; ++i)
    {
        float2 Offset = Hammersley(i, 16) * FilterRadius;
        float2 SampleUV = ShadowMapUV + Offset;
        
        float ShadowDepth = ShadowDepthTextures.Sample(SamplerLinear, float3(SampleUV, CascadeIdx)).r;
        
        if (ReceiverZ > ShadowDepth + 0.00048)  // In shadow
            Shadow += 1.0 / SampleCount;
    }
    
    return 1.0 - Shadow;  // 1.0 = lit, 0.0 = in shadow
}

float4 main(...) : SV_Target
{
    // Determine cascade index from pixel depth
    uint CascadeIdx = DetermineCascade(PixelDepth);
    
    // Calculate shadow (0-1, 1 = fully lit)
    float ShadowFactor = PCSSCalculateShadow(WorldPos, Normal, CascadeIdx);
    
    // Apply to lighting
    float3 DirectLighting = LightIntensity * ShadowFactor * Lambert(Normal, LightDir);
    
    return float4(DirectLighting, 1.0);
}
```

### Data Flow Summary

```
DirectionalLight (Game)
  ├─ Intensity = 1000
  ├─ LightColor = White
  └─ CascadeCount = 4
     │
     Render Thread: SetupCSMConstants()
     ├─ Compute 4 light view-projection matrices
     ├─ Compute cascade depth ranges [0-500, 500-1000, 1000-1500, 1500-2000]
     └─ Populate FCSMConstants (CB)
        │
        ├─ RenderShadowMaps()
        │  ├─ Render cascade 0 (near) to 2048×2048 shadow map
        │  ├─ Render cascade 1 to shadow map
        │  ├─ Render cascade 2 to shadow map
        │  └─ Render cascade 3 (far) to shadow map
        │
        └─ Deferred Light Pass
           ├─ DetermineCascade(PixelDepth)
           ├─ PCSSCalculatePenumbra(ReceiverZ)
           │  └─ Sample shadow map blockers, estimate penumbra
           ├─ PCSSCalculateShadow(WorldPos)
           │  └─ 16×PCF samples in adaptive kernel
           └─ Apply ShadowFactor to final lighting
```

---

## Key Patterns

### Pattern 1: Component → Proxy → CB → Shader

```
Game Thread                Render Thread              GPU
Component                  Proxy                      CB
(UExponentialHeight        (FFogSceneInfo)            (cbuffer FogCB)
 FogComponent)             
│                          │                         │
FogDensity=0.02 ────→ UpdateFromComponent() ────→ FogDensity=0.02
FogHeightFalloff=0.2  FogDensity                    FogHeightFalloff=0.2
InscatteringColor     InscatteringColor             InscatteringColor
                      (snapshot per frame)          (read in shader)
```

**Latency**: 1 frame (game thread Frame N → render thread Frame N+1 → GPU Frame N+2)

### Pattern 2: Array-Based Binding

```
Component Array (Game)        Scene Array (Render)     CB Array
AReflectionCapture[0]         FFogSceneInfo[0]        FFogConstants[0]
AReflectionCapture[1]    ──→  FFogSceneInfo[1]   ──→  FFogConstants[1]
AReflectionCapture[2]         FFogSceneInfo[2]        FFogConstants[2]

In Shader:
uint ProbeIdx = FindClosestProbe(WorldPos);
sample(CubemapArray, ProbeIdx);
```

**Advantage**: Single shader, multiple instances, no permutation explosion.

### Pattern 3: Rendering to Texture (Capture)

```
Render Target                  Texture Binding
ShadowDepth[0]  ───┐           ShadowDepthArray[0]
ShadowDepth[1]  ───┼─→ Combine ──┤ ShadowDepthArray[1]
ShadowDepth[2]  ───┤            ShadowDepthArray[2]
ShadowDepth[3]  ───┘            ShadowDepthArray[3]
                                     │
                                     └─ Shader samples
```

**Workflow**:
1. Render to individual RT (depth buffer)
2. Copy to texture array element
3. Bind texture array in later passes

---

## Validation

### For Each Example

1. **Component values** — Verify constants match expected values
2. **CB layout** — Ensure byte offsets match (alignment)
3. **Shader output** — Compare vs UE5 reference (PSNR)
4. **Temporal consistency** — No flicker or 1-frame glitches

### PSNR Testing

```bash
# Capture UzEngine output
uzengine.exe --screenshot output.png --frames 120

# Capture UE5 reference
UE5Editor ... --screenshot reference.png --frames 120

# Compare PSNR
compare_psnr(output.png, reference.png)  # Should be ≥ 35 dB
```

---

## Summary

These examples show the **complete data flow** for three complex systems:

1. **Exponential Height Fog** — Simple scalar/vector parameters → CB → shader
2. **Reflection Probes** — Array-based capture + binding → texture array → shader
3. **CSM+PCSS** — Multi-pass rendering (capture) → CB array → complex shader logic

Use these as templates for implementing other features. The pattern is always:

```
Game Thread              Render Thread             GPU
Component        ──→     Proxy          ──→       CB
(user edits)            (snapshot)              (immutable)
                                                 │
                                            Texture (if needed)
                                                 │
                                              Shader
```

