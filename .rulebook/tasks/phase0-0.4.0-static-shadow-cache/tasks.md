## 1. Cache Data Structures

- [x] 1.1 Create `shadow_cache.h` with `CachedShadowMapData` struct: light initializer snapshot, cached depth texture handle, buffer resolution, MaxSubjectZ/MinSubjectZ, last used frame number. Source: `ScenePrivate.h:2685-2722`.
- [x] 1.2 Create `ShadowCacheMode` enum: `Uncached`, `StaticPrimitivesOnly`, `MovablePrimitivesOnly`, `CSMScrolling`. Source: `ShadowRendering.h:183-189`.
- [x] 1.3 Create shadow cache map: `std::unordered_map<uint32_t, LightShadowCache>` in `ShadowCacheManager`. Source: `ScenePrivate.h:3200`.
- [x] 1.4 Add `ShadowCacheConfig` parameters: `enabled` (bool, default true), `cacheBudgetMB` (uint32_t, default 150), `scrollingOverlapThreshold` (float, default 0.75). Source: `ShadowSetup.cpp:69-100`.

## 2. Primitive Mobility Classification

- [x] 2.1 Add `EMobility` enum (`Static`, `Stationary`, `Movable`) to `draw_list.h`.
- [x] 2.2 Add `mobility` field to `DrawCommand`, classified in `SyncWorldToDrawList()`.
- [x] 2.3 Shadow pass filters draws by `cacheMode` — `StaticPrimitivesOnly` skips Movable, `MovablePrimitivesOnly` skips Static/Stationary.

## 3. Two-Pass Shadow Rendering

- [x] 3.1 Modify `ShadowPassDesc` to accept `cacheMode` parameter (default `Uncached` for backward compat).
- [x] 3.2 Separate static shadow atlas (`m_staticShadowAtlas`) created in `SceneRenderer::Init()`.
- [x] 3.3 Static pass registered conditionally — only when cache invalid via `m_shadowCacheManager.UpdateCache()`.
- [x] 3.4 Dynamic pass registered every frame with `cacheMode = MovablePrimitivesOnly`.
- [x] 3.5 Track rendered static primitives in `std::vector<bool>` bitset (`staticPrimitiveMap` in `CachedShadowMapData`). `MarkStaticPrimitiveRendered()` and `IsStaticPrimitiveRendered()` on `ShadowCacheManager`. Used by CSM scrolling to identify which primitives need re-rendering in edge strips. Source: UE5 `ScenePrivate.h:2697`.

## 4. Cache Invalidation

- [x] 4.1 Implement `IsCacheValid()` comparing `ShadowCacheKey`: light direction (1e-4 epsilon), up vector, subject radius, Z-extent, projection scale, resolution. Source: `ShadowSetup.cpp:3869-3954`.
- [x] 4.2 If ANY parameter changed → `UpdateCache()` returns `StaticPrimitivesOnly` (re-render needed).
- [x] 4.3 Track memory usage of all cached shadow targets. If total exceeds `cacheBudgetMB` → fallback to `Uncached`. Source: `ShadowSetup.cpp:432-451`.

## 5. Shadow Compositing

- [x] 5.1 In `shadow_common.hlsli`: `SampleShadowFilteredFromAtlas()` samples explicit SRV-indexed shadow atlas (bindless).
- [x] 5.2 `SampleShadowComposited()` composites: `finalShadow = min(staticShadow, dynamicShadow)`. Early-out when `useShadowCache == 0`.
- [x] 5.3 Added `staticShadowAtlasIndex` and `useShadowCache` to `ShadowDataCB` in `constant_buffers.h`.
- [x] 5.4 Updated `shadow_common.hlsli` — compositing integrated into `SampleShadowMap()`.

## 6. CSM Scrolling (Incremental Update)

- [x] 6.1 Compute overlap ratio between old and new cascade AABB bounds in light space. `ComputeScrollingData()` projects cascade centers into light space, computes intersection area, returns `overlapRatio`. Source: UE5 `ShadowSetup.cpp:3893-3913`.
- [x] 6.2 If overlap > `scrollingOverlapThreshold` (0.75): `UpdateCache()` returns `CSMScrolling`. `IsCacheValidForScrolling()` validates light dir/up/radius match with center mismatch. Source: UE5 `ShadowSetup.cpp:3916-3919`.
- [x] 6.3 GPU copy overlapping region: static shadow atlas is reused in-place (overlap data already at correct position). `ShadowCSMScrolling` render graph pass renders only edge-strip primitives without clearing the atlas. Source: UE5 `ShadowDepthRendering.cpp:1121-1126`.
- [x] 6.4 Compute up to 4 culling planes for non-overlapped edge strips (top/bottom/left/right). Stored in `CSMScrollingData::cullingPlanes[]`. Primitives are tested against planes via dot product — negative side = intersects strip. Source: UE5 `ShadowSetup.cpp:5641-5667`.
- [x] 6.5 If overlap < threshold: `UpdateCache()` returns `StaticPrimitivesOnly` (full static re-render, same as cache miss). Source: UE5 `ShadowSetup.cpp:3921-3931`.

## 7. Memory Management

- [x] 7.1 Track total VRAM via `GetTotalMemoryUsage()` — computed from cached texture count * resolution.
- [x] 7.2 When budget exceeded: `UpdateCache()` returns `Uncached` (no eviction).
- [x] 7.3 `EvictStale()` releases textures not used since a given frame number.
- [x] 7.4 Runtime toggle: `SetShadowCacheEnabled(bool)` / `IsShadowCacheEnabled()` on `SceneRenderer`. Lua API: `uz.renderer.set_shadow_cache(bool)` / `uz.renderer.get_shadow_cache()`. Source: UE5 `ShadowSetup.cpp:393-398`.

## 8. Visual Verification

- [x] 8.5 Regression: PBR demo renders correctly with cache infrastructure (default disabled).

## 9. Documentation

- [x] 9.1 Shadow caching architecture documented in `docs/guides/shadow-caching-guide.md` — two-pass rendering, cache modes, compositing, invalidation.
- [x] 9.2 CSM scrolling algorithm documented — overlap computation, edge-strip culling planes, primitive tracking bitset, with diagrams.
- [x] 9.3 Performance guide — VRAM budget table, when to enable/disable, profiling tips, runtime toggle API.

## Note
All items complete. Shadow cache system implements UE5-parity two-pass rendering with static/dynamic atlas separation, CSM scrolling with incremental edge-strip re-rendering, per-cascade primitive tracking bitset, runtime Lua toggle, and comprehensive documentation.
