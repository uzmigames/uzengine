<!-- doc-meta
title: UE5 Static Shadow Map Caching System Analysis
category: rendering
status: complete
last-updated: 2026-03-16
-->

# UE5 Static Shadow Map Caching System Analysis

## Executive Summary

UE5's static shadow map caching system optimizes whole-scene shadow rendering for movable point and spot lights by caching the static geometry's shadow contribution across frames and only updating dynamic geometry shadows each frame. The system uses a two-pass strategy where static shadow maps are rendered infrequently (on demand) and movable shadow maps are rendered every frame, then composited during shading.

Key insight: **The cache is keyed by LightID + CascadeIndex, storing separate render targets and a bitset tracking which static primitives were rendered into the cache.**

---

## 1. FCachedShadowMapData Structure

**Definition**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ScenePrivate.h:2685-2722`

```cpp
class FCachedShadowMapData
{
public:
    FWholeSceneProjectedShadowInitializer Initializer;        // Light/camera setup at cache creation
    FShadowMapRenderTargetsRefCounted ShadowMap;              // Actual render target (depth + optional color)
    float LastUsedTime;                                        // For eviction: frame time of last use
    bool bCachedShadowMapHasPrimitives;                       // Has any renderable primitives
    bool bCachedShadowMapHasNaniteGeometry;                   // Contains Nanite static meshes

    // Bitset: one bit per persistent primitive index
    // Set to TRUE if that static primitive was rendered into this cache
    TBitArray<> StaticShadowSubjectPersistentPrimitiveIdMap;

    FIntPoint ShadowBufferResolution;                         // Resolution used at cache time
    FVector PreShadowTranslation;                             // Light space transform
    float MaxSubjectZ;                                         // Subject bounds in light space
    float MinSubjectZ;

    // CSM scrolling: how many extra static geometry draw calls needed
    // If > r.Shadow.MaxCSMScrollingStaticShadowSubjects, rebuild cache
    int32 LastFrameExtraStaticShadowSubjects;

    void InvalidateCachedShadow()
    {
        ShadowMap.Release();  // Release render target refs
        StaticShadowSubjectPersistentPrimitiveIdMap.SetRange(0, Num(), false);  // Clear bitset
    }
};
```

### Key Members Analysis

| Member | Purpose | Notes |
|--------|---------|-------|
| `Initializer` | Captures shadow setup at cache creation | Used for validation: if light direction changes, cache invalidates |
| `ShadowMap` | Pooled render target reference-counted container | `TArray<TRefCountPtr<IPooledRenderTarget>>` for color + depth |
| `StaticShadowSubjectPersistentPrimitiveIdMap` | Bitset of static mesh primitives rendered | Scene has `MaxPersistentPrimitiveIndex()` for bitset size |
| `LastUsedTime` | Frame time of last access | Used for LRU eviction when cache exceeds memory limit |

---

## 2. Cache Key Structure

**Location**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ScenePrivate.h:3200`

```cpp
TMap<int32, TArray<FCachedShadowMapData>> CachedShadowMaps;
```

### Key Design

- **Primary key**: `int32 LightID` (from `FLightSceneInfo->Id`)
- **Secondary index**: Array index = `CascadeSettings.ShadowSplitIndex` (CSM cascade)
- **Example**: Light ID 5, Cascade 2 → `CachedShadowMaps[5][2]`

**Access pattern** (ShadowSetup.cpp:3862):
```cpp
TArray<FCachedShadowMapData>* CachedShadowMapDatas = Scene->GetCachedShadowMapDatas(LightSceneInfo->Id);
FCachedShadowMapData* CachedShadowMapData = &(*CachedShadowMapDatas)[ProjectedShadowInitializer.CascadeSettings.ShadowSplitIndex];
```

---

## 3. Cache Invalidation Triggers

**Location**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:3869-3955`

The cache validates on multiple conditions. If ANY fail, `InvalidateCachedShadow()` is called:

### Validation Check 1: Light Configuration (Lines 3869-3873)

```cpp
bool bIsValidCachedViewDependentWholeSceneShadow =
    ProjectedShadowInitializer.WorldToLight == CachedShadowInitializer.WorldToLight  // Light dir changed
    && ProjectedShadowInitializer.WAxis == CachedShadowInitializer.WAxis              // Light up vector changed
    && ProjectedShadowInitializer.bOnePassPointLightShadow == CachedShadowInitializer.bOnePassPointLightShadow
    && ProjectedShadowInitializer.bRayTracedDistanceField == CachedShadowInitializer.bRayTracedDistanceField
    && ProjectedShadowInitializer.SubjectBounds.SphereRadius == CachedShadowInitializer.SubjectBounds.SphereRadius;
```

**Invalidates if:**
- Light direction changed (WorldToLight matrix differs)
- Light up-vector changed
- Point light settings changed
- Subject bounding sphere radius changed

### Validation Check 2: Viewport Consistency (Lines 3877-3881)

```cpp
bool bExactlyEqual =
    ProjectedShadowInitializer.PreShadowTranslation == CachedShadowInitializer.PreShadowTranslation
    && ProjectedShadowInitializer.Scales == CachedShadowInitializer.Scales
    && ProjectedShadowInitializer.SubjectBounds.Origin == CachedShadowInitializer.SubjectBounds.Origin
    && ProjectedShadowInitializer.MinLightW == CachedShadowInitializer.MinLightW
    && ProjectedShadowInitializer.MaxDistanceToCastInLightW == CachedShadowInitializer.MaxDistanceToCastInLightW;
```

**Invalidates if:**
- Light-space translation changed (view moved in light space)
- Shadow projection scale changed
- Subject bounds origin changed
- Z-extent changed (near/far plane)

### Validation Check 3: Resource Validity (Lines 3884-3945)

```cpp
if (CachedShadowMapData->ShadowMap.IsValid()
    && CachedShadowMapData->ShadowBufferResolution == ShadowMapSize
    && CachedShadowMapData->LastFrameExtraStaticShadowSubjects <= CVarCSMScrollingMaxExtraStaticShadowSubjects.GetValueOnAnyThread())
{
    // Cache is valid, can reuse
}
else if (CachedShadowMapsSize < GWholeSceneShadowCacheMb * 1024 * 1024)
{
    // Memory available, can allocate new cache
    CachedShadowMapData->ShadowBufferResolution = ShadowMapSize;
    CachedShadowMapData->Initializer = ProjectedShadowInitializer;
}
else
{
    // Out of memory, invalidate and render uncached
    CachedShadowMapData->InvalidateCachedShadow();
    OutCacheModes[0] = SDCM_Uncached;
}
```

**Invalidates if:**
- Resolution changed (render target too small/large)
- Memory budget exceeded (no space for new allocation)
- Too many extra static subjects to scroll (exceeds throttle)

---

## 4. Static vs Dynamic Geometry Separation

**Location**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:4986-5013, 2361-2364`

### Cache Modes Enum

**ShadowRendering.h:183-189**:
```cpp
enum EShadowDepthCacheMode
{
    SDCM_MovablePrimitivesOnly,     // Render only dynamic/movable geometry (every frame)
    SDCM_StaticPrimitivesOnly,      // Render only static geometry (on-demand, cached)
    SDCM_CSMScrolling,              // Scroll cached static, update moving edges
    SDCM_Uncached                   // Render everything (memory budget exceeded)
};
```

### Separation Logic

**For SDCM_MovablePrimitivesOnly** (ShadowSetup.cpp:4986-4991):
```cpp
if (ProjectedShadowInfo->CacheMode == SDCM_MovablePrimitivesOnly)
{
    // Only render primitives that are OFTEN MOVING
    bShadowRelevance = PrimitiveProxy->IsMeshShapeOftenMoving();
}
```

**For SDCM_StaticPrimitivesOnly** (ShadowSetup.cpp:4993-4996):
```cpp
else if (ProjectedShadowInfo->CacheMode == SDCM_StaticPrimitivesOnly)
{
    // Only render primitives that are STATIC
    bShadowRelevance = !PrimitiveProxy->IsMeshShapeOftenMoving();
}
```

**For SDCM_CSMScrolling** (ShadowSetup.cpp:4997-5012):
```cpp
else if (ProjectedShadowInfo->CacheMode == SDCM_CSMScrolling)
{
    // Render movable OR newly-added static meshes
    // Do NOT re-render primitives already in cache (in StaticShadowSubjectPersistentPrimitiveIdMap)
    bShadowRelevance = PrimitiveProxy->IsMeshShapeOftenMoving()
        || !CachedShadowMapData.StaticShadowSubjectPersistentPrimitiveIdMap[PrimitiveSceneInfo->GetPersistentIndex().Index];

    // Additional culling: only render if primitive intersects non-overlapped area
    for (int32 i = 0; i < ProjectedShadowInfo->CSMScrollingExtraCullingPlanes.Num(); ++i)
    {
        bShadowRelevance |= FMath::PlaneAABBRelativePosition(...) <= 0;
    }
}
```

### Tracking Static Subjects

**ShadowSetup.cpp:2361-2364** (during shadow pass setup):
```cpp
if (CacheMode == SDCM_StaticPrimitivesOnly)
{
    CachedShadowMapData.StaticShadowSubjectPersistentPrimitiveIdMap[
        PrimitiveSceneInfo->GetPersistentIndex().Index] = true;
}
```

Each primitive rendered into a static-only pass is marked in the bitset. This allows CSM scrolling to know which primitives are already cached.

---

## 5. Cached Shadow Atlas Organization

**Location**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\SceneRendering.h:1894-1910`

### Atlas Structure

```cpp
struct FSortedShadowMapAtlas
{
    FShadowMapRenderTargetsRefCounted RenderTargets;      // Actual render targets
    TArray<FProjectedShadowInfo*> Shadows;                // Which shadows use this atlas
};

struct FSortedShadowMaps
{
    TArray<FSortedShadowMapAtlas> ShadowMapAtlases;       // For point/spot lights
    TArray<FSortedShadowMapAtlas> ShadowMapCubemaps;      // For cubemap point lights
    FSortedShadowMapAtlas PreshadowCache;                 // Preshadow (character shadows)
    TArray<FSortedShadowMapAtlas> TranslucencyShadowMapAtlases;
    TArray<FSortedShadowMapAtlas> CompleteShadowMapAtlases;  // VSM
};
```

### Per-Cache Render Target

**SceneRendering.h:1828-1892**:
```cpp
class FShadowMapRenderTargetsRefCounted
{
public:
    TArray<TRefCountPtr<IPooledRenderTarget>, TInlineAllocator<4>> ColorTargets;
    TRefCountPtr<IPooledRenderTarget> DepthTarget;

    int64 ComputeMemorySize() const
    {
        int64 MemorySize = 0;
        for (int32 i = 0; i < ColorTargets.Num(); i++)
            MemorySize += ColorTargets[i]->ComputeMemorySize();
        if (DepthTarget)
            MemorySize += DepthTarget->ComputeMemorySize();
        return MemorySize;
    }
};
```

**Key insight**: Each cached shadow map has its own ref-counted render target. Static and movable passes use **separate render targets** (different atlass entries), not a shared atlas. This allows parallel rendering without synchronization.

---

## 6. Memory Management

**Location**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:68-100, 3003-3024, 432-451`

### Memory Budget CVars

| CVar | Default | Purpose |
|------|---------|---------|
| `r.Shadow.WholeSceneShadowCacheMb` | 150 MB | Total VRAM for whole-scene cached shadows |
| `r.Shadow.CacheWholeSceneShadows` | 1 | Master enable/disable |
| `r.Shadow.MaxNumPointShadowCacheUpdatesPerFrame` | -1 | Cap point light cache rebuilds per frame (-1 = unlimited) |
| `r.Shadow.MaxNumSpotShadowCacheUpdatesPerFrame` | -1 | Cap spot light cache rebuilds per frame |

### Memory Calculation

**RendererScene.cpp:3003-3024**:
```cpp
int64 FScene::GetCachedWholeSceneShadowMapsSize() const
{
    int64 CachedShadowmapMemory = 0;

    for (TMap<int32, TArray<FCachedShadowMapData>>::TConstIterator CachedShadowMapIt(CachedShadowMaps);
         CachedShadowMapIt; ++CachedShadowMapIt)
    {
        const TArray<FCachedShadowMapData>& ShadowMapDatas = CachedShadowMapIt.Value();
        for (const FCachedShadowMapData& ShadowMapData : ShadowMapDatas)
        {
            CachedShadowmapMemory += ShadowMapData.ShadowMap.ComputeMemorySize();
        }
    }

    return CachedShadowmapMemory;
}
```

### Cache Allocation Decision

**ShadowSetup.cpp:432-451** (`TryToCacheShadowMap`):
```cpp
void TryToCacheShadowMap(
    const FScene* Scene,
    int64 CachedShadowMapsSize,         // Current size
    int32& OutNumShadowMaps,
    EShadowDepthCacheMode* OutCacheModes,
    uint32& NumCachesUpdatedThisFrame,
    CacheLambdaType&& CacheLambda,      // Execute if memory available
    UnCacheLambdaType&& UnCacheLambda)  // Execute if out of memory
{
    if (CachedShadowMapsSize < (int64)GWholeSceneShadowCacheMb * 1024 * 1024)
    {
        // Memory available: allocate separate static+movable passes
        OutNumShadowMaps = 2;
        OutCacheModes[0] = SDCM_StaticPrimitivesOnly;
        OutCacheModes[1] = SDCM_MovablePrimitivesOnly;
        ++NumCachesUpdatedThisFrame;
        CacheLambda();  // Add to cache
    }
    else
    {
        // Out of memory: render everything uncached
        OutNumShadowMaps = 1;
        OutCacheModes[0] = SDCM_Uncached;
        UnCacheLambda();  // Invalidate and fallback
    }
}
```

### Eviction Strategy

No explicit LRU eviction is implemented. Instead:
1. When memory budget exceeded, new shadows fall back to uncached mode
2. `LastUsedTime` field suggests intent to implement LRU, but not active in 5.6

---

## 7. CSM-Specific CVars

**Location**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:393-417`

### CSM Caching CVars

```cpp
static TAutoConsoleVariable<int32> CVarCSMCaching(
    TEXT("r.Shadow.CSMCaching"),
    0,  // Default: disabled (enable with = 1)
    TEXT("0: Render CSM every frame.\n")
    TEXT("1: Enable CSM caching."),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarCSMScrollingOverlapAreaThrottle(
    TEXT("r.Shadow.CSMScrollingOverlapAreaThrottle"),
    0.75f,  // 75% overlap required before scrolling instead of rebuilding
    TEXT("The minimum ratio of the overlap area for CSM scrolling."),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarCSMScrollingMaxExtraStaticShadowSubjects(
    TEXT("r.Shadow.MaxCSMScrollingStaticShadowSubjects"),
    5,  // Max 5 extra static meshes before full rebuild
    TEXT("The maximum number of extra static shadow subjects to draw when scrolling CSM."),
    ECVF_RenderThreadSafe);

static TAutoConsoleVariable<bool> CVarCSMScissorOptim(
    TEXT("r.Shadow.CSMScissorOptim"),
    false,
    TEXT("Compute optimized scissor rect size to exclude portions of CSM slices outside view frustum"),
    ECVF_RenderThreadSafe);
```

### Preshadow Caching CVars

```cpp
static TAutoConsoleVariable<int32> CVarCachePreshadows(
    TEXT("r.Shadow.CachePreshadow"),
    1,
    TEXT("Whether preshadows can be cached as an optimization"),
    ECVF_RenderThreadSafe);
```

### Other Related CVars

```cpp
int32 GCachedShadowsCastFromMovablePrimitives = 1;
// r.Shadow.CachedShadowsCastFromMovablePrimitives
// Whether movable geometry casts shadows in cached shadow maps
// Disabling removes the copy operation (saves GPU bandwidth)

int32 GWholeSceneShadowCacheMb = 150;
// r.Shadow.WholeSceneShadowCacheMb
// Memory budget in MB
```

---

## 8. Cache Integration with Rendering Pipeline

**Location**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp:3704-3970`

### Two-Pass Rendering Strategy

When cache mode is `SDCM_StaticPrimitivesOnly + SDCM_MovablePrimitivesOnly`:

1. **Frame N**: If cache is invalid or memory available:
   - **Pass 1** (`SDCM_StaticPrimitivesOnly`): Render static geometry → stored in `CachedShadowMapData.ShadowMap`
   - **Pass 2** (`SDCM_MovablePrimitivesOnly`): Render movable geometry → separate render target
   - Result: Two depth maps that are later composited in shaders

2. **Frame N+1**: If cache is valid:
   - **Pass 1**: Skip (reuse cached static depth map)
   - **Pass 2** (`SDCM_MovablePrimitivesOnly`): Render only movable geometry
   - Shader composites: `shadow = shadow_static AND shadow_movable`

### CSM Scrolling Strategy (Lines 3883-3932)

If camera moves but light direction unchanged:

```cpp
// Calculate overlap between cached and current viewport
float OverlappedAreaRatio = (overlappedArea.X * overlappedArea.Y) /
                            (cachedRadius.X * cachedRadius.Y * 4);

if (OverlappedAreaRatio > CVarCSMScrollingOverlapAreaThrottle.GetValueOnAnyThread())
{
    // Sufficient overlap (>75%): use CSM scrolling
    OutCacheModes[0] = SDCM_CSMScrolling;
    // GPU will copy overlapped region, render only new edges
}
else
{
    // Low overlap (<75%): full rebuild
    OutCacheModes[0] = SDCM_StaticPrimitivesOnly;  // Rebuild cache
    OutCacheModes[1] = SDCM_MovablePrimitivesOnly;
    CachedShadowMapData->InvalidateCachedShadow();
}
```

### Scrolling Culling Planes (Lines 5600-5668)

When scrolling, four culling planes define the non-overlapped region:

```cpp
// For each side of the current viewport that extends beyond cached viewport:
// Top plane
if (CurrentViewport.Max.Y > OverlappedViewport.Max.Y)
    ExtraCullingPlanes.Add(FPlane(position, -UpVector));  // Cull above

// Bottom plane
if (CurrentViewport.Min.Y < OverlappedViewport.Min.Y)
    ExtraCullingPlanes.Add(FPlane(position, UpVector));   // Cull below

// Left plane
if (CurrentViewport.Min.X < OverlappedViewport.Min.X)
    ExtraCullingPlanes.Add(FPlane(position, RightVector));

// Right plane
if (CurrentViewport.Max.X > OverlappedViewport.Max.X)
    ExtraCullingPlanes.Add(FPlane(position, -RightVector));
```

During rendering (ShadowSetup.cpp:5007-5010):
```cpp
for (int32 i = 0; i < ProjectedShadowInfo->CSMScrollingExtraCullingPlanes.Num(); ++i)
{
    // Only render if primitive intersects the non-overlapped area
    bShadowRelevance |= FMath::PlaneAABBRelativePosition(Plane, PrimitiveBounds) <= 0;
}
```

---

## 9. Implementation Pattern Summary

### Data Structure Hierarchy
```
FScene::CachedShadowMaps (TMap<int32, TArray<FCachedShadowMapData>>)
  └─ [LightID] → TArray<FCachedShadowMapData>
       └─ [CascadeIndex] → FCachedShadowMapData
            ├─ Initializer (FWholeSceneProjectedShadowInitializer)
            ├─ ShadowMap (FShadowMapRenderTargetsRefCounted)
            │  ├─ ColorTargets (TArray<TRefCountPtr<IPooledRenderTarget>>)
            │  └─ DepthTarget (TRefCountPtr<IPooledRenderTarget>)
            └─ StaticShadowSubjectPersistentPrimitiveIdMap (TBitArray<>)
```

### Cache Lifecycle

1. **Creation** (ShadowSetup.cpp:3818):
   ```cpp
   Scene->CachedShadowMaps.Add(LightSceneInfo->Id).Add(FCachedShadowMapData(Initializer, RealTime));
   ```

2. **Validation** (ShadowSetup.cpp:3869-3884):
   ```cpp
   bool bIsValid =
       (LightDirection unchanged) &&
       (ViewportOffset acceptable) &&
       (ShadowMap.IsValid()) &&
       (ShadowBufferResolution unchanged) &&
       (ExtraStaticSubjects <= Throttle);
   ```

3. **Reuse** (ShadowSetup.cpp:3888-3889):
   ```cpp
   OutCacheModes[0] = SDCM_MovablePrimitivesOnly;  // Skip static pass
   ```

4. **Invalidation** (ShadowSetup.cpp:2709-2714):
   ```cpp
   ShadowMap.Release();  // Release render targets
   StaticShadowSubjectPersistentPrimitiveIdMap.SetRange(0, Num(), false);  // Clear tracking
   ```

5. **Removal** (ShadowSetup.cpp:3829):
   ```cpp
   Scene->CachedShadowMaps.Remove(LightSceneInfo->Id);  // Light destroyed
   ```

---

## 10. UzEngine Adaptation Strategy

### Key Patterns to Adopt

1. **Cache Keying**: Follow `TMap<int32 LightID, TArray<CachedShadow>>` pattern
   - Per-cascade indexing for directional lights
   - Single entry for point/spot lights

2. **Bitset Tracking**: Use dynamic bitset to track rendered static primitives
   - Enables CSM scrolling culling
   - Size = `max_persistent_index` set at scene init

3. **Two-Pass Separation**:
   - Static-only pass: cached, rendered infrequently
   - Dynamic-only pass: every frame
   - Separate render targets, not shared atlas

4. **Overlap-Based Scrolling**: When viewport moves in light space:
   - Calculate overlap % of cached vs current viewport
   - If > 75%: GPU copy + edge render (CSM scrolling)
   - If < 75%: Full cache rebuild

5. **Memory Budget Enforcement**:
   - Track total VRAM via `ComputeMemorySize()` per cache entry
   - If exceeded, fall back to uncached mode (no allocation)
   - No explicit LRU (future enhancement)

### Anti-Patterns to Avoid

1. **❌ Shared Atlas for Static+Movable**: UE5 uses separate render targets
   - This allows parallel rendering and independent lifetime management

2. **❌ Lazy Bitset Allocation**: UE5 allocates full bitset at cache creation (size = max_persistent_index)
   - Enables O(1) lookup; don't allocate dynamically

3. **❌ Rebuild on every view change**: Check overlap thresholds first
   - Only rebuild if overlap < 75% of subject bounds
   - Otherwise use CSM scrolling

4. **❌ No primitive tracking**: Static-only pass MUST mark rendered primitives
   - Use `StaticShadowSubjectPersistentPrimitiveIdMap[index] = true`
   - Required for CSM scrolling culling

5. **❌ Unbounded cache growth**: Check memory budget before allocation
   - If `current_size >= budget_limit`, render uncached (don't allocate)

---

## 11. File Reference Map

| System | File | Key Lines | Purpose |
|--------|------|-----------|---------|
| **Struct Definition** | `ScenePrivate.h` | 2685-2722 | `FCachedShadowMapData` class |
| **Cache Map** | `ScenePrivate.h` | 3200 | `TMap<int32, TArray<FCachedShadowMapData>>` |
| **Render Targets** | `SceneRendering.h` | 1828-1892 | `FShadowMapRenderTargetsRefCounted` |
| **Cache Modes Enum** | `ShadowRendering.h` | 183-189 | `EShadowDepthCacheMode` (SDCM_*) |
| **Caching Logic** | `ShadowSetup.cpp` | 3846-3970 | `ComputeViewDependentWholeSceneShadowCacheModes()` |
| **Memory Budget** | `ShadowSetup.cpp` | 68-100 | CVar declarations |
| **Allocation Decision** | `ShadowSetup.cpp` | 432-451 | `TryToCacheShadowMap()` |
| **Primitive Culling** | `ShadowSetup.cpp` | 4986-5012 | Cache mode-based relevance checks |
| **CSM Scrolling** | `ShadowSetup.cpp` | 5600-5668 | Overlap viewport calculation + culling planes |
| **Memory Calculation** | `RendererScene.cpp` | 3003-3024 | `GetCachedWholeSceneShadowMapsSize()` |

---

## 12. Critical Implementation Details for UzEngine

### Bitset Initialization

The bitset MUST be sized to `Scene->GetMaxPersistentPrimitiveIndex()` at cache creation:

```cpp
CachedShadowMapData->StaticShadowSubjectPersistentPrimitiveIdMap.Init(
    false,  // Initial value: all false
    Scene->GetMaxPersistentPrimitiveIndex());  // Size = max index + 1
```

**Why**: Scene has a persistent index allocator for all primitives. Every primitive gets a stable index across frames. Bitset size must match to enable O(1) lookup.

### Validation Point: ShadowMapResolution

CSM scrolling requires shadow buffer size to be unchanged. If resolution changes:

```cpp
if (CachedShadowMapData->ShadowBufferResolution != ShadowMapSize)
{
    // Resolution mismatch: can't scroll, must rebuild
    CachedShadowMapData->InvalidateCachedShadow();
    CachedShadowMapData->ShadowBufferResolution = ShadowMapSize;
}
```

### Reference-Counted Render Targets

Use pooled render targets with ref-counting:
- `TArray<TRefCountPtr<IPooledRenderTarget>>` for color
- `TRefCountPtr<IPooledRenderTarget>` for depth

Release via `.Release()` method, not manual deletion.

---

## Conclusion

UE5's static shadow caching system is a sophisticated optimization that trades memory (150 MB VRAM budget) for GPU time savings. The key insight is **separating static and dynamic shadow passes into independent render targets**, allowing the static component to be cached and reused while dynamic geometry updates every frame.

The CSM scrolling feature adds intelligent invalidation: if viewport moves but overlap > 75%, the GPU copies the overlapped region and only renders the new edges. This avoids expensive full rebuilds for small camera movements.

For UzEngine, the pattern to adopt is: **cache keyed by (LightID, CascadeIndex) with primitive bitset tracking + overlap-based scrolling logic**.
