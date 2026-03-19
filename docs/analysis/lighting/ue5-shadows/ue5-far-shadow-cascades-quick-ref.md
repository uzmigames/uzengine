<!-- doc-meta
title: UE5 Far Shadow Cascades - Quick Reference
description: One-page reference of UE5 far shadow cascade parameters, defaults, and code locations
created: 2026-03-16
status: quick-reference
tags: [ue5-shadows, quick-ref, rendering]
-->

# UE5 Far Shadow Cascades — Quick Reference

## File Locations

| Component | File | Lines |
|-----------|------|-------|
| **Light Component Decl** | `DirectionalLightComponent.h` | 110-118 |
| **Proxy Decl** | `LightSceneProxy.h` | 502-506 |
| **Shadow Setup** | `ShadowSetup.cpp` | 1798-1814 (LOD bias), 2124-2148 (culling) |
| **Shadow Rendering** | `ShadowDepthRendering.cpp` | 1197-1200 (LOD factor), 55-74 (CVars) |
| **Initialization** | `DirectionalLightComponent.cpp` | 345-352 (enable), 1023 (default), 1076 (clamp) |
| **Distance Calc** | `DirectionalLightComponent.cpp` | 807 (far formula), 480 (total cascades) |
| **bFarShadowCascade Flag** | `DirectionalLightComponent.cpp` | 517 |

## Property Defaults

```cpp
// DirectionalLightComponent.cpp:1023, 1076
FarShadowDistance = 300000.0f;           // 3 km (world units)
FarShadowCascadeCount = 0;               // Disabled by default
// Post-edit clamp: 0 <= FarShadowCascadeCount <= 10
```

## Console Variables

```cpp
// DirectionalLightComponent.cpp:62-66
r.Shadow.MaxNumFarShadowCascades = 10    // Hard limit on active cascades

// DirectionalLightComponent.cpp:68-74
r.Shadow.FarShadowStaticMeshLODBias = 0  // LOD offset (only static meshes)

// DirectionalLightComponent.cpp:76-80
r.Shadow.FarShadowDistanceOverride = 0.0f // Override distance (0=disabled)

// ShadowDepthRendering.cpp:55-74
r.Shadow.LODDistanceFactor = 1.0f        // Main cascade LOD distance scale
r.Shadow.LODDistanceFactor.CascadeScale = 0.0f // Scale per main cascade index
r.Shadow.FarShadow.LODDistanceFactor = 1.0f   // Far cascade LOD distance scale
```

## Activation Condition

```cpp
// DirectionalLightComponent.cpp:345-352
float FarCascadeSize = FarShadowDistance - WholeSceneDynamicShadowRadius;

bool ActivateFarCascades = (FarShadowCascadeCount > 0) &&
                           (FarCascadeSize > 100.0f);  // 100 cm minimum
```

## Cascade Indexing

```
Cascade 0 .. N-1:          Main CSM (0 to WholeSceneDynamicShadowRadius)
Cascade N .. N+M-1:        Far CSM (WholeSceneDynamicShadowRadius to FarShadowDistance)

Where:
  N = GetNumShadowMappedCascades(...)
  M = min(CVarMaxNumFarShadowCascades, FarShadowCascadeCount)
```

## Distance Split Formula

```cpp
// Main cascades (SplitIndex < NumNearCascades)
Distance = MainNear + AccumScale(Exponent, SplitIndex, NumMainCascades)
                    * (MainDistance - MainNear);

// Far cascades (SplitIndex >= NumNearCascades)
FarIndex = SplitIndex - NumNearCascades;
Distance = MainDistance + AccumScale(Exponent, FarIndex, NumFarCascades)
                        * (FarShadowDistance - MainDistance);
```

## Key Flags & Methods

```cpp
// In FProjectedShadowInfo.CascadeSettings
bool bFarShadowCascade;  // Set during GetViewDependentWholeSceneProjectedShadowInitializer()

// In FPrimitiveSceneProxy (meshes implement)
bool CastsFarShadow();   // Must be true for mesh to appear in far cascades

// LOD selection (ShadowDepthRendering.cpp:1197)
float LODDistanceFactor = bFarShadowCascade ? GFarShadowLODDistanceFactor
                                            : GShadowLODDistanceFactor + ...;

// LOD bias application (ShadowSetup.cpp:1798)
if (bFarShadowCascade) {
    LODIndex += GFarShadowStaticMeshLODBias;  // Only if single-LOD
}
```

## Primitive Culling (ShadowSetup.cpp:2124-2148)

```cpp
bool TestPrimitiveFarCascadeConditions(bool bPrimitiveCastsFarShadow, ...) {
    if (bFarShadowCascade) {
        return bPrimitiveCastsFarShadow;  // Explicit opt-in required
    }
    // For main cascades: check distance & VSM settings
    return true;
}
```

## Draw Flags

```cpp
// ShadowSetup.cpp:2743-2746
if (CascadeSettings.bFarShadowCascade) {
    DrawDynamicFlags |= EDrawDynamicFlags::FarShadowCascade;
}
```

## AtlasLayout

```
Single contiguous atlas:
[Cascade0] [Cascade1] ... [CascadeN] [FarCascade0] ... [FarCascadeM]
```

No special resolution tweaking. Same allocation per cascade regardless of tier.

## Adaptation Checklist for UzEngine

- [ ] Add `FarShadowDistance` and `FarShadowCascadeCount` to light component
- [ ] Add `cast_far_shadow` flag to mesh/primitive
- [ ] Implement cascade range check (100 cm minimum gap)
- [ ] Add `bFarShadowCascade` to cascade info struct
- [ ] Implement far cascade distance formula
- [ ] Apply `GFarShadowStaticMeshLODBias` in LOD selection
- [ ] Add `FarShadowCascade` draw flag
- [ ] Implement primitive filtering (`CastsFarShadow()` check)
- [ ] Add 3 console variables (MaxNumFarShadowCascades, LODBias, LODDistanceFactor)
- [ ] Test with 4 main + 2 far cascades configuration
