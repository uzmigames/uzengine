# Shadow Caching Guide

<!-- doc-meta
title: Shadow Caching Guide
category: guide
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: render-graph-engineer
-->

## Overview

UzEngine's shadow cache system eliminates redundant shadow map rendering for static geometry. When enabled, static meshes are rendered once into a cached depth atlas and reused across frames. Only dynamic (movable) objects are re-rendered every frame. CSM scrolling further reduces cost by incrementally updating the cache when the camera moves.

## Context

This system achieves UE5 parity with `EShadowDepthCacheMode` (ShadowRendering.h:183-189) and the CSM scrolling algorithm (ShadowSetup.cpp:5600-5668). It is part of the phase0-0.4.0 static shadow cache milestone.

## Architecture

### Two-Pass Shadow Rendering

When shadow caching is active, each frame renders shadows in two passes:

```
Pass 1 (conditional): Static geometry -> Cached Shadow Atlas
  - Only executes when the cache is invalidated
  - Invalidation triggers: light direction change, cascade parameter change,
    new static geometry added to the scene
  - Output: D32Float depth atlas containing only static/stationary geometry

Pass 2 (every frame): Dynamic geometry -> Per-Frame Shadow Atlas
  - Always executes
  - Renders only Movable primitives
  - Pixel shader composites both atlases: finalShadow = min(static, dynamic)
```

### Shadow Cache Modes

| Mode | When Used | What Is Rendered |
|------|-----------|-----------------|
| `Uncached` | Caching disabled or budget exceeded | All geometry every frame |
| `StaticPrimitivesOnly` | Cache miss (first frame or invalidated) | Static + Stationary geometry only |
| `MovablePrimitivesOnly` | Cache hit (parameters unchanged) | Movable geometry only |
| `CSMScrolling` | Camera moved but light unchanged | GPU copy overlap + edge strips |

Source: UE5 `ShadowRendering.h:183-189` (`EShadowDepthCacheMode`).

### Primitive Mobility Classification

Every draw command has an `EMobility` field:
- **Static** — Never moves. Cached in the static shadow atlas.
- **Stationary** — Baked lighting but may have dynamic shadows. Treated as Static for shadow caching.
- **Movable** — Fully dynamic. Re-rendered every frame into the per-frame atlas.

Source: UE5 `EngineTypes.h` (`EComponentMobility::Type`).

### Shadow Compositing

The pixel shader composites both shadow atlases:

```hlsl
float staticShadow = SampleShadowFilteredFromAtlas(staticAtlas, uv);
float dynamicShadow = SampleShadowFilteredFromAtlas(dynamicAtlas, uv);
float finalShadow = min(staticShadow, dynamicShadow);
```

This is correct because a pixel is in shadow if it is occluded by either static OR dynamic geometry. The `min()` operation selects the most shadowed result.

### Cache Invalidation

The `ShadowCacheManager` compares a `ShadowCacheKey` snapshot per cascade:

| Field | Tolerance | Reason |
|-------|-----------|--------|
| Light direction | 1e-4 | Small angular drift shifts shadows at distance |
| Light up vector | 1e-4 | Same as direction |
| Subject radius | 1e-2 | Small changes have minimal visual impact |
| Min/Max subject Z | 1e-2 | Cascade extent changes |
| Projection scale | 1e-2 | Ortho projection size |
| Resolution | exact | Different texture size = incompatible |

Source: UE5 `ShadowSetup.cpp:3869-3954` (`CompareShadowVolume`).

## CSM Scrolling Algorithm

When the camera moves but the light direction stays constant, the cached shadow map content partially overlaps with the new cascade bounds. Instead of re-rendering all static geometry, CSM scrolling:

1. **Computes overlap ratio** between old and new cascade AABBs in light space
2. **If overlap > threshold** (default 0.75): uses scrolling mode
3. **GPU copies** the overlapping region from the cached position to the new position
4. **Computes culling planes** for the 4 edge strips (top, bottom, left, right)
5. **Re-renders only** static primitives that intersect the edge strips

### Overlap Computation

```
Light-space projection:
  Current cascade: [-radius, -radius] to [radius, radius] (centered at origin)
  Cached cascade:  offset by (cachedCenter - currentCenter) projected to light space

Overlap = AABB intersection of current and cached viewports
Ratio = overlapArea / cachedArea

If ratio > 0.75: use CSM scrolling
If ratio <= 0.75: full static re-render (too much has changed)
```

Source: UE5 `ShadowSetup.cpp:3893-3919`.

### Edge-Strip Culling Planes

Up to 4 culling planes are generated — one for each edge where the new cascade extends beyond the overlap region:

```
+------------------+
|   Top strip      |  <- Plane: point at overlap top, normal pointing up
+==================+
|                  |
|   Overlap        |  <- GPU-copied from cached atlas
|   (reused)       |
|                  |
+==================+
|   Bottom strip   |  <- Plane: point at overlap bottom, normal pointing down
+------------------+
```

Only static primitives on the negative side of any culling plane (intersecting the edge strip) need re-rendering.

Source: UE5 `ShadowSetup.cpp:5641-5667` (`CSMScrollingExtraCullingPlanes`).

### Primitive Tracking Bitset

A per-cascade `std::vector<bool>` tracks which primitive indices were rendered into the cached shadow map. During CSM scrolling:
- **New primitives** (not in the bitset) are always rendered
- **Existing primitives** are only rendered if they intersect an edge-strip culling plane

Source: UE5 `ScenePrivate.h:2697` (`StaticShadowSubjectPersistentPrimitiveIdMap`).

## Performance Guide

### VRAM Budget

| Cascades | Resolution | Per-Cascade (D32F) | Static Atlas Total |
|----------|------------|--------------------|--------------------|
| 4 | 1024x1024 | 4 MB | 16 MB |
| 4 | 2048x2048 | 16 MB | 64 MB |
| 8 | 2048x2048 | 16 MB | 128 MB |

Default budget: 150 MB (`ShadowCacheConfig::cacheBudgetMB`). If exceeded, caching falls back to `Uncached` for that cascade.

### When to Enable Shadow Caching

**Enable when:**
- Scene has significant static geometry (buildings, terrain, props)
- Camera moves frequently (FPS, TPS gameplay)
- Light direction is mostly constant (sun moves slowly)
- Shadow map resolution is high (2048+)

**Disable when:**
- All geometry is dynamic (no static meshes)
- Light direction changes every frame (rotating light)
- VRAM is constrained (mobile, low-end)
- Debugging shadow issues (caching can mask problems)

### Profiling Tips

1. Use `UZ_PROFILE_SCOPE` markers to measure static vs dynamic pass times
2. Check `ShadowCacheManager::GetTotalMemoryUsage()` for VRAM consumption
3. Compare frame times with caching on vs off
4. Monitor cache hit rate: `MovablePrimitivesOnly` = cache hit, `StaticPrimitivesOnly` = miss
5. CSM scrolling efficiency: count extra static subjects per frame (logged at Trace level)

### Runtime Toggle

Toggle shadow caching at runtime via Lua:

```lua
-- Disable shadow caching
uz.renderer.set_shadow_cache(false)

-- Enable shadow caching
uz.renderer.set_shadow_cache(true)

-- Query current state
local enabled = uz.renderer.get_shadow_cache()
```

Or from C++:

```cpp
sceneRenderer.SetShadowCacheEnabled(false);
bool enabled = sceneRenderer.IsShadowCacheEnabled();
```

## Key Takeaways

- Shadow caching splits rendering into static (cached) and dynamic (per-frame) passes
- CSM scrolling avoids full re-renders when only the camera moves
- The pixel shader composites both atlases with `min(static, dynamic)`
- VRAM budget is configurable; exceeded budget falls back to uncached
- Runtime toggle available via Lua `uz.renderer.set_shadow_cache(bool)`

## References

- UE5 `ShadowRendering.h:183-189` — `EShadowDepthCacheMode` enum
- UE5 `ShadowSetup.cpp:3869-3954` — Cache validation
- UE5 `ShadowSetup.cpp:5600-5668` — CSM scrolling data computation
- UE5 `ShadowDepthRendering.cpp:1000-1035` — GPU copy for scrolling
- UE5 `ScenePrivate.h:2685-2722` — `FCachedShadowMapData`
- [Shadow Cache Implementation](../../runtime/engine/renderer/shadows/shadow_cache.h)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-17 | Initial creation — architecture, CSM scrolling, performance guide |
