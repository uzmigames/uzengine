# Godot 4 Debug Modes — Quick Reference

## Complete List (27 Modes)

```
0  DISABLED                  Normal rendering (no debug)
1  UNSHADED                  Albedo + vertex colors only (no lighting)
2  LIGHTING                  Lighting only (no albedo)
3  OVERDRAW                  Colored transparency per-object (overdraw detection)
4  WIREFRAME                 Triangle edges only
5  NORMAL_BUFFER             World-space normals as RGB
6  VOXEL_GI_ALBEDO          VoxelGI probe albedo
7  VOXEL_GI_LIGHTING        VoxelGI probe direct lighting
8  VOXEL_GI_EMISSION        VoxelGI probe emission
9  SHADOW_ATLAS             Point/Omni shadow atlas texture
10 DIRECTIONAL_SHADOW_ATLAS Directional cascades + frustum overlay
11 SCENE_LUMINANCE          Auto-exposure luminance histogram (8×8)
12 SSAO                     Screen-Space Ambient Occlusion
13 SSIL                     Screen-Space Indirect Light
14 PSSM_SPLITS              Directional splits colored per-cascade
15 DECAL_ATLAS              Decal texture atlas
16 SDFGI                    SDFGI grid 3D slices
17 SDFGI_PROBES            SDFGI probe point cloud
18 GI_BUFFER                Combined GI result
19 DISABLE_LOD              Force highest LOD for all meshes
20 CLUSTER_OMNI_LIGHTS     Light cluster voxel grid (omni)
21 CLUSTER_SPOT_LIGHTS     Light cluster voxel grid (spot)
22 CLUSTER_DECALS          Decal cluster voxel grid
23 CLUSTER_REFLECTION_PROBES Reflection probe cluster voxel grid
24 OCCLUDERS                Hi-Z pyramid or occlusion buffer
25 MOTION_VECTORS           Velocity vectors (red/green magnitude)
26 INTERNAL_BUFFER          Raw internal render target
```

## Quick Lookup by Use Case

### Lighting & GI Debugging
- `LIGHTING` — Is there any light hitting this?
- `VOXEL_GI_*` — Is VoxelGI being applied?
- `SDFGI` / `SDFGI_PROBES` — SDFGI convergence?
- `GI_BUFFER` — Final GI color

### Geometry Validation
- `UNSHADED` — Can I see the mesh?
- `WIREFRAME` — Topology correct?
- `NORMAL_BUFFER` — Normals pointing right?
- `DISABLE_LOD` — LOD transitions broken?

### Shadow Debugging
- `SHADOW_ATLAS` — Shadow atlas full?
- `DIRECTIONAL_SHADOW_ATLAS` — Cascade coverage correct?
- `PSSM_SPLITS` — Which cascade is rendering?

### Performance Analysis
- `OVERDRAW` — Too much transparency blending?
- `CLUSTER_OMNI_LIGHTS` / `CLUSTER_SPOT_LIGHTS` — Cluster density?
- `MOTION_VECTORS` — Too much motion (TAA artifacts)?

### Screen-Space Effects
- `SSAO` — Ambient occlusion correct?
- `SSIL` — Indirect light bouncing?
- `SCENE_LUMINANCE` — Exposure data

### Advanced GI & Baking
- `DECAL_ATLAS` — Decal placement?
- `OCCLUDERS` — Occlusion pyramid correct?
- `INTERNAL_BUFFER` — Raw framebuffer before post-process

---

## Implementation Patterns

### 1. Material Override (Shader Behavior Change)
Modes that render normally but change material output:
- UNSHADED, OVERDRAW, WIREFRAME, LIGHTING, DISABLE_LOD

**How**: Pass flag to RenderList, which feeds shader uniform or variant selector.

### 2. Post-Process Copy (Buffer Visualization)
Modes that display intermediate buffers:
- NORMAL_BUFFER, SSAO, SSIL, SHADOW_ATLAS, etc.

**How**: After main render pass, call `copy_effects->copy_to_fb_rect(buffer, framebuffer)`

### 3. Compute/Draw Overlay (Specialized Visualization)
Modes that render additional geometry:
- CLUSTER_*, SDFGI_PROBES, MOTION_VECTORS

**How**: Call specialized visualization shader (cluster_debug.glsl, debug_effects.cpp)

### 4. GI-Specific (Through GI System)
- VOXEL_GI_*, SDFGI*, GI_BUFFER

**How**: GI system outputs visualization in dedicated render passes

---

## Key Code Paths

| Concept | File | Lines |
|---------|------|-------|
| Enum definition | viewport.h | 158–186 |
| Server enums | rendering_server_enums.h | 545–573 |
| Decision logic | renderer_scene_render_rd.cpp | 995–1039 |
| Buffer visualization | renderer_scene_render_rd.cpp | 1041–1135 |
| Mode setter | renderer_scene_render_rd.cpp | 1534 |
| Material override checks | render_forward_clustered.cpp | 752, 1129, 1387, 2116, etc. |
| Debug effects | debug_effects.h/.cpp | frustum drawing, motion vectors |
| Shader variant | scene_forward_clustered.glsl | 2625 (#ifdef DEBUG_DRAW_PSSM_SPLITS) |

---

## Critical Design Decisions

### No Composite Grid
Godot doesn't show multiple buffers simultaneously (like UE5's "4-up" view).
- **Why**: Simpler implementation, less viewport fragmentation
- **Tradeoff**: Can't compare modes side-by-side without manual swapping

### Minimal Shader Variants
Only PSSM_SPLITS uses `#ifdef`. Everything else is runtime.
- **Why**: Faster shader compilation, simpler code
- **Tradeoff**: Slightly more GPU overhead per-frame

### Global Effect Toggle
All post-process effects are on/off as a group per mode (no selective control).
- **Why**: Simpler UI, fewer decision branches
- **Tradeoff**: Can't have "normal buffer with tonemapping"

### Post-Process Visualization
All buffer visualization happens **after rendering** in a separate pass.
- **Why**: No render graph modification, easy to add new modes
- **Tradeoff**: Visualization always overlays final frame

---

## For UzEngine Implementation

### Start With These (Easiest)
1. DISABLED (control)
2. UNSHADED (shader flag)
3. WIREFRAME (render list flag)
4. NORMAL_BUFFER (post-copy)

### Medium Complexity
5. LIGHTING (shader path)
6. OVERDRAW (alpha override)
7. SHADOW_ATLAS (texture copy)

### Advanced (Requires System Support)
- VOXEL_GI_* (needs GI system)
- SDFGI_* (needs SDFGI system)
- CLUSTER_* (needs light clustering)

**Recommend**: Implement core 7–10 modes, extend as systems are built.

---

## Performance Characteristics

| Mode | Overhead | Notes |
|------|----------|-------|
| DISABLED | 0% | Baseline |
| UNSHADED | <1% | Material flag branch |
| WIREFRAME | ~5–10% | Rasterizer switch (fewer pixels) |
| NORMAL_BUFFER | ~2–3% | Small post-process copy |
| SHADOW_ATLAS | ~2–3% | Texture copy + scaling |
| MOTION_VECTORS | ~5–10% | Separate visualization pass |
| CLUSTER_* | ~10–15% | Compute shader dispatch |
| SDFGI_PROBES | ~10–20% | Probe point cloud rendering |

**TL;DR**: Most modes add <5% overhead. Full implementation cost << one high-quality shader.
