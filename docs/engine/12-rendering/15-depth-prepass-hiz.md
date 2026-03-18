# Depth Prepass & Hi-Z Pyramid

<!-- doc-meta
title: Depth Prepass & Hi-Z Pyramid
category: guide
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

> **Status**: Done (Phase 0.8.0)
> **Source**: `runtime/engine/renderer/passes/geometry/depth_prepass.h/cpp`, `runtime/engine/renderer/passes/postprocess/hiz_pyramid_pass.h/cpp`
> **Shader**: `runtime/shaders/runtime/compute/hiz_pyramid_cs.hlsl`
> **Depends on**: Render Graph (§12-04), RHI (§03), Shader Pipeline (§05), Deferred Pipeline (§12-11)

## Overview

The depth prepass renders all opaque geometry to a depth-only target before the GBuffer pass. The Hi-Z pyramid pass then builds a hierarchical min-depth mip chain from that depth buffer. Together they serve two purposes:

1. **Early-Z rejection**: Because the depth buffer is pre-populated, the GBuffer pixel shader is only invoked for fragments that survive the depth test. Overdraw is eliminated for opaque geometry — each pixel is shaded at most once.
2. **Hierarchical depth for downstream effects**: GTAO, SSR, contact shadows, and GPU occlusion culling all require a mip-mapped depth representation. The Hi-Z pyramid provides this at zero additional cost beyond the prepass itself.

This matches the architecture used by UE5 (`FDepthPrepassMeshProcessor`, `HZBOcclusionTests.cpp`) and is standard in all modern AAA deferred renderers.

## Architecture

### Pass Ordering

```
[Shadow Pass]
      |
      v
[Depth Prepass]          ← AddDepthPrepass()
      |                     depth-only, no color output, LessEqual reversed-Z
      v
[Hi-Z Pyramid]           ← AddHiZPyramidPass()
      |                     compute: per-mip 4-tap min(), 8x8 thread groups
      v
[GBuffer Pass]           ← AddGBufferPass()
      |                     reads depth from Depth Prepass
      v
[Lighting Pass]          ← AddLightingPass()
```

### Resource Flow

```
Depth Prepass
  writes → DepthBuffer (D32Float, full-res)
                |
                ├─→ Hi-Z Pyramid
                │     mip 0: copy of full-res depth
                │     mip 1: 1/2 res min-depth
                │     mip 2: 1/4 res min-depth
                │     ...
                │     mip N: 1/(2^N) res min-depth
                │
                └─→ GBuffer Pass (depth input, early-Z)
                          └─→ Lighting Pass (SRV for world position reconstruction)
```

The Hi-Z mip textures are stored as separate R32_FLOAT textures (one per mip level), each with both an SRV and a UAV descriptor. They are owned by `SceneRenderer::m_hizMipLevels[]`.

## Depth Prepass

### API

```cpp
/// Descriptor for the depth-only prepass.
struct DepthPrepassDesc {
    RGResourceId    depthBuffer;        // Render graph depth target (D32Float)
    const DrawList* drawList;           // Sorted opaque draw commands
    CBAllocator*    cbAllocator;        // Per-object CB ring allocator
    RHIAllocation   perFrameAlloc;      // PerFrameCB allocation
    RHIPipeline     pipeline;           // Depth-only PSO (no pixel shader)
    bool            bindless;           // SM6.6 bindless or SM5 fallback
    float           viewportWidth;
    float           viewportHeight;
};

/// Register a depth prepass into the render graph.
void AddDepthPrepass(RGBuilder& builder, const DepthPrepassDesc& desc);
```

### Toggle

The depth prepass can be disabled at runtime for debugging or profiling:

```cpp
// In SceneRenderer:
scene_renderer.SetDepthPrepassEnabled(true);   // default — enables early-Z
scene_renderer.SetDepthPrepassEnabled(false);  // disable — GBuffer writes depth directly
```

When disabled, the Hi-Z pyramid still runs but reads from the GBuffer depth output instead.

### PSO Properties

| Property | Value | Notes |
|---|---|---|
| Pixel shader | None | Depth-only; no color RT bound |
| Depth function | LessEqual | Reversed-Z: 1.0 = near, 0.0 = far |
| Depth write | Enabled | Writes Z for all opaque fragments |
| Depth bias | 0 | No bias (shadow pass manages its own bias separately) |
| Bindless | Yes / fallback | Same two-path pattern as GBuffer pass |
| Instancing | Yes | Uses the same instance buffer as GBuffer |

### Early-Z Interaction with GBuffer

When the depth prepass is active, the GBuffer PSO uses an `EQUAL` depth compare function. Any fragment that passes depth comparison (depth == prepass value) proceeds to the pixel shader; all others are discarded at the rasterizer stage.

> **ℹ️ Note**: The EQUAL depth test switch is item 1.3 in the implementation roadmap. The depth prepass already writes correct depth; wiring the GBuffer PSO to use EQUAL is a one-line change that will be done in the next pass.

### Render Graph State Transitions

| Resource | Before | After |
|---|---|---|
| `depthBuffer` | None | DepthStencilWrite |
| → GBuffer uses | DepthStencilWrite | DepthStencilWrite (EQUAL read + depth discard for failing pixels) |
| → Lighting uses | DepthStencilWrite | ShaderResource (SRV for world position reconstruction) |

## Hi-Z Pyramid

### API

```cpp
/// Descriptor for the Hi-Z min-depth pyramid compute pass.
struct HiZPyramidPassDesc {
    RGResourceId depthInput;            // Full-res depth from Depth Prepass
    RHITexture   hizMipTextures[kHiZMaxMipLevels]; // Per-mip R32_FLOAT textures
    uint32_t     mipCount;              // Actual mip count for this resolution
    uint32_t     baseWidth;             // Full-res width
    uint32_t     baseHeight;            // Full-res height
};

constexpr uint32_t kHiZMaxMipLevels = 12;  // Covers up to 4096×4096

/// Register a Hi-Z pyramid compute pass into the render graph.
void AddHiZPyramidPass(RGBuilder& builder, const HiZPyramidPassDesc& desc);
```

### Compute Shader (`hiz_pyramid_cs.hlsl`)

Based on `UE5/Engine/Shaders/Private/HZB.usf:183-293`.

```hlsl
/// @source UE5 HZB.usf:183 — DownsampleDepthCS
/// @constants kHiZMaxMipLevels = 12, thread group [8,8,1]
/// @deviations NONE

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 dstCoord = dispatchId.xy;
    uint2 srcCoord = dstCoord * 2;

    // Clamp to avoid reading out-of-bounds on odd-dimension textures
    uint2 srcMax = uint2(srcWidth - 1, srcHeight - 1);
    float d0 = srcDepth.Load(int3(min(srcCoord + uint2(0,0), srcMax), 0)).r;
    float d1 = srcDepth.Load(int3(min(srcCoord + uint2(1,0), srcMax), 0)).r;
    float d2 = srcDepth.Load(int3(min(srcCoord + uint2(0,1), srcMax), 0)).r;
    float d3 = srcDepth.Load(int3(min(srcCoord + uint2(1,1), srcMax), 0)).r;

    // min() for reversed-Z: smaller value = farther away
    float result = min(min(d0, d1), min(d2, d3));

    dstMip[dstCoord] = result;
}
```

**Key design choices**:
- Uses `Load()` rather than `Sample()` to avoid interpolation artifacts at mip boundaries
- Takes the **minimum** of the 4 source texels — correct for reversed-Z where 0.0 = far and 1.0 = near
- Clamps source coordinates rather than branching, avoiding divergent warps
- 8x8 thread groups match UE5's preferred tile size for depth processing

### Mip Allocation

Each mip level is a separate R32_FLOAT texture with both SRV and UAV descriptors. Separate textures (rather than a single mip-chain texture) allow the compute shader to dispatch each mip independently via a UAV write without requiring the previous mip to be fully flushed — the render graph inserts the correct barriers between dispatches.

```
mip 0 → 1920 × 1080  (R32F, SRV + UAV)
mip 1 →  960 ×  540  (R32F, SRV + UAV)
mip 2 →  480 ×  270  (R32F, SRV + UAV)
mip 3 →  240 ×  135  (R32F, SRV + UAV)
...
mip 11→    1 ×    1  (R32F, SRV + UAV)  ← single min-depth of entire scene
```

### Allocation in SceneRenderer

```cpp
// In scene_renderer.h:
RHITexture m_hizMipLevels[kHiZMaxMipLevels];
uint32_t   m_hizActiveMips = 0;

// Created in SceneRenderer::Init():
void SceneRenderer::CreateHiZPyramid(uint32_t width, uint32_t height);

// Released in SceneRenderer::Shutdown():
void SceneRenderer::DestroyHiZPyramid();

// Recreated on window resize in SceneRenderer::Resize():
void SceneRenderer::Resize(uint32_t width, uint32_t height);
```

## Configuration

| Setting | Default | Description |
|---|---|---|
| `SetDepthPrepassEnabled(bool)` | `true` | Enable or disable the depth prepass |
| `kHiZMaxMipLevels` | `12` | Maximum Hi-Z mip levels (covers up to 4096×4096) |
| Depth format | D32Float | Hardware depth, reversed-Z (1.0 = near, 0.0 = far) |
| Hi-Z format | R32Float | One texture per mip, copied from depth output |
| Thread group size | [8, 8, 1] | Fixed per UE5 HZB reference |

## Downstream Consumers

The Hi-Z pyramid is available as input to any render graph pass registered after `AddHiZPyramidPass()`. The following effects use it:

| Effect | Phase | Consumption Pattern |
|---|---|---|
| GTAO (ground truth AO) | 3.15.0 | Hierarchical ray march: samples coarse mips first, refines with fine mips |
| Screen-space reflections | 3.16.0 | Hi-Z ray march: traces reflected ray through mip pyramid for O(log n) intersection |
| Contact shadows | 3.29.0 | Compares shadow ray depth against Hi-Z to short-circuit missed-geometry cases |
| GPU occlusion culling | 3.x | Tests object bounding box projected size against Hi-Z at appropriate mip level |

The classic Hi-Z SSR ray march works as follows: start at the lowest-resolution mip, advance the ray by the mip cell size, refine when an intersection is found by stepping to the next finer mip. This achieves correct screen-space ray marching in O(log n) steps instead of O(n).

## Performance

The depth prepass adds a full opaque geometry draw. On scenes with significant overdraw (city environments, dense foliage, character close-ups), the GBuffer savings more than compensate:

| Scenario | Depth Prepass Cost | GBuffer Savings | Net Gain |
|---|---|---|---|
| Open environment (low overdraw ~1.5x) | ~0.8 ms | ~0.2 ms | −0.6 ms (not beneficial) |
| City street (medium overdraw ~3x) | ~0.8 ms | ~1.6 ms | +0.8 ms |
| Dense indoor scene (high overdraw ~6x) | ~0.8 ms | ~4.0 ms | +3.2 ms |
| Character close-up (very high overdraw ~10x) | ~0.8 ms | ~7.2 ms | +6.4 ms |

The Hi-Z pyramid build is a fixed-cost compute pass regardless of scene complexity: approximately 0.2–0.4 ms at 1080p.

> **ℹ️ Note**: Disable the depth prepass for low-overdraw scenes by calling `SetDepthPrepassEnabled(false)`. The Hi-Z pyramid remains useful even without the early-Z benefit.

## UE5 Reference

| UzEngine System | UE5 Equivalent | UE5 Source |
|---|---|---|
| `DepthPrepassDesc` / `AddDepthPrepass` | `FDepthPrepassMeshProcessor` | `DepthRendering.cpp` |
| Depth prepass PSO (no PS) | `TDepthOnlyVS`, no pixel shader bound | `DepthOnlyVertexShader.usf` |
| `AddHiZPyramidPass` | `BuildHZB()` in `SceneRendering.cpp` | `SceneRendering.cpp:5200+` |
| `hiz_pyramid_cs.hlsl` | `DownsampleDepthCS` | `HZB.usf:183-293` |
| `m_hizMipLevels[]` | `FSceneViewState::HZBOcclusionTests` | `ScenePrivate.h` |

## Key Files

| File | Purpose |
|---|---|
| `runtime/engine/renderer/passes/geometry/depth_prepass.h` | `DepthPrepassDesc` struct + `AddDepthPrepass()` declaration |
| `runtime/engine/renderer/passes/geometry/depth_prepass.cpp` | Depth prepass render graph pass implementation |
| `runtime/engine/renderer/passes/postprocess/hiz_pyramid_pass.h` | `HiZPyramidPassDesc` struct + `AddHiZPyramidPass()` declaration |
| `runtime/engine/renderer/passes/postprocess/hiz_pyramid_pass.cpp` | Hi-Z pyramid compute pass implementation |
| `runtime/shaders/runtime/compute/hiz_pyramid_cs.hlsl` | Hi-Z downsample compute shader (UE5 HZB.usf:183-293) |
| `runtime/engine/renderer/scene/scene_renderer.h` | `m_depthPrepassEnabled`, `m_hizMipLevels[]`, `SetDepthPrepassEnabled()` |
| `runtime/engine/renderer/scene/ (split: scene_lifecycle.cpp, scene_render_frame.cpp, scene_sync.cpp, scene_assets.cpp, scene_debug.cpp)` | `CreateHiZPyramid()`, `DestroyHiZPyramid()`, wiring in `RenderFrame()` |
| `runtime/engine/renderer/core/pipeline_library.h/cpp` | `GetDepthPrepass(bindless)`, `GetHiZPyramid()` PSO accessors |
| `runtime/tests/engine/renderer/test_depth_prepass.cpp` | 30 unit tests covering pass setup, PSO selection, mip allocation |

## Rules

1. **ALWAYS run Hi-Z after Depth Prepass** — the mip chain must be built from pre-populated depth, not from an empty buffer.
2. **NEVER write color in the depth prepass** — no render targets are bound; writing to an unbound RT is undefined behavior.
3. **Use `min()` for reversed-Z Hi-Z** — taking `max()` would give the wrong occlusion result (near depth instead of far).
4. **Use `Load()` not `Sample()` in the Hi-Z shader** — bilinear sampling introduces incorrect interpolated depth values at mip boundaries.
5. **Clamp source coordinates** — odd-dimension textures produce a partial last row/column; clamping avoids out-of-bounds reads without branching.
6. **Allocate one texture per mip level** — a single mipped texture requires special UAV flags and complicates barrier scheduling; separate textures are simpler and equally fast.
7. **Call `DestroyHiZPyramid()` before `CreateHiZPyramid()` on resize** — leaking D32/R32 textures on resize causes unbounded VRAM growth.
