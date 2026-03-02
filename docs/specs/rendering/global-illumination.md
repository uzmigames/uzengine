# Spec: Global Illumination System

> **Status**: Planned (Phase 2–3)
> **Owner**: `Renderer` module (src/engine/renderer/)
> **Approach**: Hybrid GI — Static lightmaps + Dynamic DDGI probes + Reflection captures
> **Thread Safety**: Single-threaded (game thread for baking coordination, GPU for compute)
> **Key Benefit**: Real-time GI with offline quality, subsecond light update latency

---

## Overview

Global Illumination (GI) solves the costly problem of indirect lighting — light that bounces from one surface to another, illuminating areas that aren't directly lit. Computing true GI in real-time is prohibitively expensive; instead, UzEngine uses a **hybrid approach**:

1. **Static Lightmaps** — High-quality pre-baked illumination for static geometry
2. **Dynamic DDGI Probes** — Real-time dynamic GI via irradiance sampling
3. **Reflection Captures** — Specular environment reflections (IBL)

| Technique | Cost | Quality | Updates | Best For |
|-----------|------|---------|---------|----------|
| **Lightmaps** | ~10–30 min bake | AAA (64+ bakes/texel) | Manual rebake | Static scenes, outdoor skylight |
| **DDGI** | 1–2 ms/frame | Good (fast probes update radiance) | Every frame | Dynamic lights, moving objects, caverns |
| **Reflection Captures** | 0.5–1 ms/frame | Good (cubemap reflection) | Manual rebake | Glossy surfaces, specular highlights |

This mirrors **AAA engine approaches** (Unreal Engine 5 Lumen, Sony Decima, Guerrilla):
- Lightmaps for static, high-quality results
- DDGI for dynamic updates without baking cost
- Captures for reflections without ray tracing

---

## Architecture

```
Scene (UV2 unwrap)
│
├── Static Geometry (.uzscene)
│   └── Each node has optional lightmap_uv_channel = 1
│
├── Lightmap Bake (offline, Phase 3)
│   ├── Scene → lightmass_input.fbx (with UV2)
│   ├── GPU Baker (DirectXMesh + compute shaders)
│   └── Output: lightmap_atlas.ktx2, sample_data.uzlightmap
│
├── Runtime Lightmap Sampling
│   ├── GBuffer includes lightmap_uv
│   ├── Lighting pass: albedo × lightmap_color
│   └── Integrates with DDGI for dynamic updates
│
├── DDGI (Dynamic Global Illumination)
│   ├── DDGIVolume components (grid of irradiance probes)
│   ├── Each frame: dispatch rays from each probe
│   └── Update irradiance via circular buffers
│
├── Reflection Captures
│   ├── CReflectionCapture components (manually placed)
│   ├── Static bake (offline) or dynamic capture
│   └── Blend by distance for glossy surfaces
│
└── Shader Integration
    ├── PBR shader reads: lightmap + DDGI + reflection
    └── Ambient = lightmap × diffuse + reflection × specular
```

### Why Hybrid?

- **Lightmaps alone** → Quality but inflexible (static only)
- **DDGI alone** → Flexible but expensive (ray dispatch per frame)
- **Hybrid** → Best of both: static quality + dynamic flexibility

Key insight: Most scenes have **80% static geometry** (terrain, architecture) and **20% dynamic** (characters, movables). Lightmap the static, DDGI the dynamic.

---

## Lightmap System

### Overview

Lightmaps are pre-baked texture atlases containing indirect illumination color per surface. Baking is **offline** but can be re-triggered in editor via IPC command.

### Lightmap Asset Format (.uzlightmap)

```
┌─ Header ────────────────────────────────┐
│ Magic              4 bytes "UZLM"       │
│ Version            2 bytes (1)          │
│ Format             1 byte (0=RGBA)      │
│ Compression        1 byte (0=none, 1=zstd)
│ Atlas Width        4 bytes (pixels)     │
│ Atlas Height       4 bytes (pixels)     │
│ Probe Count        4 bytes (for DDGI)   │
│ Irradiance Res     1 byte (8x8 or 16x16)
│ Reserved           padding to 64 bytes  │
├─ Texture Data ──────────────────────────┤
│ Atlas RGBA         width × height × 4B  │
│ (compressed if compression=1)           │
├─ Per-Texel Data ────────────────────────┤
│ For each texel:                         │
│ - Sample count (uint16)                 │
│ - Weighted direction (3×int8)           │
│ - Reserved (1 byte padding)             │
└─────────────────────────────────────────┘
```

### Lightmap UVs

Each mesh requiring lightmaps must have a second UV channel (UV2) with **non-overlapping, [0,1] normalized coordinates**. This is created during modeling or via UV baker tools.

```cpp
// In scene node:
struct StaticMeshComponent {
    Handle<Mesh> mesh;
    uint32_t lightmap_uv_channel = 1;  // Use UV2 for lightmap
    Handle<Lightmap> lightmap;         // Reference to .uzlightmap
};
```

### Baking Pipeline

Baking is **offline** and triggered via editor IPC or CI/CD.

```cpp
// Editor command: baker.bake_scene(scene_path)
//
// 1. Load scene geometry (static nodes only)
// 2. Export to intermediate format (FBX with UV2)
// 3. Run lightmass baker (compute shader based)
// 4. Produce lightmap texture + per-texel metadata
// 5. Import as .uzlightmap asset
// 6. Update scene nodes with lightmap references

struct LightmassConfig {
    uint32_t atlas_width = 4096;
    uint32_t atlas_height = 4096;
    uint32_t bounces = 4;              // Light bounces
    uint32_t samples_per_texel = 64;   // Quality (time ∝ samples²)
    float texel_density = 2.0f;        // Texels per meter
    std::string output_path = "res://lightmaps/";
};

class LightmassBaker {
public:
    Result bake_scene(
        const Scene& scene,
        const LightmassConfig& config,
        std::string& out_lightmap_asset_path
    );

private:
    // GPU-accelerated baker
    void dispatch_radiance_traces(ID3D12GraphicsCommandList* cmd_list);
    void filter_and_pack_lightmap();
};
```

### In-Shader Lightmap Sampling

The PBR shader samples the lightmap for base indirect lighting.

```hlsl
// In fragment shader
Texture2D<float4> lightmap_atlas : register(t10);
SamplerState lightmap_sampler : register(s1);

float3 sample_lightmap(float2 lightmap_uv) {
    // Lightmap already contains color × irradiance
    // No additional multiplication needed
    return lightmap_atlas.Sample(lightmap_sampler, lightmap_uv).rgb;
}

float3 compute_indirect_diffuse(
    float3 base_color,
    float3 normal,
    float2 lightmap_uv,
    float3 ddgi_irradiance
) {
    // Static lightmap + dynamic DDGI
    float3 static_gi = sample_lightmap(lightmap_uv);
    float3 dynamic_gi = ddgi_irradiance;

    // Blend: use DDGI to modulate lightmap for dynamic changes
    // (e.g., moving light on static surface)
    float3 total = static_gi;

    // Optional: add dynamic DDGI bounce
    if (dynamic_light_needs_update) {
        total += dynamic_gi * 0.3f;  // 30% contribution from dynamic
    }

    return base_color * total;
}
```

---

## DDGI System (Dynamic Global Illumination)

### Overview

DDGI approximates real-time indirect lighting by placing a regular grid of **irradiance probes** throughout the scene. Each probe:

1. **Dispatches rays** into the scene (compute shader)
2. **Samples radiance** (hit point surface color)
3. **Accumulates irradiance** (weighted by ray direction)
4. **Updates next frame** via circular buffer (temporal smoothing)

**Key benefit**: Probes update every frame, so dynamic lights and moving geometry are captured in ~33 ms (1 frame).

### DDGIVolume Component

```cpp
// In scene node (.uzscene):
[DDGI_Volume]
position = (0, 0, 0)
scale = (20, 15, 20)           // Grid extents
probe_spacing = 2.0            // Distance between probes
irradiance_resolution = 16     // 16×16 irradiance per probe
distance_resolution = 16       // 16×16 distance per probe
max_rays_per_probe = 128       // Trade-off: quality vs speed
octahedral_encoding = true     // Compress via octahedral mapping

class DDGIVolumeComponent {
    // Grid layout
    Vec3 position;
    Vec3 scale;                    // World-space extent
    float probe_spacing = 2.0f;
    uint32_t probes_x, probes_y, probes_z;  // Computed from scale/spacing

    // Irradiance/distance rendering
    uint32_t irradiance_res = 16;  // per-probe resolution
    uint32_t distance_res = 16;
    uint32_t max_rays = 128;

    // GPU resources
    Handle<Texture> irradiance_texture;     // texture2d_array
    Handle<Texture> distance_texture;       // texture2d_array
    Handle<Texture> hit_offset_texture;     // For relocation

    // Runtime state
    std::vector<uint32_t> probe_frames;     // Frame update counter per probe
    std::vector<Vec3> probe_offsets;        // Small jitter for temporal variety
};
```

### DDGI Ray Dispatch (Compute Shader)

Each frame, for each probe in the volume:

```hlsl
// dispatch_ddgi_probes.hlsl

[numthreads(16, 16, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    uint probe_idx = dispatch_id.x + dispatch_id.y * grid_width;

    // Ray direction in octahedral space
    uint2 ray_uv = dispatch_id.xy;
    float2 oct_coord = ray_uv / float2(distance_res, distance_res);
    float3 ray_dir = octahedral_decode(oct_coord);

    // Probe world position
    Vec3 probe_pos = ddgi_volume.get_probe_position(probe_idx);

    // Ray march from probe
    RayHit hit = ray_march(probe_pos, ray_dir, max_ray_distance);

    // Sample radiance at hit point
    float3 radiance = float3(0, 0, 0);
    if (hit.is_valid()) {
        radiance = sample_gbuffer_color(hit.position, hit.normal);
    } else {
        // Sky radiance as fallback
        radiance = sample_sky(ray_dir);
    }

    // Store distance (for hysteresis/filtering)
    distance_texture[probe_idx][ray_uv] = float4(hit.distance, 0, 0, 0);

    // Accumulate into circular buffer
    // (temporal smoothing via exponential moving average)
}
```

### Irradiance Accumulation

Irradiance is computed from 128 rays per probe, stored in compact form.

```cpp
// Per-probe data structure
struct DDGIProbeData {
    // Octahedral irradiance map (16×16 or 32×32)
    // Each texel = float3 (R, G, B in sRGB)
    // Stored as texture2d_array[irradiance_res × irradiance_res × probe_count]

    // Octahedral distance map (for geometric validation)
    // Each texel = float (distance to hit surface)
    // Stored as texture2d_array[distance_res × distance_res × probe_count]

    // Update state
    uint32_t last_frame_updated;
    uint32_t frame_since_moved;  // For probe relocation
};

// Temporal blending:
// new_irradiance = lerp(old_irradiance, current_irradiance, alpha)
// alpha = 0.1f  (10% new, 90% old = very stable)
// Higher alpha = more responsive but noisier

// Filtering:
// Gaussian blur across neighboring probes to smooth discontinuities
```

### Probe Relocation

Probes can get stuck in geometry (inside walls, under floors). A **relocation system** moves them to nearby empty space.

```cpp
bool should_probe_relocate(uint32_t probe_idx) {
    // Sample distance texture: if most rays are very close,
    // probe is surrounded by geometry → relocate

    const auto& data = probe_data[probe_idx];
    uint32_t near_count = 0;

    for (uint32_t i = 0; i < max_rays; i++) {
        if (data.distances[i] < relocation_threshold) {
            near_count++;
        }
    }

    return near_count > (max_rays * 0.8f);  // >80% blocked
}

Vec3 compute_probe_offset(uint32_t probe_idx) {
    // Move probe in direction of least obstruction
    // Use distance map to find open space

    Vec3 offset = Vec3(0, 0, 0);
    for (uint32_t i = 0; i < max_rays; i++) {
        float distance = probe_data[probe_idx].distances[i];
        Vec3 ray_dir = ...; // from octahedral coords
        offset += ray_dir * (distance - avg_distance);
    }

    return normalize(offset) * relocation_amount;
}
```

### Shader Integration: DDGI Sampling

In the lighting pass, sample irradiance from the nearest probes.

```hlsl
// In PBR fragment shader

float3 sample_ddgi(Vec3 world_pos, Vec3 normal) {
    // Find 8 nearest probes (trilinear interpolation)
    DDGIVolume volume = get_ddgi_volume();
    uint3 probe_coords = volume.world_to_grid(world_pos);

    float3 ddgi_irradiance = float3(0, 0, 0);
    float total_weight = 0;

    // Blend 8 neighboring probes
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++) {
            for (int z = 0; z < 2; z++) {
                uint3 neighbor = probe_coords + uint3(x, y, z);
                if (volume.is_valid_probe(neighbor)) {
                    Vec3 probe_pos = volume.grid_to_world(neighbor);

                    // Trilinear weight based on distance from world_pos
                    float weight = calculate_trilinear_weight(world_pos, probe_pos);

                    // Check visibility (backface) to avoid light leak
                    bool is_visible = check_probe_backface(world_pos, neighbor);
                    weight *= is_visible ? 1.0f : 0.1f;  // Penalize backfacing

                    // Sample octahedral irradiance map
                    float3 probe_irradiance = sample_probe_irradiance(
                        volume,
                        neighbor,
                        normal  // Use normal to sample toward-looking rays
                    );

                    ddgi_irradiance += probe_irradiance * weight;
                    total_weight += weight;
                }
            }
        }
    }

    return ddgi_irradiance / max(total_weight, 0.001f);
}
```

### Performance & Tuning

| Setting | Value | GPU Cost | Quality |
|---------|-------|----------|---------|
| **Max rays/probe** | 64 | 1.2 ms | Good |
| | 128 | 2.0 ms | Great |
| | 256 | 4.0 ms | Excellent |
| **Probe grid density** | 2m spacing | 1.5 ms | Smooth |
| | 4m spacing | 0.5 ms | Visible bands |
| **Irradiance resolution** | 8×8 | 0.3 ms | Blocky |
| | 16×16 | 1.0 ms | Good |
| | 32×32 | 3.5 ms | Excellent |

**Recommended for 60 FPS**: 128 rays, 2m spacing, 16×16 irradiance → ~2.5 ms/frame.

---

## Reflection Captures

### Overview

Reflection Captures are **cubemap snapshots** of the scene that provide **specular reflections** for glossy surfaces. They blend by distance and are manually placed or auto-generated.

### CReflectionCapture Component

```cpp
// In scene node (.uzscene):
[Reflection_Capture]
position = (0, 5, 0)
radius = 20.0              // Blend radius
intensity = 1.2            // Brightness multiplier
type = "Stationary"        // Baked once or auto-update

class CReflectionCapture {
    Vec3 position;
    float influence_radius = 20.0f;
    float intensity = 1.0f;

    enum class Type { Stationary, Movable, Realtime };
    Type type = Type::Stationary;

    // GPU resources
    Handle<TextureCube> cubemap;        // 512×512 or 1024×1024
    Handle<Texture> spherical_harmonic; // SH coefficients (optional)
};
```

### Capture Baking (Offline)

```cpp
// Editor command: baker.bake_reflection_capture(capture_id)
//
// 1. Render scene from capture position in all 6 directions
// 2. Post-process: downsample, filter, compress
// 3. Encode as cubemap texture
// 4. Optionally compute SH coefficients for diffuse

struct ReflectionCaptureBaker {
    Handle<TextureCube> bake_capture(
        const CReflectionCapture& capture,
        const Scene& scene,
        uint32_t cubemap_size = 512
    );
};
```

### Shader Integration

In the PBR shader, sample specular reflections from captures.

```hlsl
// In PBR fragment shader

float3 sample_reflection_capture(
    Vec3 world_pos,
    Vec3 normal,
    Vec3 view_dir,
    float roughness
) {
    // Find nearest 2-3 reflection captures
    std::vector<CReflectionCapture> captures = find_nearest_captures(world_pos, 3);

    float3 reflection = float3(0, 0, 0);
    float total_weight = 0;

    for (const auto& capture : captures) {
        // Reflect view direction across normal
        Vec3 reflect_dir = reflect(-view_dir, normal);

        // Sample cubemap with roughness mip level
        float mip = roughness * max_mips;
        float3 capture_color = capture.cubemap.SampleLevel(
            capture_sampler,
            reflect_dir,
            mip
        );

        // Distance-based blending
        float distance = length(world_pos - capture.position);
        float weight = 1.0f / (1.0f + (distance / capture.influence_radius) * 2.0f);
        weight = saturate(weight);

        reflection += capture_color * capture.intensity * weight;
        total_weight += weight;
    }

    return reflection / max(total_weight, 0.001f);
}
```

---

## Shader Integration Summary

The final ambient term combines all three GI techniques:

```hlsl
float3 compute_ambient(
    float3 base_color,
    float3 normal,
    Vec3 world_pos,
    float2 lightmap_uv,
    float roughness
) {
    // Diffuse indirect (from lightmap + DDGI)
    float3 indirect_diffuse = float3(0, 0, 0);
    indirect_diffuse += sample_lightmap(lightmap_uv);           // Static
    indirect_diffuse += sample_ddgi(world_pos, normal);         // Dynamic

    // Specular indirect (from reflection captures)
    float3 indirect_specular = sample_reflection_capture(world_pos, normal, view_dir, roughness);

    // Final ambient
    float3 ambient = base_color * indirect_diffuse + indirect_specular;

    return ambient;
}
```

---

## Integration with Related Systems

### Render Graph

DDGI ray dispatch is a compute pass in the render graph:

```cpp
RgPass& ddgi_pass = graph.add_compute_pass("ddgi_probes");
ddgi_pass.writes_textures = {ddgi_irradiance, ddgi_distance};
ddgi_pass.execute = [this](RgPassContext& ctx) {
    dispatch_ddgi_probes(ctx);
};
```

### Animation & Character Lighting

Moving characters should also affect DDGI. Use a simplified approach:
- Place temporary DDGI probes near dynamic objects
- Update once per 5 frames (lower frequency)
- Or use ambient occlusion as a fallback

### Baking Workflow

1. **Scene authoring**: Place DDGIVolume and ReflectionCaptures
2. **Lightmap bake**: (offline, Phase 3) Export scene → bake → import
3. **Play/Preview**: Probes update in real-time
4. **Ship**: Lightmaps are fixed, DDGI probes in ship binary (optional: freeze probes for determinism)

### Related Docs

- [Renderer Architecture](../engine/lifecycle-and-concurrency.md) — Frame lifecycle, GPU sync
- [Render Graph](render-graph.md) — DDGI pass integration, resource management
- [Shader System](shader-system.md) — SH coefficients, octahedral encoding
- [Screen-Space Effects](screen-space-effects.md) — Ambient occlusion, SSR complement
- [Asset Pipeline](asset-pipeline.md) — Lightmap asset import/export

---

## Test Coverage

### Unit Tests

```cpp
TEST(DDGI, ProbeRelocationDetectsStuckProbes) {
    DDGIVolumeComponent volume;
    volume.probes_x = 2;
    volume.probes_y = 2;
    volume.probes_z = 2;

    // Simulate probe surrounded by geometry (all distances near)
    for (uint32_t i = 0; i < volume.max_rays; i++) {
        volume.probe_data[0].distances[i] = 0.1f;  // Very close
    }

    EXPECT_TRUE(volume.should_probe_relocate(0));
}

TEST(GI, LightmapUVsAreNormalized) {
    Mesh mesh = load_test_mesh("models/test_box.fbx");
    const auto& uv2_channel = mesh.uv_channels[1];

    for (const auto& uv : uv2_channel) {
        EXPECT_GE(uv.x, 0.0f);
        EXPECT_LE(uv.x, 1.0f);
        EXPECT_GE(uv.y, 0.0f);
        EXPECT_LE(uv.y, 1.0f);
    }
}

TEST(ReflectionCapture, BlendsByDistance) {
    Vec3 world_pos = Vec3(0, 0, 0);

    CReflectionCapture capture_near;
    capture_near.position = Vec3(5, 0, 0);
    capture_near.influence_radius = 10.0f;

    CReflectionCapture capture_far;
    capture_far.position = Vec3(50, 0, 0);
    capture_far.influence_radius = 10.0f;

    float weight_near = compute_capture_weight(world_pos, capture_near);
    float weight_far = compute_capture_weight(world_pos, capture_far);

    EXPECT_GT(weight_near, weight_far);
}
```

### Integration Tests

```cpp
TEST(GI, DDGIProbesUpdateEachFrame) {
    Scene scene = create_test_scene_with_ddgi();

    for (int frame = 0; frame < 10; frame++) {
        scene.update(delta_time);
        scene.render(renderer);

        // Verify probes updated
        const auto& ddgi_volume = scene.get_component<DDGIVolumeComponent>(ddgi_entity);
        for (uint32_t i = 0; i < ddgi_volume.probe_count(); i++) {
            EXPECT_EQ(ddgi_volume.probe_frames[i], frame + 1);
        }
    }
}

TEST(GI, ReflectionCaptureAffectsGlossySurfaces) {
    // Place glossy sphere in scene
    // Place reflection capture with bright color
    // Render and verify sphere reflects capture color

    Scene scene = create_test_scene();
    CReflectionCapture capture = scene.get_component<CReflectionCapture>(capture_entity);

    // Bake capture
    TextureCube cubemap = baker.bake_capture(capture, scene);

    // Sample from sphere (glossy material)
    float3 reflection_color = sample_reflection_from_glossy_sphere(scene, cubemap);

    // Should be non-zero and match capture
    EXPECT_GT(length(reflection_color), 0.1f);
}
```

---

## Performance Profile

### Memory

| Component | Size | Notes |
|-----------|------|-------|
| Lightmap atlas (4K×4K RGBA) | 64 MB | Per lightmap |
| DDGI irradiance (16×16 per 100 probes) | ~12 MB | Texture2DArray |
| DDGI distance (16×16 per 100 probes) | ~12 MB | Texture2DArray |
| Reflection capture cubemap (512³) | 6 MB | Per capture |
| SH coefficients per capture | 0.2 MB | Optional |

### GPU Time (per frame, 1920×1080)

| Operation | Time | Notes |
|-----------|------|-------|
| DDGI ray dispatch (128 rays, 256 probes) | 2.5 ms | Compute |
| Irradiance filter & bake | 0.8 ms | Compute |
| Lightmap sampling in lighting pass | 0 ms | Free (texture sample) |
| Reflection capture sampling | 0.3 ms | 2-3 captures per pixel |
| **Total** | ~3.6 ms | At 60 FPS = 6% budget |

---

## Phase Roadmap

### Phase 2 (Planned)

- Core DDGI probe system (regular grid layout)
- Ray dispatch compute shader
- Irradiance accumulation & temporal smoothing
- Reflection capture baking and sampling
- Shader integration (PBR + GI)
- Debug visualization (probe positions, ray directions)

### Phase 3+ (Future)

- Adaptive probe placement (denser in complex areas)
- Lightmap baking pipeline (offline tool)
- Real-time lightmap baking (slower but update-friendly)
- Super resolution DDGI (AI-upscale low-res probes)
- Cone tracing (replace ray marching for faster probes)
- World-space probe caching (cache across frames)

---

## Related Docs

- [Render Graph](render-graph.md) — DDGI ray dispatch as render pass
- [Shader Library](shader-library.md) — Octahedral encoding, SH coefficients
- [Asset Pipeline](asset-pipeline.md) — Lightmap asset format and import
- [Screen-Space Effects](screen-space-effects.md) — Ambient occlusion complement
- [Design Decisions](../engine/design-decisions.md) — Why hybrid GI vs. pure real-time ray tracing
