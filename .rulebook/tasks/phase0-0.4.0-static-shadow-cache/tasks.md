## 1. Cache Data Structures

- [x] 1.1 Create `shadow_cache.h` with `CachedShadowMapData` struct: light initializer snapshot, cached depth texture handle, buffer resolution, MaxSubjectZ/MinSubjectZ, last used frame number. Source: `ScenePrivate.h:2685-2722`.
- [x] 1.2 Create `ShadowCacheMode` enum: `Uncached`, `StaticPrimitivesOnly`, `MovablePrimitivesOnly`, `CSMScrolling`. Source: `ShadowRendering.h:183-189`.
- [x] 1.3 Create shadow cache map: `std::unordered_map<uint32_t, LightShadowCache>` in `ShadowCacheManager`. Source: `ScenePrivate.h:3200`.
- [x] 1.4 Add `ShadowCacheConfig` parameters: `enabled` (bool, default true), `cacheBudgetMB` (uint32_t, default 150), `scrollingOverlapThreshold` (float, default 0.75). Source: `ShadowSetup.cpp:69-100`.

## 2. Primitive Mobility Classification

- [ ] 2.1 Add `EMobility` enum (`Static`, `Stationary`, `Movable`) to mesh/primitive component if not already present. (Deferred — requires scene system)
- [ ] 2.2 Classify each draw command as static or movable in the shadow draw list. (Deferred — requires mobility enum)
- [ ] 2.3 Split shadow draw commands into two lists: `staticDraws` and `movableDraws`. (Deferred — requires mobility enum)

## 3. Two-Pass Shadow Rendering

- [x] 3.1 Modify `ShadowPassDesc` to accept `cacheMode` parameter (default `Uncached` for backward compat).
- [ ] 3.2 Allocate separate depth targets: one for cached static shadows, one for per-frame dynamic shadows. (Deferred — requires draw list split)
- [ ] 3.3 For `StaticPrimitivesOnly` pass: only execute if cache is invalid. (Deferred — requires draw list split)
- [ ] 3.4 For `MovablePrimitivesOnly` pass: always execute every frame. (Deferred — requires draw list split)
- [ ] 3.5 Track rendered static primitives in bitset for CSM scrolling culling. (Deferred)

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

- [ ] 6.1 Compute overlap ratio between old and new cascade bounds. (Deferred — requires GPU copy infrastructure)
- [ ] 6.2 If overlap > threshold: use CSM scrolling mode. (Deferred)
- [ ] 6.3 GPU copy overlapping region. (Deferred)
- [ ] 6.4 Compute culling planes for non-overlapped edges. (Deferred)
- [ ] 6.5 If overlap < threshold: full static re-render. (Deferred)

## 7. Memory Management

- [x] 7.1 Track total VRAM via `GetTotalMemoryUsage()` — computed from cached texture count * resolution.
- [x] 7.2 When budget exceeded: `UpdateCache()` returns `Uncached` (no eviction).
- [x] 7.3 `EvictStale()` releases textures not used since a given frame number.
- [ ] 7.4 Add `r.Shadow.CacheEnabled` CVar equivalent for runtime toggle. (Deferred — requires CVar system)

## 8. Visual Verification

- [x] 8.5 Regression: PBR demo renders correctly with cache infrastructure (default disabled).

## 9. Documentation

- [ ] 9.1 Document shadow caching architecture
- [ ] 9.2 Document CSM scrolling algorithm
- [ ] 9.3 Performance guide

## Note
Core cache infrastructure complete (types, invalidation, VRAM budget, compositing shader). Two-pass rendering and CSM scrolling deferred — require primitive mobility classification and GPU copy infrastructure that don't exist yet.
