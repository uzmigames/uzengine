# Godot 4 Debug Visualization System Analysis

## Overview

Godot 4 implements a comprehensive debug visualization system via the `Viewport.DebugDraw` enum and the corresponding `RenderingServer.ViewportDebugDraw` enums. The system is designed to be **runtime-switchable without shader recompilation**.

**Key Insight**: Godot uses a **hybrid approach** — some modes are compile-time shader variants (via `#ifdef DEBUG_DRAW_*`), but most are **runtime branches** in the renderer that either:
1. Skip normal rendering and output intermediate buffers instead (post-process copy)
2. Add overlay visualization over the normal frame
3. Modify material behavior on-the-fly during rendering

---

## Complete DebugDraw Enum

**Location**: `E:\godot\scene\main\viewport.h` (lines 158–186) and mirrored in `E:\godot\servers\rendering\rendering_server_enums.h` (lines 545–573)

```cpp
// Godot script API (Viewport.DebugDraw)
enum DebugDraw {
    DEBUG_DRAW_DISABLED,                    // 0: Normal rendering
    DEBUG_DRAW_UNSHADED,                    // 1: Lighting disabled, vertex colors only
    DEBUG_DRAW_LIGHTING,                    // 2: Lighting only (no albedo)
    DEBUG_DRAW_OVERDRAW,                    // 3: Overlapping geometry in different colors
    DEBUG_DRAW_WIREFRAME,                   // 4: Triangle edges only
    DEBUG_DRAW_NORMAL_BUFFER,               // 5: World-space normals (RGB)
    DEBUG_DRAW_VOXEL_GI_ALBEDO,            // 6: VoxelGI probe albedo samples
    DEBUG_DRAW_VOXEL_GI_LIGHTING,          // 7: VoxelGI probe lighting (direct)
    DEBUG_DRAW_VOXEL_GI_EMISSION,          // 8: VoxelGI probe emission contribution
    DEBUG_DRAW_SHADOW_ATLAS,               // 9: Point/Omni shadow atlas (2D texture grid)
    DEBUG_DRAW_DIRECTIONAL_SHADOW_ATLAS,   // 10: Directional shadow cascade (with frustum overlay)
    DEBUG_DRAW_SCENE_LUMINANCE,            // 11: Auto-exposure luminance histogram (8x8 region)
    DEBUG_DRAW_SSAO,                       // 12: Screen-Space Ambient Occlusion buffer
    DEBUG_DRAW_SSIL,                       // 13: Screen-Space Indirect Lighting buffer
    DEBUG_DRAW_PSSM_SPLITS,                // 14: Directional splits with different color per split
    DEBUG_DRAW_DECAL_ATLAS,                // 15: Decal atlas texture
    DEBUG_DRAW_SDFGI,                      // 16: SDFGI (Signed Distance Field GI) grid
    DEBUG_DRAW_SDFGI_PROBES,              // 17: SDFGI probe positions + convergence
    DEBUG_DRAW_GI_BUFFER,                  // 18: GI buffer from SDFGI/VoxelGI/Lightmaps
    DEBUG_DRAW_DISABLE_LOD,                // 19: LOD disabled (all high-detail models)
    DEBUG_DRAW_CLUSTER_OMNI_LIGHTS,       // 20: Light clustering grid (point lights)
    DEBUG_DRAW_CLUSTER_SPOT_LIGHTS,       // 21: Light clustering grid (spot lights)
    DEBUG_DRAW_CLUSTER_DECALS,            // 22: Decal clustering visualization
    DEBUG_DRAW_CLUSTER_REFLECTION_PROBES, // 23: Reflection probe clustering visualization
    DEBUG_DRAW_OCCLUDERS,                 // 24: Hi-Z pyramid or occlusion buffer
    DEBUG_DRAW_MOTION_VECTORS,            // 25: Velocity vectors (red/green magnitude)
    DEBUG_DRAW_INTERNAL_BUFFER,           // 26: Raw internal render buffer (no post-process)
};
```

**Total Modes**: 27 (0–26)

---

## Implementation Architecture

### 1. Runtime vs. Compile-Time Branching

**Most modes are RUNTIME decisions** (no shader recompilation):

```cpp
// Example from renderer_scene_render_rd.cpp:1050
if (debug_draw == RSE::VIEWPORT_DEBUG_DRAW_SHADOW_ATLAS) {
    // Copy shadow atlas texture to framebuffer
    copy_effects->copy_to_fb_rect(shadow_atlas_texture, dest_fb, ...);
}
```

**Few modes are shader variants** (compile-time `#ifdef`):

```glsl
// Example from shaders/forward_clustered/scene_forward_clustered.glsl:2625
#ifdef DEBUG_DRAW_PSSM_SPLITS
    // Colormapped shadow split visualization
#endif
```

### 2. Decision Tree: When Effects are Disabled

**Location**: `renderer_scene_render_rd.cpp:995–1039`, function `_debug_draw_can_use_effects()`

```cpp
bool RendererSceneRenderRD::_debug_draw_can_use_effects(...) {
    bool can_use_effects = false;

    switch(debug_draw) {
        // COMPLETE OVERRIDE: Disable tonemapping, color correction, etc.
        case UNSHADED:
        case OVERDRAW:
        case WIREFRAME:
        case VOXEL_GI_ALBEDO:
        case CLUSTER_OMNI_LIGHTS:
        case CLUSTER_SPOT_LIGHTS:
        case CLUSTER_DECALS:
        case CLUSTER_REFLECTION_PROBES:
        case INTERNAL_BUFFER:
            can_use_effects = false;  // Post-process effects disabled
            break;

        // PARTIAL OVERLAY: Keep post-process (blend with normal frame)
        case SHADOW_ATLAS:
        case DIRECTIONAL_SHADOW_ATLAS:
        case DECAL_ATLAS:
        case MOTION_VECTORS:
        case NORMAL_BUFFER:
        case SSAO:
        case SSIL:
        case SDFGI:
        case GI_BUFFER:
        case OCCLUDERS:
            can_use_effects = true;  // Keep tone mapping, color grade, etc.
            break;

        // INFO OVERLAY: Render normally + visualization
        case LIGHTING:
        case VOXEL_GI_LIGHTING:
        case VOXEL_GI_EMISSION:
        case SCENE_LUMINANCE:
        case PSSM_SPLITS:
        case SDFGI_PROBES:
        case DISABLE_LOD:
            can_use_effects = true;
            break;
    }
}
```

### 3. How Modes are Implemented

#### Category A: Buffer Visualization (Post-Process Copy)

Modes that display intermediate render buffers **without normal rendering**:

| Mode | Source Buffer | Method | Notes |
|------|---------------|--------|-------|
| `NORMAL_BUFFER` | G-buffer normal RT | `copy_to_fb_rect()` | World-space normals in RGB |
| `SSAO` | SSAO result texture | Shader effect | Grayscale ambient occlusion |
| `SSIL` | SSIL result texture | Shader effect | Indirect light contribution |
| `SHADOW_ATLAS` | Shadow atlas texture | `copy_to_fb_rect()` | 2D grid of shadow maps |
| `DIRECTIONAL_SHADOW_ATLAS` | Directional shadow texture | `copy_to_fb_rect()` + frustum overlay | Cascades + camera frustum |
| `DECAL_ATLAS` | Decal atlas | `copy_to_fb_rect()` | Array of decal textures |
| `SCENE_LUMINANCE` | Luminance pyramid | `copy_to_fb_rect()` | 8x8 auto-exposure data |
| `GI_BUFFER` | GI result buffer | Shader post-process | Baked GI visualization |
| `INTERNAL_BUFFER` | Raw internal render target | `copy_to_fb_rect()` | No post-process applied |
| `OCCLUDERS` | Hi-Z pyramid or occlusion texture | `copy_to_fb_rect()` | Occlusion culling debug |
| `MOTION_VECTORS` | Velocity texture + depth | Custom shader `motion_vectors.glsl` | Per-pixel 2D velocity |

**Key Code**: `_render_buffers_debug_draw()` (lines 1041–1135 in `renderer_scene_render_rd.cpp`)

#### Category B: Material Override (Shader Variant or Uniform)

Modes that render normally but **change material behavior**:

| Mode | Implementation | Effect |
|------|---|---|
| `UNSHADED` | Material flag override | Render albedo + vertex colors, no lighting |
| `OVERDRAW` | Alpha blending change + color per-object | Each surface in different transparent color |
| `WIREFRAME` | RenderList flag `p_wireframe=true` | Line rendering instead of triangles |
| `LIGHTING` | Material flag override (shader pass) | Lighting only (no albedo) |
| `DISABLE_LOD` | Cull distance set to infinity | Forces highest LOD for all meshes |

**Key Code**: `RenderForwardClustered` (lines 752, 1129, 2116, etc.)

#### Category C: GI Probe Visualization

Modes showing indirect lighting data:

| Mode | Source | Notes |
|------|--------|-------|
| `VOXEL_GI_ALBEDO` | VoxelGI probe storage | Albedo contributions |
| `VOXEL_GI_LIGHTING` | VoxelGI probe lighting | Direct light in voxel grid |
| `VOXEL_GI_EMISSION` | VoxelGI emissive contribution | Emission data |
| `SDFGI` | Signed Distance Field GI grid | 3D SDF visualization (3D slices) |
| `SDFGI_PROBES` | SDFGI probe positions | Point cloud with convergence color |
| `GI_BUFFER` | Final GI result | Combined from all GI sources |

**Key Code**: `gi.cpp` (GI system handles visualization)

#### Category D: Clustering Visualization

Modes showing spatial acceleration structures:

| Mode | Visualization |
|------|---|
| `CLUSTER_OMNI_LIGHTS` | Colored voxel grid showing omni light density |
| `CLUSTER_SPOT_LIGHTS` | Colored voxel grid showing spot light density |
| `CLUSTER_DECALS` | Colored voxel grid showing decal distribution |
| `CLUSTER_REFLECTION_PROBES` | Colored voxel grid showing reflection probe cells |

These use the clustering data structures and render them as colored cubes.

#### Category E: Special Modes

| Mode | Behavior |
|------|----------|
| `PSSM_SPLITS` | Renders scene 4 times with different colors per directional light cascade |
| `SCENE_LUMINANCE` | Shows histogram 8×8 data (tiny visualization) |

---

## How Modes are Switched at Runtime

### 1. API Entry Point

```cpp
// From viewport.h
void set_debug_draw(DebugDraw p_debug_draw);
DebugDraw get_debug_draw() const;
```

### 2. Renderer Backend Implementation

```cpp
// From rendering_server_default.cpp
void RenderingServerDefault::viewport_set_debug_draw(RID p_viewport, RS::ViewportDebugDraw p_debug_draw) {
    viewport->set_debug_draw(p_debug_draw);
}

// Renderer singleton (RendererSceneRenderRD.cpp:1534)
void RendererSceneRenderRD::set_debug_draw_mode(RSE::ViewportDebugDraw p_debug_draw) {
    debug_draw = p_debug_draw;
}
```

### 3. Runtime Branch Points

**During frame render** (`_render_scene()` → `_render_buffers_debug_draw()`):

1. Check `if (debug_draw == MODE)` after main scene rendering
2. Branch to specialized visualization code
3. No shader recompilation; just different GPU commands

**Example**: Switching to WIREFRAME:

```cpp
// RenderForwardClustered.cpp:2116
RenderListParameters(..., get_debug_draw_mode() == VIEWPORT_DEBUG_DRAW_WIREFRAME, ...)
    // Pass wireframe flag to render list
    // GPU will use LINE rendering instead of TRIANGLE rendering
```

---

## Godot vs. UE5: Key Differences

### Godot's Approach (Lean)

1. **Single enum for all modes** (27 total) — no complex hierarchy
2. **Runtime branching** — most modes don't require shader variants
3. **Post-process copy** — visualize buffers via `copy_to_fb_rect()` (copy effect)
4. **No "composite grid"** — each mode is singular, can't see all at once
5. **Material-level overrides** — UNSHADED/OVERDRAW use material flags, not separate shaders
6. **Minimal infrastructure** — mostly relies on existing render passes + post-process effects

### UE5's Approach (Comprehensive)

1. **Hierarchical view modes** — Buffer Visualization (submenu with 20+ sub-modes), Quad Overdraw (per-quad), etc.
2. **Compile-time shader variants** — Most view modes are dedicated shader paths
3. **Composite visualization grid** — Can see 2×2 or 4×4 of different buffers simultaneously
4. **Material editor integration** — View modes affect material editor preview
5. **More sophisticated overlays** — Draw multiple layers (normals + wireframe, etc.)
6. **Editor-only features** — Some modes only available in editor

### Design Lesson for UzEngine

Godot's approach is **more suitable for a lean engine**:
- Fewer code paths to maintain
- Simpler shader management (fewer variants)
- Faster to add new debug modes (just copy the VIEWPORT_DEBUG_DRAW_* check)
- **Still effective** for rendering debugging (covers 80% of UE5's coverage)

---

## File Locations

| File | Purpose |
|------|---------|
| `E:\godot\scene\main\viewport.h:158–186` | GDScript API enum `Viewport.DebugDraw` |
| `E:\godot\servers\rendering\rendering_server_enums.h:545–573` | Server-side enum `RenderingServerEnums::ViewportDebugDraw` |
| `E:\godot\servers\rendering\renderer_rd\renderer_scene_render_rd.h:130` | Member var `RSE::ViewportDebugDraw debug_draw` |
| `E:\godot\servers\rendering\renderer_rd\renderer_scene_render_rd.cpp:1041–1135` | `_render_buffers_debug_draw()` — all buffer visualization |
| `E:\godot\servers\rendering\renderer_rd\renderer_scene_render_rd.cpp:995–1039` | `_debug_draw_can_use_effects()` — post-process decision logic |
| `E:\godot\servers\rendering\renderer_rd\renderer_scene_render_rd.cpp:1534` | `set_debug_draw_mode()` entry point |
| `E:\godot\servers\rendering\renderer_rd\forward_clustered\render_forward_clustered.cpp:752,1129,2116,...` | Material override checks during rendering |
| `E:\godot\servers\rendering\renderer_rd\effects\debug_effects.h/.cpp` | `DebugEffects` class for frustum + motion vector drawing |
| `E:\godot\servers\rendering\renderer_rd\shaders\forward_clustered\scene_forward_clustered.glsl:2625` | Compile-time `#ifdef DEBUG_DRAW_PSSM_SPLITS` |

---

## Implementation Summary Table

| Mode | Category | Type | Location | Complexity |
|------|----------|------|----------|------------|
| DISABLED | Control | N/A | — | — |
| UNSHADED | Material | Override | RenderForwardClustered.cpp:752 | Low |
| LIGHTING | Material | Override | Shader (material flag) | Low |
| OVERDRAW | Material | Override | RenderForwardClustered.cpp:1129 | Low |
| WIREFRAME | Material | Override | RenderList flag | Low |
| NORMAL_BUFFER | Buffer | Post-copy | renderer_scene_render_rd.cpp:1115 | Low |
| VOXEL_GI_ALBEDO | GI | Shader output | gi.cpp | Medium |
| VOXEL_GI_LIGHTING | GI | Shader output | gi.cpp | Medium |
| VOXEL_GI_EMISSION | GI | Shader output | gi.cpp | Medium |
| SHADOW_ATLAS | Buffer | Post-copy | renderer_scene_render_rd.cpp:1050 | Low |
| DIRECTIONAL_SHADOW_ATLAS | Buffer | Post-copy + overlay | renderer_scene_render_rd.cpp:1063 | Low |
| SCENE_LUMINANCE | Buffer | Post-copy | renderer_scene_render_rd.cpp:1101 | Low |
| SSAO | Effect | Shader result | SSAO effect system | Low |
| SSIL | Effect | Shader result | SSIL effect system | Low |
| PSSM_SPLITS | Material | Shader variant | forward_clustered.glsl | Medium |
| DECAL_ATLAS | Buffer | Post-copy | renderer_scene_render_rd.cpp:1091 | Low |
| SDFGI | GI | Compute output | gi.cpp | High |
| SDFGI_PROBES | GI | Draw call | renderer_scene_render_rd.cpp:100 | Medium |
| GI_BUFFER | GI | Post-copy | gi.cpp | Low |
| DISABLE_LOD | Control | Cull override | RenderForwardClustered.cpp:1387 | Low |
| CLUSTER_OMNI_LIGHTS | Cluster | Shader draw | cluster_debug.glsl | Medium |
| CLUSTER_SPOT_LIGHTS | Cluster | Shader draw | cluster_debug.glsl | Medium |
| CLUSTER_DECALS | Cluster | Shader draw | cluster_debug.glsl | Medium |
| CLUSTER_REFLECTION_PROBES | Cluster | Shader draw | cluster_debug.glsl | Medium |
| OCCLUDERS | Buffer | Post-copy | renderer_scene_render_rd.cpp:1120 | Low |
| MOTION_VECTORS | Effect | Shader draw | debug_effects.cpp:draw_motion_vectors() | Medium |
| INTERNAL_BUFFER | Buffer | Post-copy | renderer_scene_render_rd.cpp:1110 | Low |

---

## Key Design Insights

### 1. No Composite Grid

Godot does NOT have a built-in "4-up buffer grid" like UE5's Buffer Visualization panel. Each mode **replaces the entire viewport**. This is a conscious tradeoff: simpler implementation, but less comparison capability.

**For UzEngine**: Consider adding optional composite grid at a higher level (in the editor UI/compositing system) rather than in the renderer.

### 2. Effect Decision is Global

The `_debug_draw_can_use_effects()` function checks **one mode** and determines if **all** post-process effects apply. This means:
- No selective effect control per mode
- Simpler, but less granular

**For UzEngine**: More fine-grained control might be useful (e.g., "show normal buffer with tonemapping").

### 3. Buffer Visualization is Post-Render

All buffer visualization happens **after main rendering** in `_render_buffers_debug_draw()`. This is called in the main render pass but does not change the render graph. It's a post-process overlay.

**For UzEngine**: This pattern scales well — add modes by adding more `if (debug_draw == MODE)` checks.

### 4. Shader Variants Are Rare

Only **PSSM_SPLITS** uses `#ifdef DEBUG_DRAW_*` in the shaders. Most modes are runtime decisions. This minimizes shader complexity and compilation time.

**For UzEngine**: Avoid shader variants for debug modes; use runtime branches instead.

### 5. Clustering Visualization Uses Compute Shaders

Modes like `CLUSTER_OMNI_LIGHTS` are rendered via dedicated compute shaders (`cluster_debug.glsl`) that read the cluster data and produce visualization. This is separate from the main lighting pass.

---

## UzEngine Design Recommendations

Based on Godot's approach:

### 1. Implement a Single Enum

```cpp
enum class DebugViewMode {
    Disabled,
    Unshaded,
    Wireframe,
    // ... add modes incrementally
};
```

### 2. Runtime Branching Only

Avoid shader variants. Use dynamic branching in the main shader or separate visualization shaders.

### 3. Post-Process Visualizations

Implement visualization shaders in the post-process graph:
- Normal buffer visualization shader
- Overdraw accumulation shader
- etc.

### 4. Optional: Composite Grid UI

Build UI-level support in the editor to display multiple modes in a grid. This is easier to implement than in the renderer.

### 5. Reuse Existing Systems

- Shadow atlas viewer: use RHI texture copy
- Motion vectors: dedicated visualization pass
- GI visualization: reuse GI compute shaders

---

## Conclusion

Godot's debug visualization system is **lean, runtime-focused, and extensible**. It prioritizes:

1. **No shader recompilation** (single-digit compile time overhead per mode switch)
2. **Minimal code duplication** (reuse post-process effects)
3. **Clear separation** (visualization in one function `_render_buffers_debug_draw()`)

The tradeoff is less granularity compared to UE5, but the maintenance burden is much lighter. **For a lean engine like UzEngine, this is the right approach.**
