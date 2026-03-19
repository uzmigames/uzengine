<!-- doc-meta
title: UE5 Far Shadow Cascades - Exact Code Snippets
description: All critical code snippets from UE5.6 source with exact file:line references
created: 2026-03-16
status: reference
tags: [ue5-source, code-snippets, shadows]
-->

# UE5 Far Shadow Cascades — Exact Code Snippets

All code extracted directly from W:\UE_5.6_Source with exact file:line references.

## 1. Component Property Declarations

**File**: `Engine\Source\Runtime\Engine\Classes\Components\DirectionalLightComponent.h`

```cpp
// Lines 110-118
/** 0: no Far Shadow Cascades, otherwise the number of cascades between DynamicShadowDistance
    and FarShadowDistance that are covered by Far Shadow Cascades. */
UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=CascadedShadowMaps,
          meta=(UIMin = "0", UIMax = "4"), DisplayName = "Far Shadow Cascade Count")
int32 FarShadowCascadeCount;

/**
 * Distance at which the far shadow cascade should end.  Far shadows will cover the range
 * between 'Dynamic Shadow Distance' and this distance.
 */
UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=CascadedShadowMaps,
          meta=(UIMin = "0", UIMax = "800000"), DisplayName = "Far Shadow Distance")
float FarShadowDistance;
```

## 2. Scene Proxy Members

**File**: `Engine\Source\Runtime\Engine\Public\LightSceneProxy.h`

```cpp
// Lines 502-506
/** Only for whole scene directional lights, if FarShadowCascadeCount > 0 and
    FarShadowDistance >= WholeSceneDynamicShadowRadius, where far shadow cascade should end. */
float FarShadowDistance;

/** Only for whole scene directional lights, 0: no FarShadowCascades, otherwise the count of
    cascades between WholeSceneDynamicShadowRadius and FarShadowDistance that are covered by
    distant shadow cascades. */
uint32 FarShadowCascadeCount;
```

## 3. Initialization & Validation

**File**: `Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp`

### Default Values (Lines 1023)
```cpp
FarShadowDistance = 300000.0f;
```

### Post-Edit Clamping (Lines 1076)
```cpp
FarShadowCascadeCount = FMath::Clamp(FarShadowCascadeCount, 0, 10);
```

## 4. Console Variables

**File**: `Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp`

### CVarMaxNumFarShadowCascades (Lines 62-66)
```cpp
static TAutoConsoleVariable<int32> CVarMaxNumFarShadowCascades(
    TEXT("r.Shadow.MaxNumFarShadowCascades"),
    10,
    TEXT("Max number of far shadow cascades that can be cast from a directional light"),
    ECVF_RenderThreadSafe | ECVF_Scalability);
```

### GFarShadowStaticMeshLODBias (Lines 68-74)
```cpp
ENGINE_API int32 GFarShadowStaticMeshLODBias = 0;
FAutoConsoleVariableRef CVarFarShadowStaticMeshLODBias(
    TEXT("r.Shadow.FarShadowStaticMeshLODBias"),
    GFarShadowStaticMeshLODBias,
    TEXT("Notice: only selected geometry types (static meshes and landscapes) respect this value."),
    ECVF_Scalability | ECVF_RenderThreadSafe);
```

### CVarFarShadowDistanceOverride (Lines 76-80)
```cpp
static TAutoConsoleVariable<float> CVarFarShadowDistanceOverride(
    TEXT("r.Shadow.FarShadowDistanceOverride"),
    0.0f,
    TEXT("Overriding far shadow distance for all directional lights"),
    ECVF_RenderThreadSafe | ECVF_Scalability);
```

## 5. Activation Condition

**File**: `Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (Lines 343-352)

```cpp
WholeSceneDynamicShadowRadius = ComputeWholeSceneDynamicShadowRadius(
    Component->Mobility,
    Component->DynamicShadowDistanceMovableLight,
    Component->DynamicShadowDistanceStationaryLight);

const float FarCascadeSize = Component->FarShadowDistance - WholeSceneDynamicShadowRadius;

// 100 is just some number to avoid cascades of 0 size, the user still can still create
// many tiny cascades (very inefficient)
if(Component->FarShadowCascadeCount && FarCascadeSize > 100.0f)
{
    FarShadowDistance = Component->FarShadowDistance;
    FarShadowCascadeCount = Component->FarShadowCascadeCount;
}
```

## 6. Total Cascade Count Calculation

**File**: `Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (Lines 476-483)

```cpp
/** Returns the number of view dependent shadows this light will create,
    not counting distance field shadow cascades. */
virtual uint32 GetNumViewDependentWholeSceneShadows(const FSceneView& View,
                                                    bool bPrecomputedLightingIsValid) const override
{
    uint32 ClampedFarShadowCascadeCount = FMath::Min(
        (uint32)CVarMaxNumFarShadowCascades.GetValueOnAnyThread(),
        FarShadowCascadeCount);

    uint32 TotalCascades = GetNumShadowMappedCascades(View.MaxShadowCascades, bPrecomputedLightingIsValid)
                         + ClampedFarShadowCascadeCount;

    return TotalCascades;
}
```

## 7. bFarShadowCascade Flag Assignment

**File**: `Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (Line 517)

```cpp
OutInitializer.CascadeSettings.bFarShadowCascade = !bRayTracedCascade
                                                 && OutInitializer.CascadeSettings.ShadowSplitIndex
                                                    >= (int32)NumNearCascades;
```

## 8. Distance Split Formula (Far Cascades)

**File**: `Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (Lines 800-814)

```cpp
else
{
    // the far cascades start at the after the near cascades
    uint32 ClampedFarShadowCascadeCount = FMath::Min(
        (uint32)CVarMaxNumFarShadowCascades.GetValueOnAnyThread(),
        FarShadowCascadeCount);

    return CascadeDistanceWithoutFar
         + ComputeAccumulatedScale(
             EffectiveCascadeDistributionExponent,
             SplitIndex - NumNearCascades,
             ClampedFarShadowCascadeCount)
         * ((CVarFarShadowDistanceOverride.GetValueOnAnyThread() > 0.0f
             ? CVarFarShadowDistanceOverride.GetValueOnAnyThread()
             : FarShadowDistance) - CascadeDistanceWithoutFar);
}
```

**For Main Cascades** (Lines 810-814):
```cpp
else
{
    return ShadowNear
         + ComputeAccumulatedScale(EffectiveCascadeDistributionExponent, SplitIndex, NumNearCascades)
         * (CascadeDistanceWithoutFar - ShadowNear);
}
```

## 9. LOD Bias Application

**File**: `Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (Lines 1798-1814)

```cpp
if (CascadeSettings.bFarShadowCascade)
{
    if (!ShadowLODToRender.IsLODRange()) // todo: GPU LOD doesn't support this bias.
    {
        extern ENGINE_API int32 GFarShadowStaticMeshLODBias;
        int8 LODToRenderScan = ShadowLODToRender.LODIndex0 + GFarShadowStaticMeshLODBias;

        for (int32 Index = PrimitiveSceneInfo->StaticMeshRelevances.Num() - 1; Index >= 0; Index--)
        {
            if (LODToRenderScan == PrimitiveSceneInfo->StaticMeshRelevances[Index].GetLODIndex())
            {
                ShadowLODToRender.SetLOD(LODToRenderScan);
                break;
            }
        }
    }
}
```

## 10. Primitive Filtering for Far Cascades

**File**: `Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (Lines 2124-2148)

```cpp
bool FProjectedShadowInfo::TestPrimitiveFarCascadeConditions(
    bool bPrimitiveCastsFarShadow,
    const FBoxSphereBounds& Bounds) const
{
    if (CascadeSettings.bFarShadowCascade)
    {
        return bPrimitiveCastsFarShadow;
    }

    if (GEnableNonNaniteVSM != 0 && bVSM)
    {
        const bool bWholeSceneDirectionalShadow = IsWholeSceneDirectionalShadow();

        if (bWholeSceneDirectionalShadow && MaxNonFarCascadeDistance > 0.0f)
        {
            check(DependentView);

            FVector ViewOrigin = DependentView->GetShadowViewMatrices().GetViewOrigin();
            float ViewDistance = (Bounds.Origin - ViewOrigin) | DependentView->GetViewDirection();
            if (ViewDistance - Bounds.SphereRadius > MaxNonFarCascadeDistance)
            {
                return bPrimitiveCastsFarShadow;
            }
        }
    }
    return true;
}
```

**Used in AddSubjectPrimitive** (Lines 2178-2181):
```cpp
if (!ReceiverPrimitives.Contains(PrimitiveSceneInfo)
    // Far cascade only casts from primitives marked for it
    && TestPrimitiveFarCascadeConditions(
        PrimitiveSceneInfoCompact.Proxy->CastsFarShadow(),
        FBoxSphereBounds(PrimitiveSceneInfoCompact.Bounds)))
{
```

## 11. LOD Distance Factor Selection

**File**: `Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp` (Lines 1197-1200)

```cpp
float FProjectedShadowInfo::GetLODDistanceFactor() const
{
    return CascadeSettings.bFarShadowCascade ? GFarShadowLODDistanceFactor
                                             : GShadowLODDistanceFactor
                                               + CascadeSettings.ShadowSplitIndex
                                               * GShadowLODDistanceFactorCascadeScale;
}
```

## 12. LOD Distance Factor CVars

**File**: `Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp` (Lines 55-74)

```cpp
static float GShadowLODDistanceFactor = 1;
static FAutoConsoleVariableRef CVarShadowScreenMultiple(
    TEXT("r.Shadow.LODDistanceFactor"),
    GShadowLODDistanceFactor,
    TEXT("Multiplier for LOD selection distance when rendering regular shadows (global)"),
    ECVF_RenderThreadSafe);

static float GShadowLODDistanceFactorCascadeScale = 0.0f;
static FAutoConsoleVariableRef CVarShadowScreenMultipleCascadeScale(
    TEXT("r.Shadow.LODDistanceFactor.CascadeScale"),
    GShadowLODDistanceFactorCascadeScale,
    TEXT("Multiplier for LOD selection distance when rendering regular shadows "
         "(scales with cascade index)"),
    ECVF_RenderThreadSafe);

static float GFarShadowLODDistanceFactor = 1;
static FAutoConsoleVariableRef CVarFarShadowScreenMultiple(
    TEXT("r.Shadow.FarShadow.LODDistanceFactor"),
    GFarShadowLODDistanceFactor,
    TEXT("Multiplier for LOD selection distance when rendering far shadows"),
    ECVF_RenderThreadSafe);
```

## 13. Draw Flags Assignment

**File**: `Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (Lines 2743-2746)

```cpp
if (CascadeSettings.bFarShadowCascade)
{
    (int32&)ShadowDepthView->DrawDynamicFlags |= (int32)EDrawDynamicFlags::FarShadowCascade;
}
```

## 14. Distance Override Application

**File**: `Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (Line 535)

```cpp
FarDistance = FMath::Max(FarDistance,
    CVarFarShadowDistanceOverride.GetValueOnAnyThread() > 0.0f
        ? CVarFarShadowDistanceOverride.GetValueOnAnyThread()
        : FarShadowDistance);
```

## Summary Table

| Feature | Default | CVar | File:Line |
|---------|---------|------|-----------|
| Distance | 300,000 cm | r.Shadow.FarShadowDistanceOverride | DLC.cpp:1023 |
| Count | 0 | r.Shadow.MaxNumFarShadowCascades | DLC.cpp:64 |
| LOD Bias | 0 | r.Shadow.FarShadowStaticMeshLODBias | DLC.cpp:68 |
| LOD Factor | 1.0 | r.Shadow.FarShadow.LODDistanceFactor | SDR.cpp:72 |

**Key Files**:
- DLC.cpp = DirectionalLightComponent.cpp
- SDR.cpp = ShadowDepthRendering.cpp
