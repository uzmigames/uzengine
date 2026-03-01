# NavMesh / Pathfinding Spec

## Overview

UzEngine's navigation system provides automatic pathfinding for AI agents using **Recast** (navmesh generation) and **Detour** (path query at runtime), both MIT-licensed and battle-tested in Godot, Unity, and hundreds of indie games. Local collision avoidance uses **RVO2** (Reciprocal Velocity Obstacles) so multiple agents navigate smoothly around each other without constant replanning. The system supports up to 256 simultaneous agents per scene, all sharing a single baked navmesh.

---

## 1. Library Stack

| Library | Role | License |
|---------|------|---------|
| **Recast** | Voxelization + navmesh polygon generation | MIT |
| **Detour** | Runtime path query, path following, crowd | MIT |
| **DetourCrowd** | Multi-agent local steering + RVO2 | MIT |
| **DetourTileCache** | Dynamic obstacle runtime rebuild | MIT |

All four are part of the `recastnavigation` repository. Integrated via vcpkg.

---

## 2. Navmesh Baking

### Bake Configuration (`.uzscene` or `project.uzproj`)

```ini
[navmesh]
cell_size        = 0.3    ; voxel XZ size (meters) — smaller = more precise, slower bake
cell_height      = 0.2    ; voxel Y size (meters)
agent_radius     = 0.4    ; minimum corridor width = 2 * agent_radius
agent_height     = 1.8    ; minimum ceiling clearance
agent_max_climb  = 0.4    ; maximum step height
agent_max_slope  = 45.0   ; maximum walkable slope (degrees)
edge_max_len     = 12.0   ; maximum polygon edge length
edge_max_error   = 1.3    ; edge simplification tolerance
region_min_size  = 8      ; minimum region area (voxels²)
region_merge_size = 20    ; region merge threshold (voxels²)
```

### Bake Modes

| Mode | When | Output |
|------|------|--------|
| **Offline (Cook)** | `UzBuild cook` step | `.import/navmesh/scene_name.uznavmesh` binary |
| **Runtime (Scene Load)** | When `runtime_bake = true` in `.uzscene` navmesh block | Baked in background thread on load |

Offline baking is preferred for shipped games (zero runtime overhead). Runtime baking is useful during development or for procedurally generated levels.

### Bake Process (C++)

```cpp
// src/navigation/NavBaker.h
class NavBaker {
public:
    // Collect all StaticBody + StaticMesh geometry in scene
    void collect_geometry(const SceneManager& scene, rcContext& ctx);

    // Recast pipeline: voxelize → filter → build regions → contours → poly mesh
    bool bake(const NavConfig& cfg, rcContext& ctx);

    // Output: Detour navmesh binary blob
    bool export_binary(const std::string& out_path);
    dtNavMesh* get_navmesh();  // for runtime bake — ownership transferred to NavSystem
};
```

---

## 3. Binary Format (`.uznavmesh`)

The `.uznavmesh` file is the direct output of `dtCreateNavMeshData()` — Detour's own serialization format — prepended with a UzEngine header.

```
Header (32 bytes):
  magic          uint32 = 'UZNM'
  version        uint32 = 1
  tile_count     uint32
  data_size      uint64   ; total size of all tile blobs
  bake_timestamp uint64   ; unix time of bake, for invalidation
  reserved       4 bytes

Per Tile (variable):
  tile_ref       dtTileRef
  data_size      uint32
  data_blob      uint8[data_size]   ; raw Detour tile blob
```

The file is stored alongside the scene file under `.import/navmesh/`. It is never human-edited (binary). The bake timestamp is compared against scene file mtime; if the scene is newer, the editor prompts to rebake.

---

## 4. NavMesh Areas and Cost

Areas define walkability cost for different surface types. Agents prefer low-cost paths.

```ini
; Defined in project.uzproj or per-scene
[navmesh.areas]
; name       id   cost
default    =  0   1.0
road       =  1   0.5    ; agents prefer roads
grass      =  2   1.5
water      =  3   4.0    ; passable but slow
mud        =  4   3.0
cliff      = 63   0.0    ; impassable (area 63 = blocked by Recast convention)
```

Areas are painted on the navmesh by associating scene geometry with area IDs:

```ini
[node id=12 type=StaticBody name="RoadSurface" parent=1]
mesh             = "res://meshes/road.glb"
navmesh_area     = road    ; override default area for this geometry
```

---

## 5. NavMeshAgent Component

```ini
[node id=20 type=NavMeshAgent name="Guard" parent=1]
speed             = 3.5      ; maximum movement speed (m/s)
acceleration      = 8.0      ; acceleration (m/s²)
turn_speed        = 120.0    ; angular speed (deg/s)
radius            = 0.4      ; agent collision radius (must match bake config)
height            = 1.8      ; agent height
stopping_distance = 0.5      ; stop this many meters from destination
area_mask         = 0xFFFF   ; bitmask of walkable areas (default: all)
avoidance_quality = medium   ; none | low | medium | high | ultra
```

### C++ Struct

```cpp
struct CNavMeshAgent {
    float  speed             = 3.5f;
    float  acceleration      = 8.0f;
    float  turn_speed        = 120.0f;
    float  radius            = 0.4f;
    float  height            = 1.8f;
    float  stopping_distance = 0.5f;
    uint16_t area_mask       = 0xFFFF;
    uint8_t  avoidance_quality = 2;   // 0=none, 1=low, 2=med, 3=high, 4=ultra

    // Runtime state (not serialized)
    dtCrowdAgent*  crowd_agent = nullptr;  // owned by DetourCrowd
    glm::vec3      destination = {0,0,0};
    bool           has_destination = false;
    bool           reached = false;
};
```

---

## 6. Lua API

```lua
-- Set agent destination; agent starts pathfinding immediately
-- Returns false if destination is unreachable (no path found)
local ok = ctx.nav:set_destination(agent_node_id, vec3(10, 0, 5))

-- Get current agent velocity (world-space, m/s)
local vel = ctx.nav:get_velocity(agent_node_id)  -- returns Vec3

-- Check if a valid path exists to the last destination
local has_path = ctx.nav:has_path(agent_node_id)  -- bool

-- Stop movement (clears destination; agent decelerates to rest)
ctx.nav:stop(agent_node_id)

-- Teleport agent to a new position (snaps to nearest navmesh point)
ctx.nav:warp(agent_node_id, vec3(0, 0, 0))

-- Query navigation without an agent (one-off path query)
local path = ctx.nav:find_path(start_vec3, end_vec3, area_mask)
-- path is array of vec3 waypoints, or nil if no path

-- Get nearest navmesh point to a world position
local navpos = ctx.nav:nearest_point(world_vec3, search_radius)

-- Register a callback fired when agent reaches its destination
ctx.nav:on_arrived(agent_node_id, function()
    print("Agent arrived!")
end)
```

### Example: Simple Patrol

```lua
local waypoints = {
    vec3(5, 0, 0),
    vec3(5, 0, 10),
    vec3(-5, 0, 10),
    vec3(-5, 0, 0),
}
local current_wp = 1

ctx.nav:on_arrived(self.id, function()
    current_wp = (current_wp % #waypoints) + 1
    ctx.nav:set_destination(self.id, waypoints[current_wp])
end)

ctx.nav:set_destination(self.id, waypoints[current_wp])
```

---

## 7. RVO2 Local Avoidance

DetourCrowd implements RVO2 internally. Each agent is registered with the crowd:

```cpp
dtCrowdAgentParams params = {};
params.radius               = comp.radius;
params.height               = comp.height;
params.maxAcceleration      = comp.acceleration;
params.maxSpeed             = comp.speed;
params.collisionQueryRange  = comp.radius * 12.0f;
params.pathOptimizationRange = comp.radius * 30.0f;
params.updateFlags          = DT_CROWD_ANTICIPATE_TURNS
                            | DT_CROWD_OBSTACLE_AVOIDANCE
                            | DT_CROWD_OPTIMIZE_VIS
                            | DT_CROWD_OPTIMIZE_TOPO;
params.obstacleAvoidanceType = comp.avoidance_quality;
params.separationWeight     = 2.0f;
```

Avoidance quality levels control the number of sampled velocity candidates per agent per frame:

| Level | Samples | Cost |
|-------|---------|------|
| `none` | 0 | Agents can overlap |
| `low` | 5 | Budget-friendly |
| `medium` | 16 | Good for most games (default) |
| `high` | 32 | Dense crowds |
| `ultra` | 64 | Cinematic — expensive |

---

## 8. Dynamic Obstacles

The `NavObstacle` component marks a scene node as a dynamic obstacle. The obstacle is registered with `dtTileCache`, which rebuilds affected navmesh tiles at runtime.

```ini
[node id=30 type=NavObstacle name="MovingCrate" parent=1]
shape   = cylinder     ; cylinder | box
radius  = 0.5          ; cylinder radius (meters)
height  = 1.0          ; cylinder height
; For box shape:
; half_extents = [0.5, 0.5, 0.5]
```

```cpp
struct CNavObstacle {
    enum class Shape { Cylinder, Box };
    Shape    shape  = Shape::Cylinder;
    float    radius = 0.5f;
    float    height = 1.0f;
    glm::vec3 half_extents = {0.5f, 0.5f, 0.5f};

    // Runtime (not serialized)
    dtObstacleRef  obstacle_ref = 0;
};
```

### Tile Rebuild Budget

`dtTileCache::update()` is called each frame with a fixed time budget (1 ms). Tile rebuilds are queued and processed incrementally to avoid frame spikes.

---

## 9. NavSystem C++ API

```cpp
// src/navigation/NavSystem.h
class NavSystem {
public:
    // Called on scene load — loads or bakes navmesh
    bool load(const SceneManager& scene, const std::string& navmesh_path);

    // Bake navmesh from current scene geometry (blocking — run on worker thread)
    bool bake(const SceneManager& scene, const NavConfig& cfg);

    // Register/unregister ECS agents and obstacles
    void register_agent(NodeId node, CNavMeshAgent& comp);
    void unregister_agent(NodeId node);
    void register_obstacle(NodeId node, CNavObstacle& comp);
    void unregister_obstacle(NodeId node);

    // Called each frame
    void update(float dt, const SceneManager& scene);

    // Path query (synchronous, fast — Detour is single-threaded friendly)
    std::vector<glm::vec3> find_path(glm::vec3 start, glm::vec3 end,
                                      uint16_t area_mask = 0xFFFF) const;
    glm::vec3 nearest_point(glm::vec3 pos, float search_radius = 2.0f) const;

    // Debug
    void set_debug_draw(bool enabled);
    void draw_debug(ImDrawList* draw_list, const Camera& cam);

private:
    dtNavMesh*       m_navmesh  = nullptr;
    dtNavMeshQuery*  m_query    = nullptr;
    dtCrowd*         m_crowd    = nullptr;
    dtTileCache*     m_tile_cache = nullptr;
};
```

---

## 10. Debug Visualization

When debug draw is enabled (editor mode only), the NavSystem renders:

- **Navmesh triangles**: colored by area (green = walkable, red = impassable, blue = water, etc.)
- **Agent paths**: white polyline from each agent to its destination
- **Agent velocity vectors**: magenta arrows showing current velocity
- **Dynamic obstacle extents**: orange wireframe cylinders/boxes
- **Off-mesh connections**: yellow arcs (for jumps, ladders — Phase 5)

Visualization is drawn via `ImGui::GetBackgroundDrawList()` projected to screen-space using the active camera's view-projection matrix.

Toggle with the **NavMesh** button in the editor overlay toolbar, or via IPC:

```
debug.setNavDrawEnabled  { enabled: true }
```

---

## 11. IPC Handlers

```
nav.bake             {}  → streams progress events, emits nav.bakeComplete
nav.setDestination   { agent_id, target: [x, y, z] }  → { ok, reachable }
nav.stop             { agent_id }
nav.warp             { agent_id, position: [x, y, z] }
nav.findPath         { start: [x,y,z], end: [x,y,z], area_mask? } → { waypoints }
nav.getAgentState    { agent_id }  → { position, velocity, has_path, reached }
nav.setDebugDraw     { enabled }
```

---

## 12. Limits and Performance

| Constraint | Value |
|------------|-------|
| Max agents per scene | 256 (DetourCrowd limit) |
| Max navmesh tile count | 1024 (configurable) |
| Max dynamic obstacles | 128 (TileCache limit) |
| Path query cost | O(1) average with Detour's A* |
| `dtCrowd::update()` budget | ~0.5 ms for 64 agents (medium avoidance) |

For open worlds with large navmeshes, tile-based streaming is handled by the World Partition system (Phase 5), which loads/unloads Detour tiles based on camera position.
