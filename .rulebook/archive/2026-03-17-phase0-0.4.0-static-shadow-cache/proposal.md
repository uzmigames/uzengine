# Static Shadow Map Caching — UE5 Parity

## Priority: P2 — Performance optimization, eliminates redundant shadow rendering

## Why

UzEngine re-renders ALL shadow geometry every frame, wasting 50-80% of shadow rendering cost on static meshes that never move. UE5 caches shadow maps for static geometry and only re-renders dynamic objects, then composites both. CSM scrolling further reduces cost by GPU-copying overlapping regions when the camera moves.

## Summary

UzEngine re-renders all shadow geometry every frame, including static meshes that never move. UE5 caches shadow maps for static geometry and only re-renders dynamic/movable objects each frame, then composites both. This eliminates 50-80% of shadow rendering cost in scenes with mostly static geometry (buildings, terrain, props). UE5 also supports "CSM scrolling" — GPU-copying the overlapping region when the camera moves and only re-rendering the newly exposed edges.

## UE5 Reference

### Architecture
**Source**: `ScenePrivate.h:2685-2722`, `ShadowSetup.cpp:3846-3970`

**Four rendering modes** (`EShadowDepthCacheMode`):
1. **SDCM_StaticPrimitivesOnly** — Render only static meshes. Cached, re-rendered infrequently.
2. **SDCM_MovablePrimitivesOnly** — Render only dynamic meshes. Every frame.
3. **SDCM_CSMScrolling** — GPU copy overlap + render new edges. Incremental update.
4. **SDCM_Uncached** — Render everything. Fallback when memory budget exceeded.

### Cache Key & Data
**Source**: `ScenePrivate.h:3200`
```cpp
TMap<int32 LightID, TArray<FCachedShadowMapData>> CachedShadowMaps;
// FCachedShadowMapData contains:
//   - FWholeSceneProjectedShadowInitializer (light setup at cache time)
//   - FShadowMapRenderTargetsRefCounted (pooled depth targets)
//   - TBitArray<> StaticShadowSubjectPersistentPrimitiveIdMap (rendered statics bitset)
//   - FIntPoint ShadowBufferResolution
//   - float MaxSubjectZ, MinSubjectZ
```

### Cache Invalidation
**Source**: `ShadowSetup.cpp:3869-3954`

Cache invalidates when ANY of:
- Light direction/up vector changed
- Subject bounds origin/radius changed
- Light-space translation changed
- Projection scale changed
- Z-extent changed (MinLightW, MaxDistanceToCastInLightW)
- Render target released or resolution changed
- Memory budget exceeded

### Memory Budget
**Source**: `ShadowSetup.cpp:94`
- `r.Shadow.WholeSceneShadowCacheMb = 150` MB default
- If total cached shadow memory exceeds budget → fallback to SDCM_Uncached

### CSM Scrolling
**Source**: `ShadowSetup.cpp:5600-5668`
- Compute overlap ratio between old and new cascade bounds
- If overlap > 75%: GPU copy overlap region, render only new edges (4 planes)
- If overlap < 75%: Full rebuild (too much changed)

### Compositing
- Static shadows → separate depth target (cached)
- Dynamic shadows → separate depth target (per-frame)
- Final: `shadow = static_shadow AND dynamic_shadow` (multiply)

## Impact

- **Files modified**: shadow_pass.h/cpp, scene_renderer.cpp, constant_buffers.h, shadow_common.hlsli
- **New files**: shadow_cache.h/cpp (cache management), shadow_cache_types.h (data structures)
- **VRAM**: +150MB budget for cached shadow targets (configurable)
- **Performance**: Saves 50-80% of shadow rendering cost in static-heavy scenes
- **Complexity**: MEDIUM-HIGH — requires static/dynamic primitive classification, separate render targets, cache invalidation logic
- **Dependency**: Requires primitive mobility classification (static vs movable) in the scene system

## Acceptance Criteria

1. Static geometry shadow maps cached and reused across frames
2. Dynamic geometry shadow maps rendered every frame to separate target
3. Compositing multiplies static and dynamic shadow results
4. Cache invalidation triggers on light direction change, bounds change, resolution change
5. Memory budget limits total VRAM for cached shadows (default 150MB)
6. Fallback to uncached mode when budget exceeded
7. CSM scrolling copies overlap region and re-renders edges only
8. Performance: measurable FPS improvement in static-heavy scenes (capture before/after)
9. No visual difference between cached and uncached rendering
10. Demo: scene with 50+ static meshes — show shadow render time reduction
