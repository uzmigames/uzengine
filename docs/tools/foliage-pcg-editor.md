# Foliage & Procedural Content Generation (PCG) Editor

> **Tool Category**: World Authoring
> **Phase**: 4
> **Depends on**: terrain-system, scene-system, gpu-instancing, editor-panels, undo-redo, editor-gizmos, shader-library
> **File Formats**: `.uzfoliage_type`, `.uzfoliage_cluster`, `.uzpcg`, `.uzbiome`
> **Related**: [terrain-editor.md](terrain-editor.md) (basic foliage brush painting), [gpu-instancing.md](../specs/gpu-instancing.md)

---

## 1. Overview & Goals

The Foliage & PCG Editor is a dedicated authoring suite for large-scale vegetation placement and procedural content generation. It supersedes the basic foliage painting tools in the Terrain Editor with a professional, purpose-built system that handles millions of instances across open worlds.

The editor has two tightly integrated halves:

1. **Foliage Editor** -- a dedicated instancing tool for hand-painting, selecting, and managing foliage instances with per-type asset definitions, cluster presets, LOD chains, wind, collision, and GPU-indirect rendering.

2. **PCG Graph Editor** -- a node-based procedural content generation system that generates foliage, mesh instances, scene nodes, audio emitters, and lights from rules evaluated against terrain data, spline paths, and volumes.

### Design Goals

| Goal | Description |
|------|-------------|
| **Scale** | 2 million foliage instances per scene (configurable), GPU-culled and LOD-managed |
| **Artist control** | Hand-painting with per-type asset definitions, cluster presets, and bulk operations |
| **Procedural power** | Node graph PCG system with deterministic seeds, biome blending, and real-time preview |
| **Non-destructive** | Manual edits always take priority over procedural; exclusion zones are preserved across regeneration |
| **GPU-driven** | All culling, LOD selection, and indirect draw argument generation run on the GPU compute pipeline |
| **AI-native** | All operations are MCP tool calls; AI can author foliage types, PCG graphs, and biomes |
| **Integration** | PCG outputs feed directly into the existing `FoliageRenderer` (`ExecuteIndirect` pipeline) |

### Relationship to Terrain Editor

The Terrain Editor (`docs/tools/terrain-editor.md` Section 5) provides basic foliage brush painting with inline foliage layer definitions inside `.uzterrain` files. This specification **replaces and extends** that system:

- Foliage type definitions move from inline `[foliage.*]` sections to standalone `.uzfoliage_type` asset files.
- Foliage clusters (multi-type groups) are a new concept defined in `.uzfoliage_cluster` files.
- The PCG graph system is entirely new and generates foliage (and other content) procedurally.
- The existing `FoliageRenderer`, `FoliagePainter`, and `.uzfoliage` binary format are reused unchanged.

Backward compatibility: `.uzterrain` files with inline `[foliage.*]` sections continue to work. They are internally converted to ephemeral `FoliageType` objects at load time.

---

## 2. Editor Layout

### Combined Foliage & PCG Editor Panel

```
+===========================================================================+
| FOLIAGE & PCG EDITOR                                    [Foliage] [PCG]  |
+===========================================================================+
|                                                                           |
| +--PALETTE (left)------+ +--VIEWPORT (center)--------+ +--PROPERTIES-----+
| |                      | |                            | |  (right)        |
| | FOLIAGE TYPES        | |  [3D Viewport with         | | SELECTED TYPE   |
| | +------------------+ | |   foliage overlay,          | | Pine Tree v2    |
| | | [*] Pine Tree v2 | | |   brush ring,               | | --------------- |
| | | [ ] Oak Large    | | |   density heatmap,          | | Mesh:           |
| | | [ ] Grass Clump  | | |   or PCG point cloud]       | | pine_01.glb     |
| | | [ ] Fern Small   | | |                            | | LODs: 4 + bill  |
| | | [ ] Rock Medium  | | |                            | | Cull: 500m      |
| | +------------------+ | |                            | | Scale: 0.7-1.8  |
| |                      | |                            | | Density: 0.05   |
| | CLUSTERS             | |                            | | Max slope: 25   |
| | +------------------+ | |                            | | Layer mask: 0x01|
| | | [ ] Dense Forest | | |                            | | Wind:           |
| | | [ ] Meadow       | | |                            | |  trunk: 0.15    |
| | | [ ] Rocky Shore  | | |                            | |  branch: 0.3    |
| | +------------------+ | |                            | |  leaf: 0.8      |
| |                      | |                            | | Collision:      |
| | [+ Type] [+ Cluster]| |                            | |  capsule r=0.25 |
| +----------------------+ +----------------------------+ |  h=6.0          |
|                                                         | Billboard:      |
| +--TOOLS (bottom-left)--+ +--STATS (bottom)----------+ |  atlas: 8x8     |
| | TOOL: (*)Paint        | | Instances: 1,241,800      | |  dist: 400m     |
| |  ( )Erase ( )Select   | | Visible:   42,100         | | Custom data:    |
| |  ( )Reapply ( )Fill   | | Draw calls: 38            | |  [0]: color_var |
| |  ( )Single            | | GPU time:   1.2 ms        | |  [1]: age_var   |
| | BRUSH:                | | Memory:     78 MB GPU     | |                 |
| |  Shape: [Circle    v] | +--PER-TYPE BREAKDOWN------+ | [Apply Changes] |
| |  Radius:  [===] 15m   | | Pine:    12K vis / 52 DC  | | [Reset Defaults]|
| |  Density: [===] 1.0x  | | Oak:      8K vis / 12 DC  | +----------------+
| |  Align:   [x] Normal  | | Grass:   20K vis /  4 DC  |                  |
| |  Ground only: [x]     | | Fern:     1K vis /  8 DC  |                  |
| +------------------------+ | Rock:     1K vis / 12 DC  |                  |
|                            +---------------------------+                  |
+===========================================================================+
| STATUS: Foliage mode | Brush: circle r=15m | Pine Tree v2 | FPS: 60      |
+===========================================================================+
```

### PCG Graph Editor Mode (toggled via [PCG] tab)

```
+===========================================================================+
| FOLIAGE & PCG EDITOR                                    [Foliage] [PCG]  |
+===========================================================================+
|                                                                           |
| +--ASSET PALETTE----+ +--NODE CANVAS (center)----------------+ +-PROPS--+
| |                   | |                                       | |        |
| | INPUTS            | |  [SampleTerrain]--->[FilterBySlope]   | | NODE:  |
| |  SampleTerrain    | |        |                  |           | | Filter |
| |  SampleSpline     | |        v                  v           | | BySlop |
| |  SampleVolume     | |  [PoissonDisk]    [FilterByAltitude]  | |--------|
| |  RandomPoints     | |        |                  |           | | min: 0 |
| |  GridPoints       | |        v                  v           | | max: 25|
| |  PoissonDisk      | |  [AlignToNormal]-->[SelectMesh]       | | invert |
| | FILTERS           | |        |                  |           | |  [ ]   |
| |  FilterBySlope    | |        v                  v           | |        |
| |  FilterByAltitude | |  [AddNoise]------>[SpawnFoliage]      | |--------|
| |  FilterByLayer    | |                          |            | | OUTPUT |
| |  FilterByDensity  | |                  [SpawnMeshInstances] | | count: |
| |  FilterByDistance  | |                                       | | 24,810 |
| | TRANSFORMS        | +---------------------------------------+ |        |
| |  ProjectToTerrain | | +--POINT PREVIEW (mini)--+             | |        |
| |  AlignToNormal    | | |  . . .  . .  . . .     |             | |        |
| |  ScatterInRadius  | | |   .  . .  . .   . .    |             | |        |
| |  AddNoise         | | |  . . . .   . .  . .    |             | |        |
| |  Jitter           | | | (colored dots = points  |             | |        |
| | DENSITY           | | |  at current node)       |             | |        |
| |  DensityByAttr    | | +-------------------------+             | |        |
| |  DensityNoise     | |                                         | |        |
| |  DensityFromTex   | | EXECUTION: [On Build v] Seed: 42       | |        |
| | SELECT            | | [Regenerate] [Export Instances]         | |        |
| |  SelectMesh       | |                                         | |        |
| |  SwitchByBiome    | | Stats: 48,200 input > 24,810 output     | |        |
| | OUTPUT            | |        filter ratio: 51.5%              | |        |
| |  SpawnFoliage     | |        exec time: 340 ms                | |        |
| |  SpawnMeshInst    | +------------------------------------------+ |        |
| |  SpawnSceneNode   |                                              |        |
| |  SpawnAudioEmit   |                                              |        |
| |  SpawnLight       |                                              |        |
| |  WriteAttribute   |                                              |        |
| +-------------------+                                              +--------+
+===========================================================================+
| STATUS: PCG mode | Graph: forest_biome.uzpcg | Last exec: 340ms | 24.8K pts|
+===========================================================================+
```

---

## 3. Part 1 -- Foliage Editor

### 3.1 Foliage Type Assets (`.uzfoliage_type`)

A foliage type is a standalone asset that defines everything needed to render and simulate one kind of foliage instance (one tree species, one grass variant, one rock shape).

#### File Format

```ini
[uzfoliage_type version=1 name="PineTree_v2"]

; Primary mesh and LOD chain
mesh_lod0        = "res://meshes/foliage/pine_01_lod0.glb"
mesh_lod1        = "res://meshes/foliage/pine_01_lod1.glb"
mesh_lod2        = "res://meshes/foliage/pine_01_lod2.glb"
mesh_lod3        = "res://meshes/foliage/pine_01_lod3.glb"
lod_distances    = [50.0, 150.0, 300.0, 450.0]

; Billboard impostor fallback at max distance
billboard_enabled = true
billboard_atlas  = "res://textures/foliage/pine_01_impostor_8x8.png"
billboard_start  = 400.0    ; meters -- blend from LOD3 to billboard
billboard_end    = 500.0    ; meters -- fully billboard, cull beyond

; Material overrides (optional, applied over mesh's default material)
material_override = ""      ; empty = use mesh default
albedo_tint      = [1.0, 1.0, 1.0, 1.0]

; Placement parameters
density_default  = 0.05     ; instances/m^2 default for brush painting
min_scale        = 0.7
max_scale        = 1.8
scale_distribution = "uniform"  ; uniform | normal | biased_small | biased_large
random_yaw       = true
align_to_normal  = true
align_strength   = 0.8      ; 0=upright, 1=fully aligned to surface normal
max_slope        = 25.0     ; degrees, skip placement on steeper surfaces
min_height       = -100.0   ; world Y
max_height       = 9999.0

; Culling
cull_distance    = 500.0    ; meters, beyond this instance is not drawn
fade_distance    = 50.0     ; fade-out range before cull (alpha dither)

; Wind
wind_trunk_amplitude = 0.15
wind_trunk_frequency = 0.5
wind_branch_amplitude = 0.3
wind_branch_frequency = 1.2
wind_leaf_amplitude = 0.8
wind_leaf_frequency = 3.0
wind_phase_variation = 1.0  ; random phase offset range (radians)

; Collision
collision_enabled = true
collision_shape  = "capsule"    ; none | capsule | box | convex_hull
collision_radius = 0.25
collision_height = 6.0
collision_offset = [0.0, 3.0, 0.0]   ; local-space offset of collider center
collision_distance = 50.0              ; only create colliders within this range

; Custom data channels (passed to shader via per-instance StructuredBuffer)
; Up to 4 float channels (packed into custom_data float4)
custom_channel_0 = "color_variation"    ; channel name (for editor display)
custom_channel_0_range = [0.0, 1.0]     ; random range
custom_channel_1 = "age_variation"
custom_channel_1_range = [0.0, 1.0]
custom_channel_2 = ""
custom_channel_3 = ""

; Tags (for PCG and Lua queries)
tags = ["tree", "pine", "large", "climbable"]
```

#### FoliageType C++ Struct

```cpp
// src/engine/foliage/foliage_type.h

struct FoliageType {
    std::string         name;
    ResourceHandle<Mesh> mesh_lods[4];      // LOD0..LOD3
    float               lod_distances[4];   // transition distances

    // Billboard impostor
    bool                billboard_enabled;
    ResourceHandle<Texture> billboard_atlas;
    float               billboard_start;
    float               billboard_end;

    // Material
    ResourceHandle<Material> material_override;  // null = mesh default
    glm::vec4           albedo_tint;

    // Placement
    float               density_default;
    float               min_scale;
    float               max_scale;
    ScaleDistribution   scale_distribution;
    bool                random_yaw;
    bool                align_to_normal;
    float               align_strength;
    float               max_slope;
    float               min_height;
    float               max_height;

    // Culling
    float               cull_distance;
    float               fade_distance;

    // Wind
    float               wind_trunk_amplitude;
    float               wind_trunk_frequency;
    float               wind_branch_amplitude;
    float               wind_branch_frequency;
    float               wind_leaf_amplitude;
    float               wind_leaf_frequency;
    float               wind_phase_variation;

    // Collision
    bool                collision_enabled;
    CollisionShapeType  collision_shape;
    float               collision_radius;
    float               collision_height;
    glm::vec3           collision_offset;
    float               collision_distance;

    // Custom data
    std::string         custom_channel_names[4];
    glm::vec2           custom_channel_ranges[4];

    // Tags
    std::vector<std::string> tags;
};

enum class ScaleDistribution : uint8_t {
    Uniform,        // flat random between min/max
    Normal,         // gaussian centered at midpoint
    BiasedSmall,    // skewed toward min_scale
    BiasedLarge     // skewed toward max_scale
};

enum class CollisionShapeType : uint8_t {
    None, Capsule, Box, ConvexHull
};
```

### 3.2 Foliage Clusters (`.uzfoliage_cluster`)

A cluster defines a group of foliage types that are painted together as a biome-like preset. When the artist paints with a cluster, all member types are scattered simultaneously according to their relative weights.

```ini
[uzfoliage_cluster version=1 name="DenseForest"]

; Description for the palette tooltip
description = "Mixed conifer forest: pines, oaks, undergrowth, and rocks"

; Member types with relative weights (weights are normalized to sum to 1.0)
[member.0]
foliage_type     = "res://foliage/pine_tree_v2.uzfoliage_type"
weight           = 0.40     ; 40% of scattered instances are pine trees
density_override = 0.0      ; 0 = use type's default density

[member.1]
foliage_type     = "res://foliage/oak_large.uzfoliage_type"
weight           = 0.20
density_override = 0.0

[member.2]
foliage_type     = "res://foliage/fern_small.uzfoliage_type"
weight           = 0.15
density_override = 2.0      ; override: 2 ferns/m^2

[member.3]
foliage_type     = "res://foliage/grass_clump.uzfoliage_type"
weight           = 0.15
density_override = 4.0

[member.4]
foliage_type     = "res://foliage/rock_medium.uzfoliage_type"
weight           = 0.10
density_override = 0.0

; Cluster-level placement rules (AND-combined with per-type rules)
min_altitude     = 5.0
max_altitude     = 180.0
max_slope        = 30.0
terrain_layers   = [0]      ; splatmap layer indices where cluster is allowed
```

### 3.3 Painting Tools

| Tool | Mouse Action | Description |
|------|-------------|-------------|
| **Paint** | LMB drag | Scatters foliage instances within brush radius at target density. Uses active type or cluster. |
| **Erase** | RMB drag (or dedicated tool) | Removes instances of the active type(s) within brush radius. |
| **Single Place** | LMB click | Places exactly one instance at click position with full transform gizmo. |
| **Lasso Select** | LMB drag (freeform) | Draws a freeform polygon; selects all instances inside for bulk operations. |
| **Reapply** | LMB drag over selected | Re-randomizes scale, rotation, and yaw of selected instances using current type settings. |
| **Fill** | LMB click inside closed area | Procedurally fills a closed polygon or terrain region at target density. |

#### Brush Settings

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `shape` | enum | circle, square, texture_mask | circle | Brush footprint shape |
| `radius` | float | 0.5 -- 500 m | 15 m | Brush radius in world meters |
| `density_multiplier` | float | 0.01 -- 10.0 | 1.0 | Multiplier on type's default density |
| `falloff` | enum | constant, linear, smooth, gaussian | smooth | Density falloff from brush center to edge |
| `spacing` | float | 0.1 -- 10.0 | 1.0 | Minimum distance between instances in brush (meters) |
| `align_to_normal` | bool | -- | true | Align instances to terrain surface normal |
| `ground_only` | bool | -- | true | Only place on terrain/ground surfaces (not on other meshes) |
| `jitter` | float | 0.0 -- 1.0 | 0.8 | Position jitter within brush cells |
| `texture_mask` | texture path | -- | "" | Custom grayscale texture for brush shape (white = paint) |

#### Brush Visual

The brush is rendered as a translucent circle/square projected onto the terrain surface in the editor overlay pass. Color coding:

- **Green ring**: paint mode
- **Red ring**: erase mode
- **Blue ring**: select mode
- **Yellow ring**: reapply mode
- Density dots shown inside brush preview to indicate approximate placement density.

### 3.4 Instance Data -- GPU Representation

Each foliage instance is stored in a `StructuredBuffer<FoliageInstance>` on the GPU:

```cpp
// src/engine/foliage/foliage_instance.h

struct FoliageInstance {
    // Packed for GPU efficiency: 48 bytes per instance
    float     position[3];      // 12 bytes  world XYZ
    float     scale;            //  4 bytes  uniform scale
    uint16_t  yaw;              //  2 bytes  Y rotation, 0..65535 maps to 0..2*PI
    uint16_t  normal_oct[2];    //  4 bytes  octahedron-encoded surface normal (2x uint16)
    float     custom_data[4];   // 16 bytes  per-type shader channels
    uint16_t  lod_bias;         //  2 bytes  per-instance LOD bias
    uint16_t  flags;            //  2 bytes  bitfield: visible, shadow_caster, fading
    float     wind_phase;       //  4 bytes  per-instance random wind phase
    uint16_t  type_index;       //  2 bytes  index into FoliageTypeArray
    uint16_t  _pad;             //  2 bytes  alignment padding
};
// Total: 48 bytes per instance
```

HLSL mirror:

```hlsl
// shaders/include/foliage_instance.hlsli

struct FoliageInstance {
    float3   position;       // world XYZ
    float    scale;          // uniform scale
    uint     yaw_packed;     // uint16 yaw + uint16 normal_oct.x (packed)
    uint     normal_oct_y_lod_flags; // uint16 normal_oct.y + uint16 (lod_bias:6 | flags:10)
    float4   custom_data;    // per-type shader variation channels
    float    wind_phase;     // random wind phase offset
    uint     type_index_pad; // uint16 type_index + uint16 pad
};

StructuredBuffer<FoliageInstance> g_FoliageInstances : register(t8);
```

### 3.5 Hierarchical Instance Culling

Culling runs entirely on the GPU via compute shaders in three stages:

```
Stage 1: Spatial Hash Grid (coarse)
  - World divided into 64m x 64m cells
  - Each cell has an AABB
  - Compute shader tests cell AABBs against frustum planes
  - Output: bitmask of visible cells

Stage 2: Per-Instance Frustum Cull + LOD Select (fine)
  - Only processes instances in visible cells
  - Tests instance bounding sphere against frustum
  - Computes distance to camera -> selects LOD level
  - Computes fade alpha for fade_distance range
  - Output: appends to per-type, per-LOD IndirectArguments buffer

Stage 3: Occlusion Cull (optional, Hi-Z)
  - Reads previous frame's Hi-Z depth buffer
  - Tests instance screen-space AABB against Hi-Z mip
  - Removes occluded instances from IndirectArguments
  - Output: final IndirectArguments per type per LOD
```

#### IndirectArguments Layout

```cpp
// One IndirectArguments per foliage type per LOD level
// Total buffers = num_foliage_types * num_lods (e.g., 16 types * 5 LODs = 80)

struct IndirectDrawArgs {
    uint32_t index_count_per_instance;
    uint32_t instance_count;    // written by cull compute shader
    uint32_t start_index;
    int32_t  base_vertex;
    uint32_t start_instance;
};
```

Draw submission uses `ExecuteIndirect`:

```cpp
// Per foliage type, per LOD:
cmd->ExecuteIndirect(
    m_indirect_command_sig,
    1,                              // one draw per buffer
    indirect_args_buffer,
    type_lod_offset * sizeof(IndirectDrawArgs),
    nullptr, 0                      // no count buffer (count baked into args)
);
```

### 3.6 Instance Budget and Memory

| Budget | Default | Configurable | Notes |
|--------|---------|-------------|-------|
| Max instances per scene | 2,000,000 | `project.uzproj [foliage] max_instances` | Hard GPU buffer limit |
| GPU memory per 1M instances | ~46 MB | -- | 48 bytes * 1M |
| Spatial hash cell size | 64 m | -- | Balances cull granularity vs overhead |
| Max foliage types per scene | 64 | -- | Limited by type_index uint16 and descriptor table |
| Max LOD levels per type | 5 | -- | LOD0..LOD3 + billboard impostor |
| IndirectArguments buffers | 320 max | -- | 64 types * 5 LODs |

### 3.7 Collision Generation

Per-type collision is generated at runtime for instances within `collision_distance` of any active character controller or physics query origin:

```cpp
// foliage_collision.h

class FoliageCollisionManager {
public:
    // Called each frame after physics step
    void update(
        const entt::registry& world,
        const std::vector<FoliageTypeInstance>& instances,
        const std::vector<FoliageType>& types,
        JPH::PhysicsSystem& jph,
        float dt
    );

private:
    // Active colliders keyed by instance index
    struct ActiveCollider {
        JPH::BodyID body_id;
        uint32_t    instance_index;
        float       distance_sq;
    };

    std::vector<ActiveCollider> m_active;
    float m_update_interval = 0.5f;  // re-evaluate which colliders are active every 500ms
    float m_timer = 0.0f;
};
```

Collider shapes by type:

| `collision_shape` | Jolt Shape | Parameters |
|-------------------|-----------|------------|
| `capsule` | `CapsuleShapeSettings` | `radius`, `height`, `offset` |
| `box` | `BoxShapeSettings` | computed from mesh AABB * scale |
| `convex_hull` | `ConvexHullShapeSettings` | auto-cooked from LOD2 mesh vertices |
| `none` | -- | no physics body created |

### 3.8 Procedural Paint Rules

Procedural paint rules define automated placement that fills areas based on terrain attributes. Rules are evaluated top-to-bottom; each rule either places or excludes instances.

```ini
; Stored in the scene or as a standalone .uzfoliage_rules file

[procedural_rule.0]
name             = "Pine trees on grassy slopes"
foliage_type     = "res://foliage/pine_tree_v2.uzfoliage_type"
density          = 0.05
priority         = 10         ; higher = evaluated later (overrides lower)
; Conditions (all must be true -- AND logic)
min_altitude     = 20.0
max_altitude     = 180.0
min_slope        = 0.0
max_slope        = 25.0
terrain_layer    = 0          ; splatmap layer index
terrain_layer_min_weight = 0.5 ; require >= 50% grass
distance_from_water_min = 5.0
distance_from_path_min  = 2.0

[procedural_rule.1]
name             = "Dense grass on flat terrain"
foliage_type     = "res://foliage/grass_clump.uzfoliage_type"
density          = 4.0
priority         = 5
min_altitude     = 0.0
max_altitude     = 150.0
max_slope        = 15.0
terrain_layer    = 0
terrain_layer_min_weight = 0.3

[procedural_rule.2]
name             = "Rocks on steep slopes"
foliage_type     = "res://foliage/rock_medium.uzfoliage_type"
density          = 0.2
priority         = 15
min_slope        = 30.0
max_slope        = 90.0
```

#### Exclusion Zones

Exclusion zones are axis-aligned or oriented bounding volumes where no procedural foliage is placed. Manual brush-erased areas are stored as exclusion zones and survive procedural regeneration.

```ini
[exclusion_zone.0]
name     = "Town Center"
shape    = "box"
center   = [150.0, 0.0, 200.0]
extents  = [50.0, 100.0, 50.0]  ; half-extents XYZ
rotation = [0.0, 30.0, 0.0]      ; Euler degrees

[exclusion_zone.1]
name     = "Road Corridor"
shape    = "spline"
spline   = "res://paths/main_road.uzspline"
width    = 4.0                     ; meters on each side of spline
```

### 3.9 Wind System Integration

Wind is computed per-vertex in the foliage vertex shader. The system supports three levels of wind detail: trunk sway, branch oscillation, and leaf flutter.

#### Global Wind Component

```cpp
// src/engine/ecs/components.h

struct CSceneWind {
    glm::vec3 direction = {1.0f, 0.0f, 0.0f};  // world-space wind direction (normalized)
    float     speed     = 2.0f;                   // meters/second
    float     gust_frequency = 0.1f;              // Hz
    float     gust_strength  = 0.5f;              // multiplier on base speed
    float     turbulence     = 0.3f;              // high-frequency noise amount
};
```

#### HLSL Wind Shader

```hlsl
// shaders/foliage_wind.hlsli

// SmoothTriangleWave: cheap periodic wave that avoids sin() ALU cost
float SmoothTriangleWave(float x) {
    x = frac(x);
    return abs(x * 2.0 - 1.0);
}

float3 ComputeFoliageWind(
    float3 vertex_pos_local,   // vertex position in model space
    float3 world_pos,          // instance world position
    float  vertex_height_01,   // normalized height: 0=root, 1=top
    float  wind_phase,         // per-instance random phase
    float  time,               // g_Time from GlobalParams
    FoliageWindParams params,  // per-type wind parameters
    float3 wind_dir,           // global wind direction (normalized)
    float  wind_speed,         // global wind speed
    float  gust_amount         // current gust multiplier
) {
    float3 displacement = float3(0, 0, 0);

    // --- Trunk sway: slow, large displacement at trunk base propagating up ---
    float trunk_wave = SmoothTriangleWave(
        time * params.trunk_frequency + wind_phase
    );
    float trunk_mask = vertex_height_01;  // linear: more sway at top
    displacement += wind_dir * trunk_wave * params.trunk_amplitude
                  * trunk_mask * wind_speed;

    // --- Branch oscillation: medium frequency, side-to-side ---
    float branch_wave = SmoothTriangleWave(
        time * params.branch_frequency + wind_phase * 2.3
    );
    float3 branch_dir = cross(wind_dir, float3(0, 1, 0));
    float branch_mask = saturate(vertex_height_01 * 2.0 - 0.5);  // top half only
    displacement += branch_dir * branch_wave * params.branch_amplitude
                  * branch_mask * wind_speed;

    // --- Leaf flutter: high frequency, small displacement, all axes ---
    float leaf_noise = SmoothTriangleWave(
        time * params.leaf_frequency + wind_phase * 5.7 + world_pos.x * 0.1
    );
    float leaf_mask = saturate(vertex_height_01 * 3.0 - 2.0);  // top third only
    displacement += float3(leaf_noise, leaf_noise * 0.3, leaf_noise * 0.7)
                  * params.leaf_amplitude * leaf_mask * wind_speed;

    // --- Gust modulation ---
    displacement *= (1.0 + gust_amount * 0.5);

    return displacement;
}
```

#### Foliage Vertex Shader Integration

```hlsl
// shaders/foliage.hlsl

#include "include/foliage_instance.hlsli"
#include "include/foliage_wind.hlsli"
#include "include/global_params.hlsli"

cbuffer FoliageTypeParams : register(b1) {
    FoliageWindParams g_WindParams;
    float4 g_AlbedoTint;
    float  g_FadeStart;
    float  g_FadeEnd;
    float2 _pad;
};

struct VSIn {
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float2 uv      : TEXCOORD0;
    float4 color   : COLOR0;     // vertex color: R=height_01 for wind mask
    uint   inst_id : SV_InstanceID;
};

VSOut VS_Foliage(VSIn v) {
    VSOut o;

    FoliageInstance inst = g_FoliageInstances[v.inst_id];

    // Decode yaw rotation
    float yaw = (inst.yaw_packed & 0xFFFF) / 65535.0 * 6.283185;
    float cos_y = cos(yaw);
    float sin_y = sin(yaw);
    float3x3 yaw_rot = float3x3(
        cos_y, 0, sin_y,
        0,     1, 0,
        -sin_y, 0, cos_y
    );

    // Apply scale and rotation in model space
    float3 local_pos = mul(yaw_rot, v.pos * inst.scale);

    // Wind displacement (before world translation)
    float vertex_height_01 = v.color.r;  // baked in vertex color channel
    float3 wind_disp = ComputeFoliageWind(
        local_pos, inst.position, vertex_height_01,
        inst.wind_phase, g_Time, g_WindParams,
        g_WindDirection, g_WindSpeed, g_GustAmount
    );
    local_pos += wind_disp;

    // World position
    float3 world_pos = local_pos + inst.position;

    o.pos_cs  = mul(g_ViewProj, float4(world_pos, 1.0));
    o.normal  = mul(yaw_rot, v.normal);
    o.uv      = v.uv;
    o.world   = world_pos;
    o.custom  = inst.custom_data;

    // Distance-based fade (alpha dither in PS)
    float dist = length(world_pos - g_CameraPos);
    o.fade    = saturate((g_FadeEnd - dist) / max(g_FadeEnd - g_FadeStart, 0.001));

    return o;
}
```

### 3.10 Foliage Editor UI -- Detailed Panel

#### Foliage Type Palette

```
+-FOLIAGE TYPE PALETTE-----------+
| [Search foliage types...]      |
+--------------------------------+
| [x] Pine Tree v2          0.05 |  <- checkbox = active for painting
|     [icon] 4 LODs  500m cull   |
|     12,200 instances  0.3ms    |
+--------------------------------+
| [ ] Oak Large              0.02|
|     [icon] 4 LODs  400m cull   |
|      8,100 instances  0.2ms    |
+--------------------------------+
| [x] Grass Clump           4.00 |
|     [icon] 2 LODs   60m cull   |
|    450,000 instances  0.2ms    |
+--------------------------------+
| [ ] Fern Small             0.50|
|     [icon] 2 LODs   40m cull   |
|     22,500 instances  0.1ms    |
+--------------------------------+
| [ ] Rock Medium            0.10|
|     [icon] 3 LODs  200m cull   |
|     19,910 instances  0.3ms    |
+--------------------------------+
| [+ Add Type] [+ Add Cluster]  |
| [Import .uzfoliage_type]      |
+--------------------------------+
```

#### Instance Statistics Panel

```
+-INSTANCE STATISTICS------------+
| Scene Total:  1,241,800        |
| GPU Buffer:   57.1 MB / 92 MB |
| Budget Used:  [========--] 62% |
+--------------------------------+
| TYPE BREAKDOWN:                |
| Name            Total  Visible |
| Pine Tree v2    12,200   8,400 |
| Oak Large        8,100   5,200 |
| Grass Clump    450,000  20,000 |
| Fern Small      22,500   1,100 |
| Rock Medium     19,910   3,040 |
+--------------------------------+
| GPU TIMING:                    |
| Cull compute:      0.15 ms    |
| Draw (all types):  1.05 ms    |
| Total foliage:     1.20 ms    |
+--------------------------------+
| DRAW CALLS:    38              |
|  Pine:  12  Oak: 8  Grass: 4  |
|  Fern:   6  Rock: 8           |
+--------------------------------+
```

#### Density Heatmap Overlay

The density heatmap is a viewport overlay that visualizes instance density as a color gradient projected onto the terrain surface:

```
Density (instances/m^2):
  [0.0]  transparent (no foliage)
  [0.1]  dark blue
  [0.5]  cyan
  [1.0]  green
  [2.0]  yellow
  [4.0]  orange
  [8.0+] red (very dense)
```

Activated via the toolbar: `View > Foliage Density Heatmap` or `Shift+7`.

---

## 4. Part 2 -- PCG (Procedural Content Generation) Graph Editor

### 4.1 Overview

The PCG system is a node-based graph that generates scene content procedurally. A PCG graph reads input data (terrain, splines, volumes), transforms it through filter and scatter operations, and outputs placed instances.

Key properties:
- **Deterministic**: Same seed + same inputs = same output. Always.
- **Non-destructive**: Source data is never modified; outputs can be regenerated at any time.
- **Composable**: PCG graphs can reference other PCG graphs as subgraphs.
- **Dual execution**: runs at author-time (baked to static instances) or at runtime (dynamic generation).

### 4.2 PCG Data Model -- Point Sets

The fundamental data type flowing through a PCG graph is a **PCGPointSet**: an array of points with attributes.

```cpp
// src/engine/pcg/pcg_point_set.h

struct PCGPointSet {
    uint32_t count = 0;

    // Core attributes (always present)
    std::vector<glm::vec3> positions;
    std::vector<glm::quat> rotations;
    std::vector<float>     scales;

    // Optional named attributes (added by nodes)
    struct AttributeBuffer {
        std::string name;
        enum Type { Float, Float2, Float3, Float4, Int } type;
        std::vector<uint8_t> data;  // type-punned buffer
    };
    std::vector<AttributeBuffer> attributes;

    // Attribute access helpers
    float*      get_float_attr(const std::string& name);
    glm::vec3*  get_vec3_attr(const std::string& name);
    int*        get_int_attr(const std::string& name);
    void        add_float_attr(const std::string& name, float default_val);
    void        add_vec3_attr(const std::string& name, glm::vec3 default_val);

    // Memory management
    void reserve(uint32_t n);
    void clear();
    size_t memory_bytes() const;
};
```

### 4.3 PCG Node Types

#### Input Nodes

| Node | Description | Output Attributes |
|------|-------------|-------------------|
| `SampleTerrain` | Generates points on terrain surface at configurable spacing | `position`, `normal`, `slope`, `altitude`, `terrain_layer_weights[4]` |
| `SampleSpline` | Generates points along a spline path at even intervals | `position`, `tangent`, `distance_along`, `spline_t` |
| `SampleVolume` | Fills a box/sphere volume with random or grid points | `position` |
| `SampleMesh` | Scatters points on a mesh surface (Poisson or random) | `position`, `normal`, `triangle_id` |
| `RandomPoints` | N random points within a bounding box | `position` |
| `GridPoints` | Regular grid of points with configurable spacing | `position`, `grid_x`, `grid_z` |
| `PoissonDisk` | Blue-noise distributed points with minimum spacing | `position` |

#### Filter Nodes

| Node | Description | Parameters |
|------|-------------|------------|
| `FilterBySlope` | Removes points on surfaces steeper/shallower than threshold | `min_slope`, `max_slope`, `invert` |
| `FilterByAltitude` | Removes points above/below altitude range | `min_y`, `max_y`, `invert` |
| `FilterByLayerWeight` | Keeps points where splatmap layer weight exceeds threshold | `layer_index`, `min_weight`, `invert` |
| `FilterByDensity` | Randomly removes points to match target density | `target_density`, `seed` |
| `FilterByDistance` | Removes points too close/far from another PCG output or spline | `source`, `min_distance`, `max_distance` |
| `FilterBySteepness` | Like FilterBySlope but uses curvature (second derivative) | `min_curvature`, `max_curvature` |
| `FilterByAttribute` | Generic: removes points where a named attribute is outside range | `attribute_name`, `min`, `max` |
| `FilterByMask` | Removes points outside a grayscale texture mask projected onto terrain | `mask_texture`, `threshold` |

#### Transform Nodes

| Node | Description | Parameters |
|------|-------------|------------|
| `ProjectToTerrain` | Snaps point Y to terrain height at XZ | -- |
| `AlignToNormal` | Sets rotation to align with surface normal | `strength` (0..1), `up_axis` |
| `ScatterInRadius` | Spawns N child points within radius of each input point | `count`, `radius`, `seed` |
| `AddNoise` | Adds Perlin/simplex noise to position, scale, or rotation | `target` (pos/scale/rot), `amplitude`, `frequency`, `seed` |
| `Jitter` | Random offset to position within a range | `max_offset` (vec3) |
| `ClampTransform` | Clamps scale to range, position to bounds | `min_scale`, `max_scale`, `bounds` |
| `SnapToGrid` | Snaps position to regular grid | `grid_size` |
| `OffsetAlongNormal` | Moves points along their normal by a distance | `offset` |

#### Density Nodes

| Node | Description | Parameters |
|------|-------------|------------|
| `DensityByAttribute` | Sets point density from a named attribute (e.g., terrain weight) | `attribute`, `remap_curve` |
| `DensityNoise` | Modulates density by Perlin noise | `frequency`, `amplitude`, `seed` |
| `DensityFromTexture` | Reads density from a projected grayscale texture | `texture`, `channel`, `scale` |
| `DensityFromCurve` | Maps altitude or slope to density via a user-defined curve | `input_attr`, `curve_points[]` |

#### Select Nodes

| Node | Description | Parameters |
|------|-------------|------------|
| `SelectMesh` | Picks a mesh from a weighted list per point (e.g., by attribute) | `mesh_list[]`, `weights[]`, `attribute` |
| `SwitchByBiome` | Routes points to different sub-graphs based on biome map | `biome_map`, `biome_channels[]` |

#### Output Nodes

| Node | Description | Output |
|------|-------------|--------|
| `SpawnFoliage` | Creates foliage instances consumed by `FoliageRenderer` | Writes to `FoliageInstanceBuffer` |
| `SpawnMeshInstances` | Creates instanced static mesh draws via `BatchBuilder` | Writes to scene instancing system |
| `SpawnSceneNode` | Creates full scene nodes (with components) in the scene tree | Writes to `SceneManager` |
| `SpawnAudioEmitter` | Places `CAudioSource` nodes at point positions | Writes to scene tree |
| `SpawnLight` | Places `PointLight` or `SpotLight` nodes | Writes to scene tree |
| `WriteAttribute` | Writes a computed value back to points (for downstream nodes) | Modifies point set attribute |

### 4.4 PCG Graph Execution

#### Execution Modes

| Mode | When | Persistence | Use Case |
|------|------|-------------|----------|
| **On Build** | Author presses "Regenerate" or saves scene | Instances baked to `.uzfoliage` + `.uzscene` | Final production content |
| **On Load** | Scene loads at runtime | Generated in memory, not saved to disk | Lightweight variation per playthrough |
| **Dynamic** | Runtime, reacts to parameter changes | In memory, regenerated when inputs change | Interactive terrain modification, player actions |

#### Execution Pipeline

```
1. Topological sort: order nodes by dependency (inputs first, outputs last)
2. For each node in order:
   a. Read input point sets from connected upstream nodes
   b. Execute node logic (CPU or GPU compute)
   c. Write output point set
   d. Report progress: input_count -> output_count, time_ms
3. For each output node:
   a. Convert final point set to target format (FoliageInstance, SceneNode, etc.)
   b. Upload to GPU buffers or insert into scene tree
4. Cache results per node for interactive editing (change one param -> re-execute from that node)
```

#### Deterministic Seeds

```cpp
// Seed computation: deterministic from graph seed + node index + position
uint32_t compute_point_seed(uint32_t graph_seed, uint32_t node_id, glm::vec3 position) {
    uint32_t h = graph_seed;
    h = hash_combine(h, node_id);
    h = hash_combine(h, bit_cast<uint32_t>(position.x));
    h = hash_combine(h, bit_cast<uint32_t>(position.z));
    return h;
}
```

#### GPU Acceleration

Filter and transform nodes that process large point sets (>10,000 points) automatically dispatch as compute shaders:

```hlsl
// shaders/pcg/pcg_filter_slope.hlsl

StructuredBuffer<PCGPoint>  g_InputPoints  : register(t0);
RWStructuredBuffer<PCGPoint> g_OutputPoints : register(u0);
RWBuffer<uint>              g_OutputCount  : register(u1);

cbuffer FilterParams : register(b0) {
    float g_MinSlope;
    float g_MaxSlope;
    uint  g_InputCount;
    uint  g_Invert;
};

[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_InputCount) return;

    PCGPoint pt = g_InputPoints[id.x];
    float slope = acos(saturate(pt.normal.y)) * 57.2958;  // degrees

    bool pass = (slope >= g_MinSlope) && (slope <= g_MaxSlope);
    if (g_Invert) pass = !pass;

    if (pass) {
        uint idx;
        InterlockedAdd(g_OutputCount[0], 1, idx);
        g_OutputPoints[idx] = pt;
    }
}
```

#### Streaming: Per-Tile Execution

For large worlds using `.uzworld` tile streaming, PCG graphs execute per tile:

1. Tile begins loading (enters `stream_radius`).
2. PCG graph is executed with the tile's heightmap, splatmap, and bounding box as inputs.
3. Output instances are appended to the tile's `FoliageInstanceBuffer`.
4. When tile unloads, generated instances are discarded.

This ensures memory usage scales with visible area, not total world size.

### 4.5 Biome System

Biomes are named collections of PCG rules and foliage clusters that define the ecological character of a terrain region.

#### Biome Definition (`.uzbiome`)

```ini
[uzbiome version=1 name="TemperateForest"]
description = "Dense mixed forest with conifers, deciduous trees, and undergrowth"

; Biome map channel assignment
; The biome map is an RGBA texture where each channel = one biome weight
; This biome reads from the R channel
biome_channel = 0   ; R=0, G=1, B=2, A=3

; PCG graph executed for this biome
pcg_graph = "res://pcg/temperate_forest.uzpcg"

; Foliage cluster used as fallback for manual painting in this biome
default_cluster = "res://foliage/clusters/dense_forest.uzfoliage_cluster"

; Ambient audio
ambient_audio = "res://audio/ambient/forest_birds.wav"
ambient_volume = 0.6

; Color tint for editor biome overlay visualization
editor_color = [0.2, 0.6, 0.1, 0.5]
```

#### Biome Map

The biome map is a texture (same resolution as splatmap) where each RGBA channel represents the weight of one biome:

```
biome_map.png (RGBA, 8-bit per channel):
  R = Biome 0 weight (e.g., TemperateForest)
  G = Biome 1 weight (e.g., Alpine)
  B = Biome 2 weight (e.g., Grassland)
  A = Biome 3 weight (e.g., Desert)
```

For more than 4 biomes, a second biome map texture is used (same pattern as splatmap layers 4-7).

Maximum biomes: **8** (2 RGBA textures).

#### Biome Blending

At boundaries between biomes, weights are interpolated. The PCG system handles blending by:

1. Sampling the biome map at each generated point's XZ position.
2. If the dominant biome weight > 0.7, use that biome's PCG graph exclusively.
3. If no biome dominates (all weights < 0.7), run all active biome PCG graphs and scale instance density by biome weight. This naturally produces mixed-biome transition zones.

#### Quick-Apply Biome

In the editor: drag a `.uzbiome` asset from the Asset Browser onto a world tile. The system:
1. Paints the biome map channel to 1.0 across the entire tile.
2. Executes the biome's PCG graph for that tile.
3. Records both operations as undoable commands.

### 4.6 PCG File Format (`.uzpcg`)

```ini
[uzpcg version=1 name="ForestBiome_PCG"]
description = "Generates a mixed conifer/deciduous forest with undergrowth and rock scatter"
execution_mode = "on_build"    ; on_build | on_load | dynamic
seed = 42

; ============================================================
; NODE DEFINITIONS
; ============================================================

[node id=1 type=SampleTerrain name="TerrainSamples"]
spacing          = 2.0         ; meters between sample points
bounds           = "tile"      ; "tile" = entire current tile, or explicit [x0,z0,x1,z1]

[node id=2 type=FilterBySlope name="FlatAreas"]
min_slope        = 0.0
max_slope        = 25.0
invert           = false

[node id=3 type=FilterByAltitude name="ForestAltitude"]
min_y            = 5.0
max_y            = 180.0
invert           = false

[node id=4 type=FilterByLayerWeight name="GrassOnly"]
layer_index      = 0           ; splatmap layer 0 = Grass
min_weight       = 0.4
invert           = false

[node id=5 type=PoissonDisk name="TreeSpacing"]
min_distance     = 4.0         ; meters between trees
seed_offset      = 0

[node id=6 type=AlignToNormal name="AlignTrees"]
strength         = 0.6

[node id=7 type=AddNoise name="ScaleVariation"]
target           = "scale"     ; position | scale | rotation
amplitude        = 0.4
frequency        = 0.1
seed_offset      = 100

[node id=8 type=SelectMesh name="PickTreeSpecies"]
mesh_list        = [
    "res://foliage/pine_tree_v2.uzfoliage_type",
    "res://foliage/oak_large.uzfoliage_type",
    "res://foliage/birch_medium.uzfoliage_type"
]
weights          = [0.5, 0.3, 0.2]
attribute        = ""          ; empty = random weighted selection

[node id=9 type=SpawnFoliage name="PlaceTrees"]
; No extra params needed; reads from upstream point set + mesh selection

; --- Undergrowth sub-chain ---

[node id=10 type=FilterBySlope name="UndergrowthSlope"]
min_slope        = 0.0
max_slope        = 15.0
invert           = false

[node id=11 type=DensityNoise name="UndergrowthDensity"]
frequency        = 0.05
amplitude        = 0.6
seed_offset      = 200

[node id=12 type=SelectMesh name="PickUndergrowth"]
mesh_list        = [
    "res://foliage/fern_small.uzfoliage_type",
    "res://foliage/grass_clump.uzfoliage_type",
    "res://foliage/wildflower.uzfoliage_type"
]
weights          = [0.3, 0.5, 0.2]

[node id=13 type=SpawnFoliage name="PlaceUndergrowth"]

; --- Rock scatter sub-chain ---

[node id=14 type=FilterBySlope name="SteepSlopes"]
min_slope        = 25.0
max_slope        = 60.0
invert           = false

[node id=15 type=FilterByDensity name="SparseRocks"]
target_density   = 0.1
seed_offset      = 300

[node id=16 type=AddNoise name="RockScaleNoise"]
target           = "scale"
amplitude        = 0.8
frequency        = 0.2
seed_offset      = 400

[node id=17 type=SpawnFoliage name="PlaceRocks"]
foliage_type     = "res://foliage/rock_medium.uzfoliage_type"

; ============================================================
; CONNECTIONS (edge list)
; ============================================================

[connections]
; format: source_node.output_pin -> target_node.input_pin
1.points   -> 2.points          ; TerrainSamples -> FlatAreas
2.points   -> 3.points          ; FlatAreas -> ForestAltitude
3.points   -> 4.points          ; ForestAltitude -> GrassOnly

; Tree chain
4.points   -> 5.points          ; GrassOnly -> TreeSpacing (Poisson)
5.points   -> 6.points          ; TreeSpacing -> AlignTrees
6.points   -> 7.points          ; AlignTrees -> ScaleVariation
7.points   -> 8.points          ; ScaleVariation -> PickTreeSpecies
8.points   -> 9.points          ; PickTreeSpecies -> PlaceTrees (output)

; Undergrowth chain (branches from GrassOnly)
4.points   -> 10.points         ; GrassOnly -> UndergrowthSlope
10.points  -> 11.points         ; UndergrowthSlope -> UndergrowthDensity
11.points  -> 12.points         ; UndergrowthDensity -> PickUndergrowth
12.points  -> 13.points         ; PickUndergrowth -> PlaceUndergrowth (output)

; Rock chain (branches from TerrainSamples, different slope filter)
1.points   -> 14.points         ; TerrainSamples -> SteepSlopes
14.points  -> 15.points         ; SteepSlopes -> SparseRocks
15.points  -> 16.points         ; SparseRocks -> RockScaleNoise
16.points  -> 17.points         ; RockScaleNoise -> PlaceRocks (output)
```

### 4.7 PCG Editor UI -- Detailed

#### Node Canvas

The node canvas is the main editing area. Nodes are rendered as rounded boxes with input pins (left) and output pins (right). Connections are drawn as curved Bezier lines.

```
+-- NODE CANVAS ---------------------------------------------------+
|                                                                   |
|  +================+       +=================+                     |
|  | SampleTerrain  |       | FilterBySlope   |                     |
|  |  spacing: 2.0  |       |  min: 0  max:25 |                     |
|  |  bounds: tile   |----->|  invert: no     |---+                 |
|  +================+  |    +=================+   |                 |
|                      |                          |                 |
|                      |    +=================+   |                 |
|                      |    | FilterBySlope   |   |                 |
|                      +--->|  min:25 max:60  |   |                 |
|                           |  invert: no     |---+-->[...rocks]    |
|                           +=================+   |                 |
|                                                 |                 |
|          +==================+                   |                 |
|          | FilterByAltitude |<------------------+                 |
|          |  min:5  max:180  |                                     |
|          +==================+                                     |
|                  |                                                |
|                  v                                                |
|          +==================+     +=============+                 |
|          | PoissonDisk      |---->| AlignNormal |-->[...trees]    |
|          |  min_dist: 4.0   |     |  str: 0.6   |                |
|          +==================+     +=============+                 |
|                                                                   |
+-------------------------------------------------------------------+
| Zoom: 100%  |  Nodes: 17  |  Connections: 16  |  [Fit All]       |
+-------------------------------------------------------------------+
```

#### Point Preview Mini-Viewport

A small viewport in the bottom-left corner shows the selected node's output point set as colored dots projected onto a simplified terrain mesh. Colors indicate attributes:

```
+--POINT PREVIEW (256x256)------+
|                                |
|  . . .  . .  . . . . .  . .   |  Green dots: trees (passed all filters)
|   .  . .  . .   . .  . . .    |  Blue dots: undergrowth
|  . . . .   . .  . . . .  .    |  Gray dots: rocks
|   .  .  . .  .   .  . .  .    |  Red dots: filtered out (shown dimmed)
|  . . .  . . .  . .  .  . .    |
|                                |
| Selected: FilterBySlope        |
| In: 48,200  Out: 31,440       |
| Ratio: 65.2%                  |
+--------------------------------+
```

#### Properties Panel (Right Side)

When a node is selected in the canvas, its properties are shown in the right panel:

```
+-NODE PROPERTIES----------------+
| FilterBySlope                  |
| ID: 2                          |
+--------------------------------+
| PARAMETERS                     |
| Min Slope   [====] 0.0 deg    |
| Max Slope   [======] 25.0 deg |
| Invert      [ ]               |
+--------------------------------+
| STATISTICS                     |
| Input points:   48,200        |
| Output points:  31,440        |
| Filter ratio:   65.2%         |
| Exec time:      12 ms         |
+--------------------------------+
| PREVIEW                        |
| [x] Show in point preview     |
| Color: [GREEN ####]           |
+--------------------------------+
| CONNECTIONS                    |
| In:  SampleTerrain.points     |
| Out: FilterByAltitude.points  |
+--------------------------------+
```

### 4.8 Integration with Level Blueprint / Lua

PCG outputs can be tagged and queried from Lua scripts at runtime:

```lua
-- Query PCG-generated instances by tag
local trees = ctx.pcg:get_instances("trees_oak")
-- Returns: array of { position: Vec3, rotation: Quat, scale: number }

for _, tree in ipairs(trees) do
    local dist = (tree.position - player_pos):length()
    if dist < 10.0 then
        -- Player is near a tree, trigger gameplay event
        ctx.events:emit("near_tree", { position = tree.position })
    end
end

-- Runtime PCG: spawn a PCG volume that generates content dynamically
local vol = ctx.pcg:create_volume("res://pcg/campsite.uzpcg", {
    position = Vec3(100, 0, 200),
    extents  = Vec3(20, 10, 20),
    seed     = math.random(1, 99999)
})

-- Later: destroy the volume (removes all generated content)
ctx.pcg:destroy_volume(vol)

-- Force regeneration of a named PCG graph
ctx.pcg:regenerate("ForestBiome_PCG", { seed = 123 })
```

### 4.9 PCG C++ Class Hierarchy

```cpp
// src/engine/pcg/pcg_graph.h

class PCGGraph {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Node management
    PCGNodeId add_node(PCGNodeType type, const std::string& name);
    void      remove_node(PCGNodeId id);
    PCGNode*  get_node(PCGNodeId id);

    // Connection management
    void connect(PCGNodeId source, const std::string& output_pin,
                 PCGNodeId target, const std::string& input_pin);
    void disconnect(PCGNodeId source, const std::string& output_pin,
                    PCGNodeId target, const std::string& input_pin);

    // Execution
    PCGExecutionResult execute(const PCGExecutionContext& ctx);

    // Metadata
    const std::string&  name() const;
    PCGExecutionMode    execution_mode() const;
    uint32_t            seed() const;

private:
    std::string                       m_name;
    PCGExecutionMode                  m_mode;
    uint32_t                          m_seed;
    std::vector<std::unique_ptr<PCGNode>> m_nodes;
    std::vector<PCGConnection>        m_connections;
};

// src/engine/pcg/pcg_node.h

struct PCGNodeId { uint32_t value; };

enum class PCGNodeType : uint16_t {
    // Inputs
    SampleTerrain, SampleSpline, SampleVolume, SampleMesh,
    RandomPoints, GridPoints, PoissonDisk,
    // Filters
    FilterBySlope, FilterByAltitude, FilterByLayerWeight,
    FilterByDensity, FilterByDistance, FilterBySteepness,
    FilterByAttribute, FilterByMask,
    // Transforms
    ProjectToTerrain, AlignToNormal, ScatterInRadius,
    AddNoise, Jitter, ClampTransform, SnapToGrid, OffsetAlongNormal,
    // Density
    DensityByAttribute, DensityNoise, DensityFromTexture, DensityFromCurve,
    // Select
    SelectMesh, SwitchByBiome,
    // Outputs
    SpawnFoliage, SpawnMeshInstances, SpawnSceneNode,
    SpawnAudioEmitter, SpawnLight, WriteAttribute,

    _Count
};

class PCGNode {
public:
    virtual ~PCGNode() = default;

    PCGNodeId           id() const;
    PCGNodeType         type() const;
    const std::string&  name() const;

    // Pin definitions (overridden per type)
    virtual std::vector<PCGPin> input_pins() const = 0;
    virtual std::vector<PCGPin> output_pins() const = 0;

    // Execution
    virtual PCGNodeResult execute(
        const PCGNodeInputs& inputs,
        const PCGExecutionContext& ctx
    ) = 0;

    // Serialization
    virtual void serialize(IniWriter& writer) const = 0;
    virtual void deserialize(const IniSection& section) = 0;

    // Properties (for editor UI)
    virtual std::vector<PCGProperty> properties() const = 0;
    virtual void set_property(const std::string& key, const JsonValue& val) = 0;

protected:
    PCGNodeId   m_id;
    PCGNodeType m_type;
    std::string m_name;
};

struct PCGPin {
    std::string name;
    enum Type { PointSet, Attribute, Mesh, Texture } type;
};

struct PCGConnection {
    PCGNodeId   source_node;
    std::string source_pin;
    PCGNodeId   target_node;
    std::string target_pin;
};

// src/engine/pcg/pcg_node_registry.h

class PCGNodeRegistry {
public:
    static PCGNodeRegistry& instance();

    // Register a node factory
    using Factory = std::function<std::unique_ptr<PCGNode>()>;
    void register_type(PCGNodeType type, const std::string& display_name,
                       const std::string& category, Factory factory);

    // Create node by type
    std::unique_ptr<PCGNode> create(PCGNodeType type) const;

    // List all registered types (for editor palette)
    struct TypeInfo {
        PCGNodeType type;
        std::string display_name;
        std::string category;  // "Input", "Filter", "Transform", etc.
    };
    const std::vector<TypeInfo>& all_types() const;

private:
    std::vector<TypeInfo> m_types;
    std::unordered_map<PCGNodeType, Factory> m_factories;
};

// src/engine/pcg/pcg_executor.h

struct PCGExecutionContext {
    const TerrainData*      terrain;        // heightmap, splatmap, normals
    const SplineData*       splines;        // world splines (roads, rivers)
    const VolumeData*       volumes;        // box/sphere volumes in scene
    entt::registry*         world;          // ECS registry for output nodes
    SceneManager*           scene;          // scene tree for SpawnSceneNode
    FoliageSystem*          foliage;        // foliage system for SpawnFoliage
    uint32_t                seed;           // graph-level seed
    AABB                    bounds;         // execution bounds (tile or full scene)
    ID3D12Device*           device;         // for GPU compute dispatch
    ID3D12GraphicsCommandList* cmd;         // command list for GPU nodes

    // Progress reporting
    std::function<void(float progress, const std::string& node_name)> on_progress;
};

struct PCGExecutionResult {
    bool     success;
    float    total_time_ms;
    uint32_t total_input_points;
    uint32_t total_output_points;
    struct PerNodeStats {
        PCGNodeId   node_id;
        std::string node_name;
        uint32_t    input_count;
        uint32_t    output_count;
        float       time_ms;
    };
    std::vector<PerNodeStats> per_node;
};

class PCGExecutor {
public:
    // Execute a graph within a context
    PCGExecutionResult execute(PCGGraph& graph, const PCGExecutionContext& ctx);

private:
    // Topological sort of nodes by dependency
    std::vector<PCGNodeId> topological_sort(const PCGGraph& graph) const;

    // Node output cache (invalidated when upstream params change)
    std::unordered_map<PCGNodeId, PCGPointSet> m_cache;

    // GPU dispatch for compute-accelerated nodes
    void dispatch_gpu_node(PCGNode* node, const PCGNodeInputs& inputs,
                           const PCGExecutionContext& ctx, PCGPointSet& output);

    // Threshold for GPU dispatch (points below this run on CPU)
    static constexpr uint32_t GPU_DISPATCH_THRESHOLD = 10000;
};
```

### 4.10 Foliage System -- Integration Point

The `FoliageSystem` is the runtime system that manages all foliage instances, whether hand-painted or PCG-generated. It consumes `PCGPointSet` outputs from `SpawnFoliage` nodes and merges them with brush-painted instances.

```cpp
// src/engine/foliage/foliage_system.h

class FoliageSystem {
public:
    void init(ID3D12Device* device, uint32_t max_instances);
    void shutdown();

    // --- Instance management ---

    // Add instances from brush painting (manual)
    void add_painted_instances(uint32_t type_index,
                                Span<const FoliageInstance> instances);

    // Remove painted instances in a region
    void erase_painted_instances(uint32_t type_index,
                                  glm::vec2 center_xz, float radius);

    // Set PCG-generated instances (replaces previous PCG output for this graph)
    void set_pcg_instances(const std::string& pcg_graph_name,
                            uint32_t type_index,
                            Span<const FoliageInstance> instances);

    // Clear all PCG instances from a specific graph
    void clear_pcg_instances(const std::string& pcg_graph_name);

    // --- Per-frame pipeline ---

    // Upload dirty instance buffers to GPU
    void upload(ID3D12GraphicsCommandList* cmd);

    // Dispatch cull compute shader
    void cull(ID3D12GraphicsCommandList* cmd,
              const CCamera& camera, const Frustum& frustum);

    // Submit ExecuteIndirect draw calls
    void draw(ID3D12GraphicsCommandList* cmd);

    // --- Queries ---

    uint32_t total_instances() const;
    uint32_t visible_instances() const;
    uint32_t draw_call_count() const;
    float    gpu_time_ms() const;

    struct TypeStats {
        uint32_t type_index;
        std::string name;
        uint32_t total;
        uint32_t visible;
        uint32_t draw_calls;
        float    gpu_time_ms;
    };
    std::vector<TypeStats> per_type_stats() const;

private:
    // Merged instance buffer (painted + PCG)
    struct TypeBuffer {
        std::vector<FoliageInstance> painted;
        std::unordered_map<std::string, std::vector<FoliageInstance>> pcg;
        GpuBuffer*  gpu_buffer;          // merged StructuredBuffer on GPU
        bool        dirty;               // needs re-upload
        uint32_t    total_count;
    };

    std::vector<TypeBuffer>        m_type_buffers;
    std::vector<const FoliageType*> m_types;

    // GPU resources
    GpuBuffer*      m_indirect_args_buffer;
    GpuBuffer*      m_spatial_hash_buffer;
    ID3D12PipelineState* m_cull_pso;
    ID3D12PipelineState* m_foliage_pso[5];  // per-LOD PSO
    ID3D12CommandSignature* m_indirect_cmd_sig;

    uint32_t m_max_instances;
    uint32_t m_visible_count;
};
```

---

## 5. IPC Handlers -- Full Reference

### Foliage Editor IPC

```
; Foliage type management
foliage.loadType         { path }                    -> { type_index }
foliage.unloadType       { type_index }              -> { ok }
foliage.getTypeList      {}                          -> { types: [{index, name, path}] }

; Cluster management
foliage.loadCluster      { path }                    -> { cluster_index }
foliage.getClusterList   {}                          -> { clusters: [{index, name, path}] }

; Painting
foliage.paint            { type_or_cluster, center:[x,z], radius, density_mult, align, ground_only }
foliage.erase            { type_index, center:[x,z], radius }
foliage.placeSingle      { type_index, position:[x,y,z], rotation:[x,y,z,w], scale }
foliage.fill             { type_or_cluster, polygon:[[x,z],...], density }
foliage.reapply          { instance_ids:[], randomize_scale, randomize_yaw }
foliage.lassoSelect      { polygon:[[x,z],...] }     -> { instance_ids:[] }
foliage.deleteSelected   { instance_ids:[] }         -> { ok }

; Procedural rules
foliage.addRule          { rule_def }                -> { rule_index }
foliage.removeRule       { rule_index }              -> { ok }
foliage.setExclusionZone { zone_def }               -> { zone_index }
foliage.regenerateProcedural {}                      -> streams progress events

; Statistics
foliage.getStats         {}                          -> { total, visible, gpu_time, per_type:[...] }
foliage.getDensityMap    { resolution }              -> { png_base64 }

; Wind
foliage.setWind          { direction:[x,y,z], speed, gust_freq, gust_str }
```

### PCG Editor IPC

```
; Graph management
pcg.loadGraph            { path }                    -> { graph_id }
pcg.saveGraph            { graph_id, path }          -> { ok }
pcg.newGraph             { name, execution_mode }    -> { graph_id }
pcg.getGraphList         {}                          -> { graphs: [{id, name, path}] }

; Node management
pcg.addNode              { graph_id, type, name, position:[x,y] } -> { node_id }
pcg.removeNode           { graph_id, node_id }       -> { ok }
pcg.setNodeProperty      { graph_id, node_id, key, value }
pcg.getNodeProperties    { graph_id, node_id }       -> { properties:{} }

; Connections
pcg.connect              { graph_id, src_node, src_pin, dst_node, dst_pin } -> { ok }
pcg.disconnect           { graph_id, src_node, src_pin, dst_node, dst_pin } -> { ok }
pcg.getConnections       { graph_id }                -> { connections:[...] }

; Execution
pcg.execute              { graph_id, seed }          -> streams progress events -> { result }
pcg.getNodePreview       { graph_id, node_id }       -> { points_count, preview_png_base64 }
pcg.exportInstances      { graph_id, output_path }   -> { ok, instance_count }

; Biome
pcg.loadBiome            { path }                    -> { biome_id }
pcg.applyBiome           { biome_id, tile }          -> streams progress events
pcg.getBiomeList         {}                          -> { biomes: [{id, name, channel, color}] }
pcg.paintBiomeMap        { biome_id, center:[x,z], radius, strength }

; Runtime PCG volumes
pcg.createVolume         { graph_path, position, extents, seed } -> { volume_id }
pcg.destroyVolume        { volume_id }               -> { ok }
pcg.getVolumeList        {}                          -> { volumes:[...] }
```

---

## 6. Configuration (`project.uzproj`)

```toml
[foliage]
max_instances          = 2000000     # hard GPU buffer limit
spatial_hash_cell_size = 64.0        # meters, for hierarchical culling
enable_occlusion_cull  = true        # Hi-Z occlusion culling for foliage
collision_distance     = 50.0        # default distance for foliage colliders
billboard_quality      = "high"      # low (4x4 atlas) | medium (6x6) | high (8x8)
wind_enabled           = true        # global wind toggle

[pcg]
gpu_dispatch_threshold = 10000       # point count above which nodes run on GPU compute
max_points_per_graph   = 500000      # safety limit per PCG graph execution
cache_enabled          = true        # cache intermediate node results
execution_timeout_ms   = 30000       # kill graph execution after 30s
```

---

## 7. System Limits

| Constraint | Value | Notes |
|------------|-------|-------|
| Max foliage instances per scene | 2,000,000 | Configurable; ~92 MB GPU at 48 bytes/instance |
| Max foliage types per scene | 64 | `type_index` is uint16 but descriptor table limited |
| Max LOD levels per type | 5 | LOD0..LOD3 + billboard impostor |
| Max foliage clusters | 32 | Editor palette limit |
| Max members per cluster | 16 | Per `.uzfoliage_cluster` file |
| Max procedural paint rules | 64 | Per scene |
| Max exclusion zones | 256 | Per scene |
| Max PCG graphs per scene | 32 | Loaded simultaneously |
| Max nodes per PCG graph | 256 | Editor canvas limit |
| Max connections per graph | 512 | |
| Max points per PCG execution | 500,000 | Configurable safety limit |
| Max biomes | 8 | 2 RGBA biome map textures |
| GPU cull compute dispatch | 256 threads/group | Standard compute shader group size |
| Spatial hash cell size | 64 m | Non-configurable at runtime |
| Billboard atlas max | 8x8 (64 views) | Per foliage type |
| PCG execution timeout | 30 seconds | Configurable |
| Foliage physics colliders (simultaneous) | 512 | Jolt body budget for foliage |

---

## 8. File Structure

```
src/engine/foliage/
    foliage_type.h/cpp              -- FoliageType asset definition, loader/serializer
    foliage_cluster.h/cpp           -- FoliageCluster: multi-type group definition
    foliage_instance.h              -- FoliageInstance GPU struct (48 bytes)
    foliage_system.h/cpp            -- FoliageSystem: instance management, upload, cull, draw
    foliage_painter.h/cpp           -- Brush painting, erase, fill, lasso select
    foliage_collision.h/cpp         -- Dynamic capsule/box collider management
    foliage_billboard.h/cpp         -- Impostor atlas generation and rendering
    foliage_wind.h/cpp              -- CSceneWind component, wind parameter upload

src/engine/pcg/
    pcg_graph.h/cpp                 -- PCGGraph: node graph container, load/save .uzpcg
    pcg_node.h/cpp                  -- PCGNode base class, PCGPin, PCGConnection
    pcg_node_registry.h/cpp         -- PCGNodeRegistry: factory for all node types
    pcg_executor.h/cpp              -- PCGExecutor: topological sort, execute, GPU dispatch
    pcg_point_set.h/cpp             -- PCGPointSet: array of points with attributes
    pcg_biome.h/cpp                 -- Biome definition, biome map sampling, blending

src/engine/pcg/nodes/
    pcg_input_nodes.h/cpp           -- SampleTerrain, SampleSpline, SampleVolume, etc.
    pcg_filter_nodes.h/cpp          -- FilterBySlope, FilterByAltitude, etc.
    pcg_transform_nodes.h/cpp       -- ProjectToTerrain, AlignToNormal, AddNoise, etc.
    pcg_density_nodes.h/cpp         -- DensityByAttribute, DensityNoise, etc.
    pcg_select_nodes.h/cpp          -- SelectMesh, SwitchByBiome
    pcg_output_nodes.h/cpp          -- SpawnFoliage, SpawnMeshInstances, SpawnSceneNode, etc.

src/engine/foliage/shaders/
    foliage.hlsl                    -- Foliage VS + PS (wind, LOD morph, fade)
    foliage_wind.hlsli              -- SmoothTriangleWave, ComputeFoliageWind
    foliage_instance.hlsli          -- FoliageInstance struct for GPU
    foliage_cull.hlsl               -- Compute: frustum + LOD + occlusion cull
    foliage_billboard.hlsl          -- Billboard impostor rendering
    foliage_shadow.hlsl             -- Depth-only foliage shadow pass

src/engine/pcg/shaders/
    pcg_filter_slope.hlsl           -- Compute: slope filter on point buffer
    pcg_filter_altitude.hlsl        -- Compute: altitude filter
    pcg_filter_density.hlsl         -- Compute: density-based random removal
    pcg_transform_noise.hlsl        -- Compute: noise displacement on points
    pcg_poisson_disk.hlsl           -- Compute: Poisson disk point generation
```

---

## 9. Implementation Priority

| Phase | Feature | Complexity |
|-------|---------|-----------|
| 4.1 | `FoliageType` asset loader + `.uzfoliage_type` format | Medium |
| 4.1 | `FoliageSystem` with GPU StructuredBuffer + ExecuteIndirect | High |
| 4.1 | GPU cull compute shader (frustum + distance + LOD) | High |
| 4.1 | Brush painting tools (paint, erase, single place) | Medium |
| 4.2 | Wind system (HLSL `foliage_wind.hlsli` + `CSceneWind`) | Medium |
| 4.2 | Foliage clusters (`.uzfoliage_cluster`) | Low |
| 4.2 | Procedural paint rules + exclusion zones | Medium |
| 4.2 | Foliage collision manager | Medium |
| 4.3 | PCG node framework (`PCGGraph`, `PCGNode`, `PCGExecutor`) | High |
| 4.3 | Input nodes (SampleTerrain, PoissonDisk, GridPoints) | Medium |
| 4.3 | Filter nodes (slope, altitude, layer weight, density) | Medium |
| 4.3 | Transform nodes (AlignToNormal, AddNoise, Jitter) | Medium |
| 4.3 | Output nodes (SpawnFoliage, SpawnMeshInstances) | Medium |
| 4.4 | PCG Editor UI (node canvas, point preview, properties) | High |
| 4.4 | GPU compute acceleration for PCG nodes | High |
| 4.4 | Biome system (`.uzbiome`, biome map, blending) | Medium |
| 4.4 | Billboard impostor rendering | Medium |
| 4.5 | Lasso select, reapply, fill tools | Low |
| 4.5 | Hi-Z occlusion culling for foliage | Medium |
| 4.5 | Runtime PCG volumes (Lua API) | Medium |
| 4.5 | Density heatmap overlay | Low |

---

## 10. Related Documentation

- [terrain-editor.md](terrain-editor.md) -- Terrain sculpt/paint tools, basic foliage painting (Section 5)
- [specs/terrain-system.md](../specs/terrain-system.md) -- Terrain runtime: heightmap, splatmap, chunk LOD
- [specs/gpu-instancing.md](../specs/gpu-instancing.md) -- `BatchBuilder`, `StructuredBuffer<float4x4>`, `ExecuteIndirect`
- [specs/scene-system.md](../specs/scene-system.md) -- Node tree, `.uzscene` format, `SceneManager` API
- [specs/shader-library.md](../specs/shader-library.md) -- `GlobalParams` cbuffer at b15, shared HLSL includes
- [specs/shader-variants.md](../specs/shader-variants.md) -- `INSTANCED` define, PSO cache
- [specs/editor-panels.md](../specs/editor-panels.md) -- Editor panel WebView architecture, IPC protocol
- [specs/undo-redo.md](../specs/undo-redo.md) -- `UndoStack`, `EditorCommand` pattern
- [specs/physics-system.md](../specs/physics-system.md) -- Jolt integration for foliage colliders
- [specs/tags-groups.md](../specs/tags-groups.md) -- CTags used for foliage type tagging
- [specs/lua-scripting.md](../specs/lua-scripting.md) -- `ctx.pcg:*` Lua API for runtime PCG
