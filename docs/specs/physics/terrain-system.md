# Terrain System Spec

## Overview

UzEngine's terrain system provides height-map based terrain with splatmap material blending, GPU-instanced foliage, a water plane, and an editor sculpt/paint toolset. It is implemented as the `uz_terrain` optional plugin (Phase 4). Terrain data is stored in plain-text `.uzterrain` INI files plus companion binary assets, keeping it AI-legible while keeping large height data efficient.

---

## 1. File Formats

### `.uzterrain` INI File

```ini
[uzterrain version=1 name="WorldTerrain"]

; Heightmap dimensions and scale
heightmap        = "res://terrain/world.heightmap"  ; 16-bit grayscale PNG (see below)
width_meters     = 1024.0   ; X world extent (meters)
depth_meters     = 1024.0   ; Z world extent (meters)
height_scale     = 200.0    ; Y range: 0..200 meters (maps 16-bit 0..65535)
base_height      = -10.0    ; world Y offset of the lowest heightmap value

; Chunk system
chunk_size       = 32.0     ; chunk dimension in meters (32x32 m per chunk)
; Derived: chunk_count_x = width_meters / chunk_size = 32 chunks
; Derived: chunk_count_z = depth_meters / chunk_size = 32 chunks

; Splatmap
splatmap         = "res://terrain/world_splat.png"  ; RGBA PNG (4 material layers)

[material.layer0]
name             = "Grass"
albedo           = "res://textures/terrain/grass_albedo.png"
normal           = "res://textures/terrain/grass_normal.png"
roughness        = 0.9
tiling           = 8.0          ; UV tiling scale

[material.layer1]
name             = "Rock"
albedo           = "res://textures/terrain/rock_albedo.png"
normal           = "res://textures/terrain/rock_normal.png"
roughness        = 0.7
tiling           = 6.0

[material.layer2]
name             = "Sand"
albedo           = "res://textures/terrain/sand_albedo.png"
normal           = "res://textures/terrain/sand_normal.png"
roughness        = 0.85
tiling           = 10.0

[material.layer3]
name             = "Snow"
albedo           = "res://textures/terrain/snow_albedo.png"
normal           = "res://textures/terrain/snow_normal.png"
roughness        = 0.5
tiling           = 5.0

; Foliage layers (GPU instanced)
[foliage.grass_clumps]
mesh             = "res://meshes/foliage/grass_clump.glb"
density          = 4.0          ; instances per m² (approximate)
min_scale        = 0.8
max_scale        = 1.4
random_rotation  = true         ; random Y-axis rotation per instance
max_slope        = 30.0         ; do not spawn on slopes steeper than 30°
min_height       = 0.0          ; world Y, below which no spawn
max_height       = 150.0
wind_sway        = 0.2          ; sway amplitude (shader param)
layer_mask       = 0x01         ; spawn only on splatmap layer 0 (Grass)
cull_distance    = 60.0         ; meters; beyond this, instances not rendered

[foliage.pine_trees]
mesh             = "res://meshes/foliage/pine_01.glb"
density          = 0.05
min_scale        = 0.7
max_scale        = 1.8
random_rotation  = true
max_slope        = 25.0
layer_mask       = 0x01
cull_distance    = 500.0

; Water plane
[water]
enabled          = true
height           = 0.0          ; world Y of water surface
normal_map       = "res://textures/water_normal.png"
normal_scroll_speed = [0.02, 0.015]    ; UV scroll per second
foam_texture     = "res://textures/water_foam.png"
foam_depth       = 0.5          ; depth below surface for foam edge
shallow_color    = [0.1, 0.6, 0.5, 0.7]
deep_color       = [0.0, 0.2, 0.4, 1.0]
fresnel_strength = 1.0
```

### `.heightmap` Binary (16-bit PNG Grayscale)

The heightmap is a standard 16-bit grayscale PNG file (not a custom format). Pixel value `0` maps to world Y = `base_height`. Pixel value `65535` maps to world Y = `base_height + height_scale`.

Default size: **1025 × 1025 pixels**. This is the standard power-of-two-plus-one size used by most terrain systems, giving exact chunk boundaries for a 32 × 32 chunk grid (each chunk = 33 × 33 vertices, shared edges between chunks).

Custom sizes are supported as long as both dimensions are `(N × chunk_size_pixels) + 1`.

The splatmap is a standard RGBA PNG (8 bits per channel). R = layer 0 weight, G = layer 1, B = layer 2, A = layer 3. Channel values are normalized: sum does not need to equal 1.0 (the shader normalizes).

---

## 2. Scene Integration

### Node Type in `.uzscene`

```ini
[node id=5 type=Terrain name="World" parent=1]
terrain = "res://terrain/world.uzterrain"
position = [0.0, 0.0, 0.0]
; scale and rotation are ignored for terrain (always axis-aligned, uniform scale via uzterrain file)
cast_shadow     = true
receive_shadow  = true
navmesh_area    = default   ; area ID for NavMesh baking over terrain surface
```

The `Terrain` node type is registered by the `uz_terrain` plugin. The core engine treats it as an opaque node until the plugin is loaded.

---

## 3. Chunk System

### Chunk Grid

The terrain is divided into a uniform grid of **32 × 32 meter chunks**. For a 1024 × 1024 m terrain, this is a 32 × 32 chunk grid = 1,024 chunks total.

Each chunk is an independent draw call and frustum cull unit.

```cpp
struct TerrainChunk {
    int         grid_x;         // chunk column index
    int         grid_z;         // chunk row index
    AABB        bounds;         // world-space AABB for frustum culling
    bool        is_visible;     // computed per frame
    uint8_t     lod_level;      // 0 = highest detail, 3 = lowest
    float       center_dist;    // distance to camera center

    // One vertex buffer per LOD level
    ID3D12Resource* vb[4];
    ID3D12Resource* ib[4];
    uint32_t        index_count[4];
};
```

### Frustum Culling

Each frame, the terrain system tests all chunk AABBs against the camera frustum planes. Invisible chunks are not drawn. This reduces draw calls significantly for large terrains with rolling terrain that occludes distant chunks.

---

## 4. LOD System

Each chunk has 4 LOD levels generated by meshoptimizer at cook time:

| LOD | Distance | Vertex grid | Triangle count (33×33 chunk) |
|-----|----------|-------------|-------------------------------|
| 0 | 0 – 80 m | 33 × 33 | ~2,048 |
| 1 | 80 – 200 m | 17 × 17 | ~512 |
| 2 | 200 – 400 m | 9 × 9 | ~128 |
| 3 | > 400 m | 5 × 5 | ~32 |

LOD selection is per-chunk based on distance from the camera to the chunk center.

LOD transition uses vertex shader morphing (geomorphing) to avoid popping: the vertex shader blends between LOD N and LOD N+1 positions using a per-chunk morph factor computed from distance.

---

## 5. Splatmap Shader

The terrain uses a custom `terrain.hlsl` pixel shader that samples all 4 material layers and blends them by splatmap weights:

```hlsl
// Simplified terrain pixel shader
float4 PS_Terrain(TerrainVS_Out input) : SV_Target
{
    float2 splat_uv = input.world_xz / float2(g_TerrainWidth, g_TerrainDepth);
    float4 weights  = SplatmapTex.Sample(LinearSampler, splat_uv);
    // Normalize weights (ensure they sum to 1)
    float weight_sum = weights.r + weights.g + weights.b + weights.a + 1e-6;
    weights /= weight_sum;

    // Sample each layer at its own tiling UV
    float3 albedo = float3(0, 0, 0);
    float3 normal = float3(0, 0, 1);
    float  roughness = 0;

    [unroll]
    for (int i = 0; i < 4; ++i) {
        float2 layer_uv = input.world_xz * g_LayerTiling[i];
        albedo    += AlbedoTex[i].Sample(LinearSampler, layer_uv).rgb * weights[i];
        normal    += UnpackNormal(NormalTex[i].Sample(LinearSampler, layer_uv)) * weights[i];
        roughness += g_LayerRoughness[i] * weights[i];
    }

    // PBR shading with blended material properties
    return PBR_Shade(albedo, normalize(normal + input.vertex_normal),
                     roughness, /*metallic*/ 0.0, input.world_pos);
}
```

---

## 6. Collision

Terrain collision uses Jolt's `HeightFieldShape` — an O(1) per-point query with no triangle iteration:

```cpp
// Cook step: extract float heightmap from 16-bit PNG
std::vector<float> heights(1025 * 1025);
for (int i = 0; i < 1025 * 1025; ++i)
    heights[i] = (raw_pixels[i] / 65535.0f) * height_scale + base_height;

// Create Jolt HeightFieldShape
JPH::HeightFieldShapeSettings hf_settings(
    heights.data(), JPH::Vec3::sZero(),
    JPH::Vec3(cell_size, 1.0f, cell_size),  // scale per sample
    1025                                    // sample count per side
);
auto hf_shape = hf_settings.Create().Get();

// Static body — no mass, no dynamics
JPH::BodyCreationSettings body_settings(
    hf_shape, JPH::RVec3(terrain_origin),
    JPH::Quat::sIdentity(),
    JPH::EMotionType::Static,
    Layers::NON_MOVING
);
physics_system.GetBodyInterface().CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);
```

Lua API for terrain queries:

```lua
-- Get world-space height at a given XZ position
local y = ctx.terrain:get_height_at(x, z)    -- float

-- Get surface normal at a given XZ position (useful for foliage alignment)
local n = ctx.terrain:get_normal_at(x, z)    -- Vec3

-- Get splatmap blend weights at a given XZ position
local w = ctx.terrain:get_weights_at(x, z)   -- { r, g, b, a }
```

---

## 7. Editor Tools

The editor provides sculpt and paint tools in the Terrain Editor panel (registered by the `uz_terrain` VSCode extension):

### Sculpt Tools

| Tool | Description |
|------|-------------|
| Raise | Increases terrain height in a circular brush radius |
| Lower | Decreases terrain height |
| Smooth | Averages heights within brush to reduce sharp peaks |
| Flatten | Sets all pixels in brush to a target height |
| Stamp | Applies a height profile from a template image |

Brush parameters: `radius` (meters), `strength` (0..1), `falloff` (linear/smooth/constant).

Sculpt edits are applied to the in-memory heightmap buffer and saved back to the `.heightmap` PNG file. Physics shape is rebuilt on save.

### Paint Tools

| Tool | Description |
|------|-------------|
| Paint Layer | Sets target splatmap channel weight in brush area |
| Clear Layer | Sets channel weight to 0 in brush area |
| Smooth Weights | Blurs splatmap weights within brush to soften transitions |

### Foliage Painting

The Foliage Painter mode scatters foliage instances within a brush area. Foliage instance positions are stored as a point cloud in `.import/foliage/<terrain_name>.uzfoliage` (binary). This file is used at runtime to populate GPU instance buffers. The `.uzterrain` file specifies density and rules; the actual instance positions are deterministically regenerated from the heightmap + seed at cook time (not stored for default density foliage).

---

## 8. Foliage GPU Rendering

Foliage is rendered using GPU-instanced `DrawIndexedInstanced`. Instance data is uploaded to a GPU `StructuredBuffer<FoliageInstance>`:

```hlsl
struct FoliageInstance {
    float4x4 world_matrix;   // full world transform (position, rotation, scale)
    float     wind_phase;    // per-instance random phase for wind sway animation
    float3    _pad;
};
```

The foliage vertex shader reads instance data from `StructuredBuffer<FoliageInstance>` using `SV_InstanceID`, and applies a sinusoidal wind sway displacement:

```hlsl
float sway = sin(g_Time * g_WindFrequency + instance.wind_phase)
           * g_WindStrength * vertex.y;  // more sway near top
output.world_pos.xz += sway * g_WindDir;
```

GPU culling: a compute shader tests each foliage instance's AABB against the camera frustum and builds an `IndirectArguments` buffer. Only visible instances are drawn via `ExecuteIndirect`.

---

## 9. Water Plane

The `[water]` section in `.uzterrain` defines a flat water mesh that covers the entire terrain extent at a specified world Y height:

```cpp
// Water is a single mesh: N×N quad grid (N = chunk_count, so seams align with chunks)
// Rendered after opaque terrain with alpha blending

struct WaterUniforms {
    float    time;
    float2   normal_scroll_speed;
    float4   shallow_color;
    float4   deep_color;
    float    fresnel_strength;
    float    foam_depth;
    // ... scene depth buffer for edge foam + refraction
};
```

The water shader samples the scene depth buffer to compute edge foam (where water meets terrain) and simple screen-space refraction.

---

## 10. Streaming (Phase 5 Extension)

In Phase 4, all terrain chunks are always in GPU memory. Phase 5 World Partition adds streaming:

- Chunks are grouped into **streaming cells** (e.g., 4 × 4 chunk blocks).
- Cells beyond a `stream_radius` (default: 500 m) are unloaded from GPU memory.
- Heightmap tiles are stored in a tiled `.uzterrain_tiles` binary and loaded on demand from background IO.

This is a Phase 5 concern — Phase 4 terrain loads everything on scene open.

---

## 11. IPC Handlers

```
terrain.getHeightAt   { x, z }  → { height }
terrain.getNormalAt   { x, z }  → { normal: [x, y, z] }
terrain.getWeightsAt  { x, z }  → { r, g, b, a }
terrain.sculpt        { tool, center: [x,z], radius, strength, falloff }
terrain.paintLayer    { layer, center: [x,z], radius, strength }
terrain.rebuildCollision  {}
terrain.bakeNavmesh   {}
terrain.addFoliageLayer   { ... }
terrain.removeFoliageLayer { name }
```

---

## 12. Limits

| Constraint | Value |
|------------|-------|
| Max terrain size | 4096 × 4096 m (limited by Jolt HeightFieldShape: 4096 samples) |
| Default terrain size | 1024 × 1024 m |
| Heightmap resolution | 1025 × 1025 pixels (default) |
| Splatmap layers | 4 (RGBA) |
| Max foliage layers per terrain | 16 |
| Max simultaneous foliage instances | 1,000,000 (GPU buffer, ~64 MB) |
| LOD levels per chunk | 4 |
| Water planes per scene | 1 (multiple water volumes are a Phase 5 feature) |
