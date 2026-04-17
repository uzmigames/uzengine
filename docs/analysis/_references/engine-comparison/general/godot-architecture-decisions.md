# Godot 4 Architecture Decisions: Why and How They Help UzEngine

**Date**: 2026-03-14
**Analysis**: Decision patterns that made Godot's renderer lean and flexible

---

## Decision 1: Forward Clustering Over Deferred Rendering

### What Godot Chose
**Forward clustering** with per-tile light culling (GPU compute).

### Why
1. **Transparency blends naturally** - forward shading composites over previous pixels
2. **MSAA works out-of-the-box** - no deferred + MSAA complexity
3. **Material flexibility** - StandardMaterial3D can have custom shaders without gbuffer unpacking
4. **Memory bandwidth** - no gbuffer reads on every light application
5. **Smaller codebases** - one pass (forward) vs three (gbuffer, lighting, composite)

### What This Meant
```
Deferred (UE5-style):
  GBuffer pass       (1 call, all geometry)
    ├─ Write depth
    ├─ Write normal
    ├─ Write albedo
    └─ Write material ID
  Lighting pass      (N calls, per light)
  Transparency pass  (1 call, blended)
  Total: N+2 passes

Forward clustering (Godot):
  Light cluster build (GPU compute, ~0.3ms)
  Forward render      (1 call, all geometry + all lights)
  Total: 2 passes (1 compute + 1 rasterize)
```

### Lesson for UzEngine
**Decision**: Adopt forward clustering.

**Implementation**:
```cpp
// Don't do this:
class DeferredRenderer {
    void RenderGBuffer();
    void RenderLighting();
    void RenderTransparency();
};

// Do this:
class ForwardRenderer {
    void BuildClusters();  // GPU compute
    void RenderScene();    // Single forward pass
};
```

**Impact**: Simpler codebase, better transparency support, less memory pressure.

---

## Decision 2: Unified Light Data Layout

### What Godot Chose
Single `LightData` struct for omni/spot, separate `DirectionalLightData`, all in GPU buffers accessed by index.

### Why
1. **Zero divergence** in shader - no `if (type == OMNI) {...} else if (type == SPOT) {...}`
2. **Single shader path** - all lights use same BRDF evaluation
3. **GPU cache coherence** - sequential buffer reads hit L1 cache
4. **Scalability** - can pack 1000s of lights without shader rewrites

### What This Meant
```
Bad (switch on type):
  for each light {
    if (light.type == DIRECTIONAL) {
        // 50 instructions for directional
    } else if (light.type == POINT) {
        // 60 instructions for point
    } else if (light.type == SPOT) {
        // 80 instructions for spot
    }
  }
  // GPU: divergent branches waste SIMD lanes

Good (unified structure):
  for each light {
    LightData L = lights[idx];
    float attenuation = ComputeAttenuation(L);  // works for all types
    vec3 to_light = ComputeLightDir(L);         // works for all types
    ApplyBRDF(L, attenuation, to_light);        // universal
  }
  // GPU: no divergence, all lanes execute same code
```

### Lesson for UzEngine
**Decision**: Unify light data into one struct.

**Implementation**:
```cpp
// File: src/engine/renderer/light_data.h
struct LightDataGPU {
    enum Type { DIRECTIONAL, POINT, SPOT };

    Type type;
    uint32_t flags;

    // Common
    Vec3 direction;
    Vec3 color;
    float energy;
    float specular;

    // Point/Spot only
    Vec3 position;
    float range;
    float size;

    // Spot only
    float cone_angle;
    float cone_attenuation;

    // Shadow
    uint32_t shadow_index;
    float shadow_bias;
    Mat4 shadow_matrix;

    // Compute light contribution
    // (no branching in shader)
};
```

**Impact**: Better GPU utilization, cleaner shader code, easier to add 100 lights.

---

## Decision 3: Environment as a Single Resource

### What Godot Chose
One `RendererEnvironmentStorage` resource holding **all** ambient, fog, tonemap, bloom, SSAO, SSR parameters.

### Why
1. **Single versioning point** - one dirty flag for entire environment
2. **Cache locality** - all params in one UBO
3. **Consistency** - tonemap/exposure applied uniformly everywhere
4. **Editor UX** - one WorldEnvironment node per scene
5. **Hot-reload friendly** - swap entire environment at once

### What This Meant
```
Bad (scattered):
  class Fog { Vec3 color; float density; };
  class Tonemap { TonemapMode mode; float exposure; };
  class SSAO { float radius; float intensity; };

  // In shader:
  layout(binding=5) uniform Fog { ... } fog;
  layout(binding=6) uniform Tonemap { ... } tone;
  layout(binding=7) uniform SSAO { ... } ssao;
  // 3 separate UBOs, 3 dirty flags, cache misses

Good (unified):
  struct Environment {
      Fog fog;
      Tonemap tonemap;
      SSAO ssao;
      // ... all settings
  };

  // In shader:
  layout(binding=5) uniform Environment { ... } env;
  // 1 UBO, 1 dirty flag, coherent reads
```

### Lesson for UzEngine
**Decision**: Consolidate SceneEnvironment with Fog, Tonemap, PostProcess.

**Current** (scattered):
```cpp
// Multiple places
class SkyLight { Vec3 ambient_color; };
class Fog { Vec3 color; float density; };
class PostProcessStack { Tonemapper tone; BloomEffect bloom; };
```

**Recommended** (unified):
```cpp
// File: src/engine/renderer/scene_environment.h
struct SceneEnvironment {
    // Ambient/Sky
    Vec3 ambient_color;
    float ambient_intensity;
    RID sky_cubemap;
    RID irradiance_sh;

    // Fog
    struct { Vec3 color; float density; float height; } fog;

    // Tonemap
    struct { TonemapMode mode; float exposure; float white; } tonemap;

    // Post-effects
    struct { bool enabled; float threshold; float intensity; } bloom;
    struct { bool enabled; float radius; float power; } ssao;
    struct { bool enabled; int max_steps; float fade; } ssr;

    uint64_t version;  // single dirty flag
};

// Passed as UBO to all shaders
layout(binding=7, std140) uniform SceneEnvironmentUBO {
    vec3 ambient_color;
    float ambient_intensity;
    vec3 fog_color;
    float fog_density;
    // ... all fields
} scene_env;
```

**Impact**: Easier to swap entire environment, better cache coherence, fewer UBO bindings.

---

## Decision 4: Shadow Atlas with Dynamic Reallocation

### What Godot Chose
Single 2K×2K shadow atlas divided into 4 quadrants, each subdividable into 1/4/16/64 slots. Quadrants realloc dynamically when needed.

### Why
1. **Memory efficient** - one 32 MB 2K atlas vs 8× 512×512 per light
2. **No allocation failures** - realloc guarantees space for new lights
3. **Gradual degradation** - if 16 lights shadow, 16th may have lower resolution
4. **LRU eviction** - invisible lights' shadows freed automatically

### What This Meant
```
Bad (per-light):
  Light 0: 512×512 depth = 1 MB
  Light 1: 512×512 depth = 1 MB
  ...
  Light 7: 512×512 depth = 1 MB
  Total: 8 MB for 8 lights
  Limit: can't add 9th light (might not be reachable!)

Good (atlas with realloc):
  Quadrant 0: 1×1 = 1 MB (Light 0 gets full quadrant)
  Quadrant 1: 2×2 = 1 MB (Lights 1-4)
  Quadrant 2: 4×4 = 1 MB (Lights 5-20)
  Quadrant 3: 8×8 = 1 MB (Lights 21+)
  Total: 4 MB for up to 64 lights

  Add light 33: shrink Q3 to 4×4, expand Q2 to 8×8
  Lights 5-20 still have shadows (lower res)
  Lights 21-32 realloc to Q3
```

### Lesson for UzEngine
**Current** (fixed limit):
```cpp
struct ShadowManager {
    RID point_light_shadows[8];  // Fixed: only 8 lights can shadow!

    void AddPointLightShadow(Light* L) {
        if (point_light_count >= 8) return;  // Fail silently
        point_light_shadows[point_light_count++] = CreateShadow();
    }
};
```

**Recommended** (atlas with realloc):
```cpp
// File: src/engine/renderer/shadow_atlas.h
class ShadowAtlas {
    struct Quadrant {
        uint32_t subdivision;  // 1, 4, 16, 64
        std::vector<ShadowEntry> entries;
    } quadrants[4];

    RID depth;  // Single 2K texture

    ShadowEntry* AllocateShadow(uint32_t light_id) {
        // Try to fit in any quadrant
        for (int q = 0; q < 4; q++) {
            if (entries_available(quadrants[q])) {
                return &quadrants[q].entries.emplace_back(light_id);
            }
        }

        // Realloc: shrink quadrant with fewest lights
        Reallocate();
        return AllocateShadow(light_id);
    }

    void Reallocate() {
        // Find smallest populated quadrant Q_min
        // For all Q < Q_min: halve subdivision
        // For all Q >= Q_min: double (if space permits)
        // Remap shadow rects
        RebuildAtlas();
    }
};
```

**Impact**: Handles 32+ dynamic shadows seamlessly.

---

## Decision 5: Shader Variant Caching

### What Godot Chose
StandardMaterial3D generates shader code on-demand, caching by feature bitmask.

### Why
1. **Minimal code overhead** - only compile paths actually used
2. **Fast iteration** - change material properties, shader recompiles instantly
3. **Predictable performance** - no dead code in shader
4. **Memory efficient** - fewer registers used per variant

### What This Meant
```
Bad (uber-shader):
  uniform sampler2D texAlbedo;
  uniform sampler2D texNormal;
  uniform sampler2D texMetallic;

  void fragment() {
    vec3 albedo = texAlbedo.sample();
    vec3 normal = texNormal.sample();
    float metallic = texMetallic.sample();
  }
  // Even if you don't use texMetallic, it's sampled!

Good (variant cache):
  if (has_texture_flag(TEXTURE_ALBEDO)) {
    // Only if used:
    uniform sampler2D texAlbedo;
    vec3 albedo = texAlbedo.sample();
  }
  // Compiled out if not used
```

### Lesson for UzEngine
**Recommended**:
```cpp
// File: src/engine/renderer/materials/standard_material.h
class StandardMaterial3D {
    struct Variant {
        uint32_t feature_mask;  // USE_ALBEDO | USE_NORMAL | ...
        RID shader;
    };

    static std::unordered_map<uint32_t, Variant> s_variant_cache;

    void OnTextureChanged(TextureType type) {
        uint32_t old_mask = CalculateFeatureMask();

        // Feature bitmap changed
        uint32_t new_mask = CalculateFeatureMask();

        if (old_mask != new_mask) {
            // Request new shader variant
            RID new_shader = GetOrCreateShaderVariant(new_mask);
            SetShader(new_shader);
        }
    }

    RID GetOrCreateShaderVariant(uint32_t mask) {
        if (s_variant_cache.find(mask) != s_variant_cache.end()) {
            return s_variant_cache[mask].shader;
        }

        // Generate shader code for this mask
        std::string code = GenerateShaderCode(mask);
        RID shader = rhi->CompileShader(code);
        s_variant_cache[mask] = {mask, shader};

        return shader;
    }
};
```

**Impact**: Faster shaders, less register pressure, faster compilation.

---

## Decision 6: RenderingDevice Abstraction

### What Godot Chose
All rendering goes through `RenderingDevice` interface with pluggable backends (Vulkan, DX12, Metal).

### Why
1. **No vendor lock-in** - swap backends without rewriting renderer
2. **Deferred command buffer** - record to CPU command buffer, GPU thread executes
3. **Thread-safe** - GPU thread isolated from game thread
4. **Portable** - same rendering code on Windows, Mac, Linux, mobile

### Key Abstraction
```cpp
// File: /e/godot/servers/rendering/rendering_device.h
class RenderingDevice {
    // Command recording (game thread)
    RID::CommandBuffer begin_command_buffer();
    void draw_command_buffer(RID::CommandBuffer cmds);

    // Resource creation (thread-safe)
    RID texture_create(...);
    RID buffer_create(...);
    RID pipeline_create(...);

    // GPU execution (render thread)
    void submit();
    void sync();
};
```

### Lesson for UzEngine
**Current**: DX12 directly exposed in some modules (gizmo_renderer.h takes ID3D12Device*)

**Recommended**: Full RHI abstraction
```cpp
// File: src/engine/renderer/rhi/rhi.h
class Rhi {
    virtual RID CreateTexture(const TextureDesc&) = 0;
    virtual RID CreateBuffer(const BufferDesc&) = 0;
    virtual RID CreatePipeline(const PipelineDesc&) = 0;
    virtual void DrawMesh(const DrawDesc&) = 0;
};

// Implementations:
class RhiDx12 : public Rhi { ... };
class RhiVulkan : public Rhi { ... };
```

**Impact**: Enables Vulkan backend, cleaner abstraction, easier testing.

---

## Decision 7: No "Default Values" — Everything is Explicit

### What Godot Does
When creating a new DirectionalLight3D or Environment, **all parameters have sensible defaults**:
- DirectionalLight3D: energy=1.0, color=white, shadow_bias=0.01
- StandardMaterial3D: metallic=0.0, roughness=0.5, albedo=white
- Environment: tonemap=ACES, exposure=1.0, fog_enabled=false

### Why
1. **Predictability** - new project looks reasonable out-of-the-box
2. **No null checks** - shader never reads null texture or NaN value
3. **Forward compatibility** - new material properties don't break old scenes

### Lesson for UzEngine
**Rule**: Every resource/component must be renderable with zero parameters set.

```cpp
// File: src/engine/renderer/materials/standard_material.h
class StandardMaterial3D {
    StandardMaterial3D() {
        // Constructor sets defaults
        albedo_color = Vec3(1.0f);      // white
        metallic = 0.0f;
        roughness = 0.5f;
        emission_energy = 1.0f;
        normal_scale = 1.0f;
        // ... all params initialized
    }

    // No member can be uninitialized
};
```

**Impact**: No null pointer dereferences, simpler error handling, better UX.

---

## Decision 8: Compute Shaders for "Build" Operations

### What Godot Does
- Light cluster assignment → ClusterStoreShaderRD (compute)
- Sky irradiance baking → compute shader
- Image filtering → compute shader

### Why
1. **Parallelism** - thousands of threads on GPU doing same work
2. **CPU-free** - game thread doesn't calculate frustums or light intersections
3. **Fast iteration** - change logic in shader, instant feedback

### Lesson for UzEngine
**Add compute shaders for**:
- Light cluster building
- IBL cubemap convolution
- Shadow atlas invalidation
- Depth pyramid generation (for SSR/SSAO)
- Visibility culling (indirect draws)

```cpp
// File: src/engine/renderer/effects/cluster_build.hlsl
// Compute shader (groups of 8×8×1)
[numthreads(8, 8, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    uint cluster_idx = dispatch_id.y * cluster_width + dispatch_id.x;

    // For each light, test intersection with cluster frustum
    for (uint light_idx = 0; light_idx < light_count; light_idx++) {
        if (FrustumLightIntersect(clusters[cluster_idx], lights[light_idx])) {
            AppendLight(cluster_idx, light_idx);
        }
    }
}
```

**Impact**: Offload heavy CPU work to GPU, scale to thousands of lights.

---

## Summary: 8 Decisions That Made Godot Lean

| # | Decision | Benefit |
|---|----------|---------|
| 1 | Forward clustering | No GBuffer, simpler transparency |
| 2 | Unified light data | Zero divergence, scalable |
| 3 | Environment blob | Single versioning point |
| 4 | Shadow atlas + realloc | Handles 32+ lights |
| 5 | Shader variant cache | Only compile used paths |
| 6 | RenderingDevice abstraction | Portable, thread-safe |
| 7 | Explicit defaults | No null checks, good UX |
| 8 | Compute for build ops | CPU-free, highly parallel |

---

## Implementation Roadmap for UzEngine

### Phase 1: Adopt Forward Clustering
- [ ] Implement ClusterBuilder compute shader
- [ ] Refactor forward renderer to use clusters
- [ ] Remove deferred GBuffer passes

### Phase 2: Unify Light Data
- [ ] Define LightDataGPU union struct
- [ ] Update light shader to use unified struct
- [ ] Remove per-light-type branching

### Phase 3: Consolidate Environment
- [ ] Create SceneEnvironment struct
- [ ] Merge SkyLight, Fog, PostProcess into one resource
- [ ] Single UBO for all environment params

### Phase 4: Shadow Atlas
- [ ] Implement ShadowAtlas with 4 quadrants
- [ ] Add reallocation logic
- [ ] Use for all omni/spot lights

### Phase 5: Material Variants
- [ ] Implement shader variant cache by feature mask
- [ ] Update StandardMaterial3D to use variants
- [ ] Profile instruction count per variant

### Phase 6: Full RHI Abstraction
- [ ] Encapsulate all DX12 types behind RHI interface
- [ ] Remove direct ID3D12* from non-RHI modules
- [ ] Set up Vulkan backend skeleton

---

**End of Architecture Decisions**
