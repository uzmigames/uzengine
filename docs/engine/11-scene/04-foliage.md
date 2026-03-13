# Foliage & Instancing — GPU-Driven Instance Rendering

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/foliage/`, `engine/subsystem/foliage_world.h`
> **Depends on**: Terrain (§37), Rendering (§12), Physics (§16)
> **References**: UE5 `UFoliageInstancedStaticMeshComponent` + Hierarchical ISM, Godot `MultiMeshInstance3D`

## Purpose

Render and manage millions of grass blades, trees, rocks, and other repeating objects via GPU instancing. Instead of one draw call per tree (impossible at scale), cluster instances into 64-256 groups, frustum-cull clusters on GPU, and submit a single indirect draw. Per-instance LOD, wind animation, and density painting provide visual richness while maintaining 60 FPS with 100k–1M instances.

## Architecture — Clusters & Grid

Foliage is organized in a **spatial grid**:

```cpp
struct FoliageCluster {
    uint32_t      mesh_index;           // which mesh in the foliage volume?
    uint32_t      instance_offset;      // first instance in global buffer
    uint32_t      instance_count;       // how many instances in this cluster
    Sphere        bounds;               // world-space for frustum culling
    uint32_t      lod_level;            // all instances at same LOD
    uint32_t      is_visible;           // frustum culled? (written by compute)
};

struct FoliageGrid {
    uint32_t                     grid_size;      // e.g., 256×256 cells
    std::vector<FoliageCluster>  clusters;       // all clusters
    ResourceHandle<Buffer>       cluster_buffer; // GPU-resident
    ResourceHandle<Buffer>       instance_buffer; // position, rotation, scale, lod
};

/// One foliage volume can have 2-8 mesh types (grass, small shrub, tree, large tree, etc.)
struct CFoliageVolume {
    std::vector<ResourceHandle<Mesh>>  meshes;     // LOD hierarchies
    std::vector<ResourceHandle<Material>> materials;
    FoliageGrid                        grid;
    Sphere                             bounds;
    Vec3                               grid_origin;
    float                              grid_cell_size;  // world units per cell
};
```

**Why clusters?** Each cluster is ~128 instances. A 1k×1k foliage volume with 4m spacing = 250k instances = 2k clusters. Frustum culling clusters instead of individual instances reduces compute work 2000× (100k → 50 clusters typically visible).

## Instance Data Layout

Instance buffer is **flat** — no per-mesh indirection:

```cpp
struct InstanceData {
    Vec3          position;           // 12 bytes, world-space
    uint32_t      rotation_packed;    // 4 bytes, unit quaternion packed to 4×8-bit
    Vec3          scale_and_lod;      // 12 bytes: scale.xyz in first 3 floats
    // Note: LOD level derived from distance in vertex shader, not stored per-instance
    // Bit 31 of scale.w is "delete" flag for removed instances
};

// Total: 28 bytes per instance in base format
// 1M instances = 28 MB (fits in mid-range GPU VRAM)

// GPU buffer: tightly packed array
struct InstanceBuffer {
    InstanceData instances[kMaxInstances];  // GPU-resident, updatable via mapping
    uint32_t     instance_count;            // live instance count
};
```

## Frustum Culling — GPU Compute

Every frame, a compute shader tests each cluster:

```glsl
// Per-cluster invocation
layout(local_size_x = 64) in;
void main() {
    uint cluster_id = gl_GlobalInvocationID.x;
    if (cluster_id >= cluster_count) return;

    FoliageCluster cluster = cluster_buffer[cluster_id];

    // Frustum test (fast 6-plane check)
    bool visible = FrustumTestSphere(camera_frustum, cluster.bounds);

    // Distance-based LOD
    float dist = distance(camera_pos, cluster.bounds.center);
    uint lod = 0;
    if (dist > 150.0) lod = 1;
    if (dist > 500.0) lod = 2;

    cluster_buffer[cluster_id].is_visible = visible ? 1 : 0;
    cluster_buffer[cluster_id].lod_level = lod;

    // If visible, append to indirect draw buffer
    if (visible) {
        uint draw_index = atomicCounterIncrement(visible_cluster_count);
        indirect_buffer[draw_index] = MakeDrawCall(cluster);
    }
}
```

**Result**: Visible clusters are compacted into a new indirect buffer, submitted in a single `DrawIndexedInstancedIndirect()`. Culled clusters are never sent to GPU.

## Per-Instance LOD & Dithering

LOD is **distance-based**, not pre-baked:

```cpp
// Vertex Shader:
float dist = distance(vs_input.instance_position, camera_pos);
uint current_lod = 0;
if (dist > 100.0) current_lod = 1;  // half vertex count
if (dist > 500.0) current_lod = 2;  // quarter vertex count
if (dist > 2000.0) discard;         // cull beyond 2km

// Select LOD variant from mesh
uint vertex_offset = mesh.lod_vertex_offsets[current_lod];
uint index_offset = mesh.lod_index_offsets[current_lod];
```

**Cross-fade dithering** smooths LOD transitions:

```glsl
// Pixel Shader:
float lod_fade = frac(dist * 0.001);  // [0..1] dither pattern
if (current_lod > previous_lod) {     // transitioning to coarser LOD
    if (hash(vs_output.screen_pos) > lod_fade) discard;  // probabilistic fade
}
```

Trees at 150m fade over 50m distance, dither prevents visible LOD pop. Grass uses quicker LOD transitions (visible range <500m).

## Wind Animation — GPU Vertex Displacement

All instances animate with the same **global wind direction** but unique **phase offsets**:

```glsl
// Vertex Shader:
vec3 wind_world_space = uniform_wind_direction * sin(time_seconds * wind_frequency);
float instance_phase = hash(instance_id) * TWO_PI;  // 0..2π per instance
float animation = sin(time_seconds * wind_frequency + instance_phase);
vec3 vertex_offset = vertex_local.xyz;
vertex_offset.xz += wind_world_space.xz * animation * wind_strength;
vertex_final = world_matrix * vec4(vertex_offset, 1.0);
```

Each tree bends slightly differently — phase offset ensures they don't sway in lockstep. Compute-driven wind parameters (direction, frequency, strength) come from global weather system or artist-defined timeline.

## Density Painting Tool (Editor)

User paints a **density map** (0-255) that controls spawn probability:

```cpp
struct DensityMap {
    uint32_t            width, height;
    std::vector<uint8_t> data;       // one value per grid cell
    ResourceHandle<Texture> gpu_texture;
};

// Painting with brush:
// 1. Brush circle at (world_pos, radius)
// 2. Falloff gaussian: strength = exp(-dist² / radius²)
// 3. For each cell under brush: density += brush_strength * delta_time
// 4. GPU texture updated immediately (single memcpy)
// 5. Foliage respawned in dirty region (compute shader pass)
```

**Constraints** during painting:
- Max density on slopes > 45° (avoid weird placement on cliffs)
- Respect foliage holes (no instances in cave entrances)
- Per-foliage-volume density cap (don't exceed 1M instances)

## Procedural Placement — Noise & Exclusion

Initial foliage can be **placed procedurally** instead of fully painted:

```cpp
// Procedural placement algorithm:
// 1. Divide volume into grid cells
// 2. For each cell:
//    - Sample Perlin noise(grid_x, grid_y, seed)
//    - If noise > threshold: spawn instance
//    - Offset position within cell by (noise_x, noise_y)
// 3. Respect slope, altitude, and exclusion zones

for (uint32_t z = 0; z < grid_height; z++) {
    for (uint32_t x = 0; x < grid_width; x++) {
        float noise_val = perlin_noise_2d(x * 0.1, z * 0.1, seed);
        if (noise_val > 0.6) {
            Vec3 pos = GetGridCellWorldPos(x, z);
            pos.xz += (noise_val - 0.5) * cell_size;  // jitter within cell
            pos.y = terrain.GetHeightAt(pos.x, pos.z);
            SpawnInstance(mesh_type, pos);
        }
    }
}
```

**Exclusion zones** are hand-painted areas (around buildings, roads) where foliage never spawns.

## Collision Handling

Not all instances collide:

- **Large objects** (trees, rocks): Enable collision via `CTerrainCollider`
  - Each instance has a physics shape (box, capsule, or convex mesh)
  - Broad-phase uses cluster bounds; narrow-phase tests individual shapes
- **Small objects** (grass, small shrubs): Physics-disabled, visual only
  - Collision disabled, players pass through them

**Player interaction with grass**:
- On contact, grass **pushes aside** via particle effect or deformation
- Non-physical — just visual feedback, no actual forces

```cpp
// On character movement, raycast nearby instances:
std::vector<InstanceData> nearby = GetInstancesNear(character_pos, 5.0f);
for (auto& instance : nearby) {
    if (instance.mesh == grass_mesh) {
        // Play push-aside VFX
        PlayParticleEffect("grass_pushed", instance.position);
    }
}
```

## Memory Budget

Performance target: **100k–1M instances at 60 FPS**.

```
Instance Buffer:        1M × 28 bytes = 28 MB
Cluster Buffer:         ~2k × 48 bytes = ~100 KB
Indirect Draw Buffer:   ~1k × 20 bytes = ~20 KB
Mesh LOD Variants:      5 variants × 100 MB = 500 MB (loaded per-scene)
Total VRAM:             ~600 MB (mid-range GPU, shared with other systems)
```

Instances are **compressed on disk** (zstd) and **decompressed on load**. Large foliage volumes are streamed with world chunks.

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | GPU instancing, frustum culling compute shader, per-instance LOD, wind animation, cluster-based rendering. |
| **Phase 4** | Density painting tool, procedural placement, interaction (push-aside), streaming, collision for large instances. |

## Rules

1. **NEVER** use one draw call per instance — always cluster and indirect draw
2. **ALWAYS** frustum-cull on GPU compute — never CPU-side culling of individual instances
3. **ALWAYS** pack instance data tightly — every byte counts at scale
4. **NEVER** use per-instance bones/skinning — skeletal meshes not supported for foliage
5. **ALWAYS** use dithering for LOD transitions — sharp pops are unacceptable at scale
6. **NEVER** spawn instances outside grid bounds — validate before instance creation
7. **ALWAYS** respect painter-defined exclusion zones — procedural placement must not ignore artist intent
8. **NEVER** enable collision on grass or small shrubs — physics cost explodes with instance count
