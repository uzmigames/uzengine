# Global Illumination System & Editor Tools

> **Tool Category**: Rendering / Lighting Authoring
> **Phase**: 3-4
> **Depends on**: renderer, shader-system, shader-library, screen-space-effects, point-spot-light-shadows, editor-panels, undo-redo, asset-pipeline
> **File Formats**: `.uzgi`, `.uzlightmap`, `.uzreflection`
> **Spec References**: [renderer.md](../specs/renderer.md), [screen-space-effects.md](../specs/screen-space-effects.md), [shader-library.md](../specs/shader-library.md)

---

## 1. Overview & GI Strategies

UzEngine provides three Global Illumination modes, selectable per-project in `project.uzproj` and overridable per-scene in the `.uzgi` configuration file. Each mode offers a different balance between visual quality, dynamism, and performance cost.

### GI Mode Summary

```
+-------------------+------------------+------------------+-------------------+
|                   | Dynamic GI       | Baked GI         | Hybrid GI         |
|                   | (Lumen-inspired) | (Lightmaps)      | (Baked + Probes)  |
+-------------------+------------------+------------------+-------------------+
| Indirect light    | Radiance probes  | Precomputed      | Lightmaps (static)|
| source            | + SSIL           | lightmaps        | + Probes (dynamic)|
+-------------------+------------------+------------------+-------------------+
| Dynamic objects   | Full indirect    | Ambient cube     | Probe-interpolated|
|                   | from probes      | fallback only    | indirect light    |
+-------------------+------------------+------------------+-------------------+
| Time-of-day       | Fully dynamic    | Multiple baked   | Baked blendable + |
|                   |                  | scenarios        | probes auto-adapt |
+-------------------+------------------+------------------+-------------------+
| Emissive contrib. | Yes (injected)   | Baked only       | Baked + injected  |
+-------------------+------------------+------------------+-------------------+
| Bake required     | No               | Yes              | Yes (static only) |
+-------------------+------------------+------------------+-------------------+
| GPU cost (1080p)  | 3-6 ms/frame     | 0.2 ms/frame     | 1.5-3 ms/frame    |
+-------------------+------------------+------------------+-------------------+
| VRAM overhead     | 80-200 MB        | 10-100 MB        | 50-150 MB         |
+-------------------+------------------+------------------+-------------------+
```

### Hardware Requirements

| GI Mode | Minimum GPU | Recommended GPU | DXR Required | Notes |
|---------|-------------|-----------------|--------------|-------|
| Dynamic GI | DX12 FL 12_1, 6 GB VRAM | RTX 3060 / RX 6700 XT | Optional (improves probe quality) | SDF fallback for non-DXR hardware |
| Baked GI | DX12 FL 12_0, 4 GB VRAM | Any DX12 GPU | No | Baking uses CPU (Embree); runtime is cheap |
| Hybrid GI | DX12 FL 12_1, 6 GB VRAM | RTX 3060 / RX 6700 XT | Optional | Probe grid smaller than full Dynamic |

### Quality vs Performance Tradeoffs

| Setting | GPU Cost | Visual Quality | Best For |
|---------|----------|----------------|----------|
| **Off** | 0 ms | No indirect lighting | Debugging, very low-end |
| **Low** (SSIL only) | 0.8 ms | Single-bounce screen-space | Low-end GPUs, fast iteration |
| **Medium** (probes, low-res) | 2.0 ms | Multi-bounce probes, no SSIL | Mid-range GPUs |
| **High** (probes + SSIL) | 4.0 ms | Full probes + screen-space detail | Recommended default |
| **Ultra** (probes + SSIL + reflections) | 6.0 ms | Full GI + dynamic reflection captures | High-end GPUs |

---

## 2. Dynamic GI -- Radiance Probe Grid

The core of UzEngine's dynamic GI is a world-space grid of radiance probes that store incoming irradiance encoded as second-order Spherical Harmonics (L2, 9 coefficients per color channel = 27 floats per probe). Probes are updated incrementally each frame using ray casting (DXR when available, SDF fallback otherwise) and interpolated trilinearly for shading.

### Probe Grid Architecture

```
World-Space Probe Grid (top-down view, 2m spacing example):

      0m    2m    4m    6m    8m   10m   12m   14m   16m
      |     |     |     |     |     |     |     |     |
0m  --o-----o-----o-----o-----o-----o-----o-----o-----o--
      |     |     |     |     |     |     |     |     |
2m  --o-----o-----o-----o-----o-----o-----o-----o-----o--
      |     |     |     |     |     |     |     |     |
4m  --o-----o-----o-----o-----o-----o-----o-----o-----o--
      |     |     |     |     |     |     |     |     |
6m  --o-----o-----o-----o-----o-----o-----o-----o-----o--

Each 'o' is a radiance probe storing L2 SH (9 coefficients x RGB = 27 floats).
Probes also exist in the vertical axis (Y) at the configured spacing.

Side view (4 vertical layers at 2m spacing):

Y=6m  --o-----o-----o-----o-----o--
         |     |     |     |     |
Y=4m  --o-----o-----o-----o-----o--
         |     |     |     |     |
Y=2m  --o-----o-----o-----o-----o--
         |     |     |     |     |
Y=0m  --o-----o-----o-----o-----o--
         |     |     |     |     |
         X=0   X=2   X=4   X=6   X=8
```

### Configuration

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `grid_dimensions` | uint3 | `64, 16, 64` | 8-128 per axis | Number of probes along X, Y, Z |
| `probe_spacing` | float | `2.0` | 0.5 - 10.0 | World-space distance between probes (meters) |
| `grid_origin` | float3 | `auto` | - | World-space position of probe [0,0,0]; auto = scene AABB center minus half-extent |
| `rays_per_probe` | uint | `64` | 16 - 256 | Rays cast from each probe per update |
| `max_ray_distance` | float | `50.0` | 5.0 - 200.0 | Maximum ray travel distance (meters) |
| `hysteresis` | float | `0.97` | 0.8 - 0.99 | Temporal blend factor (higher = more stable, slower adaptation) |
| `update_budget` | uint | `2048` | 256 - 16384 | Maximum probes updated per frame |
| `irradiance_scale` | float | `1.0` | 0.0 - 5.0 | Global multiplier on probe irradiance output |

With default settings (64x16x64 grid), the total probe count is 65,536, covering a 128m x 32m x 128m world volume.

### Spherical Harmonics Storage

Each probe stores L2 SH coefficients (bands 0, 1, 2) for RGB irradiance:

```
Band 0 (L=0): 1 coefficient  -- DC (average irradiance)
Band 1 (L=1): 3 coefficients -- directional gradient (X, Y, Z)
Band 2 (L=2): 5 coefficients -- quadratic detail
                                --------
                                9 total coefficients per color channel
                                x 3 channels (RGB)
                                = 27 floats per probe
                                = 108 bytes per probe (float32)
                                = 54 bytes per probe (float16, used for GPU storage)
```

### GPU Storage Layout

```
ProbeIrradiance: StructuredBuffer<SHProbe>

struct SHProbe {
    half3 sh[9];   // 9 SH coefficients, each RGB half-precision
    // Total: 9 * 6 bytes = 54 bytes per probe
    // Padded to 64 bytes for alignment
    half  validity; // 0.0 = invalid (inside geometry), 1.0 = fully valid
    half3 _pad;
};

Total VRAM for 65,536 probes at 64 bytes each = 4 MB
```

### Probe Update Pipeline

Each frame, a subset of probes are updated. The update pipeline runs as async compute so it overlaps with the shadow and G-Buffer passes.

```
Probe Update Pipeline (per frame):

1. Select probes to update
   +-----------------------------+
   | Priority queue:             |
   |  - Camera-near probes first |
   |  - Recently invalidated     |
   |  - Round-robin remainder    |
   +-----------------------------+
              |
              v
2. Ray casting (compute / DXR)
   +-----------------------------+
   | For each selected probe:    |
   |  - Cast N rays (Fibonacci   |
   |    sphere distribution)     |
   |  - DXR: TraceRay() against  |
   |    TLAS (top-level AS)      |
   |  - SDF: March against SDF   |
   |    volume texture           |
   |  - Record hit radiance +    |
   |    hit distance             |
   +-----------------------------+
              |
              v
3. SH projection (compute)
   +-----------------------------+
   | For each probe:             |
   |  - Project ray results onto |
   |    L2 SH basis functions    |
   |  - Blend with previous SH   |
   |    using hysteresis factor  |
   |  - Store in ProbeIrradiance |
   +-----------------------------+
              |
              v
4. Probe validity pass (compute)
   +-----------------------------+
   | Mark probes inside geometry |
   | as invalid (validity = 0).  |
   | Use short rays in 6 axis   |
   | directions; if all hit at   |
   | < 0.2m, probe is embedded.  |
   +-----------------------------+
```

### Probe Interpolation (Sampling at Shading Time)

During the deferred lighting pass (or forward pass for transparent objects), each pixel samples the probe grid using trilinear interpolation between the 8 nearest probes:

```hlsl
// shaders/include/gi_probes.hlsli

#ifndef UZ_GI_PROBES_HLSLI
#define UZ_GI_PROBES_HLSLI

#include "common.hlsli"

struct SHProbe {
    half3 sh[9];
    half  validity;
    half3 _pad;
};

cbuffer GIProbeParams : register(b3) {
    float3 gi_grid_origin;     // world-space origin of probe [0,0,0]
    float  gi_probe_spacing;   // distance between probes (meters)
    uint3  gi_grid_dimensions; // number of probes per axis (X, Y, Z)
    float  gi_irradiance_scale;// global irradiance multiplier
};

StructuredBuffer<SHProbe> g_probe_buffer : register(t8);

// Convert world position to probe grid coordinates (fractional)
float3 world_to_probe_coords(float3 world_pos) {
    return (world_pos - gi_grid_origin) / gi_probe_spacing;
}

// Flatten 3D probe index to 1D buffer index
uint probe_index(uint3 idx) {
    return idx.x
         + idx.y * gi_grid_dimensions.x
         + idx.z * gi_grid_dimensions.x * gi_grid_dimensions.y;
}

// Evaluate L2 SH irradiance for a given normal direction
// SH basis evaluation order: Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22
float3 evaluate_sh_irradiance(SHProbe probe, float3 N) {
    // L0 band (constant)
    float3 result = probe.sh[0] * 0.282095;

    // L1 band (linear)
    result += probe.sh[1] * 0.488603 * N.y;
    result += probe.sh[2] * 0.488603 * N.z;
    result += probe.sh[3] * 0.488603 * N.x;

    // L2 band (quadratic)
    result += probe.sh[4] * 1.092548 * N.x * N.y;
    result += probe.sh[5] * 1.092548 * N.y * N.z;
    result += probe.sh[6] * 0.315392 * (3.0 * N.z * N.z - 1.0);
    result += probe.sh[7] * 1.092548 * N.x * N.z;
    result += probe.sh[8] * 0.546274 * (N.x * N.x - N.y * N.y);

    return max(result, 0.0);
}

// Sample probe grid with trilinear interpolation + validity weighting
float3 sample_probe_irradiance(float3 world_pos, float3 normal) {
    float3 coords = world_to_probe_coords(world_pos);

    // Clamp to grid bounds
    coords = clamp(coords, 0.0, float3(gi_grid_dimensions) - 1.0);

    // Integer base index and fractional offset
    uint3  base = uint3(floor(coords));
    float3 frac_coords = coords - float3(base);

    // Clamp base so base+1 is still in bounds
    base = min(base, gi_grid_dimensions - 2);

    // Trilinear interpolation over 8 neighboring probes
    float3 total_irradiance = float3(0, 0, 0);
    float  total_weight = 0.0;

    [unroll]
    for (uint z = 0; z <= 1; ++z) {
        [unroll]
        for (uint y = 0; y <= 1; ++y) {
            [unroll]
            for (uint x = 0; x <= 1; ++x) {
                uint3 offset = uint3(x, y, z);
                uint  idx = probe_index(base + offset);
                SHProbe probe = g_probe_buffer[idx];

                // Skip invalid probes (inside geometry)
                if (probe.validity < 0.01) continue;

                // Trilinear weight
                float3 w3 = lerp(1.0 - frac_coords, frac_coords, float3(offset));
                float weight = w3.x * w3.y * w3.z;

                // Backface rejection: reduce weight if probe is behind surface
                float3 probe_world = gi_grid_origin + float3(base + offset) * gi_probe_spacing;
                float3 to_probe = normalize(probe_world - world_pos);
                float backface = saturate(dot(to_probe, normal) * 0.5 + 0.5);
                weight *= backface;

                // Validity weighting
                weight *= probe.validity;

                total_irradiance += evaluate_sh_irradiance(probe, normal) * weight;
                total_weight += weight;
            }
        }
    }

    if (total_weight < EPSILON) {
        return float3(0, 0, 0);  // No valid probes nearby
    }

    return total_irradiance / total_weight * gi_irradiance_scale;
}

#endif // UZ_GI_PROBES_HLSLI
```

### Sky Light Contribution

When a probe ray misses all geometry (hits the sky), the sky radiance is sampled from the active sky cubemap (or procedural sky) and contributes to the probe's SH coefficients. This ensures outdoor probes naturally capture sky color and intensity without separate handling.

```
Probe ray hit logic:
  if (ray hits geometry)
      radiance = hit_surface.emissive + hit_surface.albedo * direct_light_at_hit
  else
      radiance = sample_sky_cubemap(ray_direction) * sky_intensity
```

### Emissive Contribution

Emissive materials contribute light to nearby probes. When a probe ray hits a surface with `emission > 0`, that emissive radiance is included in the probe's SH projection. This means emissive screens, lava, neon signs, and other glowing surfaces naturally illuminate their surroundings without any extra configuration.

The `emissive_injection_scale` parameter controls how strongly emissive materials affect probes (default 1.0; set to 0.0 to disable emissive GI contribution).

---

## 3. Dynamic GI -- Screen-Space Indirect Lighting (SSIL)

SSIL provides high-frequency indirect lighting detail that the coarse probe grid cannot capture. It works similarly to SSAO but instead of measuring occlusion, it gathers indirect color from nearby screen-space pixels. SSIL adds color bleeding: a red wall next to a white floor will tint the floor red.

### SSIL Pipeline

```
SSIL Pipeline (runs as compute after G-Buffer pass):

G-Buffer (depth, normal, albedo, emissive)
    |
    v
+-----------------------------+
| SSIL Ray Generation         |
| (compute, full-res or       |
|  half-res configurable)     |
|                             |
| For each pixel:             |
|  - Generate N rays in       |
|    hemisphere around normal |
|  - March each ray in screen |
|    space (Hi-Z acceleration)|
|  - At hit: read albedo *    |
|    direct_light from the    |
|    HDR color buffer          |
|  - Weight by cos(theta) /   |
|    distance^2               |
+-----------------------------+
    |
    v
+-----------------------------+
| Temporal Accumulation       |
|                             |
| Blend current SSIL result   |
| with previous frame using   |
| velocity buffer reprojection|
| Alpha = 0.9 (90% history)  |
+-----------------------------+
    |
    v
+-----------------------------+
| Spatial Denoiser            |
| (NRD-inspired)              |
|                             |
| - Edge-aware bilateral blur |
| - Depth + normal rejection  |
| - Variance-guided kernel    |
|   (wider blur where noisy)  |
+-----------------------------+
    |
    v
SSIL Output (RGBA16F): indirect color contribution
    |
    v
Added to ambient term in deferred lighting composite
```

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `enabled` | bool | `true` | - | Toggle SSIL |
| `quality` | enum | `Medium` | Low/Medium/High | Ray count: 1/4/8 rays per pixel |
| `max_distance` | float | `3.0` | 0.5 - 20.0 | Maximum screen-space march distance (meters) |
| `intensity` | float | `1.0` | 0.0 - 5.0 | Indirect color multiplier |
| `thickness` | float | `0.2` | 0.05 - 1.0 | Assumed surface thickness for hit validation (meters) |
| `bounce_approx` | uint | `1` | 1 - 3 | Multi-bounce approximation passes |
| `half_resolution` | bool | `true` | - | Render at half resolution for performance |
| `temporal_accumulation` | float | `0.9` | 0.5 - 0.98 | History blend factor |
| `denoiser_radius` | uint | `3` | 1 - 5 | Spatial denoiser kernel radius (in texels) |

### Quality Levels

| Level | Rays/Pixel | Resolution | Temporal Frames | Denoise | Approx Cost (1080p) |
|-------|-----------|------------|-----------------|---------|---------------------|
| Low | 1 | Half | 4 | Yes | ~0.5 ms |
| Medium | 4 | Half | 4 | Yes | ~1.2 ms |
| High | 8 | Full | 8 | Yes | ~2.5 ms |

### Multi-Bounce Approximation

True multi-bounce GI requires recursive ray tracing which is prohibitively expensive. Instead, SSIL approximates additional bounces by feeding the previous frame's indirect lighting result back as input. Each "bounce" iteration reads the accumulated indirect buffer and adds it to the current frame's gathering rays:

```
Bounce 1: Gather direct-lit color from screen neighbors
Bounce 2: Gather (direct + bounce1) color from screen neighbors
Bounce 3: Gather (direct + bounce1 + bounce2) color from screen neighbors
```

This is temporally stable because each frame only computes one iteration, using the history buffer for prior bounces.

### Integration with SSAO

SSIL and SSAO share similar screen-space traversal logic. When both are enabled, they run as a combined pass to avoid redundant depth/normal reads. The combined shader outputs both an occlusion scalar (R8) and an indirect color (RGB16F) from the same ray marches:

```hlsl
// Combined SSIL+SSAO output:
// If ray hits nearby geometry:
//   AO contribution: occlusion += 1
//   SSIL contribution: indirect_color += hit_color * cos_weight / dist^2
// If ray misses:
//   AO contribution: 0 (no occlusion in this direction)
//   SSIL contribution: 0 (nothing to gather)
```

---

## 4. Dynamic GI -- Reflection Captures

Reflection captures provide Image-Based Lighting (IBL) specular reflections using pre-filtered environment cubemaps. They complement screen-space reflections (SSR from `screen-space-effects.md`) by providing stable reflections for geometry not visible on screen.

### Capture Types

```
Reflection Capture Shapes:

Sphere Capture:                   Box Capture (Parallax-Corrected):

     +-------+                         +------------------+
    /         \                         |                  |
   /     o     \    <-- center          |        o         |  <-- center
  |    probe    |                       |      probe       |
   \           /                        |                  |
    \_________/                         +------------------+

  Blend radius = fade zone              Extent = half-size per axis
  outside the sphere                    Blend radius = inner margin

  Best for: outdoors, open spaces       Best for: rooms, corridors, boxes
```

### Capture Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `shape` | enum | `Sphere` | Sphere/Box | Capture volume shape |
| `position` | float3 | - | - | World-space center |
| `radius` | float | `10.0` | 1.0 - 500.0 | Sphere radius (meters); ignored for Box |
| `extent` | float3 | `[5,5,5]` | 0.5 - 100.0 | Box half-extents (meters); ignored for Sphere |
| `blend_radius` | float | `2.0` | 0.1 - 50.0 | Transition zone width (meters) |
| `priority` | int | `0` | -100 to 100 | Higher priority overrides lower (for overlaps) |
| `brightness` | float | `1.0` | 0.0 - 10.0 | Multiplier on captured radiance |
| `tint` | float3 | `[1,1,1]` | 0.0 - 2.0 | Color tint applied to capture |
| `resolution` | uint | `128` | 64 - 512 | Cubemap face resolution (pixels) |
| `update_mode` | enum | `Static` | Static/EveryN/OnChange | When to recapture |
| `update_interval` | uint | `30` | 1 - 300 | Frames between updates (for EveryN mode) |

### Parallax-Corrected Box Reflections

For indoor environments, box captures use parallax correction to produce accurate reflections that account for the room's geometry. The reflection vector is intersected with the box volume to find the correct lookup direction:

```hlsl
// shaders/include/reflection_capture.hlsli

float3 parallax_correct_box(float3 world_pos, float3 reflect_dir,
                            float3 box_center, float3 box_extent) {
    // Ray-box intersection: find where the reflection ray exits the box
    float3 box_min = box_center - box_extent;
    float3 box_max = box_center + box_extent;

    float3 first_plane  = (box_max - world_pos) / reflect_dir;
    float3 second_plane = (box_min - world_pos) / reflect_dir;

    float3 furthest = max(first_plane, second_plane);
    float t = min(min(furthest.x, furthest.y), furthest.z);

    // Intersection point on box surface
    float3 intersection = world_pos + reflect_dir * t;

    // New lookup direction: from capture center to intersection
    return normalize(intersection - box_center);
}
```

### Capture Priority and Blending

When multiple captures overlap, the system resolves by priority and distance:

```
Capture Blend Logic:

1. Find all captures whose volume contains the shading point
2. Sort by priority (descending), then by distance to center (ascending)
3. Highest priority capture gets full weight
4. Lower-priority captures blend in the fade zone:
   weight = 1.0 - smoothstep(inner_radius, outer_radius, distance)
5. Normalize weights so they sum to 1.0
6. Maximum 4 captures blended per pixel (for performance)
```

### Sky IBL Fallback

When no reflection capture covers a given pixel, the system falls back to the global sky IBL cubemap. This cubemap is generated from the active sky source (HDRI texture or procedural sky) and provides a baseline specular environment.

```
Reflection Lookup Order:
  1. SSR hit? Use screen-space reflection color.
  2. Reflection capture available? Use parallax-corrected cubemap.
  3. Neither? Fall back to sky IBL cubemap.

Blending: SSR confidence masks the transition.
  final_reflection = lerp(capture_or_sky_reflection, ssr_color, ssr_confidence)
```

### Capture Storage

Captured cubemaps are stored as pre-filtered mip chains for roughness-based sampling (split-sum approximation). Each mip level corresponds to a roughness value:

| Mip Level | Roughness | Description |
|-----------|-----------|-------------|
| 0 | 0.0 | Mirror-sharp reflection |
| 1 | 0.125 | Nearly sharp |
| 2 | 0.25 | Slight blur |
| 3 | 0.5 | Moderate blur |
| 4 | 0.75 | Heavy blur |
| 5 | 1.0 | Fully diffuse (nearly Lambertian) |

Format: `RGBA16F` per face, 6 faces, mip chain. At 128x128 per face with 6 mips: approximately 1.2 MB per capture.

---

## 5. Baked GI -- Lightmap System

For scenes with mostly static geometry and lighting, baked lightmaps provide the highest quality indirect illumination at the lowest runtime cost. The baking process runs a CPU path tracer (powered by Intel Embree) to compute indirect illumination and stores the result as HDR textures mapped onto mesh surfaces.

### Lightmap UV Channel (UV2)

Every static mesh that participates in lightmap baking requires a second UV set (UV2) dedicated to lightmap layout. This UV channel must:

- Be non-overlapping (no two triangles share the same UV space)
- Have consistent texel density across the mesh surface
- Pack efficiently within the [0,1] UV square
- Maintain a 2-texel gutter between islands to prevent bleeding

```
UV2 Lightmap Layout Example (a simple room):

+---+-------+---+
|   |       |   |     Floor
|   | floor |   |     Wall A (left)
|   |       |   |     Wall B (right)
+---+-------+---+     Wall C (back)
| A |  C    | B |     Ceiling
+---+-------+---+
|   ceiling     |
+---------------+

Each polygon is unwrapped into its own island
with 2-texel gutters between islands.
```

UV2 generation can be:
- **Automatic**: The asset pipeline runs an auto-unwrap (xatlas library) during mesh import. This is the default for most assets.
- **Manual**: Artists provide UV2 in the DCC tool (Blender, Maya). The asset pipeline validates non-overlap.

### Lightmap Textures

| Property | Value |
|----------|-------|
| Format | `BC6H_UF16` (HDR compressed, unsigned float16) |
| Resolution | 64 to 4096 per mesh (configurable per-mesh) |
| Default | 256 (1 texel per ~4cm at typical prop scale) |
| Channels | RGB indirect irradiance |
| Storage | `project/lightmaps/{scene_name}/{mesh_id}.uzlightmap` |

### Baking Process

```
Lightmap Bake Pipeline:

1. Scene Preparation
   +-----------------------------+
   | Collect all static meshes   |
   | + all light sources         |
   | Build Embree BVH from scene |
   | geometry (CPU, ~2s typical) |
   +-----------------------------+
              |
              v
2. UV2 Validation
   +-----------------------------+
   | For each mesh:              |
   |  - Check UV2 exists         |
   |  - Validate no overlap      |
   |  - Flag issues (red overlay |
   |    in editor)               |
   +-----------------------------+
              |
              v
3. Texel Rasterization
   +-----------------------------+
   | For each lightmap texel:    |
   |  - Compute world position   |
   |    + normal from UV2 mapping|
   |  - Create a hemisphere of   |
   |    sample directions        |
   +-----------------------------+
              |
              v
4. Path Tracing (Embree)
   +-----------------------------+
   | For each texel sample:      |
   |  - Cast cosine-weighted     |
   |    hemisphere rays          |
   |  - Trace N bounces (3-8)    |
   |  - Accumulate radiance      |
   |  - Apply Russian roulette   |
   |    after bounce 3           |
   | Samples per texel:          |
   |  Preview=16, Medium=64      |
   |  High=256, Production=1024  |
   +-----------------------------+
              |
              v
5. Denoising + Dilation
   +-----------------------------+
   | - Bilateral denoise pass    |
   | - Dilate lightmap borders   |
   |   (fill gutter texels) to   |
   |   prevent black seams       |
   +-----------------------------+
              |
              v
6. Compression + Storage
   +-----------------------------+
   | - Compress to BC6H          |
   | - Write .uzlightmap files   |
   | - Update scene manifest     |
   +-----------------------------+
```

### Lightmap Compression: BC6H

BC6H is chosen for lightmap storage because:
- Lossless quality at 16-bit HDR precision (sufficient for irradiance data)
- 6:1 compression ratio (1 byte/texel vs 6 bytes/texel for raw RGBA16F)
- Hardware-decoded at texture sample time (zero CPU cost)
- Widely supported on all DX12 hardware (Feature Level 11_0+)

### Lighting Scenarios

Multiple baked lighting sets can be stored for the same scene, enabling time-of-day or conditional lighting switches:

```ini
# Stored in scene's .uzgi file
[lightmap_scenarios]
count = 3

[scenario.day]
lightmap_path = lightmaps/village/day/
sun_direction = [0.5, -0.7, 0.3]
sun_color = [1.0, 0.95, 0.85]
sun_intensity = 3.0

[scenario.sunset]
lightmap_path = lightmaps/village/sunset/
sun_direction = [0.1, -0.3, 0.8]
sun_color = [1.0, 0.6, 0.3]
sun_intensity = 1.5

[scenario.night]
lightmap_path = lightmaps/village/night/
sun_direction = [0, -1, 0]
sun_color = [0.1, 0.1, 0.3]
sun_intensity = 0.05
```

At runtime, two scenarios can be blended with a float weight (0.0 = scenario A, 1.0 = scenario B). This enables smooth time-of-day transitions by interpolating between two baked lightmap sets:

```hlsl
// Runtime lightmap sampling with scenario blend
float3 lightmap_a = g_lightmap_a.Sample(sampler_linear_clamp, uv2).rgb;
float3 lightmap_b = g_lightmap_b.Sample(sampler_linear_clamp, uv2).rgb;
float3 indirect = lerp(lightmap_a, lightmap_b, scenario_blend_weight);
```

### Invalidation and Dirty Flags

When a static mesh or light source is modified in the editor, the affected lightmap regions are marked dirty:

| Change | Invalidation Scope |
|--------|-------------------|
| Static mesh moved/scaled | That mesh's lightmap + nearby meshes within 2x light bounce radius |
| Light position/color changed | All meshes within the light's influence radius |
| Light added/removed | All meshes within the light's influence radius |
| Material albedo changed | That mesh's lightmap (affects bounce color) |
| Mesh geometry changed | That mesh's lightmap (UV2 must be regenerated) |

The editor shows a yellow warning badge on meshes with stale lightmaps and prompts the user to rebake.

---

## 6. Editor Tools -- GI Configuration Panel

The GI Configuration Panel is a dedicated section within the Lighting panel of the VSCode editor. It provides scene-wide controls for the active GI mode and its parameters.

### Panel Layout

```
+-----------------------------------------------------------------------+
|  LIGHTING PANEL > GLOBAL ILLUMINATION                                 |
+-----------------------------------------------------------------------+
|                                                                       |
|  GI Mode:  ( ) Off  (*) Dynamic  ( ) Baked  ( ) Hybrid               |
|                                                                       |
|  --- Dynamic GI Settings ---                                          |
|                                                                       |
|  Probe Grid                                                           |
|    Grid Dimensions:  X [64] Y [16] Z [64]                            |
|    Probe Spacing:    [===2.0m====]                                    |
|    Grid Origin:      [auto      ] [Reset]                             |
|    Rays Per Probe:   [64        ]                                     |
|    Max Ray Distance: [50.0m     ]                                     |
|    Hysteresis:       [===0.97===]                                     |
|    Update Budget:    [2048 /frame]                                    |
|                                                                       |
|  Screen-Space Indirect (SSIL)                                         |
|    Enabled:          [x]                                              |
|    Quality:          [Medium v]                                       |
|    Max Distance:     [===3.0m===]                                     |
|    Intensity:        [===1.0====]                                     |
|    Half Resolution:  [x]                                              |
|    Bounce Approx:    [1        ]                                      |
|                                                                       |
|  Global Multipliers                                                   |
|    Indirect Intensity:     [===1.0====]                               |
|    Sky Light Intensity:    [===1.0====]                               |
|    Emissive Injection:     [===1.0====]                               |
|                                                                       |
|  [Show Probes] [Show Coverage] [Freeze Probes] [Reset All]           |
|                                                                       |
+-----------------------------------------------------------------------+
```

### IPC Handlers (gi.*)

```json
gi.setMode          { "mode": "dynamic" }
                --> { "ok": true }

gi.getConfig        {}
                --> {
                      "mode": "dynamic",
                      "probe_grid": {
                        "dimensions": [64, 16, 64],
                        "spacing": 2.0,
                        "origin": [-64, 0, -64],
                        "rays_per_probe": 64,
                        "max_ray_distance": 50.0,
                        "hysteresis": 0.97,
                        "update_budget": 2048
                      },
                      "ssil": {
                        "enabled": true,
                        "quality": "Medium",
                        "max_distance": 3.0,
                        "intensity": 1.0,
                        "half_resolution": true,
                        "bounce_approx": 1
                      },
                      "indirect_intensity": 1.0,
                      "sky_intensity": 1.0,
                      "emissive_injection_scale": 1.0
                    }

gi.setProbeGrid     { "dimensions": [32, 8, 32], "spacing": 4.0 }
                --> { "ok": true, "total_probes": 8192 }

gi.setSSIL          { "quality": "High", "intensity": 1.5 }
                --> { "ok": true }

gi.freezeProbes     { "frozen": true }
                --> { "ok": true }

gi.resetProbes      {}
                --> { "ok": true, "cleared": 65536 }

gi.getStats         {}
                --> {
                      "total_probes": 65536,
                      "valid_probes": 58240,
                      "probes_updated_this_frame": 2048,
                      "ssil_cost_ms": 1.2,
                      "probe_update_cost_ms": 1.8,
                      "total_gi_cost_ms": 3.0,
                      "reflection_captures": 5,
                      "active_lightmap_scenario": "day"
                    }
```

---

## 7. Editor Tools -- Lightmap Baking

The Lightmap Bake Panel provides the full workflow for authoring, baking, and inspecting lightmaps for scenes using Baked or Hybrid GI.

### Bake Panel Layout

```
+-----------------------------------------------------------------------+
|  LIGHTING PANEL > LIGHTMAP BAKING                                     |
+-----------------------------------------------------------------------+
|                                                                       |
|  Quality Preset:  ( ) Preview  (*) Medium  ( ) High  ( ) Production  |
|                                                                       |
|  --- Static Meshes ---                                                |
|  +---+--------------------+--------+--------+---------+               |
|  |   | Mesh               | LM Res | Status | UV2     |               |
|  +---+--------------------+--------+--------+---------+               |
|  | x | Floor_01           |  512   | Stale  | OK      |               |
|  | x | Wall_North         |  256   | Stale  | OK      |               |
|  | x | Wall_East          |  256   | Baked  | OK      |               |
|  | x | Ceiling_01         |  256   | Stale  | OK      |               |
|  |   | Barrel_03 (dynamic)|  ---   | N/A    | ---     |               |
|  | x | Column_A           |  128   | Stale  | OVERLAP |               |
|  | x | Terrain_Tile_0_0   | 1024   | Baked  | OK      |               |
|  +---+--------------------+--------+--------+---------+               |
|                                                                       |
|  [x] Select All Static  Total texels: 2,359,296                      |
|                                                                       |
|  --- Bake Controls ---                                                |
|  [Bake All]  [Bake Selected]  [Preview Bake]  [Cancel]               |
|                                                                       |
|  --- Progress ---                                                     |
|  Baking: Wall_North (3/7)                                             |
|  [==============================          ] 72%                       |
|  Elapsed: 1m 23s    Remaining: ~0m 31s                                |
|  Samples: 64/64     Bounces: 5                                        |
|                                                                       |
|  --- Lightmap Atlas Viewer ---                                        |
|  [View Atlas]  [Channel: RGB v]  [Exposure: 0.0 EV]                  |
|                                                                       |
|  --- Validation ---                                                   |
|  ! Column_A: UV2 overlap detected (2 islands)                        |
|    [Show in Viewport]  [Auto-Fix UV2]                                 |
|                                                                       |
+-----------------------------------------------------------------------+
```

### Quality Presets

| Preset | Samples/Texel | Bounces | Denoise | Typical Time (small scene) |
|--------|--------------|---------|---------|---------------------------|
| Preview | 16 | 3 | Yes (fast) | 5-15 seconds |
| Medium | 64 | 5 | Yes | 30-90 seconds |
| High | 256 | 8 | Yes | 3-10 minutes |
| Production | 1024 | 8 | Yes (high quality) | 15-60 minutes |

### Bake Output Structure

```
project/
  lightmaps/
    village/
      day/
        Floor_01.uzlightmap
        Wall_North.uzlightmap
        Wall_East.uzlightmap
        Ceiling_01.uzlightmap
        Column_A.uzlightmap
        Terrain_Tile_0_0.uzlightmap
        _manifest.ini
      sunset/
        ...
      night/
        ...
```

The `_manifest.ini` file lists all lightmaps and their metadata:

```ini
[manifest]
scene = village
scenario = day
bake_date = 2026-03-02T14:30:00Z
quality = High
total_meshes = 6
total_texels = 2359296

[mesh.Floor_01]
resolution = 512
file = Floor_01.uzlightmap
format = BC6H_UF16

[mesh.Wall_North]
resolution = 256
file = Wall_North.uzlightmap
format = BC6H_UF16
```

### UV Overlap Validation

The UV overlap checker rasterizes each mesh's UV2 to an internal bitmap and detects any texel written more than once. Overlapping regions are visualized in the 3D viewport as a red overlay on the affected mesh. The editor offers an "Auto-Fix UV2" button that re-runs xatlas on the mesh to generate a clean layout.

### IPC Handlers (lightmap.*)

```json
lightmap.bakeAll        { "quality": "Medium" }
                    --> { "ok": true, "job_id": "bake_001" }

lightmap.bakeSelected   { "meshes": ["Floor_01", "Wall_North"], "quality": "High" }
                    --> { "ok": true, "job_id": "bake_002" }

lightmap.cancel         { "job_id": "bake_001" }
                    --> { "ok": true }

lightmap.getProgress    { "job_id": "bake_001" }
                    --> {
                          "status": "baking",
                          "current_mesh": "Wall_North",
                          "mesh_index": 3,
                          "total_meshes": 7,
                          "percent": 72,
                          "elapsed_s": 83,
                          "estimated_remaining_s": 31
                        }

lightmap.validateUV2    { "mesh": "Column_A" }
                    --> {
                          "valid": false,
                          "overlapping_islands": 2,
                          "overlap_texels": 134
                        }

lightmap.setResolution  { "mesh": "Floor_01", "resolution": 1024 }
                    --> { "ok": true }

lightmap.setScenario    { "scenario": "sunset", "blend_weight": 0.0 }
                    --> { "ok": true }

lightmap.blendScenarios { "from": "day", "to": "sunset", "weight": 0.65 }
                    --> { "ok": true }
```

---

## 8. Editor Tools -- Probe Debug Visualization

Debug visualization for the probe grid is essential for artists and technical artists to verify GI coverage and diagnose lighting issues.

### Debug Modes

| Mode | Shortcut | Description |
|------|----------|-------------|
| **Show Probes** | `Ctrl+Shift+P` | Render each probe as a small sphere (radius = 0.2m) colored by its average irradiance (SH band 0). Brighter = more incoming light. |
| **Show SH Bands** | - | Three sub-modes: L0 only (flat color), L1 only (directional gradient), L2 only (quadratic detail). Isolates each SH band for debugging. |
| **Show Coverage** | `Ctrl+Shift+C` | Overlay a heatmap on scene surfaces: green = well-covered by probes, yellow = sparse coverage, red/black = no probe coverage. |
| **Probe Inspector** | Click probe | Selecting a probe sphere displays a detail panel with its 9 SH coefficients as an RGB bar chart, plus a small irradiance sphere preview. |
| **Freeze Probes** | `Ctrl+Shift+F` | Halts all probe updates. The scene can be navigated freely while probes remain frozen at their current state. Useful for inspecting transient lighting. |

### Probe Visualization Rendering

Debug probes are rendered in the editor overlay pass (sort_key = 900, after post-process) using instanced sphere meshes with a custom unlit shader:

```hlsl
// shaders/debug/gi_probe_debug.hlsl
// Renders probe spheres colored by SH L0 band (average irradiance)

#include "common.hlsli"

struct ProbeDebugInstance {
    float3 position;   // world-space probe center
    float3 color;      // SH L0 band * irradiance_scale (RGB)
    float  validity;   // 0 = invalid, 1 = valid
};

StructuredBuffer<ProbeDebugInstance> g_probe_instances : register(t0);

cbuffer DebugParams : register(b2) {
    float4x4 view_proj;
    float    probe_display_radius;  // 0.2m default
    uint     probe_count;
    uint     display_mode;          // 0=irradiance, 1=L0, 2=L1, 3=L2, 4=validity
    float    _pad;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float3 color : COLOR;
    float  alpha : TEXCOORD0;
};

VSOut VSMain(float3 local_pos : POSITION, uint instance_id : SV_InstanceID) {
    ProbeDebugInstance probe = g_probe_instances[instance_id];

    VSOut o;
    float3 world = local_pos * probe_display_radius + probe.position;
    o.pos = mul(view_proj, float4(world, 1.0));

    if (display_mode == 4) {
        // Validity mode: green = valid, red = invalid
        o.color = lerp(float3(1, 0, 0), float3(0, 1, 0), probe.validity);
    } else {
        o.color = probe.color;
    }

    // Fade invalid probes
    o.alpha = (probe.validity > 0.01) ? 0.8 : 0.3;

    return o;
}

float4 PSMain(VSOut i) : SV_TARGET {
    return float4(i.color, i.alpha);
}
```

### Coverage Heatmap

The coverage heatmap is computed as a fullscreen compute pass that, for each pixel, finds the 8 nearest probes and computes a coverage score:

```
coverage_score = sum(probe.validity * trilinear_weight) for 8 nearest probes
```

The score is mapped to a color ramp:
- 0.0 (black): No valid probes nearby
- 0.3 (red): Poor coverage
- 0.6 (yellow): Acceptable coverage
- 1.0 (green): Excellent coverage

### Probe Inspector Bar Chart

When a probe is clicked in the viewport (ray-cast hit test against probe sphere bounding volumes), the editor displays a detail panel:

```
+------------------------------------------+
| Probe Inspector: [42, 3, 17]             |
| World Pos: (84.0, 6.0, 34.0)            |
| Validity: 1.00 (valid)                   |
|                                          |
| SH Coefficients (RGB):                   |
|                                          |
|  L0  [0]: R=0.42  G=0.38  B=0.31        |
|           [=========|========|======]    |
|  L1 [-1]: R=0.05  G=0.04  B=0.03        |
|           [==|=|=]                       |
|  L1  [0]: R=-0.12 G=-0.10 B=-0.08       |
|           [====|===|===]                 |
|  L1  [1]: R=0.08  G=0.07  B=0.06        |
|           [===|==|==]                    |
|  L2 [-2]: R=0.01  G=0.01  B=0.01        |
|           [|]                            |
|  L2 [-1]: R=-0.02 G=-0.01 B=-0.01       |
|           [=|]                           |
|  L2  [0]: R=0.03  G=0.03  B=0.02        |
|           [=|=|]                         |
|  L2  [1]: R=0.00  G=0.00  B=0.00        |
|           []                             |
|  L2  [2]: R=0.01  G=0.01  B=0.01        |
|           [|]                            |
|                                          |
| [Irradiance Sphere Preview]              |
|    +------+                              |
|   /  warm  \   (lit hemisphere)          |
|  |  tones   |                            |
|   \ cool   /   (shadow hemisphere)       |
|    +------+                              |
|                                          |
| [Copy SH Data]  [Recalculate Probe]      |
+------------------------------------------+
```

---

## 9. Reflection Capture Editor

The Reflection Capture Editor allows level designers to place, configure, and preview reflection capture volumes in the scene.

### Placement Workflow

1. From the Lighting panel, click **Add Reflection Capture** and choose Sphere or Box
2. A new capture node appears at the camera position
3. Use the gizmo to position and scale the capture volume
4. In the Inspector, adjust blend radius, priority, brightness, and tint
5. Click **Capture** to bake the cubemap (or set Update Mode to auto-recapture)

### Inspector Panel

```
+-----------------------------------------------------------------------+
|  INSPECTOR > ReflCapture_Office_01                                    |
+-----------------------------------------------------------------------+
|                                                                       |
|  Shape:         (*) Box  ( ) Sphere                                   |
|  Position:      [12.0] [2.5] [-8.0]                                  |
|  Extent:        [5.0]  [3.0] [4.0]                                   |
|  Blend Radius:  [===1.5m===]                                         |
|  Priority:      [0        ]                                          |
|  Brightness:    [===1.0====]                                         |
|  Tint:          [#FFFFFF  ] [color picker]                            |
|  Resolution:    [128 v    ]                                          |
|                                                                       |
|  Update Mode:   (*) Static  ( ) Every N Frames  ( ) On Scene Change  |
|  Update Interval: [30 frames]  (grayed out unless EveryN)            |
|                                                                       |
|  [Capture Now]  [View Cubemap]  [Delete]                             |
|                                                                       |
|  --- Debug ---                                                        |
|  [x] Show Volume Wireframe                                           |
|  [x] Show Blend Zone                                                 |
|  [ ] Show Affected Objects (highlight meshes inside volume)           |
|                                                                       |
+-----------------------------------------------------------------------+
```

### Capture Visualization

In the viewport, reflection captures are shown as:
- **Wireframe volume**: Box outline or sphere wireframe in cyan
- **Blend zone**: Semi-transparent outer region in yellow
- **Capture preview**: A small reflective chrome sphere rendered at the capture center showing the captured environment
- **Affected objects**: When toggled, meshes inside the volume are highlighted with a blue overlay

### IPC Handlers (reflection.*)

```json
reflection.create       { "shape": "Box", "position": [12, 2.5, -8], "extent": [5, 3, 4] }
                    --> { "ok": true, "node_id": "ReflCapture_003" }

reflection.capture      { "node": "ReflCapture_003" }
                    --> { "ok": true, "capture_time_ms": 45 }

reflection.captureAll   {}
                    --> { "ok": true, "count": 5, "total_time_ms": 210 }

reflection.setProperty  { "node": "ReflCapture_003", "priority": 1, "blend_radius": 2.0 }
                    --> { "ok": true }

reflection.getCaptures  {}
                    --> {
                          "captures": [
                            { "node": "ReflCapture_Office_01", "shape": "Box",
                              "position": [12, 2.5, -8], "extent": [5, 3, 4],
                              "priority": 0, "update_mode": "Static" },
                            { "node": "ReflCapture_Outdoor", "shape": "Sphere",
                              "position": [0, 5, 0], "radius": 50.0,
                              "priority": -1, "update_mode": "Static" }
                          ]
                        }

reflection.debug        { "node": "ReflCapture_003", "show_affected": true }
                    --> { "ok": true, "affected_meshes": 12 }
```

---

## 10. Sky Light & Ambient

The sky light system provides the baseline environmental illumination that fills areas where no direct light or probe data reaches.

### Sky Light Sources

| Source | Description | Update Cost |
|--------|-------------|-------------|
| **HDRI Cubemap** | Import `.hdr` or `.exr` environment map. Highest quality. | One-time prefilter (6 mip levels, ~50ms) |
| **Procedural Sky** | Engine-generated sky from sun position + atmosphere parameters. | Recompute when sun moves |
| **Solid Color** | Uniform ambient color. Minimal. | Zero |

### Sky Light Properties

| Property | Type | Default | Range | Description |
|----------|------|---------|-------|-------------|
| `source` | enum | `HDRI` | HDRI/Procedural/Color | Sky light source type |
| `hdri_path` | string | - | - | Path to .hdr/.exr file (for HDRI mode) |
| `intensity` | float | `1.0` | 0.0 - 10.0 | Sky light brightness multiplier |
| `tint` | float3 | `[1,1,1]` | 0.0 - 2.0 | Color tint applied to sky light |
| `rotation` | float | `0.0` | 0.0 - 360.0 | Rotation of HDRI around Y axis (degrees) |
| `update_mode` | enum | `Static` | Static/Realtime/DynamicSky | When to recapture sky cubemap |
| `realtime_interval` | uint | `60` | 1 - 300 | Frames between sky recapture (Realtime mode) |

### Sky Update Modes

- **Static**: The sky cubemap is captured once when the scene loads (or when manually triggered in the editor). Cheapest at runtime.
- **Realtime**: The sky cubemap is recaptured every N frames. Useful for slowly changing skies (cloud animation).
- **DynamicSky**: The sky cubemap is recaptured automatically whenever the procedural sky parameters change (sun position, cloud density, etc.).

### Sky Prefiltering Pipeline

The sky cubemap is prefiltered into a mip chain using importance-sampled GGX for the split-sum IBL approximation:

```
Sky Source (.hdr or procedural) --> 6-face cubemap (256x256 per face, RGBA16F)
    |
    v
Prefilter Compute (importance-sampled GGX, 1024 samples per mip)
    |
    +-- Mip 0: roughness = 0.0   (256x256)
    +-- Mip 1: roughness = 0.125 (128x128)
    +-- Mip 2: roughness = 0.25  (64x64)
    +-- Mip 3: roughness = 0.5   (32x32)
    +-- Mip 4: roughness = 0.75  (16x16)
    +-- Mip 5: roughness = 1.0   (8x8)
    |
    v
Irradiance Cubemap (diffuse convolution, 32x32 per face)
    |
    v
BRDF LUT (2D texture, 512x512, precomputed once, RG16F)
```

### Ambient Cube Fallback

For minimal-cost scenarios (Baked GI with no probes), a 6-direction ambient cube provides basic directional ambient:

```hlsl
// 6 colors: +X, -X, +Y, -Y, +Z, -Z
cbuffer AmbientCube : register(b4) {
    float3 ambient_pos_x;  float _pad0;
    float3 ambient_neg_x;  float _pad1;
    float3 ambient_pos_y;  float _pad2;
    float3 ambient_neg_y;  float _pad3;
    float3 ambient_pos_z;  float _pad4;
    float3 ambient_neg_z;  float _pad5;
};

float3 sample_ambient_cube(float3 normal) {
    float3 n_sq = normal * normal;
    float3 is_neg = step(normal, 0.0);

    float3 color =
        n_sq.x * lerp(ambient_pos_x, ambient_neg_x, is_neg.x) +
        n_sq.y * lerp(ambient_pos_y, ambient_neg_y, is_neg.y) +
        n_sq.z * lerp(ambient_pos_z, ambient_neg_z, is_neg.z);

    return color;
}
```

### Indirect Lighting Cache (Small Dynamic Objects)

Small dynamic objects (radius < 1m) that move through the scene use a simplified indirect lighting lookup. Instead of full probe grid interpolation, they sample a single nearest valid probe and cache the result. The cache is invalidated when the object moves more than half a probe spacing:

```
Per small dynamic object:
  - cached_probe_index: index of nearest valid probe
  - cached_irradiance: float3 (SH L0 band evaluation for up-normal)
  - invalidation_distance: probe_spacing * 0.5
  - Refresh: when object moves > invalidation_distance from cached position
```

This avoids per-pixel probe interpolation for objects like projectiles, debris, and small props.

---

## 11. Light Propagation & Emissive Contribution

Emissive materials (neon signs, lava, screens, glowing crystals) contribute indirect light to the probe grid. This is handled by a dedicated emissive injection pass that runs before probe ray casting.

### Emissive Injection Pipeline

```
Emissive Injection Pipeline (per frame):

1. Emissive Voxelization (compute)
   +-----------------------------+
   | Rasterize emissive geometry |
   | into a 3D voxel grid:      |
   |                             |
   |  Resolution: 128x64x128    |
   |  Cell size = probe_spacing  |
   |  Format: RGBA16F per voxel  |
   |    RGB = emissive color     |
   |    A   = opacity/coverage   |
   |                             |
   | Only meshes with emission>0 |
   | are processed.              |
   +-----------------------------+
              |
              v
2. Propagation (compute, 3 iterations)
   +-----------------------------+
   | 6-directional light spread: |
   | Each voxel spreads its      |
   | emissive color to 6-face    |
   | neighbors, attenuated by    |
   | distance and opacity.       |
   |                             |
   | 3 iterations = ~6m spread   |
   | at 2m probe spacing.        |
   +-----------------------------+
              |
              v
3. Probe Injection (compute)
   +-----------------------------+
   | For each probe, read the    |
   | emissive voxel at the       |
   | probe's grid cell.          |
   | Add emissive irradiance to  |
   | probe SH coefficients       |
   | (band 0 only, omnidirect.)  |
   |                             |
   | Scale by emissive_injection |
   | _scale parameter.           |
   +-----------------------------+
```

### Parameters

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `emissive_injection_scale` | float | `1.0` | 0.0 - 10.0 | Multiplier on emissive light injected into probes |
| `propagation_steps` | uint | `3` | 1 - 8 | Number of voxel propagation iterations |
| `propagation_decay` | float | `0.75` | 0.1 - 1.0 | Light decay per propagation step (0.75 = 25% loss per step) |
| `max_emissive_distance` | float | `20.0` | 1.0 - 100.0 | Maximum distance emissive light propagates (meters) |

### Emissive Voxel Grid Storage

```
3D Texture: RGBA16F, 128 x 64 x 128
VRAM: 128 * 64 * 128 * 8 bytes = 8 MB
Aligned to probe grid (1 voxel per probe cell)
Updated only when emissive objects are present in the scene
```

---

## 12. File Format (.uzgi)

Each scene stores its GI configuration in a `.uzgi` file alongside the `.uzscene` file. The format is INI-like, consistent with other UzEngine data files.

### Format Specification

```ini
# village_outdoor.uzgi
# Global Illumination configuration for the Village outdoor scene
# Auto-generated by UzEngine GI editor. Manual edits supported.

[uzgi]
version = 1
scene = village_outdoor

# --- GI Mode ---
[gi]
mode = hybrid
# Options: off, dynamic, baked, hybrid

# --- Probe Grid (used by dynamic and hybrid modes) ---
[probe_grid]
dimensions = 64, 16, 64
spacing = 2.0
origin = auto
# 'auto' = center on scene AABB. Or explicit: origin = -64.0, 0.0, -64.0
rays_per_probe = 64
max_ray_distance = 50.0
hysteresis = 0.97
update_budget = 2048
irradiance_scale = 1.0

# --- Screen-Space Indirect Lighting ---
[ssil]
enabled = true
quality = Medium
# Options: Low, Medium, High
max_distance = 3.0
intensity = 1.0
thickness = 0.2
bounce_approx = 1
half_resolution = true
temporal_accumulation = 0.9
denoiser_radius = 3

# --- Reflection Captures ---
[reflections]
max_captures = 16
sky_fallback = true

# --- Sky Light ---
[sky_light]
source = HDRI
hdri_path = res://textures/sky/evening_field_01.hdr
intensity = 1.2
tint = 1.0, 0.95, 0.9
rotation = 45.0
update_mode = Static

# --- Ambient Cube (fallback for baked mode) ---
[ambient_cube]
pos_x = 0.15, 0.12, 0.10
neg_x = 0.08, 0.08, 0.10
pos_y = 0.30, 0.28, 0.25
neg_y = 0.05, 0.05, 0.06
pos_z = 0.12, 0.11, 0.10
neg_z = 0.10, 0.10, 0.12

# --- Emissive Injection ---
[emissive]
injection_scale = 1.0
propagation_steps = 3
propagation_decay = 0.75
max_distance = 20.0

# --- Lightmap Settings (used by baked and hybrid modes) ---
[lightmaps]
default_resolution = 256
scenario = day
scenario_blend_target = none
scenario_blend_weight = 0.0

# --- Lightmap Scenarios ---
[lightmap_scenarios]
count = 2

[scenario.day]
lightmap_path = lightmaps/village_outdoor/day/
sun_direction = 0.5, -0.7, 0.3
sun_color = 1.0, 0.95, 0.85
sun_intensity = 3.0

[scenario.night]
lightmap_path = lightmaps/village_outdoor/night/
sun_direction = 0.0, -1.0, 0.0
sun_color = 0.1, 0.1, 0.3
sun_intensity = 0.05

# --- Per-Mesh Lightmap Resolution Overrides ---
[lightmap_overrides]
Floor_Main = 512
Terrain_Tile_0_0 = 1024
SmallProp_Barrel = 64

# --- Global Multipliers ---
[multipliers]
indirect_intensity = 1.0
sky_intensity = 1.2
emissive_to_indirect = 1.0

# --- Quality Override (runtime, for scalability) ---
[quality_override]
# Set to 'none' to use project default, or override per-scene
level = none
# Options: none, off, low, medium, high, ultra
```

### .uzgi File Resolution Order

```
1. project.uzproj [renderer.gi] defaults
2. scene.uzgi per-scene overrides
3. Console variable overrides (r.gi.*)
4. Quality scalability settings (lowest priority for maximums)
```

---

## 13. Performance & Scalability

### Console Variable Overrides

All GI parameters are exposed as console variables for runtime tuning and scalability:

| Console Variable | Type | Default | Description |
|-----------------|------|---------|-------------|
| `r.gi.mode` | string | `"dynamic"` | GI mode override |
| `r.gi.probe_update_budget` | uint | `2048` | Max probes updated per frame |
| `r.gi.probe_rays` | uint | `64` | Rays per probe per update |
| `r.gi.ssil` | bool | `true` | Toggle SSIL |
| `r.gi.ssil_quality` | string | `"Medium"` | SSIL quality level |
| `r.gi.ssil_intensity` | float | `1.0` | SSIL intensity multiplier |
| `r.gi.reflections` | bool | `true` | Toggle reflection captures |
| `r.gi.emissive_injection` | bool | `true` | Toggle emissive light injection |
| `r.gi.indirect_intensity` | float | `1.0` | Global indirect light multiplier |
| `r.gi.sky_intensity` | float | `1.0` | Sky light intensity override |
| `r.gi.debug_probes` | bool | `false` | Show probe debug spheres |
| `r.gi.debug_coverage` | bool | `false` | Show coverage heatmap |
| `r.gi.freeze_probes` | bool | `false` | Freeze probe updates |
| `r.gi.quality` | string | `"high"` | Overall GI quality preset |
| `r.gi.lightmap_scenario` | string | `""` | Active lightmap scenario |
| `r.gi.lightmap_blend` | float | `0.0` | Scenario blend weight |

### Quality Level Breakdown

| Component | Off | Low | Medium | High | Ultra |
|-----------|-----|-----|--------|------|-------|
| Probe Grid | No | No | 32x8x32 (8K probes) | 64x16x64 (65K probes) | 128x32x128 (524K probes) |
| Probe Rays | 0 | 0 | 32 | 64 | 128 |
| SSIL | No | Yes (1 ray, half) | Yes (4 rays, half) | Yes (4 rays, half) | Yes (8 rays, full) |
| Reflection Captures | No | No | Static only | Static + EveryN | Static + OnChange |
| Emissive Injection | No | No | No | Yes (3 steps) | Yes (6 steps) |
| Denoiser | No | Spatial only | Spatial + temporal | Spatial + temporal | Spatial + temporal |

### GPU Budget Table (1080p, RTX 3070 reference)

| Component | Off | Low | Medium | High | Ultra |
|-----------|-----|-----|--------|------|-------|
| Probe ray casting | 0 ms | 0 ms | 0.5 ms | 1.2 ms | 2.5 ms |
| Probe SH projection | 0 ms | 0 ms | 0.1 ms | 0.2 ms | 0.4 ms |
| Probe interpolation (in lighting) | 0 ms | 0 ms | 0.1 ms | 0.2 ms | 0.3 ms |
| SSIL gather | 0 ms | 0.3 ms | 0.8 ms | 0.8 ms | 2.0 ms |
| SSIL denoise | 0 ms | 0.2 ms | 0.4 ms | 0.4 ms | 0.5 ms |
| Emissive injection | 0 ms | 0 ms | 0 ms | 0.3 ms | 0.5 ms |
| Reflection capture update | 0 ms | 0 ms | 0 ms | 0.1 ms | 0.3 ms |
| **Total GI cost** | **0 ms** | **0.5 ms** | **1.9 ms** | **3.2 ms** | **6.5 ms** |

### VRAM Budget

| Resource | Size | Notes |
|----------|------|-------|
| Probe SH buffer (65K probes) | 4 MB | StructuredBuffer, half-precision |
| Probe SH buffer (524K probes, Ultra) | 32 MB | Ultra quality only |
| SSIL history buffer (half-res, RGBA16F) | 3.5 MB | At 1080p |
| SSIL history buffer (full-res, RGBA16F) | 14 MB | Ultra only |
| Emissive voxel grid (128x64x128, RGBA16F) | 8 MB | When emissive injection enabled |
| Reflection cubemaps (16 captures x 128^2 x 6 x mips) | 19 MB | At 128 resolution |
| Sky prefiltered cubemap + irradiance | 2.5 MB | 256^2 per face |
| BRDF LUT | 0.5 MB | 512x512 RG16F, computed once |
| Lightmap textures (scene-dependent) | 10-100 MB | BC6H compressed |
| **Total (High preset, typical scene)** | **~40-60 MB** | |

### Scalability Strategy

The engine applies quality scaling based on GPU capability detected at startup:

```
GPU Detection -> Feature Level + VRAM + Vendor
    |
    v
Determine default GI quality:
    VRAM < 4 GB  or  FL < 12_1  -> Off or Low
    VRAM 4-6 GB  and FL >= 12_1 -> Medium
    VRAM 6-8 GB  and FL >= 12_1 -> High
    VRAM > 8 GB  and DXR 1.1    -> Ultra

User can always override via settings or console.
```

---

## 14. Implementation Notes

### C++ Class Hierarchy

```
src/renderer/gi/
    |
    +-- gi_system.h / .cpp
    |   GISystem (top-level manager)
    |   - Owns all sub-systems
    |   - Reads .uzgi config
    |   - Registers render passes
    |   - Handles mode switching
    |   - Console variable bindings
    |
    +-- radiance_probe_grid.h / .cpp
    |   RadianceProbeGrid
    |   - Grid allocation + layout
    |   - GPU buffer management (SH storage)
    |   - Probe indexing and world <-> grid conversion
    |   - Validity tracking
    |
    +-- probe_updater.h / .cpp
    |   ProbeUpdater
    |   - Priority queue for update scheduling
    |   - DXR ray dispatch (when available)
    |   - SDF fallback ray marching
    |   - SH projection compute pass
    |   - Validity detection pass
    |   - Temporal hysteresis blending
    |
    +-- ssil_pass.h / .cpp
    |   SSILPass
    |   - SSIL gather compute shader dispatch
    |   - Temporal accumulation buffer management
    |   - Spatial denoiser dispatch
    |   - History buffer ping-pong
    |
    +-- emissive_injector.h / .cpp
    |   EmissiveInjector
    |   - Emissive voxel grid 3D texture
    |   - Voxelization pass
    |   - Propagation compute iterations
    |   - Probe injection pass
    |
    +-- lightmap_baker.h / .cpp
    |   LightmapBaker
    |   - Embree BVH construction
    |   - Path tracing job queue (threaded)
    |   - Texel rasterization
    |   - Denoising + dilation
    |   - BC6H compression
    |   - Progress reporting (IPC callback)
    |
    +-- lightmap_manager.h / .cpp
    |   LightmapManager
    |   - Runtime lightmap loading + streaming
    |   - Scenario management + blending
    |   - Dirty flag tracking
    |   - UV2 validation
    |
    +-- reflection_capture_manager.h / .cpp
    |   ReflectionCaptureManager
    |   - Capture volume management
    |   - Cubemap rendering + prefiltering
    |   - Priority sorting + blend weight computation
    |   - Per-pixel capture selection (in lighting shader)
    |
    +-- sky_light.h / .cpp
    |   SkyLight
    |   - HDRI loading (.hdr, .exr via stb_image)
    |   - Procedural sky generation
    |   - Prefilter compute pipeline
    |   - Irradiance convolution
    |   - BRDF LUT generation (one-time)
    |
    +-- gi_debug.h / .cpp
        GIDebug
        - Probe sphere instanced rendering
        - Coverage heatmap compute pass
        - Probe inspector data extraction
        - SH band isolation rendering
```

### Class Hierarchy Diagram

```
                        +------------------+
                        |    GISystem      |
                        | (top-level mgr)  |
                        +--------+---------+
                                 |
            +--------------------+---------------------+
            |          |         |         |            |
     +------+---+ +---+------+ ++---------++ +--------+--------+
     |Radiance  | |Probe     | |SSILPass   | |Emissive         |
     |ProbeGrid | |Updater   | |           | |Injector         |
     +----------+ +----------+ +-----------+ +-----------------+
            |          |
     +------+---+ +---+------+     +-------------------+
     |Lightmap  | |Lightmap  |     |Reflection         |
     |Baker     | |Manager   |     |CaptureManager     |
     +----------+ +----------+     +-------------------+
                                           |
                                   +-------+--------+
                                   |   SkyLight      |
                                   +----------------+
                                           |
                                   +-------+--------+
                                   |   GIDebug       |
                                   +----------------+
```

### Render Graph Integration

The GI system registers the following passes into the existing render graph:

```
Existing Pass Order (from renderer.md + screen-space-effects.md):

  ShadowPass            = 100
  PointLightShadow      = 110
  SpotLightShadow       = 120
  DepthPrepass          = 200
  GBuffer               = 300
  LightCull             = 350
  -------NEW GI PASSES----------
  ProbeUpdate           = 355   (async compute, overlaps with LightCull)
  EmissiveVoxelize      = 356   (async compute)
  EmissivePropagate     = 357   (async compute)
  EmissiveInject        = 358   (async compute)
  SSILGather            = 360   (after GBuffer, needs depth+normal+color)
  SSILDenoise           = 361
  ReflectionCapture     = 365   (if any captures need update this frame)
  -------END GI PASSES----------
  Lighting              = 400   (reads probe SH, SSIL, reflection cubemaps)
  Transparency          = 500
  Skybox                = 600
  PostProcess           = 700   (SSAO, SSR, Bloom, Tonemap, TAA)
  UI                    = 800
  Overlay               = 900   (GI debug viz: probes, coverage, etc.)
  Readback              = 1000
```

### DXR (DX12 Raytracing) Integration

When the GPU supports DXR (DX12 Raytracing Tier 1.1 or higher), probe ray casting uses hardware-accelerated ray tracing:

```cpp
// src/renderer/gi/probe_updater.cpp

void ProbeUpdater::dispatch_probe_rays(CommandList& cmd) {
    if (device_supports_dxr()) {
        // DXR path: dispatch rays against TLAS
        // Ray generation shader: one thread per probe-ray pair
        // Miss shader: sample sky cubemap
        // Closest-hit shader: read surface albedo + emissive + direct light

        D3D12_DISPATCH_RAYS_DESC desc = {};
        desc.RayGenerationShaderRecord = ray_gen_table_;
        desc.MissShaderTable = miss_table_;
        desc.HitGroupTable = hit_group_table_;
        desc.Width = rays_per_probe_;
        desc.Height = probes_to_update_count_;
        desc.Depth = 1;

        cmd->DispatchRays(&desc);
    } else {
        // SDF fallback: compute shader marches against SDF volume
        dispatch_sdf_ray_march(cmd);
    }
}
```

### Software Fallback: Signed Distance Field (SDF) Ray Marching

For GPUs without DXR support, probe rays are traced against a Signed Distance Field volume. The SDF is a 3D texture (256x128x256, R16F) storing the signed distance to the nearest surface at each voxel:

```
SDF Volume:
  Resolution: 256 x 128 x 256
  Format: R16F (signed distance, in meters)
  Coverage: Same world-space extent as probe grid
  Update: Rebuilt when static geometry changes (editor operation)
  VRAM: 256 * 128 * 256 * 2 bytes = 16 MB

SDF Ray March (compute shader):
  For each ray:
    pos = probe_position
    for step in 0..64:
      dist = sample_sdf(pos)
      if dist < 0.01:  // hit surface
        read surface color from a low-res color volume
        break
      pos += ray_dir * max(dist, 0.01)  // sphere tracing
```

SDF quality is lower than DXR (no fine geometric detail, no dynamic objects) but provides acceptable GI for non-DXR hardware.

### Temporal Strategy

| System | History Buffer | Blend Factor | Frames to Converge |
|--------|---------------|-------------|-------------------|
| Probe grid | SH coefficients per probe | 0.97 (hysteresis) | ~30 frames |
| SSIL | RGBA16F per pixel | 0.9 (temporal accumulation) | ~10 frames |
| Emissive voxels | RGBA16F 3D texture | 0.95 | ~20 frames |
| Reflection captures | Cubemap faces | N/A (full recapture) | 1 frame per face |

All temporal systems use the velocity buffer (GBuffer3) for reprojection, ensuring stability under camera motion.

### Shader Include Files

The GI system adds the following shared include files:

```
shaders/
  include/
    gi_probes.hlsli          # Probe grid sampling (Section 2)
    gi_ssil.hlsli             # SSIL helper functions
    reflection_capture.hlsli  # Reflection capture sampling + parallax correction
    sh_math.hlsli             # Spherical harmonics basis functions + projection
    sky_ibl.hlsli             # Sky IBL sampling + BRDF LUT lookup
```

All includes follow the existing convention: `#ifndef` guards, `#include "common.hlsli"` dependency, documented with comments.

### Deferred Lighting Integration

The deferred lighting shader (`shaders/builtin/deferred_lighting.hlsl`) is updated to incorporate all GI contributions:

```hlsl
// Additions to deferred_lighting.hlsl

#include "gi_probes.hlsli"
#include "reflection_capture.hlsli"
#include "sky_ibl.hlsli"

// In the lighting evaluation for each pixel:

// 1. Probe-based indirect diffuse
float3 indirect_diffuse = float3(0, 0, 0);
#ifdef GI_PROBES
    indirect_diffuse = sample_probe_irradiance(world_pos, N);
#endif

// 2. SSIL contribution (additive high-frequency detail)
float3 ssil_color = float3(0, 0, 0);
#ifdef GI_SSIL
    ssil_color = g_ssil_buffer.SampleLevel(sampler_linear_clamp, uv, 0).rgb;
#endif

// 3. Lightmap contribution (baked/hybrid modes)
float3 lightmap_indirect = float3(0, 0, 0);
#ifdef GI_LIGHTMAPS
    lightmap_indirect = sample_lightmap(uv2, scenario_blend);
#endif

// 4. Combine indirect sources
float3 total_indirect = indirect_diffuse + ssil_color + lightmap_indirect;

// 5. Reflection (specular indirect)
float3 specular_indirect = float3(0, 0, 0);
float  ssr_confidence = 0.0;

#ifdef SSR_ENABLED
    float4 ssr = g_ssr_buffer.SampleLevel(sampler_linear_clamp, uv, 0);
    specular_indirect = ssr.rgb;
    ssr_confidence = ssr.a;
#endif

// Reflection capture fallback where SSR has no data
float3 capture_reflection = sample_reflection_captures(world_pos, R, roughness);
specular_indirect = lerp(capture_reflection, specular_indirect, ssr_confidence);

// 6. Apply SSAO to indirect diffuse only
float ao = g_ao_buffer.SampleLevel(sampler_linear_clamp, uv, 0);
total_indirect *= ao;

// 7. Combine direct + indirect
float3 final_color = direct_lighting + kD * albedo * total_indirect * INV_PI
                   + specular_indirect * F_env;
```

### VSCode WebView Integration

The GI editor tools are implemented as panels within the existing VSCode extension WebView infrastructure:

| Panel | WebView ID | Location |
|-------|-----------|----------|
| GI Configuration | `uz.gi.config` | Lighting panel tab |
| Lightmap Baking | `uz.gi.bake` | Lighting panel tab |
| Probe Inspector | `uz.gi.probeInspect` | Floating panel (on probe click) |
| Reflection Capture Inspector | `uz.gi.reflectionInspect` | Inspector panel section |
| Lightmap Atlas Viewer | `uz.gi.atlasViewer` | Floating panel |

All panels communicate with the engine via the existing IPC/MCP WebSocket protocol using the `gi.*`, `lightmap.*`, and `reflection.*` namespaces defined in this specification.

---

## Appendix A: Full Render Pipeline with GI

```
Complete Frame Pipeline (Dynamic GI, High quality):

CPU Side:
  1. Frustum cull
  2. Build draw lists (opaque, transparent, shadow)
  3. Select probes to update (priority queue)
  4. Queue lightmap scenario blend (if hybrid)

GPU Side:
  +-- Async Compute Queue --------+  +-- Graphics Queue -----------+
  |                               |  |                              |
  | ProbeUpdate (DXR/SDF rays)    |  | ShadowPass (CSM)             |
  | EmissiveVoxelize              |  | PointLightShadowPass         |
  | EmissivePropagate (3 iters)   |  | SpotLightShadowPass          |
  | EmissiveInject -> probes      |  | DepthPrepass                 |
  |                               |  | GBufferPass                  |
  +--- fence sync ----------------+  | LightCullCompute             |
                                     |                              |
                                     | SSILGather (compute)         |
                                     | SSILDenoise (compute)        |
                                     |                              |
                                     | ReflectionCaptureUpdate      |
                                     |  (0-1 captures per frame)    |
                                     |                              |
                                     | DeferredLighting             |
                                     |  (reads: probes, SSIL, refl  |
                                     |   captures, sky IBL, SSAO,   |
                                     |   lightmaps, direct lights)  |
                                     |                              |
                                     | TransparencyPass             |
                                     | SkyboxPass                   |
                                     | PostProcess (Bloom, Tone,    |
                                     |              TAA)            |
                                     | GI Debug Overlay (if active) |
                                     | UIPass                       |
                                     | ReadbackPass                 |
                                     +------------------------------+
```

---

## Appendix B: Glossary

| Term | Definition |
|------|-----------|
| **SH (Spherical Harmonics)** | Mathematical basis functions on a sphere. L2 SH uses 9 coefficients to represent low-frequency directional lighting. |
| **Irradiance** | Total incoming light (radiance integrated over hemisphere). Units: W/m^2. |
| **Radiance** | Light traveling in a specific direction. Units: W/m^2/sr. |
| **IBL (Image-Based Lighting)** | Lighting from an environment cubemap. Split into diffuse (irradiance map) and specular (prefiltered mip chain). |
| **SSIL** | Screen-Space Indirect Lighting. Gathers indirect color from screen neighbors. |
| **BRDF LUT** | Precomputed 2D lookup table for the split-sum approximation of the specular IBL integral. Indexed by (NdotV, roughness). |
| **BC6H** | Block compression format for HDR textures. 6:1 compression, hardware decoded. |
| **DXR** | DirectX Raytracing. Hardware-accelerated ray tracing API in DX12. |
| **SDF** | Signed Distance Field. 3D volume storing distance to nearest surface. Used for software ray marching fallback. |
| **TLAS/BLAS** | Top-Level / Bottom-Level Acceleration Structure. DXR BVH hierarchy for ray-scene intersection. |
| **Hysteresis** | Temporal blending parameter. Higher values produce more stable but slower-adapting results. |
| **Embree** | Intel's CPU ray tracing library used for offline lightmap baking. |
