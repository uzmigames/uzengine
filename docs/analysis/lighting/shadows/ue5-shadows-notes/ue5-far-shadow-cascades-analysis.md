<!-- doc-meta
title: UE5 Far Shadow Cascades System Analysis
description: Complete architectural analysis of UE5's far shadow cascades implementation, patterns, and design decisions for UzEngine adaptation
created: 2026-03-16
updated: 2026-03-16
status: analysis
tags: [ue5-architecture, rendering, shadows, cascades, lighting]
references:
  - W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\DirectionalLightComponent.h
  - W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp
  - W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\LightSceneProxy.h
  - W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\LightSceneProxy.cpp
  - W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp
  - W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp
-->

# UE5 Far Shadow Cascades System — Architectural Analysis

## Overview

UE5's Far Shadow Cascades (FSC) extend cascaded shadow mapping beyond the main dynamic shadow distance to cover distant terrain with lower-resolution, lower-LOD geometry. This analysis covers the exact implementation, design patterns, and key decisions for UzEngine adaptation.

## 1. Separation Strategy: Range-Based, Not Resolution-Based

### Problem Solved

Cascaded Shadow Maps (CSM) traditionally have a fixed distance budget. Beyond this distance, distant geometry either:
- Falls into a single coarse cascade (poor quality)
- Casts no shadows at all (visual artifacts)

Far cascades solve this by extending the CSM system into a second tier with explicit distance range and LOD control.

### How UE5 Implements It

**Range Partition** (DirectionalLightComponent.cpp:345-352):
```cpp
const float FarCascadeSize = Component->FarShadowDistance - WholeSceneDynamicShadowRadius;

// 100 is just some number to avoid cascades of 0 size
if(Component->FarShadowCascadeCount && FarCascadeSize > 100.0f)
{
    FarShadowDistance = Component->FarShadowDistance;
    FarShadowCascadeCount = Component->FarShadowCascadeCount;
}
```

**Key Points**:
- `WholeSceneDynamicShadowRadius` = end of main CSM range
- `FarShadowDistance` = end of far cascade range
- Minimum viable size: 100 cm (arbitrary guard against degenerate cascades)
- Cascades only activate if both conditions met:
  1. `FarShadowCascadeCount > 0`
  2. `FarShadowDistance - WholeSceneDynamicShadowRadius > 100.0f`

### Resolution Differences

Unlike initial assumptions, **UE5 does NOT automatically reduce resolution for far cascades**. Both tiers:
- Use same shadow atlas
- Use same resolution per cascade
- Use same filtering (PCF/VSM)

The only difference is **LOD selection** (see section 4).

## 2. Default Parameters

| Parameter | Default | Notes | File:Line |
|-----------|---------|-------|-----------|
| `FarShadowDistance` | 300,000 cm (3 km) | World units, same scale as main CSM | DirectionalLightComponent.cpp:1023 |
| `FarShadowCascadeCount` | 0 | Disabled by default | DirectionalLightComponent.h:112 |
| `CVarMaxNumFarShadowCascades` | 10 | Hard cap on active cascades | DirectionalLightComponent.cpp:64 |
| `GFarShadowStaticMeshLODBias` | 0 | No LOD offset by default | DirectionalLightComponent.cpp:68 |
| `GFarShadowLODDistanceFactor` | 1.0 | Scale for LOD selection distance | ShadowDepthRendering.cpp:72 |
| `r.Shadow.FarShadowDistanceOverride` | 0.0 (disabled) | CVar to override FarShadowDistance globally | DirectionalLightComponent.cpp:77-80 |

**Constructor Initialization** (DirectionalLightComponent.cpp:1023):
```cpp
FarShadowDistance = 300000.0f;  // 3 km world units
```

**Clamping** (DirectionalLightComponent.cpp:1076):
```cpp
FarShadowCascadeCount = FMath::Clamp(FarShadowCascadeCount, 0, 10);
```

## 3. Cascade Splits Computation

### Total Cascade Count

**Formula** (DirectionalLightComponent.cpp:480):
```cpp
uint32 TotalCascades = GetNumShadowMappedCascades(View.MaxShadowCascades, bPrecomputedLightingIsValid)
                     + ClampedFarShadowCascadeCount;

uint32 ClampedFarShadowCascadeCount = FMath::Min(
    (uint32)CVarMaxNumFarShadowCascades.GetValueOnAnyThread(),
    FarShadowCascadeCount
);
```

**Example**: 4 main cascades + 2 far cascades = 6 total cascades

### Per-Cascade Distance Split

**For Main Cascades** (DirectionalLightComponent.cpp:812):
```cpp
// SplitIndex = 0 .. NumNearCascades-1
return ShadowNear + ComputeAccumulatedScale(Exponent, SplitIndex, NumNearCascades)
                  * (CascadeDistanceWithoutFar - ShadowNear);
```

**For Far Cascades** (DirectionalLightComponent.cpp:807):
```cpp
// SplitIndex = NumNearCascades .. NumNearCascades + FarCount - 1
uint32 ClampedFarShadowCascadeCount = FMath::Min(
    (uint32)CVarMaxNumFarShadowCascades.GetValueOnAnyThread(),
    FarShadowCascadeCount
);

return CascadeDistanceWithoutFar
     + ComputeAccumulatedScale(
         EffectiveCascadeDistributionExponent,
         SplitIndex - NumNearCascades,          // Relative index within far cascades
         ClampedFarShadowCascadeCount
       )
     * (FarShadowDistance - CascadeDistanceWithoutFar);
```

**Key Insight**: Far cascades use the **same distribution exponent** as main cascades, but relative to the far-tier start distance.

Example split distribution (4 main + 2 far, exponent=3.0):
```
Main CSM:     0 .. 1000m .. 2000m .. 4000m .. 10000m
Far CSM:      10000m .. 50000m .. 300000m
                        |-- relative 0 .. 1 scale applied twice --|
```

### bFarShadowCascade Flag Assignment

**Source** (DirectionalLightComponent.cpp:517):
```cpp
OutInitializer.CascadeSettings.bFarShadowCascade = !bRayTracedCascade
                                                 && OutInitializer.CascadeSettings.ShadowSplitIndex
                                                    >= (int32)NumNearCascades;
```

This flag determines:
- Which LOD distance factor to use
- Whether to apply `GFarShadowStaticMeshLODBias`
- Which primitives can cast shadows (`CastsFarShadow()` filter)

## 4. LOD Bias for Far Shadows

### Static Mesh LOD Selection

**Application** (ShadowSetup.cpp:1798-1814):
```cpp
if (CascadeSettings.bFarShadowCascade)
{
    if (!ShadowLODToRender.IsLODRange())  // Only single-LOD, not GPU LOD ranges
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

**Behavior**:
- Default bias = 0 (use same LOD as main cascades)
- Positive bias forces coarser LOD (higher number = lower detail)
- Only applies to static meshes (GPU LOD ranges are not supported)
- Respects existing LOD availability — doesn't create invalid LODs

**Console Variable** (DirectionalLightComponent.cpp:69-74):
```cpp
ENGINE_API int32 GFarShadowStaticMeshLODBias = 0;
FAutoConsoleVariableRef CVarFarShadowStaticMeshLODBias(
    TEXT("r.Shadow.FarShadowStaticMeshLODBias"),
    GFarShadowStaticMeshLODBias,
    TEXT("Notice: only selected geometry types (static meshes and landscapes) respect this value."),
    ECVF_Scalability | ECVF_RenderThreadSafe
);
```

### LOD Distance Factor

Controls how LOD selection distance is computed for primitives.

**Source** (ShadowDepthRendering.cpp:1197-1200):
```cpp
float FProjectedShadowInfo::GetLODDistanceFactor() const
{
    return CascadeSettings.bFarShadowCascade ? GFarShadowLODDistanceFactor
                                             : GShadowLODDistanceFactor
                                               + CascadeSettings.ShadowSplitIndex
                                               * GShadowLODDistanceFactorCascadeScale;
}
```

**CVars** (ShadowDepthRendering.cpp:55-74):
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
    TEXT("Multiplier for LOD selection distance when rendering regular shadows (scales with cascade index)"),
    ECVF_RenderThreadSafe);

static float GFarShadowLODDistanceFactor = 1;
static FAutoConsoleVariableRef CVarFarShadowScreenMultiple(
    TEXT("r.Shadow.FarShadow.LODDistanceFactor"),
    GFarShadowLODDistanceFactor,
    TEXT("Multiplier for LOD selection distance when rendering far shadows"),
    ECVF_RenderThreadSafe);
```

**Mechanism**:
- Default all factors = 1.0 (no scaling)
- Far cascades use independent `GFarShadowLODDistanceFactor`
- Main cascades scale per-cascade index (if `GShadowLODDistanceFactorCascadeScale > 0`)

## 5. Rendering Differences

### Draw Flags

**Propagation** (ShadowSetup.cpp:2743-2746):
```cpp
if (CascadeSettings.bFarShadowCascade)
{
    (int32&)ShadowDepthView->DrawDynamicFlags |= (int32)EDrawDynamicFlags::FarShadowCascade;
}
```

The `FarShadowCascade` flag informs the render thread:
- Which LOD distance factor to use
- Whether mesh LOD bias applies
- Used for debug visualization and profiling

### Primitive Culling

**Main CSM vs Far CSM** (ShadowSetup.cpp:2124-2148):
```cpp
bool FProjectedShadowInfo::TestPrimitiveFarCascadeConditions(
    bool bPrimitiveCastsFarShadow,
    const FBoxSphereBounds& Bounds) const
{
    if (CascadeSettings.bFarShadowCascade)
    {
        // Far cascades: only include primitives flagged CastsFarShadow
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

**Behavior**:
- **Main cascades**: Include all primitives with `CastShadow=true`
- **Far cascades**: Only include primitives with `CastsFarShadow=true` (explicit opt-in)
- VSM mode has additional distance-based culling for optimized rendering

### Shadow Filtering & Resolution

**No automatic changes**:
- Far cascades use same shadow map resolution per cascade
- Same PCF/VSM filtering as main cascades
- Distinction is purely distance range and LOD selection

The "lower quality" impression comes from LOD reduction, not filtering changes.

## 6. Atlas Layout

### Single Contiguous Atlas

**Architecture** (DirectionalLightComponent.cpp:480):
```cpp
// All cascades (main + far) use sequential indices in same shadow atlas
uint32 TotalCascades = GetNumShadowMappedCascades(...) + ClampedFarShadowCascadeCount;
```

**Layout**:
```
Shadow Atlas
├─ Cascade 0 (main CSM)    [0 .. 1000m]
├─ Cascade 1 (main CSM)    [1000m .. 2000m]
├─ Cascade 2 (main CSM)    [2000m .. 4000m]
├─ Cascade 3 (main CSM)    [4000m .. 10000m]
├─ Cascade 4 (far CSM)     [10000m .. 50000m]
└─ Cascade 5 (far CSM)     [50000m .. 300000m]
```

**Allocation**:
- No special per-cascade resolution tweaking required in atlas
- VSM atlas allocates same space per cascade
- Mobile atlasing identical

**Distance Override** (DirectionalLightComponent.cpp:76-80, 535):
```cpp
static TAutoConsoleVariable<float> CVarFarShadowDistanceOverride(
    TEXT("r.Shadow.FarShadowDistanceOverride"),
    0.0f,
    TEXT("Overriding far shadow distance for all directional lights"),
    ECVF_RenderThreadSafe | ECVF_Scalability);

// Applied as:
FarDistance = FMath::Max(FarDistance,
    CVarFarShadowDistanceOverride.GetValueOnAnyThread() > 0.0f ?
        CVarFarShadowDistanceOverride.GetValueOnAnyThread() :
        FarShadowDistance);
```

## 7. Performance Characteristics

### Optimization Levers

| Lever | Method | Impact |
|-------|--------|--------|
| **Cascade Count** | `FarShadowCascadeCount` or `r.Shadow.MaxNumFarShadowCascades` | Linear cost in shadow rendering |
| **LOD Reduction** | `GFarShadowStaticMeshLODBias` | Reduces geometry complexity, major win |
| **Distance Range** | `FarShadowDistance` | Sets outer boundary, affects cascade split math |
| **LOD Distance** | `r.Shadow.FarShadow.LODDistanceFactor` | Affects mesh LOD selection in shadow pass |

### Clamping for Scalability

**Effective Cascades** (DirectionalLightComponent.cpp:805):
```cpp
uint32 ClampedFarShadowCascadeCount = FMath::Min(
    (uint32)CVarMaxNumFarShadowCascades.GetValueOnAnyThread(),
    FarShadowCascadeCount
);
```

This allows:
- Editor/high-end: `CVarMaxNumFarShadowCascades = 10`
- Mid-tier: `CVarMaxNumFarShadowCascades = 2` (CVar override, no component change)
- Low-end: `CVarMaxNumFarShadowCascades = 0` (disables all far cascades)

## 8. Key Anti-Patterns Found

### ❌ Resolution-Based Thinking

**Wrong**: "Far cascades have lower resolution"
**Right**: "Far cascades use coarser LOD geometry"

UE5 does not reduce shadow map resolution for far cascades. The perceived quality difference is purely from LOD selection.

### ❌ Automatic Geometry Filtering

**Wrong**: "Far cascades automatically exclude small objects"
**Right**: "Far cascades respect `CastsFarShadow` primitive flag"

There is no automatic scale-based filtering. Objects must explicitly opt in or be excluded.

### ❌ Independent Atlas Allocation

**Wrong**: "Far cascades use a separate shadow atlas"
**Right**: "Far cascades are sequential entries in the main atlas"

No special allocation strategy. Standard cascade atlas logic applies.

## UzEngine Adaptation Strategy

### Pattern 1: Range-Partitioned Cascades

**UzEngine Implementation**:
```c
// In light component properties
struct uz_directional_light_t {
    float far_shadow_distance;      // End of far cascade range
    int32 far_shadow_cascade_count; // Number of far cascades
};

// In cascade setup
float far_range_size = light->far_shadow_distance - main_csm_distance;
if (light->far_shadow_cascade_count > 0 && far_range_size > 100.0f) {
    // Enable far cascades
}
```

### Pattern 2: Separate Culling via Flags

**UzEngine Implementation**:
```c
struct uz_mesh_t {
    bool cast_shadow;        // Standard CSM
    bool cast_far_shadow;    // Far cascade opt-in
};

// In shadow rendering
if (cascade_is_far) {
    include_mesh = mesh->cast_far_shadow;
} else {
    include_mesh = mesh->cast_shadow;
}
```

### Pattern 3: LOD Bias as Tuning Knob

**UzEngine Implementation**:
```c
// CVar
int32 g_far_shadow_lod_bias = 0;  // +1 = one LOD coarser

// In LOD selection
int32 lod_index = mesh->compute_shadow_lod(distance_factor);
if (cascade_is_far && lod_index + g_far_shadow_lod_bias < mesh->lod_count) {
    lod_index += g_far_shadow_lod_bias;
}
```

### Pattern 4: Distance-Based Distribution

**UzEngine Implementation**:
```c
// Reuse existing ComputeAccumulatedScale from main CSM
float SplitDistance(int split_index, int far_count, float exponent) {
    if (split_index < num_main_cascades) {
        // Main cascade formula
        return main_near + accum_scale(exponent, split_index, num_main_cascades)
                         * (main_distance - main_near);
    } else {
        // Far cascade formula
        int far_index = split_index - num_main_cascades;
        return main_distance + accum_scale(exponent, far_index, far_count)
                             * (far_distance - main_distance);
    }
}
```

## Summary

**Key Architectural Decisions**:

1. **Range-based separation** isolates far cascades to distant distance band
2. **Explicit opt-in via flags** prevents geometry overhead
3. **LOD bias as primary optimization** reduces geometry complexity far more than resolution
4. **Reused distribution math** avoids code duplication
5. **Console variable override** enables scalability without code changes

**Why This Works**:
- Distant shadows are inherently lower quality (camera perspective)
- Coarser LOD geometry is imperceptible at distance
- Explicit cascades avoid hard transition artifacts
- Atlas remains contiguous (no fragmentation)

**Adaptation Complexity**: LOW
- Few new engine systems needed
- Reuses existing CSM infrastructure
- Straightforward primitive filtering
- No new GPU techniques required
