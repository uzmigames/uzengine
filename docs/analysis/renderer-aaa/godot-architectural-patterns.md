<!-- doc-meta
title: Godot 4 Architectural Patterns for UzEngine
summary: Design patterns and code organization strategies from Godot's rendering architecture
date: 2026-04-04
-->

# Godot 4 Architectural Patterns for UzEngine

## Overview

This document extracts Godot's design patterns and architectural decisions that are directly applicable to UzEngine's rendering engine. Focus is on **proven solutions** to common rendering infrastructure problems.

---

## Pattern 1: RenderingDevice Abstract Layer (Multi-Backend Support)

### Godot's Approach

**Location**: `servers/rendering/rendering_device.h` (abstract interface)

Godot defines a **pure-virtual C++ interface** that all backends implement:

```cpp
class RenderingDevice {
public:
    // GPU Resources
    virtual RID texture_create(...) = 0;
    virtual RID buffer_create(...) = 0;
    virtual RID sampler_create(...) = 0;
    virtual RID pipeline_create(...) = 0;
    
    // Command Recording
    virtual RID draw_list_begin(...) = 0;
    virtual void draw_list_bind_uniform_set(...) = 0;
    virtual void draw_list_draw(...) = 0;
    virtual void draw_list_end(...) = 0;
    
    // Compute
    virtual RID compute_list_begin() = 0;
    virtual void compute_list_dispatch(...) = 0;
    virtual void compute_list_end() = 0;
};
```

**Implementations**:
- `RenderingDeviceVulkan` (primary, most complete)
- `RenderingDeviceDXXX` (DX12 adapter)
- `RenderingDeviceMetal` (macOS/iOS)
- `RenderingDeviceOpenGL` (fallback)

### Key Design Decisions

1. **RID (Resource ID) pattern** — Opaque 64-bit handles instead of raw pointers
   - Enables deferred deletion, reference tracking
   - Prevents use-after-free (returns INVALID_RID on freed resource)
   - No lifetime coupling between game code and GPU resources

2. **Synchronous Command API** — No VkQueue, VkCommandBuffer directly exposed
   - `draw_list_begin()` returns a handle to a fresh command buffer
   - `draw_list_end()` auto-submits
   - Feels like immediate-mode rendering, but records deferred

3. **Unified Descriptor Binding** — Hides descriptor set differences
   - No VkDescriptorSet allocation visible
   - `bind_uniform_set()` just takes a RID
   - Backend handles layout transitions, caching

4. **Specialization Constants** — Shader variants without PSO proliferation
   - `ShaderVariant` enum (e.g., `FORWARD, FORWARD_MOTION_VECTORS, DEFERRED`)
   - Compiled once, specialized at pipeline creation time
   - Replaces shader preprocessing with runtime constants

### UzEngine Application

**Current UzEngine State**:
- Already uses RHI abstraction (excellent!)
- RenderingDevice is well-designed

**Recommended Enhancements**:
1. Add **Specialization Constants** to RHI for PSO variants
2. Expose **pipeline cache** (VkPipelineCache) for faster shader compilation
3. Add **timestamp query pools** for GPU profiling
4. Implement **deferred deletion queue** (delete GPU resources 2 frames later, not immediately)

---

## Pattern 2: Scene Proxy (Deferred Game-Thread Access)

### Godot's Approach

**Location**: `renderer_geometry_instance.h`, `renderer_scene_render.h`

The renderer doesn't directly read from scene nodes. Instead:

```cpp
// Game thread: collect data from scene
struct GeometryInstance {
    Transform3D transform;
    AABB aabb;
    RID mesh;
    RID material;
    uint32_t layer_mask;
    // ... hundreds more fields
};

// Render thread: uses snapshot, not scene
struct GeometryInstanceRD {
    Transform3D transform;  // read-only copy
    RID mesh_storage_id;    // opaque ref to storage
    Vector<RID> materials;  // snapshot of material list
};

// Game thread collects, render thread consumes separately
void CullAndRender(const GeometryInstance* game_instances[], int count) {
    // Transform to render-thread format
    for (int i = 0; i < count; i++) {
        GeometryInstanceRD rd_instance = Convert(game_instances[i]);
        // ... render with rd_instance, never touches game_instance again
    }
}
```

### Key Benefits

1. **Zero synchronization overhead** — Render thread only reads game data snapshots
2. **Cache coherency** — Render-thread data laid out optimally for GPU, not game logic
3. **Safe multithreading** — No racing on scene mutations mid-render
4. **Deterministic rendering** — Exact same results every frame (no tearing from live mutations)

### UzEngine Application

**Current State**: Already does proxy-based rendering ✓

**Verification**: 
- `SceneEnvironment` gatherer ✓
- `RenderProxy` pattern in scene system ✓

---

## Pattern 3: Clustering as Spatial Data Structure

### Godot's Approach

**Location**: `cluster_builder_rd.h`, `cluster_store.glsl`, `cluster_render.glsl`

Godot divides view-space into a 3D grid and assigns lights to clusters:

```glsl
// Cluster layout: 16×16×24 = 6144 clusters
// For each cluster: light index list (packed 32-bit per light)

// In shader:
uint cluster_index = Get3DCluster(screen_pos, linear_depth);
uint light_list = cluster_buffers[cluster_index];

// Unpack and iterate assigned lights
for (int i = 0; i < LIGHTS_PER_CLUSTER; i++) {
    uint light_id = UnpackLight(light_list, i);
    if (light_id == INVALID_LIGHT) break;
    
    // Apply only this light's contribution
    lighting += EvaluateLight(light_id);
}
```

### Key Features

1. **Frustum-aligned clusters** — Z slices follow camera frustum
2. **Atomic operations for build** — GPU assigns lights to clusters in parallel
3. **Fallback path** — For GPUs without atomics (older), use double-buffering
4. **Per-light cost is O(pixels_in_cluster)** — Not O(pixels × lights)

### Performance Impact

| Setup | Lights | Forward | Deferred (Godot) |
|-------|--------|---------|------------------|
| 8 lights | 8 | Very fast | Very fast |
| 32 lights | 32 | Very slow (×4) | Fast (1.2×) |
| 128 lights | 128 | Impossible | Good (1.1×) |

### UzEngine Application

**Current State**: Already has clustering ✓

**Recommendations**:
1. Verify cluster grid size is tunable (16×16×24 vs 32×32×32)
2. Add **cluster debug visualization** (show per-cluster light count)
3. Profile cluster assignment (GPU vs CPU fallback)
4. Consider **spotlight cone culling** (refine clusters by light angle)

---

## Pattern 4: Material Storage (Two-Layer System)

### Godot's Approach

**Location**: `storage_rd/material_storage.h`

Godot separates **material definition** from **per-instance data**:

```cpp
// Layer 1: Material Shader Data (shared)
struct ShaderData {
    String code;
    RID shader_version;
    Vector<Uniform> uniforms;
    bool uses_time;
    bool uses_position;
    // ... compiled-once properties
};

// Layer 2: Material Instance (per-material)
struct Material {
    RID shader;  // Points to ShaderData
    Vector<ShaderParameterValue> parameters;  // Instance overrides
    uint32_t variant;  // Which PSO variant to use
};

// Layer 3: Rendered Geometry Instance
struct GeometryInstance {
    RID material;  // Points to Material (which points to ShaderData)
    Transform3D transform;
    // Only light, shared data
};
```

### Benefits

1. **Memory efficient** — Shared shader code, per-instance parameters only
2. **PSO caching** — Compute once per unique (shader, variant) pair
3. **Hot-reloading** — Rebuild shader → all materials using it auto-update
4. **Variant management** — Different PSO for forward vs deferred use

### UzEngine Application

**Current State**: Has this pattern (good!)

**Enhancement Idea**:
- Add **material variant matrix** tracking which variants are actually used
- Only compile "hot" variants (skip unused combinations)
- Reduces shader compilation overhead

---

## Pattern 5: Deferred Post-Processing Pipeline

### Godot's Approach

**Location**: `effects/` directory (tone_mapper, bloom, dof, etc.)

Effects are **pluggable and composable**:

```cpp
void RenderFinal(RID frame_texture) {
    // Apply effects in sequence
    
    // Step 1: Bloom (if enabled)
    if (environment->bloom_enabled) {
        bloom->Apply(frame_texture);  // modifies in-place
    }
    
    // Step 2: Tonemap + Color Grading + Glow
    tonemap->Process(frame_texture, lut_texture, glow_strength);
    
    // Step 3: AA (if temporal)
    if (temporal_aa_enabled) {
        taa->Resolve(frame_texture, history_texture);
    }
    
    // Step 4: Present to swapchain
    present(frame_texture);
}
```

### Key Pattern

1. **Each effect is self-contained** — Takes input texture, modifies output
2. **Linear pipeline** — No branching (all effects run if needed)
3. **Conditional compilation** — Unused effects compiled out
4. **Toggle-able at runtime** — `r.Bloom=false` disables bloom shader variant

### UzEngine Application

**Current State**: Has this pattern ✓ (excellent!)

**Optimization**:
- Consider **effect merging** (tonemap + glow in one shader) for expensive paths
- Add **effect chaining** (bloom → tonemap → AA) in single pass if pixel-local storage available

---

## Pattern 6: Screen-Space Effect Multi-Pass (Hi-Z, Downsample, Filter)

### Godot's Approach (SSR Example)

**Location**: `effects/ss_effects.cpp`, screen_space_reflection.glsl

Complex screen-space effects use **multiple passes**:

```cpp
void ProcessSSR(RID input_gbuffer, RID output_ssr) {
    // Pass 1: Downsample depth to Hi-Z pyramid
    RID hiz = BuildHiZPyramid(gbuffer.depth);
    
    // Pass 2: Raytrace with Hi-Z lookup
    RID raytraced = Raytrace(gbuffer.normal, gbuffer.metallic, hiz);
    
    // Pass 3: Spatial filter (bilateral)
    RID filtered = BilateralFilter(raytraced, gbuffer.depth);
    
    // Pass 4: Temporal filter (blend with history)
    RID temporal = TemporalFilter(filtered, history_ssr, gbuffer.velocity);
    
    return temporal;
}
```

### Benefits

1. **Modularity** — Each pass can be toggled independently
2. **Performance tuning** — Downsample factor, trace quality, filter strength all adjustable
3. **Debug visualization** — Render intermediate passes to visualize artifacts
4. **Reusable building blocks** — Hi-Z pyramid used by both SSR and occlusion culling

### UzEngine Application

**Recommended**:
- Document SSR pass breakdown (matching Godot's approach)
- Add **SSR debug visualizations** (show Hi-Z mips, raytrace steps)
- Consider **compute shader version** of raytrace (more efficient than raster)

---

## Pattern 7: Shader Compilation Cache (Pipeline Caching)

### Godot's Approach

**Location**: `pipeline_cache_rd.h`

Godot caches compiled shaders to disk:

```cpp
struct PipelineCacheRD {
    uint64_t hash;  // (shader_source + variant + specialization) hash
    RID pipeline;   // Cached Vulkan pipeline object
    
    void CreateOrFetch(ShaderInfo shader, int variant) {
        uint64_t key = Hash(shader.source, variant);
        
        if (cache.Has(key)) {
            return cache[key];  // Instant (RID lookup)
        }
        
        // Compile
        pipeline = CompileShader(shader, variant);
        cache[key] = pipeline;
        WriteCacheFile(key, pipeline_blob);
        return pipeline;
    }
};
```

### Godot's Optimization

1. **Async compilation** — Shader compiles in background while placeholder runs
2. **Cache invalidation** — Hash includes engine version (invalidates on engine update)
3. **Disk caching** — SPIR-V bytecode saved, no recompilation on restart

### UzEngine Application

**Recommendation**:
- Implement **pipeline cache to disk** (matches DX12 PSO cache pattern)
- Use shader source hash + variant as cache key
- Profile: typical cache hit is 10× faster than compilation

---

## Pattern 8: Bindless Resources (In Progress for Godot)

### What Godot Lacks (Opportunity for UzEngine)

Godot uses **traditional bindless-style** (descriptor sets per material), but not true bindless:

```glsl
// Godot currently does this:
layout(set = 0, binding = 0) uniform sampler2D albedo_map;
layout(set = 0, binding = 1) uniform sampler2D normal_map;
// ... requires descriptor set update per material

// True bindless (what UzEngine should explore):
layout(set = 0, binding = 0) uniform ResourceDescriptorHeap {
    sampler samplers[1024];
    texture2D textures[4096];
};

// Shader uses dynamic indexing
vec4 albedo = texture(textures[material.albedo_idx], uv);
```

### UzEngine Opportunity

True bindless rendering would:
1. Eliminate descriptor set updates (major speedup in high-LOD scenarios)
2. Enable **mega-materials** (single shader for 100+ different materials)
3. Reduce CPU overhead (one pipeline + descriptor set, not dozens)

---

## Pattern 9: Shader Preprocessor & Code Generation

### Godot's Approach

**Location**: `shader_language.h`, `shader_preprocessor.h`

Godot preprocesses shaders before DXC compilation:

```glsl
// Input: GDScript shader language
shader_type spatial;
uniform sampler2D albedo : hint_albedo;
uniform float roughness;

void fragment() {
    vec3 color = texture(albedo, UV).rgb;
    ROUGHNESS = roughness;
}
```

**Preprocessor output**:
```glsl
// Output: GLSL ready for DXC
layout(set = 1, binding = 0) uniform sampler2D albedo;
layout(set = 2, binding = 0) uniform MaterialParams {
    float roughness;
};

void main() {
    vec3 color = texture(albedo, UV).rgb;
    fragment_out.roughness = roughness;
}
```

### Benefits

1. **User-friendly syntax** — Hides descriptor layouts, Vulkan details
2. **Automatic variant generation** — Generate forward, deferred, motion vector variants automatically
3. **Hot-reload with reflection** — Parser understands uniform names, can hot-update them

### UzEngine Application

**Current State**: UzEngine uses raw HLSL ✓ (fine for tech demo)

**Recommendation** (for production):
- Consider **shader DSL or macro system** for higher-level shader authoring
- Godot's preprocessor is available as open-source reference

---

## Pattern 10: Render Pass Abstraction (RenderPass Ownership)

### Godot's Insight (Not Fully Exploited in Current Design)

Godot's render passes are registered but not fully "owned" by modules:

```cpp
// Desired pattern (better than Godot's current):
class RenderPass {
    virtual void Setup(RenderPassContext& ctx) = 0;
    virtual void Execute(RenderPassContext& ctx) = 0;
};

// Register pass
render_pipeline->RegisterPass(new GBufferPass(), "gbuffer");
render_pipeline->RegisterPass(new LightingPass(), "lighting");
render_pipeline->RegisterPass(new PostProcessPass(), "postprocess");

// Render loop automatically executes in order
render_pipeline->Execute();
```

### UzEngine Opportunity

**Implement a full render-graph system** (like Falcor, Frostbite):
1. Passes declare inputs/outputs
2. Automatic dependency resolution
3. Memory aliasing (reuse buffers across passes)
4. Async compute friendly

---

## Summary: Godot Patterns to Adopt

| Pattern | Status | Priority |
|---------|--------|----------|
| RenderingDevice abstraction | HAVE ✓ | Keep it |
| Scene proxy (deferred data) | HAVE ✓ | Keep it |
| Clustering spatial lights | HAVE ✓ | Keep it |
| Material storage (two-layer) | HAVE ✓ | Keep it |
| Post-processing pipeline | HAVE ✓ | Keep it |
| Multi-pass screen-space effects | HAVE ✓ | Keep it |
| Pipeline caching (disk) | MISSING | P1 (add) |
| Shader preprocessor | HAVE ✓ | Keep it |
| True bindless resources | MISSING | P2 (explore) |
| Render graph abstraction | PARTIAL | P2 (refine) |

---

## Key Takeaway

**Godot's greatest architectural achievement** is the **RenderingDevice abstraction** combined with **clustering deferred rendering**. These two patterns enable:

1. Multi-platform support (Vulkan/DX12/Metal/GL parity)
2. Unlimited dynamic lights (clustering handles scaling)
3. Clean separation of concerns (game thread vs render thread)
4. Extensible post-processing (effects pluggable)

UzEngine already has most of these. **Next focus**: Pipeline caching (disk), shader DSL (optional), and true bindless resources (optional but high-impact).

---

**Document Version**: 1.0  
**Date**: 2026-04-04  
**References**: Godot Engine 4.3 source code
