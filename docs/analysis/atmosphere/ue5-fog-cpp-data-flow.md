<!-- doc-meta
title: UE5 Height Fog — C++ Data Flow with Line Numbers
created: 2026-03-19
category: rendering
tags: fog-rendering, cpp-code, sky-atmosphere
-->

# UE5 Height Fog — C++ Data Flow with Line Numbers

Complete trace of how sky atmosphere data flows from components to GPU in UE5 5.6.

---

## Phase 1: Scene Proxy Initialization

### SkyAtmosphere Component → Scene Proxy

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Components/SkyAtmosphereComponent.cpp`

#### Line 603: Copy HeightFogContribution

```cpp
// Constructor: FSkyAtmosphereSceneProxy::FSkyAtmosphereSceneProxy(const USkyAtmosphereComponent* InComponent)
// Line 603:
HeightFogContribution = InComponent->HeightFogContribution;
```

**Context**: Reads the `HeightFogContribution` property from the game-thread component and stores in the render-thread proxy.

**Default value** (line 585):
```cpp
HeightFogContribution = 1.0f;
```

---

## Phase 2: Per-Frame Atmosphere Light Setup

### SkyAtmosphere Rendering Pass

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/SkyAtmosphereRendering.cpp`

#### Line 454: Sample Transmittance from Atmosphere LUT

```cpp
// Function: UpdateLightSunPropertiesFromAtmosphere(...)
// Line 454:
const FLinearColor TransmittanceTowardSun = SkyAtmosphereSceneProxy.GetAtmosphereSetup()
    .GetTransmittanceAtGroundLevel(AtmosphereLightDirection);
```

**What it does**:
- Calls the atmosphere setup's LUT or computation to get transmittance
- Returns a `FLinearColor` (RGBA) where RGB = color attenuation from space to ground
- This represents how much light gets through the atmosphere (blue scattering, absorption, etc.)

**Example values**:
- Sun at zenith: approximately (1.0, 0.98, 0.95) — slightly reddened
- Sun at 45°: approximately (0.9, 0.85, 0.75) — more orange
- Sun at horizon: approximately (0.7, 0.5, 0.3) — deep red/orange

#### Line 458: Store Transmittance in Light Proxy

```cpp
// Line 458:
AtmosphereLight.Proxy->SetAtmosphereRelatedProperties(TransmittanceTowardSun, SunDiskOuterSpaceLuminance);
```

**What it does**:
- Calls `FLightSceneProxy::SetAtmosphereRelatedProperties()` on the directional light
- Stores the transmittance inside the light proxy for later retrieval

**Called from**: The deferred rendering's atmosphere pass, runs every frame after atmosphere rendering completes.

---

## Phase 3: View Uniform Setup (Before Rendering)

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp`

### Step 1: Copy SkyAtmosphereHeightFogContribution to ViewUniformShaderParameters

#### Line 1440: Get HeightFogContribution from SkyAtmosphere Proxy

```cpp
// Function: SetupViewUniformBufferParameters(...)
// Context: Loop over all views and sky atmosphere components
// Line 1440:
ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = SkyAtmosphereSceneProxy.GetHeightFogContribution();
```

**What it does**:
- Copies the scene proxy's `HeightFogContribution` (default 1.0) to the view uniform
- This value will be accessible in all shaders as `View.SkyAtmosphereHeightFogContribution`

**Default**: 1.0f (full contribution from sky atmosphere)

---

### Step 2: Set Atmosphere Light Illuminance on Ground (Post-Transmittance)

#### Lines 1443-1461: Loop Over Atmosphere Lights

```cpp
// Line 1443:
for (uint8 Index = 0; Index < NUM_ATMOSPHERE_LIGHTS; ++Index)
{
    FLightSceneInfo* Light = Scene->AtmosphereLights[Index];  // Line 1445
    if (Light)
    {
        // Line 1449: Get disc parameters
        const float UsePerPixelAtmosphereTransmittance = Light->Proxy->GetUsePerPixelAtmosphereTransmittance() ? 1.0f : 0.0f;
        ViewUniformShaderParameters.AtmosphereLightDiscCosHalfApexAngle_PPTrans[Index] = FVector4f(FMath::Cos(Light->Proxy->GetSunLightHalfApexAngleRadian()), UsePerPixelAtmosphereTransmittance, 0.0f, 0.0f);

        // Line 1450: Get outer space luminance (sun's intensity)
        ViewUniformShaderParameters.AtmosphereLightDiscLuminance[Index] = Light->Proxy->GetOuterSpaceLuminance();

        // ===== CRITICAL LINE 1451 =====
        // Line 1451: Get sun illuminance AFTER atmospheric transmittance
        ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index]
            = Light->Proxy->GetSunIlluminanceOnGroundPostTransmittance();

        // Line 1452: Alpha = 1.0 means "this light has sky atmosphere, use it in fog"
        ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index].A = 1.0f; // interactions with HeightFogComponent

        // Line 1455: Atmosphere light direction (sun direction)
        ViewUniformShaderParameters.AtmosphereLightDirection[Index] = FVector3f(SkyAtmosphereSceneProxy.GetAtmosphereLightDirection(Index, -Light->Proxy->GetDirection()));
    }
    else
    {
        ClearAtmosphereLightData(Index);
    }
}
```

**Key insight at line 1451-1452**:
- `AtmosphereLightIlluminanceOnGroundPostTransmittance` is computed by the light proxy's `GetSunIlluminanceOnGroundPostTransmittance()`
- The `.A` (alpha) channel is set to 1.0f to flag that this light should interact with height fog

---

## Phase 4: Fog Constants Setup

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/FogRendering.cpp`

### Line 267: Set SkyAtmosphereAmbientContributionColorScale

```cpp
// Function: FSceneRenderer::InitFogConstants()
// Line 219: Check if fog exists
if (Scene->ExponentialFogs.Num() > 0)
{
    const FExponentialHeightFogSceneInfo& FogInfo = Scene->ExponentialFogs[0];

    // ... fog parameter computations ...

    // Line 267: Copy fog's sky atmosphere contribution color scale
    View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale;
```

**What it is**:
- Property on the `AExponentialHeightFog` actor
- Typically a `FLinearColor` like `(1, 1, 1, 1)` (white, full contribution)
- Controls how much the sky light's color contributes to fog ambient color

---

### Line 85: Pack SkyAtmosphereAmbientContributionColorScale into Uniform Buffer

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/FogRendering.cpp`

```cpp
// Function: SetupFogUniformParameters(...)
// Line 85:
OutParameters.SkyAtmosphereAmbientContributionColorScale = View.SkyAtmosphereAmbientContributionColorScale;
```

**Context**: Called from `CreateFogUniformBuffer()` at line 120, packs all fog parameters into `FFogUniformParameters` struct.

**Struct definition** in `HeightFogCommon.ush` line 70:
```hlsl
float4 SkyAtmosphereAmbientContributionColorScale;
```

---

## Phase 5: Light Proxy Storage of Transmittance

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Components/DirectionalLightComponent.cpp`

### Line 582-584: SetAtmosphereRelatedProperties Called

```cpp
// In FDirectionalLightSceneProxy class:
// Line 582-584:
virtual void SetAtmosphereRelatedProperties(FLinearColor TransmittanceTowardSunIn, FLinearColor SunDiscOuterSpaceLuminanceIn) override
{
    TransmittanceTowardSun = TransmittanceTowardSunIn;  // ← Stored for later retrieval
    SunDiscOuterSpaceLuminance = SunDiscOuterSpaceLuminanceIn;
}
```

**Member variable** (line 248):
```cpp
FLinearColor TransmittanceTowardSun;
```

**Default** (line 317):
```cpp
TransmittanceTowardSun(FLinearColor::White)  // (1, 1, 1, 1)
```

---

### Lines 598-606: Transmittance Retrieval for Fog

```cpp
// Line 598-601: GetAtmosphereTransmittanceTowardSun()
virtual FLinearColor GetAtmosphereTransmittanceTowardSun() const override
{
    return TransmittanceTowardSun;  // ← Returns the stored transmittance
}

// Line 603-606: GetSunIlluminanceOnGroundPostTransmittance()
virtual FLinearColor GetSunIlluminanceOnGroundPostTransmittance() const override
{
    return GetOuterSpaceIlluminance() * GetAtmosphereTransmittanceTowardSun();
    // = Light Color * Transmittance
}
```

**Computation**:
- `GetOuterSpaceIlluminance()` (line 593-595) returns the light's direct color (untransformed)
- Multiplied by transmittance (RGB only, no alpha)
- Result: the light's apparent color as seen from ground level after atmospheric filtering

**Example**:
- Light color: (1.0, 1.0, 1.0) white
- Transmittance: (0.8, 0.7, 0.5) — blue-depleted orange
- Result: (0.8, 0.7, 0.5) — sun appears orange at horizon due to atmosphere

---

## Phase 6: Shader Parameter Binding

**File**: `W:/UE_5.6_Source/Engine/Source/Runtime/Renderer/Private/FogRendering.cpp`

### Lines 117-122: Create Fog Uniform Buffer

```cpp
// Line 117-122:
TRDGUniformBufferRef<FFogUniformParameters> CreateFogUniformBuffer(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
    auto* FogStruct = GraphBuilder.AllocParameters<FFogUniformParameters>();
    SetupFogUniformParameters(GraphBuilder, View, *FogStruct);  // ← Fills the struct
    return GraphBuilder.CreateUniformBuffer(FogStruct);  // ← Registers with RDG
}
```

**Called from**: `RenderFog()` at line 548, creates RDG uniform buffer that shaders will bind.

---

## Phase 7: GPU Shader Execution

Once the uniform buffers are created and bound, the shader code runs:

1. **Fog pixel shader** reads uniform buffer
2. Calls `ComputeInscatteringColor()` → adds sky atmosphere ambient
3. Calls directional inscattering loop → injects `AtmosphereLightIlluminanceOnGroundPostTransmittance`
4. Blends inscattering + directional components
5. Outputs fog color to render target

---

## Complete Data Flow Diagram

```
USkyAtmosphereComponent (game thread)
    └─ HeightFogContribution (property, default 1.0f)
        │
        └─→ FSkyAtmosphereSceneProxy (render thread proxy)
            └─ HeightFogContribution member (line 603)
                │
                └─→ FViewInfo.SkyAtmosphereHeightFogContribution (line 1440)
                    │
                    └─→ ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution
                        │
                        └─→ GPU shader: View.SkyAtmosphereHeightFogContribution
                            ├─ Used at HeightFogCommon.ush:175 (ambient)
                            └─ Used at HeightFogCommon.ush:319 (directional)

FAtmosphereSetup::GetTransmittanceAtGroundLevel() [atmosphere LUT sample]
    └─→ SkyAtmosphereRendering.cpp:454 (TransmittanceTowardSun)
        │
        └─→ SkyAtmosphereRendering.cpp:458 (SetAtmosphereRelatedProperties)
            │
            └─→ DirectionalLightSceneProxy.TransmittanceTowardSun member
                │
                └─→ GetAtmosphereTransmittanceTowardSun() (line 600)
                    │
                    └─→ GetSunIlluminanceOnGroundPostTransmittance() (line 605)
                        = Light.Color * Transmittance
                        │
                        └─→ SceneRendering.cpp:1451
                            │
                            └─→ ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index]
                                │
                                └─→ GPU shader: InView.AtmosphereLightIlluminanceOnGroundPostTransmittance[0]
                                    └─ Used at HeightFogCommon.ush:319

AExponentialHeightFog (game thread)
    └─ SkyAtmosphereAmbientContributionColorScale (property, typically white)
        │
        └─→ FExponentialHeightFogSceneInfo
            │
            └─→ FogRendering.cpp:267 (FViewInfo.SkyAtmosphereAmbientContributionColorScale)
                │
                └─→ FogRendering.cpp:85 (FFogUniformParameters.SkyAtmosphereAmbientContributionColorScale)
                    │
                    └─→ GPU shader: FogStruct.SkyAtmosphereAmbientContributionColorScale
                        └─ Used at HeightFogCommon.ush:175 (line: SkyAmbient = ... * SkyAtmosphereAmbientContributionColorScale.rgb * ...)
```

---

## Critical Values Summary

| Value | Source | Default | Purpose |
|-------|--------|---------|---------|
| `SkyAtmosphereHeightFogContribution` | SkyAtmosphere component | 1.0f | Master blend factor (0=no sky effect, 1=full) |
| `SkyAtmosphereAmbientContributionColorScale` | HeightFog component | (1,1,1,1) | Scales ambient sky color contribution |
| `AtmosphereLightIlluminanceOnGroundPostTransmittance` | Computed from transmittance LUT | White (no atmosphere) | Light color after atmospheric filtering |
| `Transmittance` | Atmosphere LUT sample | (1,1,1) | How much light gets through (0=blocked, 1=unfiltered) |

---

## Files and Line References

| Step | File | Line | Code |
|------|------|------|------|
| 1 | SkyAtmosphereComponent.cpp | 603 | `HeightFogContribution = InComponent->HeightFogContribution;` |
| 2 | SkyAtmosphereRendering.cpp | 454 | `const FLinearColor TransmittanceTowardSun = ...GetTransmittanceAtGroundLevel(...);` |
| 3 | SkyAtmosphereRendering.cpp | 458 | `SetAtmosphereRelatedProperties(TransmittanceTowardSun, ...);` |
| 4 | SceneRendering.cpp | 1440 | `ViewUniformShaderParameters.SkyAtmosphereHeightFogContribution = ...GetHeightFogContribution();` |
| 5 | SceneRendering.cpp | 1451 | `ViewUniformShaderParameters.AtmosphereLightIlluminanceOnGroundPostTransmittance[Index] = ...GetSunIlluminanceOnGroundPostTransmittance();` |
| 6 | SceneRendering.cpp | 1452 | `...AtmosphereLightIlluminanceOnGroundPostTransmittance[Index].A = 1.0f;` |
| 7 | FogRendering.cpp | 267 | `View.SkyAtmosphereAmbientContributionColorScale = FogInfo.SkyAtmosphereAmbientContributionColorScale;` |
| 8 | FogRendering.cpp | 85 | `OutParameters.SkyAtmosphereAmbientContributionColorScale = View.SkyAtmosphereAmbientContributionColorScale;` |
| 9 | DirectionalLightComponent.cpp | 605 | `return GetOuterSpaceIlluminance() * GetAtmosphereTransmittanceTowardSun();` |

---

## Shader-Side Code Validation Points

To verify this flow in shaders, look for:

1. **HeightFogCommon.ush:175** — Sky ambient contribution must include `SkyAtmosphereHeightFogContribution` multiplier
2. **HeightFogCommon.ush:319** — Directional inscattering must add `AtmosphereLightIlluminanceOnGroundPostTransmittance`
3. Both should only compile under `#if PROJECT_SUPPORT_SKY_ATMOSPHERE_AFFECTS_HEIGHFOG`

