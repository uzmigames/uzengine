# Godot Debug Modes — Taxonomy & Implementation Architecture

## Hierarchical View

```
ViewportDebugDraw (27 modes total)
│
├─ CONTROL MODES (2)
│  ├─ DISABLED                    (0) → Normal path
│  └─ DISABLE_LOD                 (19) → Culling override
│
├─ MATERIAL OVERRIDE (5)
│  ├─ UNSHADED                    (1) → Skip lighting
│  ├─ LIGHTING                    (2) → Lighting only
│  ├─ OVERDRAW                    (3) → Alpha accumulation
│  ├─ WIREFRAME                   (4) → Edge rendering
│  └─ [PSSM_SPLITS logic in shader]
│
├─ BUFFER VISUALIZATION (12)
│  │ [All use copy_to_fb_rect() or visualization shader]
│  ├─ NORMAL_BUFFER               (5) → G-buffer normals
│  ├─ SHADOW_ATLAS                (9) → Point light atlas
│  ├─ DIRECTIONAL_SHADOW_ATLAS    (10) → Cascade grid + frustum
│  ├─ SCENE_LUMINANCE             (11) → Exposure histogram
│  ├─ SSAO                        (12) → AO texture
│  ├─ SSIL                        (13) → Indirect light
│  ├─ DECAL_ATLAS                 (15) → Decal grid
│  ├─ GI_BUFFER                   (18) → Final GI
│  ├─ OCCLUDERS                   (24) → Hi-Z / Occlusion
│  ├─ INTERNAL_BUFFER             (26) → Raw framebuffer
│  └─ MOTION_VECTORS              (25) → Velocity pass
│
├─ VOXEL GI (3)
│  │ [Through gi.cpp system]
│  ├─ VOXEL_GI_ALBEDO             (6)
│  ├─ VOXEL_GI_LIGHTING           (7)
│  └─ VOXEL_GI_EMISSION           (8)
│
├─ SIGNED DISTANCE FIELD GI (2)
│  │ [Through gi.cpp + SDFGI system]
│  ├─ SDFGI                       (16) → 3D grid visualization
│  └─ SDFGI_PROBES                (17) → Probe point cloud
│
└─ CLUSTERING VISUALIZATION (3)
   │ [Through cluster_debug.glsl compute shader]
   ├─ CLUSTER_OMNI_LIGHTS         (20) → Omni light voxel grid
   ├─ CLUSTER_SPOT_LIGHTS         (21) → Spot light voxel grid
   ├─ CLUSTER_DECALS              (22) → Decal voxel grid
   └─ CLUSTER_REFLECTION_PROBES   (23) → Reflection probe voxel grid
```

---

## Implementation Strategy by Category

### 1. CONTROL MODES

**What**: Flags that modify the render path without changing visuals.

```cpp
// Pseudocode
if (debug_draw == DISABLE_LOD) {
    cull_distance = infinity;  // Force detail
}
```

**Cost**: Negligible
**Shader Changes**: None

---

### 2. MATERIAL OVERRIDE

**What**: Change what the material shader outputs.

```cpp
// In RenderList or material uniforms
if (debug_draw == UNSHADED) {
    material_uniform.unshaded_mode = 1;  // Shader skips lighting
}

if (debug_draw == WIREFRAME) {
    render_primitive_type = LINE;        // GPU rasterizer switch
}

if (debug_draw == OVERDRAW) {
    blend_state = ADDITIVE;              // Accumulate alpha
}
```

**Key File**: `render_forward_clustered.cpp` (checks at render list construction)

**Cost**: <1–5% overhead (rasterizer state switches are fast)

**Shader Changes**:
- Material shader has `#ifdef UNSHADED_DEBUG`, `#ifdef LIGHTING_DEBUG`, etc.
- OR: Dynamic branching on `material_debug_mode` uniform (faster iteration)

---

### 3. BUFFER VISUALIZATION

**What**: Display intermediate render targets.

```cpp
// Pseudocode from _render_buffers_debug_draw()
if (debug_draw == NORMAL_BUFFER) {
    copy_effects->copy_to_fb_rect(
        source_texture: g_buffer_normal,
        dest_framebuffer: viewport_target,
        rect: full_screen,
        linear_color: true
    );
}

if (debug_draw == DIRECTIONAL_SHADOW_ATLAS) {
    // 1. Copy shadow texture
    copy_effects->copy_to_fb_rect(shadow_texture, ...);

    // 2. Overlay frustum geometry
    debug_effects->draw_shadow_frustum(light, cam_proj, cam_transform, ...);
}
```

**Cost**: 2–5% (copy is fast, bounded by framebuffer bandwidth)

**Shader Changes**: None (uses existing textures)

**Key Files**:
- `renderer_scene_render_rd.cpp:1041–1135` — All buffer copy logic
- `effects/copy.glsl` — Copy shader (handles tone mapping, normal decode, etc.)

---

### 4. VOXEL GI & SDFGI

**What**: Render GI probe/grid data as geometry or colormap.

```cpp
// Through GI system (gi.cpp)
if (debug_draw == VOXEL_GI_ALBEDO) {
    // Render voxel grid colored by albedo
    render_compute_shader("voxel_gi_debug.glsl");
}

if (debug_draw == SDFGI_PROBES) {
    // Draw probe positions as point cloud
    _debug_sdfgi_probes(render_buffers, framebuffer, camera);
}
```

**Cost**: 5–20% (depends on probe density)

**Shader Changes**: Dedicated visualization shaders

**Key Files**:
- `environment/gi.cpp` — GI system
- `shaders/environment/voxel_gi_debug.glsl`
- `shaders/environment/sdfgi_debug.glsl`
- `shaders/environment/sdfgi_debug_probes.glsl`

---

### 5. CLUSTERING VISUALIZATION

**What**: Render spatial acceleration structure (light clusters, decal clusters).

```cpp
// Through cluster_debug.glsl (compute shader)
if (debug_draw == CLUSTER_OMNI_LIGHTS) {
    // Dispatch compute shader:
    // Read: cluster buffer
    // Output: colored voxel grid per light count
    rd->compute_list_dispatch(...);
}
```

**Cost**: 10–15% (compute shader + rasterization of voxels)

**Shader Changes**: Dedicated compute shaders

**Key Files**:
- `shaders/cluster_debug.glsl` — Visualization compute
- `shaders/cluster_render.glsl` — Rasterization

---

## Decision Tree: What Path Each Mode Takes

```
set_debug_draw(mode)
│
├─ If DISABLED
│  └─ Normal rendering, no checks
│
├─ If CONTROL (DISABLE_LOD)
│  └─ Modify render list parameters, continue normal
│
├─ If MATERIAL_OVERRIDE (UNSHADED, OVERDRAW, WIREFRAME, LIGHTING)
│  └─ Pass flag to material shader
│  └─ Material outputs different colors/values
│
├─ If BUFFER_VISUALIZATION or MOTION_VECTORS
│  └─ Render normally
│  └─ In _render_buffers_debug_draw():
│      ├─ Check: do we have buffer? (e.g., normal RT, shadow atlas)
│      └─ If yes: copy_to_fb_rect() or visualization pass
│
├─ If GI (VOXEL_GI_*, SDFGI*)
│  └─ Route through GI system
│  └─ GI system renders specialized visualization
│
└─ If CLUSTERING (CLUSTER_*)
   └─ Dispatch visualization compute shader
   └─ Renders colored voxel grid
```

---

## Choosing Implementation Strategy for UzEngine

| Category | Effort | Dependencies | Value | Recommend |
|----------|--------|--------------|-------|-----------|
| CONTROL | Trivial | None | High | **First** |
| MATERIAL_OVERRIDE | Low | Shader, RenderList | High | **First** |
| BUFFER_VISUALIZATION | Low | Copy effect | High | **Second** |
| VOXEL GI | Medium | GI system | Medium | **Later** (after GI) |
| SDFGI | High | SDFGI system | Low | **Much Later** |
| CLUSTERING | Medium | Light cluster system | Medium | **Third** (after clustering) |

### Recommended Phased Approach

**Phase 1** (Week 1):
1. DISABLED (trivial)
2. UNSHADED (one shader flag)
3. WIREFRAME (render list flag)
4. NORMAL_BUFFER (one copy_to_fb_rect call)

**Phase 2** (Week 2–3):
5. LIGHTING (shader branch)
6. OVERDRAW (blend mode override)
7. SHADOW_ATLAS (copy with fallback)
8. DISABLE_LOD (cull distance)

**Phase 3** (After core systems):
9–27. As systems (GI, SDFGI, clustering) are built

---

## Post-Process Effects Decision

**Key Question**: When do post-process effects (tonemapping, color grading, bloom) apply?

**Answer**: Controlled by `_debug_draw_can_use_effects()` (lines 995–1039 in renderer_scene_render_rd.cpp)

```cpp
bool should_use_effects(ViewportDebugDraw mode) {
    switch (mode) {
        // FULL OVERRIDE: No effects (debugging needs accurate colors)
        case UNSHADED:
        case OVERDRAW:
        case WIREFRAME:
        case INTERNAL_BUFFER:
            return false;  // ← Post-process disabled

        // OVERLAY MODE: Keep effects (blend with normal)
        case SHADOW_ATLAS:
        case NORMAL_BUFFER:
        case SSAO:
            return true;   // ← Post-process enabled

        // VISUAL MODE: Keep effects (debug info overlaid)
        case LIGHTING:
        case PSSM_SPLITS:
            return true;   // ← Post-process enabled
    }
}
```

**For UzEngine**: Create similar table. Group modes by their effect requirements.

---

## Shader Variant Minimization Strategy

**Godot's approach**: Only PSSM_SPLITS uses `#ifdef`. Everything else is runtime.

**Why**: Fewer permutations = faster compilation, less memory.

**Recommendation for UzEngine**:
- Use uniforms/flags for material overrides (UNSHADED, OVERDRAW, LIGHTING)
- Use separate visualization shaders for specialized modes (cluster_debug.glsl, etc.)
- Avoid `#define` permutations for debug modes

---

## Testing Approach

Each mode should have:
1. **A demo scene** (cube with simple material)
2. **A screenshot** showing expected output
3. **A description** of what is wrong if you see something else

Example:
```
Mode: NORMAL_BUFFER
Expected: Each pixel shows world-space normal as RGB color
  Red = +X direction
  Green = +Y direction
  Blue = +Z direction
Problem: If you see all blue, normals are all pointing +Z (wrong mesh orientation)
```

---

## Summary Table: Implementation Overhead

| Mode | GPU Time | Shader Variants | Code Lines | Risk |
|------|----------|---|---|---|
| UNSHADED | <1% | 1 flag | ~50 | Low |
| WIREFRAME | 5–10% | Rasterizer | ~20 | Low |
| NORMAL_BUFFER | 2–3% | 0 | ~10 | Very Low |
| LIGHTING | 0–2% | 1 variant | ~30 | Low |
| OVERDRAW | <1% | Blend mode | ~10 | Low |
| SHADOW_ATLAS | 2–3% | 0 | ~15 | Very Low |
| MOTION_VECTORS | 5–10% | 1 shader | ~50 | Low |
| CLUSTER_OMNI | 10–15% | 1 compute | ~100 | Medium |
| SDFGI | Variable | 3+ shaders | 500+ | High |
| **Total (Phase 1)** | **~10–15%** | **3 total** | **~200** | **Low** |

---

## Final Design Pattern

```cpp
// Godot's canonical pattern
void RendererSceneRenderRD::_render_buffers_debug_draw(const RenderDataRD *p_render_data) {
    // For each visualization mode:
    if (debug_draw == MODE) {
        // 1. Check prerequisites (buffer exists?)
        if (buffer_resource.is_valid()) {
            // 2. Perform visualization (copy, overlay, or specialized render)
            copy_effects->copy_to_fb_rect(buffer, target, ...);
        }
    }
}
```

**For UzEngine**: Follow exactly this pattern. Maintain one `if (debug_draw == MODE)` per visualization.

Add new modes by:
1. Add enum value
2. Add `if (debug_draw == NEW_MODE)` block
3. Done

No refactoring needed.
