<!-- doc-meta
title: UE5 Shadow Cache Technical Reference
category: rendering
status: complete
last-updated: 2026-03-16
-->

# UE5 Shadow Cache — Quick Technical Reference

**For implementation details, see**: `ue5-static-shadow-cache-system.md`

---

## Cache Data Structure

```cpp
// W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ScenePrivate.h:2685-2722
class FCachedShadowMapData
{
public:
    FWholeSceneProjectedShadowInitializer Initializer;
    FShadowMapRenderTargetsRefCounted ShadowMap;           // IPooledRenderTarget refs
    float LastUsedTime;
    bool bCachedShadowMapHasPrimitives;
    bool bCachedShadowMapHasNaniteGeometry;
    TBitArray<> StaticShadowSubjectPersistentPrimitiveIdMap;  // Track rendered statics
    FIntPoint ShadowBufferResolution;
    FVector PreShadowTranslation;
    float MaxSubjectZ, MinSubjectZ;
    int32 LastFrameExtraStaticShadowSubjects;              // For throttling

    void InvalidateCachedShadow() {
        ShadowMap.Release();
        StaticShadowSubjectPersistentPrimitiveIdMap.SetRange(0, Num(), false);
    }
};

// W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ScenePrivate.h:3200
TMap<int32, TArray<FCachedShadowMapData>> CachedShadowMaps;  // Key: LightID, Index: CascadeIndex
```

---

## Cache Modes (Rendering Strategies)

```cpp
// W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.h:183-189
enum EShadowDepthCacheMode
{
    SDCM_MovablePrimitivesOnly,     // Render dynamic geo only (every frame)
    SDCM_StaticPrimitivesOnly,      // Render static geo only (cached, infrequent)
    SDCM_CSMScrolling,              // Scroll cached static, update new edges
    SDCM_Uncached                   // Render everything (memory exceeded)
};

// Occlusion query support
inline bool IsShadowCacheModeOcclusionQueryable(EShadowDepthCacheMode Mode)
{
    return Mode != SDCM_StaticPrimitivesOnly;  // Can't query random emits
}
```

---

## CVars — Complete List

| CVar | Default | Location | Purpose |
|------|---------|----------|---------|
| `r.Shadow.CacheWholeSceneShadows` | 1 | ShadowSetup.cpp:69-74 | Master enable: cache movable light static shadows |
| `r.Shadow.WholeSceneShadowCacheMb` | 150 | ShadowSetup.cpp:94-100 | VRAM budget in MB |
| `r.Shadow.CachedShadowsCastFromMovablePrimitives` | 1 | ShadowSetup.cpp:102-109 | Movable geo in cached shadow maps |
| `r.Shadow.CSMCaching` | 0 | ShadowSetup.cpp:393-398 | Enable cascaded shadow caching |
| `r.Shadow.CSMScrollingOverlapAreaThrottle` | 0.75 | ShadowSetup.cpp:400-404 | Min overlap % before scrolling |
| `r.Shadow.MaxCSMScrollingStaticShadowSubjects` | 5 | ShadowSetup.cpp:406-410 | Max extra static meshes for scrolling |
| `r.Shadow.CSMScissorOptim` | false | ShadowSetup.cpp:412-417 | Optimize scissor rects for CSM |
| `r.Shadow.CachePreshadow` | 1 | ShadowSetup.cpp:163-168 | Cache character preshadows |
| `r.Shadow.MaxNumPointShadowCacheUpdatesPerFrame` | -1 | ShadowSetup.cpp:76-83 | Point light cache updates per frame (-1 = unlimited) |
| `r.Shadow.MaxNumSpotShadowCacheUpdatesPerFrame` | -1 | ShadowSetup.cpp:85-92 | Spot light cache updates per frame |

---

## Validation Rules (When Cache is Valid)

**All conditions must pass** (ShadowSetup.cpp:3869-3884):

### Condition 1: Light Configuration (WorldToLight == CachedWorldToLight)
```cpp
bool valid = ProjectedShadowInitializer.WorldToLight == CachedShadowInitializer.WorldToLight
          && ProjectedShadowInitializer.WAxis == CachedShadowInitializer.WAxis
          && ProjectedShadowInitializer.bOnePassPointLightShadow == CachedInitializer.bOnePassPointLightShadow
          && ProjectedShadowInitializer.bRayTracedDistanceField == CachedInitializer.bRayTracedDistanceField
          && ProjectedShadowInitializer.SubjectBounds.SphereRadius == CachedInitializer.SubjectBounds.SphereRadius;
```

**Invalidates if:**
- Light direction changed (WorldToLight matrix differs)
- Light up vector changed (WAxis differs)
- Shadow mode changed (DistanceField, OnePass)
- Subject bounds radius changed

### Condition 2: Viewport Consistency
```cpp
bool exact = ProjectedShadowInitializer.PreShadowTranslation == CachedInitializer.PreShadowTranslation
          && ProjectedShadowInitializer.Scales == CachedInitializer.Scales
          && ProjectedShadowInitializer.SubjectBounds.Origin == CachedInitializer.SubjectBounds.Origin
          && ProjectedShadowInitializer.MinLightW == CachedInitializer.MinLightW
          && ProjectedShadowInitializer.MaxDistanceToCastInLightW == CachedInitializer.MaxDistanceToCastInLightW;
```

**Invalidates if:**
- Light-space translation changed (PreShadowTranslation differs)
- Projection scale changed (Scales differs)
- Near/far planes changed (MinLightW/MaxDistanceToCastInLightW)

### Condition 3: Resource Validity
```cpp
bool resourceValid = CachedShadowMapData->ShadowMap.IsValid()
                  && CachedShadowMapData->ShadowBufferResolution == ShadowMapSize
                  && CachedShadowMapData->LastFrameExtraStaticShadowSubjects <= CVarCSMScrollingMaxExtraStaticShadowSubjects;
```

**Invalidates if:**
- Render target released (ShadowMap.IsValid() == false)
- Resolution changed (ShadowBufferResolution != ShadowMapSize)
- Too many extra static subjects (exceeds throttle)

---

## Primitive Culling Per Cache Mode

### SDCM_MovablePrimitivesOnly
```cpp
// ShadowSetup.cpp:4986-4991
bShadowRelevance = PrimitiveProxy->IsMeshShapeOftenMoving();
```
**→ Only render dynamic primitives**

### SDCM_StaticPrimitivesOnly
```cpp
// ShadowSetup.cpp:4993-4996
bShadowRelevance = !PrimitiveProxy->IsMeshShapeOftenMoving();
```
**→ Only render static primitives, track in bitset**

### SDCM_CSMScrolling
```cpp
// ShadowSetup.cpp:4997-5012
bShadowRelevance = PrimitiveProxy->IsMeshShapeOftenMoving()
                || !CachedShadowMapData.StaticShadowSubjectPersistentPrimitiveIdMap[index];

for (int32 i = 0; i < CSMScrollingExtraCullingPlanes.Num(); ++i)
{
    bShadowRelevance |= FMath::PlaneAABBRelativePosition(Plane, Bounds) <= 0;
}
```
**→ Render movable + new static primitives, cull by scrolling planes**

### SDCM_Uncached
```cpp
// All primitives rendered (no caching)
```
**→ Fallback when memory budget exceeded**

---

## Render Target Structure

```cpp
// W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SceneRendering.h:1828-1892
class FShadowMapRenderTargetsRefCounted
{
public:
    TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<4>> ColorTargets;
    TRefCountPtr<IPooledRenderTarget> DepthTarget;

    bool IsValid() const { return DepthTarget || ColorTargets.Num() > 0; }
    FIntPoint GetSize() const;
    int64 ComputeMemorySize() const;
    void Release();
};

// Key insight: Separate render targets for static and movable passes
// Not a shared atlas — each cache entry owns its own targets
```

---

## Memory Management

```cpp
// ShadowSetup.cpp:432-451
void TryToCacheShadowMap(const FScene* Scene, int64 CachedShadowMapsSize, ...)
{
    if (CachedShadowMapsSize < (int64)GWholeSceneShadowCacheMb * 1024 * 1024)
    {
        // Memory available: allocate static + movable passes
        OutNumShadowMaps = 2;
        OutCacheModes[0] = SDCM_StaticPrimitivesOnly;
        OutCacheModes[1] = SDCM_MovablePrimitivesOnly;
        CacheLambda();  // Create cache
    }
    else
    {
        // Out of memory: render uncached
        OutNumShadowMaps = 1;
        OutCacheModes[0] = SDCM_Uncached;
        UnCacheLambda();  // Fallback
    }
}

// RendererScene.cpp:3003-3024
int64 GetCachedWholeSceneShadowMapsSize() const
{
    int64 Total = 0;
    for (auto& [LightID, ShadowArray] : CachedShadowMaps)
        for (auto& ShadowData : ShadowArray)
            Total += ShadowData.ShadowMap.ComputeMemorySize();
    return Total;
}
```

**Policy**: If `current_total >= budget_limit`, render uncached (do NOT allocate). No explicit eviction.

---

## CSM Scrolling Logic

**Trigger** (ShadowSetup.cpp:3916-3932):
```cpp
float OverlappedAreaRatio = (overlappedArea.X * overlappedArea.Y) /
                            (cachedRadius.X * cachedRadius.Y * 4);

if (OverlappedAreaRatio > 0.75f)  // CVarCSMScrollingOverlapAreaThrottle
{
    OutCacheModes[0] = SDCM_CSMScrolling;  // Scroll + update edges
}
else
{
    OutCacheModes[0] = SDCM_StaticPrimitivesOnly;   // Rebuild
    OutCacheModes[1] = SDCM_MovablePrimitivesOnly;
}
```

**Scrolling Planes** (ShadowSetup.cpp:5641-5668):

Creates up to 4 culling planes for non-overlapped regions:
```cpp
TArray<FPlane, TInlineAllocator<4>>& ExtraCullingPlanes = ProjectedShadowInfo->CSMScrollingExtraCullingPlanes;

// Top: if current viewport extends above cached viewport
if (CurrentViewport.Max.Y > OverlappedViewport.Max.Y)
    ExtraCullingPlanes.Add(FPlane(position, -UpVector));

// Bottom
if (CurrentViewport.Min.Y < OverlappedViewport.Min.Y)
    ExtraCullingPlanes.Add(FPlane(position, UpVector));

// Left
if (CurrentViewport.Min.X < OverlappedViewport.Min.X)
    ExtraCullingPlanes.Add(FPlane(position, RightVector));

// Right
if (CurrentViewport.Max.X > OverlappedViewport.Max.X)
    ExtraCullingPlanes.Add(FPlane(position, -RightVector));
```

**During rendering** (ShadowSetup.cpp:5007-5010):
```cpp
for (int32 i = 0; i < CSMScrollingExtraCullingPlanes.Num(); ++i)
{
    bShadowRelevance |= FMath::PlaneAABBRelativePosition(Plane, PrimitiveBounds) <= 0;
}
// Only render if primitive is in the non-overlapped area
```

**UV Mapping** (ShadowSetup.cpp:5629-5639):
```cpp
// Where overlapped area maps on cached shadow map
OverlappedUVOnCachedShadowMap = FVector4f(
    (Overlap.Min.X - Cached.Min.X) / CachedWidth,
    (Cached.Max.Y - Overlap.Max.Y) / CachedHeight,
    (Overlap.Max.X - Cached.Min.X) / CachedWidth,
    (Cached.Max.Y - Overlap.Min.Y) / CachedHeight);

// Where overlapped area maps on current shadow map
OverlappedUVOnCurrentShadowMap = FVector4f(
    (Overlap.Min.X - Current.Min.X) / CurrentWidth,
    (Current.Max.Y - Overlap.Max.Y) / CurrentHeight,
    (Overlap.Max.X - Current.Min.X) / CurrentWidth,
    (Current.Max.Y - Overlap.Min.Y) / CurrentHeight);
```

---

## Cache Creation & Lookup

```cpp
// ShadowSetup.cpp:3818
Scene->CachedShadowMaps.Add(LightSceneInfo->Id).Add(
    FCachedShadowMapData(Initializer, RealTime));

// ScenePrivate.h:3209-3222
FORCEINLINE TArray<FCachedShadowMapData>* GetCachedShadowMapDatas(int32 LightID)
{
    return CachedShadowMaps.Find(LightID);
}

FORCEINLINE FCachedShadowMapData* GetCachedShadowMapDataRef(int32 LightID, int32 ShadowMapIndex = 0)
{
    TArray<FCachedShadowMapData>& Datas = CachedShadowMaps.FindChecked(LightID);
    return &Datas[ShadowMapIndex];  // ShadowMapIndex == CascadeIndex for CSM
}

// Removal when light destroyed
// ShadowSetup.cpp:3829 / LightSceneInfo.cpp:160
Scene->CachedShadowMaps.Remove(LightSceneInfo->Id);
```

---

## Bitset Initialization

```cpp
// ShadowSetup.cpp:3941, 3972
checkSlow(CachedShadowMapData->StaticShadowSubjectPersistentPrimitiveIdMap.Num() ==
          Scene->GetMaxPersistentPrimitiveIndex());

// First time cache is created:
CachedShadowMapData->StaticShadowSubjectPersistentPrimitiveIdMap.Init(
    false,
    Scene->GetMaxPersistentPrimitiveIndex());

// Mark primitive as rendered in static pass
// ShadowSetup.cpp:2364
CachedShadowMapData.StaticShadowSubjectPersistentPrimitiveIdMap[
    PrimitiveSceneInfo->GetPersistentIndex().Index] = true;
```

**Key**: Bitset size = `max_persistent_index`. Every scene primitive gets a stable index. Enables O(1) culling in CSM scrolling.

---

## Access Patterns

### Query if cache is cached and valid
```cpp
if (CachedShadowMapData->ShadowMap.IsValid()
    && CachedShadowMapData->ShadowBufferResolution == CurrentResolution)
{
    // Cache is usable
    OutCacheModes[0] = SDCM_MovablePrimitivesOnly;
}
```

### Invalidate cache
```cpp
CachedShadowMapData->InvalidateCachedShadow();
```

### Update cache metadata after rebuild
```cpp
CachedShadowMapData->Initializer = NewInitializer;
CachedShadowMapData->ShadowBufferResolution = NewResolution;
CachedShadowMapData->LastUsedTime = CurrentFrameTime;
CachedShadowMapData->LastFrameExtraStaticShadowSubjects = ExtraStaticCount;
```

### Get total cache size
```cpp
int64 TotalCachedMemory = Scene->GetCachedWholeSceneShadowMapsSize();
if (TotalCachedMemory >= GWholeSceneShadowCacheMb * 1024 * 1024)
{
    // Out of memory, can't cache new shadows
}
```

---

## Performance Considerations

| Factor | Impact | Notes |
|--------|--------|-------|
| **Static mesh count** | High | Bitset allocation is O(max_index), not O(mesh_count) |
| **Light direction changes** | Cache invalidation | WorldToLight matrix comparison is exact |
| **View movement (same light dir)** | CSM scrolling | Overlap > 75% → scroll; < 75% → rebuild |
| **Resolution changes** | Cache invalidation | Can't scroll if resolution changes |
| **Memory budget** | Fallback mode | If total >= 150 MB, render uncached (no allocation) |
| **Extra static subjects** | CSM scrolling throttle | If > 5 extra meshes, prefer rebuild over scroll |

---

## Shader Integration

The shader receives separate shadow maps:
1. **Static shadow depth** (`CachedShadowMapData.ShadowMap`)
2. **Movable shadow depth** (current frame render)

Composite in shader:
```glsl
float shadow = shadow_from_static_map * shadow_from_movable_map;
```

For CSM scrolling, UV is blended between two maps using `OverlappedUVOn[Cached|Current]ShadowMap`.

---

## References

- Main analysis: `ue5-static-shadow-cache-system.md`
- UE5 source: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp`
