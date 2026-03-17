# Shadow System

<!-- doc-meta
title: Shadow System
category: spec
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

## Overview

UzEngine's shadow system implements Cascaded Shadow Maps (CSM) with UE5 exact parity, a
static shadow cache for eliminating redundant static-geometry renders, Distance Field
Shadows (DFS) for soft long-range shadows, and PCF filtering for edge softness.
The debug visualization grid (`--shadow-debug 1`) renders a 3x3 diagnostic view.

## Architecture

### Cascade Computation

`ComputeCascades()` (`shadow_cascade.h/cpp`) splits the camera frustum into up to 8
cascades (4 main + 4 far). Split distances use UE5's exponential accumulation scheme
(source: `DirectionalLightComponent.cpp:753-814`), not the Nvidia GPU Gems log/linear
blend.

Key defaults (all match UE5 5.6):

| Parameter | Value | UE5 Source |
|---|---|---|
| Cascade count | 4 | `DLC.cpp:1019` |
| Max shadow distance | 40,000 cm (400m) | `DLC.cpp:1017` |
| Distribution exponent | 3.0 | `DLC.cpp:1029` |
| Transition fraction | 0.1 (10%) | `DLC.cpp:1030` |
| Far shadow distance | 300,000 cm (3 km) | `DLC.cpp:1023` |
| CSM depth bias | 10.0 | `ShadowRendering.cpp:32` |
| Slope-scale depth bias | 3.0 | `ShadowRendering.cpp:38` |

Each cascade output (`ShadowCascade`) carries `viewProj`, `splitNear`, `splitFar`,
`splitFarFadeStart`, `texelSize`, `computedDepthBias`, `computedNormalBias`, and
`center`. The projection is texel-snapped to prevent shadow shimmer.

### Shadow Atlas

`ShadowAtlas` (`shadow_atlas.h/cpp`) owns a single D32_FLOAT depth texture laid out as
an NxM grid where each tile is `kShadowResolution` (2048) texels wide and tall. The grid
dimensions are derived from the total cascade count. The atlas exposes a texture handle
and an SRV heap index for shader sampling.

### Shadow Cache

`ShadowCacheManager` (`shadow_cache.h/cpp`) caches static-geometry depth maps across
frames, matching UE5 `EShadowDepthCacheMode` (source: `ShadowRendering.h:183-189`).

Four cache modes per cascade:

| Mode | Behavior |
|---|---|
| `Uncached` | Render all geometry every frame (fallback) |
| `StaticPrimitivesOnly` | Render only static meshes; result cached across frames |
| `MovablePrimitivesOnly` | Render only dynamic meshes; executed every frame |
| `CSMScrolling` | GPU copy overlapping region; re-render exposed edge strips only |

The pixel shader composites static (cached) and dynamic atlases by taking the minimum
(most shadowed) result. Cache invalidation uses `ShadowCacheKey` which captures light
direction, cascade center, bounding sphere radius, and resolution. Comparison uses
epsilon tolerances (source: `ShadowSetup.cpp:3869-3954`). VRAM budget default: 150 MB.

CSM scrolling computes overlap UV regions and culling planes
(source: `ShadowSetup.cpp:5600-5668`) so only the newly exposed edge strips require
re-rendering. Falls back to full re-render when overlap ratio drops below the threshold
(default: 0.75) or when extra static subjects exceed `kMaxCSMScrollingExtraStaticSubjects`
(5, source: `ShadowSetup.cpp:406-409`).

### Distance Field Shadows

Two compute shaders implement DFS:

- `dfs_cull_objects_cs.hlsl` — tile-based object culling, 200 cm world tile size
- `dfs_cone_trace_cs.hlsl` — cone-traced ray march, configurable step count

Quality presets (source: `DistanceFieldShadowing.cpp`):

| Preset | Steps | Cost (1080p half-res) |
|---|---|---|
| Off | 0 | 0 ms |
| Low | 20 | ~0.7 ms |
| Medium | 32 | ~1.2 ms |
| High | 64 | ~2.5 ms |

Default max DFS distance: 51,200 cm (512 m) — source: `DLC.cpp:1018`.
Half-resolution trace with bilateral upsample by default. Self-shadow bias: 2 cm.

`SdfAtlasManager` manages a global `Texture3D` (R8_UNORM, default 256^3 = 16 MB) using
a linear brick allocator. Each brick is 8^3 texels (512 bytes). The atlas holds 32,768
bricks at 256^3 (source: `DistanceFieldAtlas.h`).

### PCF Filtering

Three filter modes, selected via `ShadowConfig::filterMethod`:

| Mode | Description |
|---|---|
| 0 | 5x5 PCF grid (25 taps) — default |
| 1 | 16-tap Poisson disk with per-pixel rotation (softer, less aliased) |
| 2 | PCSS — blocker search + penumbra estimation + adaptive PCF |

Source: `ShadowRendering.cpp:180-192`. PCSS max soft kernel size: 40.0 texels.
PCF and PCSS logic lives in `runtime/shaders/runtime/include/pcss.hlsli`.

### Debug Visualization

Activated by `--shadow-debug 1`. The deferred lighting shader renders a 3x3 diagnostic
grid when `PerFrameCB::shadowDebugMode == 1`:

| (0,0) Albedo | (1,0) Normal | (2,0) Raw Atlas |
|---|---|---|
| (0,1) Shadow | (1,1) LIT | (2,1) Depth |
| (0,2) Cascade | (1,2) AO | (2,2) Roughness |

- Tile (2,0): Raw shadow atlas contents. All white = shadow pass not writing.
- Tile (0,1): Final shadow value per pixel. All white = shadow sampling broken.
- Tile (0,2): Cascade index color-coded (red/green/blue/yellow = 0/1/2/3).

Implementation: `runtime/shaders/runtime/include/shadow_debug.hlsli` + integration
in `deferred_lighting_ps.hlsl`.

## Key Files

| File | Purpose |
|---|---|
| `runtime/engine/renderer/shadows/shadow_cascade.h/cpp` | Cascade split + light-space matrix computation |
| `runtime/engine/renderer/shadows/shadow_atlas.h/cpp` | D32_FLOAT depth atlas ownership |
| `runtime/engine/renderer/shadows/shadow_cache.h/cpp` | Static shadow cache manager |
| `runtime/engine/renderer/shadows/shadow_renderer.h/cpp` | Shadow pass orchestration |
| `runtime/engine/renderer/shadows/distance_field_shadow_config.h` | DFS quality presets and config |
| `runtime/engine/renderer/shadows/sdf_atlas_manager.h/cpp` | SDF brick atlas (Texture3D) |
| `runtime/engine/renderer/shadows/sdf_types.h` | SDF data structures |
| `runtime/shaders/runtime/lighting/shadow_depth_vs.hlsl` | Shadow depth vertex shader |
| `runtime/shaders/runtime/shadows/dfs_cone_trace_cs.hlsl` | DFS cone trace compute shader |
| `runtime/shaders/runtime/shadows/dfs_cull_objects_cs.hlsl` | DFS object culling compute shader |
| `runtime/shaders/runtime/include/shadow_common.hlsli` | Shared shadow sampling helpers |
| `runtime/shaders/runtime/include/pcss.hlsli` | PCF and PCSS filter implementations |
| `runtime/shaders/runtime/include/shadow_debug.hlsli` | Debug visualization grid |
| `runtime/shaders/runtime/include/dfs_common.hlsli` | DFS shader shared types |

## Frame Pipeline Position

The shadow pass executes before the depth prepass and GBuffer pass:

```
[Shadow Pass]      ← per-cascade, opaque geometry → shadow atlas
      |
      v
[Depth Prepass]    ← all opaque geometry → depth buffer (see depth-prepass-hiz.md)
      |
      v
[Hi-Z Pyramid]     ← compute, builds min-depth mip chain from depth prepass output
      |
      v
[GBuffer Pass]     ← opaque geometry with early-Z (EQUAL depth test pending)
      |
      v
[Lighting Pass]    ← reads shadow atlas SRV for shadow term evaluation
```

Shadow atlas SRV indices are passed to the lighting pass via `PerFrameCB` (shadow matrices, cascade splits, and atlas SRV index). The lighting pixel shader evaluates the shadow term per cascade and applies PCF or PCSS filtering.

For depth prepass and Hi-Z details see [15-depth-prepass-hiz.md](../../../engine/12-rendering/15-depth-prepass-hiz.md).

## UE5 Reference

| UzEngine System | UE5 Equivalent |
|---|---|
| `ComputeCascades` | `FProjectedShadowInfo::ComputeWholeSceneShadowCacheTiles` + split scheme from `DirectionalLightComponent.cpp` |
| `ShadowCacheManager` | `FCachedShadowMapData` in `ScenePrivate.h:2685-2722` + `CompareShadowVolume` in `ShadowSetup.cpp:3869` |
| `ShadowCacheMode` | `EShadowDepthCacheMode` in `ShadowRendering.h:183-189` |
| `SdfAtlasManager` | `FDistanceFieldAtlas` (`DistanceFieldAtlas.h`) |
| PCF/PCSS | `ShadowProjectionPixelShader.usf`, `ShadowFilteringCommon.ush` |
| DFS | `DistanceFieldShadowing.cpp`, `DistanceFieldShadowingShader.usf` |

## Status

| Feature | Status |
|---|---|
| CSM — 4 main cascades | Done |
| CSM — 4 far cascades | Done |
| Texel-snapped projection | Done |
| Shadow atlas (D32_FLOAT) | Done |
| Shadow cache (static/dynamic separation) | Done |
| CSM scrolling | Done |
| 5x5 PCF | Done |
| 16-tap Poisson disk PCF | Done |
| PCSS | Done |
| DFS config and quality presets | Done |
| DFS cone trace compute shader | Done |
| DFS object culling compute shader | Done |
| SDF atlas (Texture3D, linear allocator) | Done |
| SDF brick upload to GPU (staging path) | Planned (Phase 1.5) |
| Shadow debug grid | Done |
| Point/spot light shadows | Planned (Phase 2) |
