## 1. Cache Data Structures

- [ ] 1.1 Create `shadow_cache.h` with `CachedShadowMapData` struct: light initializer snapshot, cached depth texture handle, `TBitArray`-equivalent bitset of rendered static primitive IDs, buffer resolution, MaxSubjectZ/MinSubjectZ, last used frame number. Source: `ScenePrivate.h:2685-2722`.
- [ ] 1.2 Create `EShadowDepthCacheMode` enum: `Uncached`, `StaticPrimitivesOnly`, `MovablePrimitivesOnly`, `CSMScrolling`. Source: `ShadowRendering.h:183-189`.
- [ ] 1.3 Create shadow cache map: `std::unordered_map<uint32_t lightID, std::array<CachedShadowMapData, kMaxShadowCascades>>`. Source: `ScenePrivate.h:3200`.
- [ ] 1.4 Add `ShadowCacheConfig` parameters: `enabled` (bool, default true), `cacheBudgetMB` (uint32_t, default 150), `scrollingOverlapThreshold` (float, default 0.75). Source: `ShadowSetup.cpp:69-100`.

## 2. Primitive Mobility Classification

- [ ] 2.1 Add `EMobility` enum (`Static`, `Stationary`, `Movable`) to mesh/primitive component if not already present.
- [ ] 2.2 Classify each draw command as static or movable in the shadow draw list.
- [ ] 2.3 Split shadow draw commands into two lists: `staticDraws` and `movableDraws`.

## 3. Two-Pass Shadow Rendering

- [ ] 3.1 Modify `AddShadowPass()` to accept cache mode parameter. In `StaticPrimitivesOnly` mode, only render `staticDraws`. In `MovablePrimitivesOnly` mode, only render `movableDraws`.
- [ ] 3.2 Allocate separate depth targets: one for cached static shadows, one for per-frame dynamic shadows. Each per cascade.
- [ ] 3.3 For `StaticPrimitivesOnly` pass: only execute if cache is invalid. Otherwise skip (reuse cached texture).
- [ ] 3.4 For `MovablePrimitivesOnly` pass: always execute every frame.
- [ ] 3.5 Track rendered static primitives in bitset for CSM scrolling culling.

## 4. Cache Invalidation

- [ ] 4.1 Implement `IsCacheValid()` function comparing current frame's light initializer against cached snapshot. Check: light direction, up vector, subject bounds, translation, projection scale, Z-extent, resolution. Source: `ShadowSetup.cpp:3869-3954`.
- [ ] 4.2 If ANY parameter changed → invalidate cache → re-render static pass.
- [ ] 4.3 Track memory usage of all cached shadow targets. If total exceeds `cacheBudgetMB` → fallback to `SDCM_Uncached` for new allocations (no eviction). Source: `ShadowSetup.cpp:432-451`.

## 5. Shadow Compositing

- [ ] 5.1 In deferred lighting / forward shadow sampling: sample both static and dynamic shadow textures.
- [ ] 5.2 Composite: `finalShadow = min(staticShadow, dynamicShadow)` (both must pass for lit).
- [ ] 5.3 Update `ShadowDataCB` to include both `staticShadowAtlasIndex` and `dynamicShadowAtlasIndex` SRV indices.
- [ ] 5.4 Update `shadow_common.hlsli` to sample both textures and multiply results.

## 6. CSM Scrolling (Incremental Update)

- [ ] 6.1 Compute overlap ratio between old cascade bounds and new cascade bounds when camera moves. Source: `ShadowSetup.cpp:3913`.
- [ ] 6.2 If overlap > `scrollingOverlapThreshold` (0.75): use CSM scrolling mode.
- [ ] 6.3 GPU copy overlapping region from old cached texture to new position in target.
- [ ] 6.4 Compute up to 4 culling planes for non-overlapped edges (top/bottom/left/right). Only render static primitives intersecting these planes. Source: `ShadowSetup.cpp:5641-5668`.
- [ ] 6.5 If overlap < threshold: full static re-render (too much changed).

## 7. Memory Management

- [ ] 7.1 Track total VRAM of cached shadow textures via `ComputeMemorySize()` per entry.
- [ ] 7.2 When budget exceeded: switch new allocations to `Uncached` mode (no eviction of existing caches).
- [ ] 7.3 Release cached textures when light is destroyed or shadow distance changes significantly.
- [ ] 7.4 Add `r.Shadow.CacheEnabled` CVar equivalent for runtime toggle.

## 8. Visual Verification

- [ ] 8.1 Screenshot: cached mode vs uncached mode — verify pixel-identical output
- [ ] 8.2 Screenshot: CSM scrolling — verify no seams or artifacts during camera movement
- [ ] 8.3 Performance capture: before (uncached) vs after (cached) — show FPS improvement
- [ ] 8.4 Screenshot: dynamic object moving through cached static shadows — correct compositing
- [ ] 8.5 Regression: all existing demos render correctly

## 9. Documentation

- [ ] 9.1 Document shadow caching architecture: two-pass model, invalidation triggers, memory budget
- [ ] 9.2 Document CSM scrolling algorithm with overlap threshold
- [ ] 9.3 Performance guide: expected savings per scene type
