<!-- doc-meta
  title: Godot 4 Sky & Atmosphere Architecture — Reference Analysis
  description: How Godot 4 implements sky rendering, atmosphere, and IBL radiance baking with minimal complexity. Patterns applicable to UzEngine.
  status: complete
  tags: [godot, sky, atmosphere, rendering, ibl, reference-engine]
  created: 2026-03-15
  by: architect-analysis
-->

# Godot 4 Sky & Atmosphere Architecture — Reference Analysis

**Focus**: Analyze Godot 4's lean approach to sky rendering, atmosphere simulation, and IBL radiance baking. Extract patterns that can be adapted to UzEngine without over-engineering.

**Godot Version**: 4.x (source: `/e/godot`)

**Key Sources**:
- `servers/rendering/renderer_rd/environment/sky.h/.cpp` — SkyRD core system
- `scene/resources/3d/sky_material.h/.cpp` — Sky material types
- `servers/rendering/renderer_rd/shaders/environment/sky.glsl` — Sky rendering shader
- `forward_clustered/render_forward_clustered.cpp` — Pipeline integration

---

## 1. Sky Material System — Lean Polymorphism Without Switch

### Structure

Godot provides **three sky material types**, each inheriting `Material`:

| Type | Purpose | Parameters | Variant Caching |
|------|---------|------------|-----------------|
| **ProceduralSkyMaterial** | Simple gradient sky | sky_top_color, sky_horizon_color, sky_curve, sky_cover (optional texture), ground colors/curve, sun properties, energy multiplier | 4 variants (debanding yes/no × sky_cover yes/no) |
| **PanoramaSkyMaterial** | HDRI dome | panorama (Texture2D), energy_multiplier, filtering_enabled | 2 variants (filtering yes/no) |
| **PhysicalSkyMaterial** | Rayleigh/Mie atmosphere | rayleigh, rayleigh_color, mie, mie_eccentricity, mie_color, turbidity, sun_disk_scale, ground_color, exposure, night_sky texture | 4 variants (debanding yes/no × night_sky yes/no) |

### Key Insight: No Switch-on-Type

Instead of runtime type checking, Godot uses:

1. **Shader caching via bit flags**:
   ```cpp
   RID ProceduralSkyMaterial::get_shader_cache() const {
       return shader_cache[int(use_debanding) + (sky_cover.is_valid() ? 2 : 0)];
   }
   // Returns shader_cache[0..3] based on flags, not material type
   ```

2. **Material-driven dirty tracking**:
   ```cpp
   void set_sky_cover(const Ref<Texture2D>& p_sky_cover) {
       sky_cover = p_sky_cover;
       _update_shader(use_debanding, sky_cover.is_valid());
       if (shader_set) {
           RS::get_singleton()->material_set_shader(_get_material(), get_shader_cache());
       }
   }
   ```
   Each material owns its shader variant logic — no parent class does type dispatch.

3. **Parameter binding is immediate** (not batched):
   ```cpp
   void set_sky_top_color(const Color& p_sky_top) {
       sky_top_color = p_sky_top;
       RS::get_singleton()->material_set_param(_get_material(), "sky_top_color",
           sky_top_color * sky_energy_multiplier);
   }
   ```

### Lesson for UzEngine

**Pattern: Material-encapsulated variant selection**

Instead of:
```cpp
if (sky_material_type == PROCEDURAL) { ... }
else if (sky_material_type == PHYSICAL) { ... }
```

Do:
```cpp
struct SkyMaterial {
    virtual RID GetShaderVariant() const = 0;  // Each subclass decides
    virtual void SetParameter(StringId name, const Variant& value) = 0;
};

class ProceduralSkyMaterial : public SkyMaterial {
    RID GetShaderVariant() const override {
        return shader_cache[has_sky_cover ? 1 : 0];  // Binary decision tree
    }
};
```

This is **cheaper than a full switch tree** and **doesn't require RTTI**.

---

## 2. Sky Rendering Pipeline — Minimal Passes

### Pass Count: 3 Passes Per Frame (Amortized)

**Pass 1: Cubemap Rendering** (every frame, but one roughness layer per frame)
- Renders sky shader to octahedral map (256×256 by default)
- Push constants: orientation (3×3), projection (4), position (3), time, luminance/brightness multipliers
- **Total push constant size**: 96 bytes (max 128)

**Pass 2: Downsampling + Fast Filter** (once per layer)
- Box-filter downsampled radiance into hierarchy
- `octmap_downsample(source, dest, size)` — simple 2×2 bilinear filter

**Pass 3: Importance Sampling** (GGX-weighted, one layer per frame by default)
- Creates roughness-filtered mipmaps via `octmap_roughness()`
- Quality option: 64/256/1024 samples per texel (sky_ggx_samples_quality)
- **Benefit**: Real-time IBL without per-frame stalls

### Data Structure: Efficient Octmap Hierarchy

```cpp
struct Sky {
    RID radiance;                           // 256×256 octmap (REAL_TIME_SIZE)
    ReflectionData reflection;              // Hierarchy of filtered versions

    struct ReflectionData {
        RID radiance_base_octmap;           // Source (rendered sky)
        RID downsampled_radiance_octmap;    // Progressively downsampled
        LocalVector<Layer> layers;          // Per-roughness layers (7 by default)

        struct Layer {
            LocalVector<Mipmap> mipmaps;    // Roughness 0.0→1.0 in 7 steps
        };
    };
};
```

**Memory efficiency**: Octahedral encoding uses 2 floats per texel (vs 8 for cubemap + face selection). No face index, no cross-seam boundaries.

### Radiance Texture Format

- **Format**: `R16G16B16A16_SFLOAT` (HDR, half-precision)
- **Size progression**:
  - Base: 256×256
  - Quarter: 64×64
  - Mip 0: 128×128 (roughness 0.0)
  - Mip 6: 2×2 (roughness 1.0)

### Dirty Tracking — When to Re-bake

```cpp
struct Sky {
    SkyMaterialData* prev_material;      // Detect material change
    Vector3 prev_position;               // Detect camera move
    float prev_time;                     // Detect animation
    bool dirty = false;
};

void setup_sky(const RenderDataRD* p_render_data, ...) {
    if (material != sky->prev_material) {
        sky->prev_material = material;
        sky->reflection.dirty = true;
    }

    if (shader_data->uses_time && time - sky->prev_time > 0.00001) {
        sky->prev_time = time;
        sky->reflection.dirty = true;
        RenderingServerDefault::redraw_request();  // Force redraw
    }

    if (!cam_transform.origin.is_equal_approx(sky->prev_position) &&
        shader_data->uses_position) {
        sky->prev_position = cam_transform.origin;
        sky->reflection.dirty = true;
    }
}
```

**Key insight**: No observer pattern or event system — just snapshot comparison. **Cheap and self-contained.**

### Multi-Frame Baking Strategy

```cpp
if (update_single_frame) {
    // Bake all layers in one frame (stalls)
    for (int i = 1; i < max_processing_layer; i++) {
        sky->reflection.create_reflection_importance_sample(i, sky_ggx_samples_quality);
    }
} else {
    // Bake one layer per frame (smooth)
    sky->reflection.create_reflection_importance_sample(processing_layer++, sky_ggx_samples_quality);
}
```

**Benefit**: 60 FPS maintained even on heavy sky updates.

---

## 3. Sky-to-Radiance — Octahedral Encoding & Filtering

### Octahedral Mapping (vs Cubemap)

**Why Godot uses octahedral**:
1. Single 2D texture (no face selection logic in shader)
2. Better memory layout for cache coherency
3. No cross-seam artifacts (cubemap edges are a nightmare)
4. Easier filter kernels (2D operations only)

**Encoding/Decoding**:
```glsl
// Encode view direction to 2D octmap UV
vec2 encode_oct(vec3 n) {
    vec2 p = n.xy * (1.0 / (abs(n.x) + abs(n.y) + abs(n.z)));
    return n.z < 0.0 ? (1.0 - abs(p.yx)) * sign(p) : p;
}

// Decode 2D octmap UV to view direction
vec3 decode_oct(vec2 p) {
    vec3 n = vec3(p, 1.0 - dot(abs(p), vec2(1.0)));
    return normalize(n * mix(1.0, -1.0, n.z < 0.0));
}
```

Godot provides `oct_to_vec3_with_border()` and `vec3_to_oct_with_border()` to handle **border regions** (prevent cross-edge sampling).

### Filtering Pipeline

**Step 1: Downsampling** (if quality == FAST)
```
radiance_base_octmap (256×256)
    ↓ octmap_downsample()
downsampled_radiance_octmap (128×128)
    ↓ octmap_downsample()
downsampled_layer[0] (64×64)
    ↓ octmap_downsample()
downsampled_layer[1] (32×32)
```

**Step 2: Importance Sampling** (if quality == HIGH)
```
downsampled_radiance_octmap (64×64, downsampled once)
    ↓ octmap_roughness(roughness = 0.0, samples = 256)
layers[1].mipmaps[0] (128×128, mip 0)
    ↓ octmap_downsample()
layers[1].mipmaps[1] (64×64, mip 1)
```

**GGX Importance Sampling Formula**:
```
For each texel in output:
    sum = 0
    for sample i in 0..num_samples:
        alpha = roughness²
        phi = 2π * random()
        cos_theta = sqrt((1 - random()) / (1 + (alpha² - 1) * random()))
        sin_theta = sqrt(1 - cos_theta²)

        // Sample input octmap at this direction
        direction = tangent_to_world(phi, cos_theta)
        sample_color = radiance[direction]

        // Weight by solid angle
        solid_angle = 4π / num_samples
        sum += sample_color * solid_angle

    output[texel] = sum / num_samples
```

**Benefit**: **Real-time, progressive IBL** without offline baking.

---

## 4. Fog Integration — Two-Layer Composition

### Fixed (Height) Fog

Applied in sky shader directly:

```glsl
vec4 fog_process(vec3 view, vec3 sky_color) {
    vec3 fog_color = mix(sky_scene_data.fog_light_color, sky_color,
                         sky_scene_data.fog_aerial_perspective);

    if (sky_scene_data.fog_sun_scatter > 0.001) {
        for (uint i = 0; i < sky_scene_data.directional_light_count; i++) {
            vec3 light_color = directional_lights.data[i].color_size.xyz *
                               directional_lights.data[i].direction_energy.w;
            float light_amount = pow(max(dot(view, light_direction), 0.0), 8.0) * M_PI;
            fog_color += light_color * light_amount * sky_scene_data.fog_sun_scatter;
        }
    }

    return vec4(fog_color, 1.0);
}
```

**Parameters in SkySceneData UBO**:
- `fog_enabled` (bool)
- `fog_density` (float)
- `fog_sky_affect` (float) — lerp amount
- `fog_light_color` (vec3)
- `fog_aerial_perspective` (float) — blend between light color and sky color
- `fog_sun_scatter` (float) — sun scattering intensity (pow exponent = 8.0)

### Volumetric Fog

Rendered separately into 3D texture via compute shader:

```cpp
struct VolumetricFog {
    RID volume_texture;     // 3D texture (e.g., 160×90×64 with reprojection)
    RID shader;             // Compute shader
    RID material;           // Material with density function
};
```

Sky samples volumetric fog independently:

```glsl
vec4 volumetric_fog_process(vec2 screen_uv) {
    vec3 fog_pos = vec3(screen_uv, 1.0);  // Unproject at far plane
    return texture(volumetric_fog_texture, fog_pos);
}

// Later in shader:
if (sky_scene_data.volumetric_fog_enabled) {
    vec4 fog = volumetric_fog_process(uv);
    frag_color.rgb = mix(frag_color.rgb, fog.rgb, sky_scene_data.volumetric_fog_sky_affect);
}
```

### Key Insight: Separation of Concerns

- **Sky shader**: Handles fixed fog + atmosphere
- **Volumetric shader**: Handles volumetric fog in compute
- **Composition**: Sky blends fixed fog first, then volumetric fog
- **No coupling**: Changing sky material doesn't affect fog

This is **Godot's lean design principle** — let each system own its domain.

---

## 5. Physical Sky Material — Rayleigh & Mie

### Exposed Parameters

```cpp
class PhysicalSkyMaterial : public Material {
    float rayleigh;              // Rayleigh scattering coefficient (0.0–2.0)
    Color rayleigh_color;        // Tint of Rayleigh (sky blue)
    float mie;                   // Mie scattering coefficient (0.0–2.0)
    float mie_eccentricity;      // Forward scattering bias (0.0–1.0)
    Color mie_color;             // Tint of Mie (haze color)
    float turbidity;             // Aerosol density (0.0–10.0)
    float sun_disk_scale;        // Sun disk size multiplier
    Color ground_color;          // Ground reflection color
    float energy_multiplier;     // Brightness
    bool use_debanding;          // Dither to reduce banding
    Texture2D night_sky;         // Optional night stars texture
};
```

### Rayleigh Model

**In-shader computation** (exact formula not exposed in headers, but derived from Preetham model):

```
Rayleigh scattering ∝ 1 / λ⁴

For view direction v and sun direction s:
    cos_angle = dot(v, s)
    rayleigh_phase = (3/4) * (1 + cos²)
    rayleigh_contrib = rayleigh_color * rayleigh * rayleigh_phase
    rayleigh_contrib *= exp(-altitude / rayleigh_scale_height)  // Exponential falloff
```

### Mie Model

**Haze and sun disk**:

```
Mie scattering ∝ aerosol size distribution

For view direction v and sun direction s:
    g = mie_eccentricity  // Forward scattering bias
    cos_angle = dot(v, s)
    mie_phase = (1 - g²) / (2 * (2 + g²)) * (1 + cos²) / (1 + g² - 2g*cos)
    mie_contrib = mie_color * mie * mie_phase
    mie_contrib *= exp(-altitude / mie_scale_height)

    // Sun disk (sharp specular component)
    sun_disk = exp(-sun_angular_distance² / sun_disk_scale²)
```

### Why Godot Doesn't Expose Shader Code

The formulas are **compiled into shader code via shader generator**:

```cpp
void PhysicalSkyMaterial::_update_shader(bool p_use_debanding, bool p_use_night_sky) {
    // Generates GLSL code with inline Rayleigh/Mie functions
    // Shader code is cached in shader_cache[variant]
    // Parameter changes don't recompile — only variant changes do
}
```

**Benefit**: Game developers tune parameters without touching GLSL.

---

## 6. Rendering Pipeline Integration

### When Sky is Rendered

In **RendererSceneRenderRD::render_scene()** (forward+ path):

```cpp
bool draw_sky = false;
bool draw_sky_fog_only = false;

// Determine if sky should be drawn
RSE::EnvironmentBG bg_mode = RendererSceneRenderRD::environment_get_background(environment);
switch (bg_mode) {
    case RSE::ENV_BG_CLEAR_COLOR:
    case RSE::ENV_BG_COLOR:
        draw_sky_fog_only = true;  // Just fog, no sky geometry
        break;
    case RSE::ENV_BG_SKY:
        draw_sky = !p_render_data->transparent_bg;  // Full sky
        break;
}

// Also need sky if using it for IBL
if (reflection_source == RSE::ENV_REFLECTION_SOURCE_SKY ||
    ambient_source == RSE::ENV_AMBIENT_SOURCE_SKY) {
    sky.setup_sky(p_render_data, screen_size);
}

// During render:
// 1. GBuffer pass (geometry)
// 2. Lighting pass (compositing)
// 3. Sky pass (this is where sky is drawn)
if (draw_sky || draw_sky_fog_only) {
    sky.update_res_buffers(...);  // Update half/quarter res textures
    sky.draw_sky(draw_list, render_buffers, environment, framebuffer, time, ...);
}
```

### Order in Frame

```
GBuffer pass (opaque geometry)
    ↓
Lighting pass (lighting composition)
    ↓
Sky pass ← HERE (drawn to backbuffer, composites with lighting)
    ↓
Transparent geometry (over sky)
    ↓
Post-processing (bloom, tonemapping, etc.)
```

**Key**: Sky is **not part of GBuffer** — it's a fullscreen pass over the lit result.

### Push Constants to Sky Shader

```cpp
struct SkyPushConstant {
    float orientation[12];      // 3×3 orientation matrix (camera rotation)
    float projection[4];        // (scale_x, scale_y, offset_x, offset_y) for aspect/FOV
    float position[3];          // Camera world position
    float time;                 // Total game time (for animated skies)
    float border_size[2];       // Octmap border region size (prevents seam artifacts)
    float luminance_multiplier; // Exposure/HDR tone mapping
    float brightness_multiplier;// Additional brightness control
};  // Total: 96 bytes (fits in max 128 push constant limit)
```

---

## 7. UzEngine Adaptation Strategy

### 1. Sky Material System

**Adopt Godot's pattern**:

```cpp
// runtime/engine/renderer/core/sky_material.h
struct SkyMaterial {
    virtual ~SkyMaterial() = default;
    virtual RID GetShaderVariant() const = 0;
    virtual void SetParameter(const char* name, const Variant& value) = 0;
    virtual void Invalidate() = 0;  // Called when needs re-baking
};

struct ProceduralSkyMaterialDesc {
    Vec3 sky_top_color = Vec3(0.6f, 0.8f, 1.0f);
    Vec3 sky_horizon_color = Vec3(0.2f, 0.4f, 0.8f);
    float sky_curve = 1.0f;
    float sky_energy_multiplier = 1.0f;
    // Optional: sky_cover texture (clouds)
};

class ProceduralSkyMaterial : public SkyMaterial {
    RID GetShaderVariant() const override;
    void SetParameter(const char* name, const Variant& value) override;
};
```

### 2. Octahedral Radiance Texture

**File**: `runtime/engine/renderer/core/octahedral_radiance.h`

```cpp
struct OctahedralRadiance {
    RID base_texture;           // 256×256 octahedral map (rendered sky)
    RID downsampled_texture;    // 128×128 (fast filter source)
    Vector<RID> roughness_layers;  // 7 mipmaps for IBL

    // Dirty tracking
    void Invalidate() {
        dirty = true;
        layer_index = 0;  // Start from layer 0 next frame
    }

private:
    bool dirty = true;
    int layer_index = 0;  // Multi-frame baking: which layer to bake this frame
};
```

### 3. Sky Rendering Pass

**File**: `runtime/engine/renderer/passes/sky_pass.cpp`

```cpp
void SkyPass::Execute(const RenderDataRD& render_data,
                      const SkyPassDesc& desc,
                      RenderGraph& rg) {
    // Push constants (96 bytes)
    SkyPushConstant pc;
    pc.orientation = camera_basis.to_matrix3();  // 3×3
    pc.position = camera_pos;
    pc.time = world_time;
    pc.luminance_multiplier = exposure_multiplier;

    // Render to octahedral texture
    rg.AddPass("SkyPass", [&](RenderGraphExecutor& exec) {
        // If dirty, render to base_texture
        // Then downsample and importance-sample one layer

        // Draw fullscreen triangle with sky shader
        exec.BindPipeline(sky_pipeline);
        exec.PushConstants(&pc, sizeof(pc));
        exec.Draw(3, 0);  // Fullscreen triangle
    });
}
```

### 4. Fog in Sky Shader

**Adapt Godot's approach**:

```hlsl
// runtime/shaders/runtime/environment/sky.hlsl

// Constant buffer for fog parameters
cbuffer SkyFogCB : register(b1) {
    float4 fog_color;
    float fog_density;
    float fog_sky_affect;
    float fog_sun_scatter;
    float pad;
};

float4 sky_main(float3 view_dir) {
    float4 sky_color = ComputeSkyColor(view_dir);  // Rayleigh/Mie or procedural

    // Apply fixed fog
    float3 fog = mix(fog_color.rgb, sky_color.rgb, fog_aerial_perspective);

    // Sun scatter
    float sun_dot = max(dot(view_dir, sun_direction), 0.0);
    fog += sun_color * pow(sun_dot, 8.0) * fog_sun_scatter;

    // Blend
    sky_color.rgb = mix(sky_color.rgb, fog, fog_sky_affect);

    return sky_color;
}
```

### 5. Dirty Tracking

```cpp
struct Sky {
    RID material;
    Vec3 prev_camera_pos;
    float prev_time;
    uint64_t prev_material_hash;  // Hash of material parameters

    void Update(const Vec3& cam_pos, float time, const SkyMaterial* mat) {
        bool material_changed = (prev_material_hash != mat->GetHash());
        bool time_changed = (time - prev_time > 0.001f);
        bool position_changed = !cam_pos.IsEqual(prev_camera_pos, 0.1f);

        if (material_changed || (mat->UsesTime() && time_changed) ||
            (mat->UsesPosition() && position_changed)) {
            Invalidate();
            prev_material_hash = mat->GetHash();
            prev_time = time;
            prev_camera_pos = cam_pos;
        }
    }
};
```

---

## 8. Godot's Leanness Lessons

### 1. Material-Driven Variant Selection (not parent-driven)

**Godot's way**: Each material decides its shader variant via `get_shader_cache()`.

**Anti-pattern** (don't do):
```cpp
// Bad: parent class routes based on type
SkyPass::Execute() {
    if (dynamic_cast<ProceduralSkyMaterial*>(material)) { ... }
    else if (dynamic_cast<PhysicalSkyMaterial*>(material)) { ... }
}
```

**Good** (Godot's way):
```cpp
// Good: material decides itself
RID shader = material->GetShaderVariant();
```

### 2. Octahedral Encoding (not cubemap)

**Godot avoids**:
- Cubemap face selection logic in shaders
- Cross-seam artifacts
- 6 separate texture views

**Godot uses**: Single 2D texture with direction → UV mapping. Simple, cache-efficient.

### 3. Multi-Frame Baking (not monolithic)

**Godot avoids**: Stalling the CPU on sky IBL generation.

```cpp
// Per frame: only bake one roughness layer
sky->reflection.create_reflection_importance_sample(processing_layer++);
if (processing_layer >= max_layer) processing_layer = 0;
```

**Result**: Smooth 60 FPS even on GPU-bound sky updates.

### 4. Dirty Tracking via Snapshots (not observers)

**Godot avoids**: Observer patterns, event callbacks, dependency graphs.

```cpp
// Simple comparison every frame
if (!cam_pos.is_equal_approx(sky->prev_position)) {
    sky->dirty = true;
    sky->prev_position = cam_pos;
}
```

**Cost**: Negligible (3 float comparisons). **Simplicity**: High.

### 5. Separation of Concerns (fog != sky)

Godot's fog is **independent**:
- Sky shader samples fog parameters but doesn't own fog
- Volumetric fog is a separate compute shader
- Fixed fog is baked into sky shader via UBO

**Result**: Changing sky type doesn't break fog. Changing fog doesn't invalidate sky cache.

### 6. Minimal Push Constants

Only 96 bytes for **all** sky state:
- Orientation (12 floats)
- Projection (4 floats)
- Position (3 floats)
- Time, borders, multipliers (5 floats)

**No per-material push constants** — all material params go into uniform buffer.

---

## 9. Comparison to UE5

| Aspect | Godot 4 | UE5 | UzEngine Recommendation |
|--------|---------|-----|------------------------|
| **Sky rendering** | Single fullscreen pass (after lighting) | `SkyAtmosphereComponent` (separate world entity) | Single pass (like Godot) — simpler |
| **Atmosphere model** | Preetham (in-shader) | Bruneton multi-scattering (compute shader) | Preetham for now (extend to Bruneton later) |
| **Radiance texture** | Octahedral 256×256 (real-time) | Cubemap with offline baking | Octahedral (Godot's approach) |
| **IBL filtering** | Per-frame importance sampling (1 layer/frame) | Offline baked reflection probes | Per-frame (Godot's approach) |
| **Fog composition** | Fixed fog in sky shader + volumetric separate | Volumetric fog only (compute-based) | Both (like Godot) |
| **Material variants** | 4-variant cache per material type | Single material with many permutations | 4-variant cache (Godot's approach) |
| **Dirty tracking** | Snapshot comparison | Scene graph observer | Snapshot comparison (Godot's approach) |

---

## 10. Implementation Checklist for UzEngine

### Phase 1 (MVP)
- [ ] Implement `SkyMaterial` interface with `ProceduralSkyMaterial` subclass
- [ ] Create octahedral radiance texture (256×256 RGBA16F)
- [ ] Write sky.hlsl shader with Rayleigh/Mie or simple gradient
- [ ] Integrate SkyPass into render graph
- [ ] Implement multi-frame layer baking (process 1 layer/frame)
- [ ] Add sky dirty tracking (position, time, material hash)
- [ ] Integrate fixed fog parameters into sky shader

### Phase 2 (IBL)
- [ ] Implement octahedral downsampling compute shader
- [ ] Implement GGX importance sampling compute shader (7 mips)
- [ ] Expose roughness_layers to renderer
- [ ] Test IBL on PBR materials (should see environment reflections)

### Phase 3 (Advanced)
- [ ] Add `PhysicalSkyMaterial` with Rayleigh/Mie UI parameters
- [ ] Implement volumetric fog compute shader
- [ ] Add night_sky texture support
- [ ] Extend to Bruneton multi-scattering if needed

---

## References

- **Godot Sky Source**: `/e/godot/servers/rendering/renderer_rd/environment/sky.{h,cpp}`
- **Sky Material Source**: `/e/godot/scene/resources/3d/sky_material.{h,cpp}`
- **Sky Shader**: `/e/godot/servers/rendering/renderer_rd/shaders/environment/sky.glsl`
- **Forward Renderer Integration**: `/e/godot/servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp`

