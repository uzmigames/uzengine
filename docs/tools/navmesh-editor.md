# NavMesh & AI Pathfinding Editor

> **Status**: Planned (Phase 4)
> **Libraries**: Recast (navmesh generation), Detour (path query), DetourCrowd (multi-agent steering), DetourTileCache (dynamic obstacles)
> **Integration**: vcpkg `recastnavigation`
> **Runtime owner**: `game.dll` (NavSystem)
> **Editor owner**: VSCode extension (NavMesh Editor panels + DX12 debug overlay)

---

## 1. Overview & Goals

The NavMesh Editor provides a complete visual authoring and debugging environment for AI navigation inside UzEngine's VSCode-based editor. It integrates the Recast+Detour+DetourCrowd stack and exposes every bake parameter, area type, agent profile, and debug visualization through dedicated editor panels and a DX12 viewport overlay.

### Target Users

| Persona | Needs |
|---------|-------|
| **Level Designer** | Paint walkable areas, place NavMesh links and obstacles, test agent paths visually without writing code |
| **AI Programmer** | Tune crowd parameters, debug RVO2 avoidance, profile pathfinding cost, iterate on agent profiles |
| **Technical Artist** | Verify NavMesh coverage on complex geometry, validate off-mesh connections on ladders/jumps |

### Goals

1. **Visual bake feedback** -- see the NavMesh polygon overlay in the 3D viewport immediately after building.
2. **Per-profile baking** -- support multiple agent profiles (human, vehicle, small creature) with independent agent radius/height, each producing a separate NavMesh layer.
3. **Interactive debugging** -- place test agents, draw paths, visualize RVO2 avoidance zones, and run crowd simulation all within the editor viewport.
4. **Incremental rebuild** -- only rebuild dirty tiles when geometry changes, keeping iteration time under 2 seconds for typical scenes.
5. **256 crowd agents** -- full DetourCrowd capacity with real-time performance stats visible in the editor.
6. **`.uznavmesh` binary + `.uznav` INI** -- binary tile data for runtime, human-readable metadata for AI agents and version control diffs.

### Non-Goals (This Phase)

- Procedural navmesh generation at runtime for infinite worlds (deferred to World Partition, Phase 5).
- Behavior tree or state machine editor (separate AI Behavior tool).
- NavMesh streaming across world partitions.

---

## 2. Editor Layout

The NavMesh Editor activates when the user opens the **NavMesh** tab in the VSCode editor sidebar or presses the `NavMesh` toolbar button in the 3D viewport. All panels are implemented as VSCode webview panels communicating with `render.exe --editor` via IPC.

### Layout Diagram

```
+-----------------------------------------------------------------------+
|  [Build All] [Build Selection] [Clear] [Auto-Rebuild: ON]  [Profile v]|
+--------------------------------------------------+--------------------+
|                                                  |  NavMesh Settings  |
|                                                  |--------------------|
|              3D VIEWPORT                         |  Cell Size   0.30  |
|                                                  |  Cell Height 0.20  |
|  +--NavMesh polygon overlay (translucent)--+     |  Agent Radius 0.40 |
|  |  green = walkable                       |     |  Agent Height 1.80 |
|  |  blue  = water                          |     |  Max Slope   45.0  |
|  |  brown = grass                          |     |  Max Climb   0.40  |
|  |  red   = danger                         |     |  Edge Max Len 12.0 |
|  |  gray  = blocked                        |     |  Edge Max Err 1.3  |
|  +-----[agent paths as white lines]--------+     |  Region Min   8    |
|  +-----[velocity vectors as magenta]-------+     |  Region Merge 20   |
|                                                  |  Partition: Water.  |
|  [A]---path--->[B]  (query debug)                |--------------------|
|                                                  |  Area Types        |
|                                                  |--------------------|
|                                                  |  [*] Walkable  1.0 |
|                                                  |  [*] Road      0.5 |
|                                                  |  [*] Grass     1.5 |
|                                                  |  [*] Water     4.0 |
|                                                  |  [*] Danger    8.0 |
|                                                  |  [x] Blocked   0.0 |
|                                                  |  [+ Add Area Type] |
|                                                  |--------------------|
|                                                  |  Agent Profiles    |
|                                                  |--------------------|
|                                                  |  > Human (default) |
|                                                  |    Radius: 0.40    |
|                                                  |    Height: 1.80    |
|                                                  |  > Vehicle         |
|                                                  |    Radius: 1.20    |
|                                                  |    Height: 2.50    |
|                                                  |  > Small Creature  |
|                                                  |    Radius: 0.15    |
|                                                  |    Height: 0.60    |
|                                                  |  [+ Add Profile]   |
+--------------------------------------------------+--------------------+
|  Debug Stats: 2,847 polys | Build: 0.42s | Mem: 1.2 MB | Agents: 12  |
+-----------------------------------------------------------------------+
```

### Panel Descriptions

| Panel | Location | Purpose |
|-------|----------|---------|
| **Build Toolbar** | Top bar | Build All, Build Selection (selected tiles only), Clear NavMesh, Auto-Rebuild toggle, Profile selector dropdown |
| **3D Viewport** | Center (main) | Scene rendering with NavMesh polygon overlay, agent paths, velocity vectors, query debug lines |
| **NavMesh Settings** | Right sidebar, top | Recast build parameters (cell size, walkable constraints, edge limits, region thresholds, partition type) |
| **Area Types** | Right sidebar, middle | Area definitions with name, cost multiplier, and color swatch; add/remove/reorder |
| **Agent Profiles** | Right sidebar, bottom | Named agent size profiles (radius, height, max climb, max slope); each profile generates a separate NavMesh |
| **Debug Stats** | Bottom bar | Polygon count, last build time, memory usage, active agent count, crowd update time |

### Visualization Modes (Viewport Toggle)

| Mode | Shortcut | Description |
|------|----------|-------------|
| **Polygons** | `N` | Translucent colored polygons on the NavMesh surface, colored by area type |
| **Connections** | `Shift+N` | Edges between adjacent polygons drawn as thin lines; portal edges highlighted |
| **Obstacles** | `Ctrl+N` | Dynamic obstacle volumes shown as orange wireframe cylinders/boxes |
| **Areas** | `Alt+N` | Solid color fill per area type with cost labels on each polygon |
| **Off** | `N` (toggle) | All NavMesh visualization disabled |

---

## 3. NavMesh Build Parameters

All build parameters are stored in the `.uznav` metadata file alongside the binary `.uznavmesh`. They are exposed in the NavMesh Settings panel and can be edited per-scene or per-project (project defaults in `project.uzproj`).

### Parameter Table

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `cell_size` | float | 0.30 | 0.05 - 2.0 | Voxel XZ resolution in meters. Smaller = more precise NavMesh, slower bake. |
| `cell_height` | float | 0.20 | 0.05 - 2.0 | Voxel Y resolution in meters. |
| `walkable_slope_max` | float | 45.0 | 0 - 89 | Maximum slope angle (degrees) considered walkable. |
| `walkable_height` | float | 1.80 | 0.1 - 10.0 | Minimum ceiling clearance (meters) for walkability. |
| `walkable_climb` | float | 0.40 | 0.0 - 5.0 | Maximum step height (meters) an agent can climb. |
| `agent_radius` | float | 0.40 | 0.05 - 5.0 | Agent collision radius. NavMesh is eroded by this amount. |
| `agent_height` | float | 1.80 | 0.1 - 10.0 | Agent height for clearance checks. |
| `edge_max_length` | float | 12.0 | 0.0 - 100.0 | Maximum polygon edge length (meters). Longer edges are subdivided. |
| `edge_max_error` | float | 1.3 | 0.1 - 10.0 | Edge simplification tolerance (meters). Higher = fewer edges, less precise. |
| `region_min_size` | int | 8 | 1 - 1000 | Minimum region area in voxel-squared. Regions below this are culled. |
| `region_merge_size` | int | 20 | 1 - 1000 | Regions smaller than this are merged into neighbors. |
| `partition_type` | enum | watershed | watershed, monotone, layers | Recast partitioning algorithm. |
| `detail_sample_dist` | float | 6.0 | 0.0 - 100.0 | Detail mesh sampling distance. |
| `detail_sample_max_error` | float | 1.0 | 0.0 - 50.0 | Detail mesh maximum error tolerance. |
| `tile_size` | int | 48 | 16 - 1024 | Tile size in voxels. Larger = fewer tiles but coarser incremental rebuild. |
| `max_tiles` | int | 1024 | 1 - 65536 | Maximum number of tiles in the NavMesh. |
| `max_polys_per_tile` | int | 32768 | 256 - 65536 | Maximum polygons per tile. |

### Filtering Options

| Filter | Default | Description |
|--------|---------|-------------|
| `filter_low_hanging_obstacles` | true | Mark spans below walkable height as unwalkable if they overhang. |
| `filter_ledge_spans` | true | Mark spans at ledge edges as unwalkable. |
| `filter_walkable_low_height_spans` | true | Filter out spans with insufficient headroom. |

### Partition Type Comparison

| Type | Quality | Speed | Notes |
|------|---------|-------|-------|
| **Watershed** | Best | Slowest | Default. Produces clean, well-shaped regions. Best for general use. |
| **Monotone** | Good | Fast | No guarantee of optimal regions. Good for simple geometry. |
| **Layers** | Good | Fastest | Best for tiled navmeshes. Recommended for large open worlds. |

---

## 4. Area Types & Cost

Area types define surface categories on the NavMesh. Each area has a traversal cost multiplier that influences pathfinding -- agents prefer lower-cost paths.

### Default Area Types

| Area Name | ID | Cost | Color (Viewport) | Description |
|-----------|-----|------|-------------------|-------------|
| Walkable | 0 | 1.0 | Green `#44CC44` | Standard walkable ground |
| Road | 1 | 0.5 | Light Gray `#CCCCCC` | Paved surface, preferred path |
| Grass | 2 | 1.5 | Dark Green `#228B22` | Soft ground, slightly slower |
| Water | 3 | 5.0 | Blue `#4488CC` | Shallow water, passable but expensive |
| Danger | 4 | 8.0 | Red `#CC4444` | Hazardous area, avoid when possible |
| Blocked | 63 | 0.0 | Dark Gray `#444444` | Impassable (Recast convention: area 63 = non-walkable) |

### Area Types Panel

```
+------------------------------+
|  Area Types                  |
+------------------------------+
|  [#44CC44] Walkable    1.0x  |  <-- color swatch, name, cost
|  [#CCCCCC] Road        0.5x  |
|  [#228B22] Grass       1.5x  |
|  [#4488CC] Water       5.0x  |
|  [#CC4444] Danger      8.0x  |
|  [#444444] Blocked     ---   |  <-- impassable, no cost
|------------------------------|
|  [+ Add Area Type]           |
|  [Paint Mode: OFF]           |  <-- toggle area paint brush
+------------------------------+
```

### Area Paint Tool

When **Paint Mode** is active, the user can brush-paint area types directly onto NavMesh polygons in the 3D viewport.

**Controls**:
- **Left Click**: Paint selected area type onto polygon under cursor
- **Right Click**: Sample area type from polygon under cursor (eyedropper)
- **Shift+Left Click**: Flood-fill connected polygons with selected area type
- **Mouse Wheel**: Adjust brush radius (1 polygon, 3 polygons, 5 polygons, flood)
- **Escape**: Exit paint mode

**Implementation**: Painting modifies the `navmesh_area` property on the underlying scene geometry nodes. The mapping from polygon to source geometry is stored in the `.uznav` metadata.

### Area Cost Configuration (`.uznav`)

```ini
[areas]
; name = id cost color
Walkable = 0  1.0  #44CC44
Road     = 1  0.5  #CCCCCC
Grass    = 2  1.5  #228B22
Water    = 3  5.0  #4488CC
Danger   = 4  8.0  #CC4444
Blocked  = 63 0.0  #444444
```

---

## 5. NavMesh Modifiers

NavMesh modifiers are scene nodes that alter the generated NavMesh without changing the underlying geometry. They are placed in the scene tree and processed during bake.

### 5.1 NavMesh Link (Off-Mesh Connection)

Manually connects two NavMesh islands that are not geometrically connected. Used for jumps, ladders, teleporters, and doors.

```ini
[node id=50 type=NavMeshLink name="JumpLink_01" parent=1]
start_position   = [5.0, 3.0, 0.0]
end_position     = [5.0, 0.0, 5.0]
radius           = 0.4           ; agent radius that can use this link
bidirectional    = false         ; one-way (drop) or two-way (ladder)
area_type        = "Walkable"   ; cost of traversing this link
link_type        = "jump"       ; jump | ladder | door | teleport (animation hint)
```

**Viewport Visualization**:
- Bidirectional: yellow double-headed arc between start and end
- One-way: yellow arrow from start to end
- Radius shown as circle at both endpoints

**C++ Struct**:

```cpp
struct CNavMeshLink {
    glm::vec3   start_position;
    glm::vec3   end_position;
    float       radius          = 0.4f;
    bool        bidirectional   = true;
    uint8_t     area_type       = 0;       // area ID for cost calculation
    enum class LinkType : uint8_t { Jump, Ladder, Door, Teleport };
    LinkType    link_type       = LinkType::Jump;

    // Runtime (not serialized)
    dtOffMeshConnectionRef  connection_ref = 0;
};
```

### 5.2 NavMesh Obstacle (Dynamic)

Marks a scene node as a dynamic obstacle that punches a hole in the NavMesh at runtime via `dtTileCache`. The obstacle moves with the node's transform.

```ini
[node id=55 type=NavObstacle name="SlidingDoor" parent=1]
shape       = cylinder          ; cylinder | box
radius      = 0.8
height      = 2.5
; For box: half_extents = [1.0, 1.25, 0.3]
```

**Viewport Visualization**: Orange wireframe cylinder or box matching the obstacle dimensions.

**Runtime Behavior**: When the obstacle moves, `dtTileCache` queues affected tiles for rebuild within the per-frame time budget (1 ms default).

### 5.3 NavMesh Volume (Include/Exclude)

A volume that forces NavMesh generation inside its bounds (include) or prevents it (exclude). Useful for forcing walkability on thin geometry or excluding interiors.

```ini
[node id=60 type=NavMeshVolume name="ExcludeInterior" parent=1]
position     = [20.0, 0.0, 10.0]
half_extents = [5.0, 3.0, 5.0]
mode         = exclude          ; include | exclude
area_type    = "Walkable"       ; only used for include mode
```

**C++ Struct**:

```cpp
struct CNavMeshVolume {
    glm::vec3   half_extents    = {1.0f, 1.0f, 1.0f};
    enum class Mode : uint8_t { Include, Exclude };
    Mode        mode            = Mode::Exclude;
    uint8_t     area_type       = 0;    // area applied inside include volumes
};
```

**Viewport Visualization**:
- Include: green translucent box with `+` icon
- Exclude: red translucent box with `x` icon

### 5.4 Off-Mesh Connection Summary

| Connection Type | Bidirectional | Typical Use | Animation Hint |
|-----------------|---------------|-------------|----------------|
| **Jump** | No (one-way drop) | Ledges, rooftops | Jump/fall blend |
| **Ladder** | Yes | Vertical surfaces | Climb animation |
| **Door** | Yes | Door frames | Open door + walk |
| **Teleport** | Yes | Portals, elevators | Fade in/out |

---

## 6. Crowd Simulation Debug

The NavMesh Editor includes a full crowd simulation debugger that runs inside the editor viewport. This allows designers and AI programmers to test agent behavior without entering Play Mode.

### 6.1 Test Agent Placement

**Workflow**:
1. Click the **Place Agent** button in the toolbar (or press `G`)
2. Click on the NavMesh surface in the viewport to spawn a test agent
3. Right-click to set the agent's target destination
4. Agent begins pathfinding and moving immediately

**Agent Properties (Inspector)**:

| Property | Default | Description |
|----------|---------|-------------|
| `max_speed` | 3.5 | Maximum movement speed (m/s) |
| `acceleration` | 8.0 | Acceleration (m/s^2) |
| `separation_weight` | 2.0 | How strongly agents avoid each other |
| `collision_query_range` | 4.8 | Range for RVO2 neighbor queries (meters) |
| `path_optimization_range` | 12.0 | Range for path corridor optimization |
| `avoidance_quality` | medium | none, low, medium, high, ultra |
| `profile` | Human | Agent profile (determines radius/height) |

### 6.2 Debug Visualization Layers

Each layer can be toggled independently in the viewport toolbar.

```
+--------------------------------------------------+
|  Crowd Debug Layers:                             |
|  [x] Agent Capsules   -- 3D capsule at agent pos |
|  [x] Path Lines       -- white polyline to dest  |
|  [x] Velocity Vectors -- magenta arrow (actual)  |
|  [ ] Preferred Vel.   -- cyan arrow (desired)    |
|  [ ] RVO2 Zones       -- yellow velocity obstacle |
|  [ ] Corridor         -- blue polygon corridor   |
|  [x] Targets          -- red crosshair at dest   |
+--------------------------------------------------+
```

**Agent Capsule Rendering**:
- Green capsule: agent is moving, has valid path
- Yellow capsule: agent is recalculating path
- Red capsule: agent is stuck (no valid path)
- Gray capsule: agent is idle (reached destination)

### 6.3 Velocity Obstacle Visualization (RVO2)

When enabled, the viewport shows the velocity space for the selected agent:

```
         Preferred velocity
              |
              v
    +-------------------+
    |   /    ORCA    \  |   <-- half-planes from neighboring agents
    |  / valid vel.   \ |
    | /    region      \|
    |/        *<--actual velocity chosen by RVO2
    |\                 /|
    | \               / |
    |  \             /  |
    +-------------------+
```

In the 3D viewport, this is projected as colored wedges around the agent:
- **Yellow wedges**: velocity obstacles from other agents
- **Cyan arrow**: preferred velocity (before avoidance)
- **Magenta arrow**: actual velocity (after RVO2 solving)

### 6.4 Crowd Performance Stats

Displayed in the Debug Stats bar at the bottom of the editor:

```
Agents: 48/256 | Crowd Update: 0.34 ms | Paths/Frame: 3 | Tile Rebuilds: 0 | Mem: 2.1 MB
```

| Metric | Description |
|--------|-------------|
| Agents | Active agents / max agents |
| Crowd Update | Time spent in `dtCrowd::update()` this frame |
| Paths/Frame | Number of path requests completed this frame |
| Tile Rebuilds | Number of TileCache tiles rebuilt this frame |
| Mem | Total NavMesh + TileCache memory usage |

---

## 7. Query Visualization

The Query Debug tool allows testing Detour queries interactively in the viewport without writing code. Activate via the toolbar **Query** button or press `Q`.

### Query Modes

| Mode | Input | Output |
|------|-------|--------|
| **Nearest Point** | Drag a point in 3D space | Draws a line from the point to the nearest NavMesh polygon; shows poly ID and distance |
| **Find Path** | Click start (A), click end (B) | Draws the full path as a green polyline with waypoint markers; shows path length and cost |
| **Raycast** | Click origin, drag to set direction | Draws ray on NavMesh surface; shows hit polygon, hit point (red dot), and traversal distance |
| **Flood Fill** | Click a point on NavMesh | Colors all reachable polygons from that point within the current area mask; unreachable polygons shown in red |
| **Circle Query** | Click center, drag to set radius | Highlights all polygons within the circle; shows polygon count and total area |

### Query Debug Viewport Overlay

```
+--------------------------------------------------+
|                                                  |
|   Query: Find Path                               |
|                                                  |
|   [A] =============> . ======> . ======> [B]    |
|        green line      waypoint   waypoint       |
|                        markers    markers         |
|                                                  |
|   Path Length: 14.7 m                            |
|   Path Cost: 22.1 (weighted)                     |
|   Polygons Traversed: 18                         |
|   Computation Time: 0.02 ms                      |
|                                                  |
+--------------------------------------------------+
```

### Raycast Debug View

```
+--------------------------------------------------+
|                                                  |
|   Query: Raycast                                 |
|                                                  |
|   [O]------ray------>(X)                         |
|    origin            hit point (red dot)          |
|                                                  |
|   Hit: YES                                       |
|   Distance: 8.3 m                                |
|   Hit Polygon: #1247                             |
|   Hit Normal: (0.0, 1.0, 0.0)                   |
|                                                  |
+--------------------------------------------------+
```

---

## 8. NavMesh Runtime API

The runtime API is exposed through two Lua tables: `ctx.navmesh` for static queries and obstacle management, and `ctx.crowd` for crowd agent control.

### 8.1 Path Queries

```lua
-- Find a path between two world-space points
-- Returns: table of Vec3 waypoints, or nil if no path exists
-- Optional area_mask: bitmask of allowed area types (default 0xFFFF = all)
local path = ctx.navmesh:find_path(start_vec3, end_vec3 [, area_mask])

-- Example:
local path = ctx.navmesh:find_path(self.position, target.position)
if path then
    for i, waypoint in ipairs(path) do
        ctx:log("Waypoint " .. i .. ": " .. waypoint:str())
    end
end
```

### 8.2 Point Queries

```lua
-- Find the nearest point on the NavMesh to a world position
-- Returns: Vec3 (snapped to NavMesh), or nil if no NavMesh nearby
local nav_point = ctx.navmesh:nearest_point(world_vec3 [, search_radius])

-- Check if a world position is on the NavMesh
local on_mesh = ctx.navmesh:is_on_mesh(world_vec3 [, tolerance])
```

### 8.3 Raycast on NavMesh

```lua
-- Cast a ray along the NavMesh surface (not a 3D ray -- follows NavMesh)
-- Returns: { hit=bool, point=Vec3, distance=float, poly_id=int }
local result = ctx.navmesh:raycast(origin_vec3, target_vec3)
if result.hit then
    ctx:log("NavMesh ray hit at distance " .. result.distance)
end
```

### 8.4 Dynamic Obstacles

```lua
-- Add a cylindrical obstacle (punches a hole in the NavMesh)
-- Returns: handle (integer) for later removal
local handle = ctx.navmesh:add_obstacle(position_vec3, radius, height)

-- Add a box obstacle
local handle = ctx.navmesh:add_box_obstacle(position_vec3, half_extents_vec3, rotation_y)

-- Remove a previously added obstacle
ctx.navmesh:remove_obstacle(handle)

-- Update obstacle position (obstacle moves, tiles rebuild incrementally)
ctx.navmesh:move_obstacle(handle, new_position_vec3)
```

### 8.5 Crowd Agent Management

```lua
-- Add a crowd agent at a position using a named profile
-- Returns: agent_id (integer)
local agent_id = ctx.crowd:add_agent(position_vec3, "Human")

-- Set agent destination (triggers pathfinding)
ctx.crowd:set_target(agent_id, target_vec3)

-- Stop agent movement
ctx.crowd:stop(agent_id)

-- Remove agent from crowd
ctx.crowd:remove_agent(agent_id)

-- Get agent state
local state = ctx.crowd:get_agent(agent_id)
-- state = {
--   position  = Vec3,
--   velocity  = Vec3,
--   speed     = float,
--   has_path  = bool,
--   reached   = bool,
--   target    = Vec3,
-- }

-- Set agent parameters at runtime
ctx.crowd:set_speed(agent_id, 5.0)
ctx.crowd:set_acceleration(agent_id, 10.0)
ctx.crowd:set_separation_weight(agent_id, 3.0)

-- Teleport agent to a new position (snaps to NavMesh)
ctx.crowd:warp(agent_id, position_vec3)

-- Register callback when agent reaches destination
ctx.crowd:on_arrived(agent_id, function()
    ctx:log("Agent " .. agent_id .. " arrived!")
end)
```

### 8.6 Batch Queries

```lua
-- Find all agents within a radius of a point
local nearby = ctx.crowd:agents_in_radius(center_vec3, radius)
-- nearby = { agent_id, agent_id, ... }

-- Set destination for all agents in a list
ctx.crowd:set_target_batch(agent_id_table, target_vec3)
```

---

## 9. File Format

The NavMesh system uses two files per scene: a binary `.uznavmesh` for Detour tile data and a human-readable `.uznav` INI for metadata, build parameters, area types, and off-mesh connections.

### 9.1 Binary Format (`.uznavmesh`)

Stored in `.import/navmesh/<scene_name>.uznavmesh`. This is the Detour-native tile format with a UzEngine header.

```
+---------------------------------------------------+
| Header (32 bytes)                                 |
|   magic:          uint32 = 0x554E4D00 ('UZNM')   |
|   version:        uint32 = 1                      |
|   profile_count:  uint32                          |
|   reserved:       uint8[20]                       |
+---------------------------------------------------+
| Per Profile (variable)                            |
|   profile_name:   char[32] (null-terminated)      |
|   tile_count:     uint32                          |
|   total_data_size:uint64                          |
|   bake_timestamp: uint64 (unix epoch)             |
|   +-----------------------------------------------+
|   | Per Tile (variable)                           |
|   |   tile_x:      int32                          |
|   |   tile_y:      int32                          |
|   |   tile_layer:   int32                         |
|   |   data_size:    uint32                        |
|   |   data_blob:    uint8[data_size]  (Detour)    |
|   +-----------------------------------------------+
+---------------------------------------------------+
```

### 9.2 Metadata Format (`.uznav`)

Stored alongside the `.uzscene` file at `<scene_name>.uznav`. Human-readable and diffable.

```ini
; =============================================================
; NavMesh Metadata -- Auto-generated by UzEngine NavMesh Editor
; Scene: res://scenes/village.uzscene
; Generated: 2026-03-02T14:30:00Z
; =============================================================

[navmesh]
version          = 1
scene            = "res://scenes/village.uzscene"
binary           = ".import/navmesh/village.uznavmesh"

; ----- Build Parameters -----
[build]
cell_size        = 0.30
cell_height      = 0.20
walkable_slope   = 45.0
walkable_height  = 1.80
walkable_climb   = 0.40
edge_max_length  = 12.0
edge_max_error   = 1.3
region_min_size  = 8
region_merge_size = 20
partition_type   = watershed
detail_sample_dist      = 6.0
detail_sample_max_error = 1.0
tile_size        = 48
max_tiles        = 1024
max_polys_per_tile = 32768

[build.filters]
low_hanging_obstacles    = true
ledge_spans              = true
walkable_low_height_spans = true

; ----- Area Types -----
[areas]
; name = id cost color
Walkable = 0  1.0  #44CC44
Road     = 1  0.5  #CCCCCC
Grass    = 2  1.5  #228B22
Water    = 3  5.0  #4488CC
Danger   = 4  8.0  #CC4444
Blocked  = 63 0.0  #444444

; ----- Agent Profiles -----
[profiles.Human]
radius           = 0.40
height           = 1.80
max_climb        = 0.40
max_slope        = 45.0

[profiles.Vehicle]
radius           = 1.20
height           = 2.50
max_climb        = 0.20
max_slope        = 25.0

[profiles.SmallCreature]
radius           = 0.15
height           = 0.60
max_climb        = 0.15
max_slope        = 60.0

; ----- Off-Mesh Connections -----
[offmesh.0]
start            = [5.0, 3.0, 0.0]
end              = [5.0, 0.0, 5.0]
radius           = 0.4
bidirectional    = false
area             = Walkable
type             = jump

[offmesh.1]
start            = [12.0, 0.0, 8.0]
end              = [12.0, 5.0, 8.0]
radius           = 0.4
bidirectional    = true
area             = Walkable
type             = ladder

[offmesh.2]
start            = [20.0, 0.0, 3.0]
end              = [20.0, 0.0, 5.0]
radius           = 0.4
bidirectional    = true
area             = Road
type             = door

; ----- NavMesh Volumes -----
[volume.0]
position         = [20.0, 0.0, 10.0]
half_extents     = [5.0, 3.0, 5.0]
mode             = exclude

[volume.1]
position         = [0.0, 0.0, 0.0]
half_extents     = [50.0, 5.0, 50.0]
mode             = include
area             = Walkable

; ----- Area Overrides (per geometry node) -----
[area_overrides]
; node_id = area_name
12 = Road
15 = Grass
18 = Water
22 = Danger
```

---

## 10. Implementation Notes

### 10.1 C++ Class Hierarchy

```
src/game/navigation/
  NavSystem               -- Top-level system: owns dtNavMesh, dtCrowd, dtTileCache
  |
  +-- NavBaker            -- Recast pipeline: geometry collection, voxelization, poly generation
  |   +-- NavGeomCollector    -- Walks SceneManager to collect triangles from StaticMesh/Terrain
  |   +-- NavTileBaker        -- Bakes a single tile (parallelizable across threads)
  |
  +-- NavQueryRunner      -- Wraps dtNavMeshQuery for find_path, nearest_point, raycast
  |
  +-- NavCrowdManager     -- Wraps dtCrowd: agent add/remove/update, RVO2 config
  |
  +-- NavObstacleManager  -- Wraps dtTileCache: obstacle add/remove/move, tile rebuild budget
  |
  +-- NavMeshSerializer   -- Read/write .uznavmesh binary and .uznav INI

src/game/navigation/editor/     [UZ_EDITOR only]
  |
  +-- NavMeshEditorTool       -- Editor tool state machine (select, paint, query, place agent)
  |
  +-- NavMeshDebugRenderer    -- DX12 debug draw: polygon overlay, path lines, agent capsules
  |
  +-- NavQueryDebugger        -- Interactive query visualization (nearest, path, raycast, flood)
  |
  +-- NavCrowdDebugger        -- Crowd sim visualization (velocities, RVO2, corridors)
  |
  +-- NavAreaPainter          -- Area type brush tool (polygon picking + area assignment)
```

### 10.2 C++ Class Definitions

```cpp
// ===== src/game/navigation/nav_system.h =====

class NavSystem {
public:
    bool        init(const ProjectConfig& cfg);
    void        shutdown();

    // Bake
    bool        bake(const SceneManager& scene, const NavConfig& cfg);
    bool        bake_tile(int tx, int ty, const SceneManager& scene);
    void        clear();

    // Load / Save
    bool        load_binary(const std::string& path);
    bool        save_binary(const std::string& path) const;
    bool        load_metadata(const std::string& path);
    bool        save_metadata(const std::string& path) const;

    // Queries (thread-safe: uses thread-local dtNavMeshQuery)
    std::vector<glm::vec3> find_path(glm::vec3 start, glm::vec3 end,
                                      uint16_t area_mask = 0xFFFF) const;
    glm::vec3   nearest_point(glm::vec3 pos, float search_radius = 2.0f) const;
    NavRaycastResult raycast(glm::vec3 start, glm::vec3 end) const;

    // Crowd
    NavCrowdManager&    crowd()     { return m_crowd; }

    // Obstacles
    NavObstacleManager& obstacles() { return m_obstacles; }

    // Frame update (call from game loop)
    void        update(float dt);

    // Debug (editor only)
    #ifdef UZ_EDITOR
    void        set_debug_draw(bool enabled);
    NavMeshDebugRenderer& debug_renderer() { return m_debug_renderer; }
    #endif

private:
    dtNavMesh*          m_navmesh       = nullptr;
    dtNavMeshQuery*     m_query         = nullptr;
    dtTileCache*        m_tile_cache    = nullptr;
    NavCrowdManager     m_crowd;
    NavObstacleManager  m_obstacles;
    NavConfig           m_config;
    #ifdef UZ_EDITOR
    NavMeshDebugRenderer m_debug_renderer;
    #endif
};
```

```cpp
// ===== src/game/navigation/nav_baker.h =====

struct NavConfig {
    float   cell_size           = 0.30f;
    float   cell_height         = 0.20f;
    float   walkable_slope      = 45.0f;
    float   walkable_height     = 1.80f;
    float   walkable_climb      = 0.40f;
    float   agent_radius        = 0.40f;
    float   agent_height        = 1.80f;
    float   edge_max_length     = 12.0f;
    float   edge_max_error      = 1.3f;
    int     region_min_size     = 8;
    int     region_merge_size   = 20;
    enum class Partition { Watershed, Monotone, Layers };
    Partition partition_type    = Partition::Watershed;
    float   detail_sample_dist  = 6.0f;
    float   detail_sample_error = 1.0f;
    int     tile_size           = 48;
    int     max_tiles           = 1024;
    int     max_polys_per_tile  = 32768;
    bool    filter_low_hanging  = true;
    bool    filter_ledge_spans  = true;
    bool    filter_low_height   = true;
};

class NavBaker {
public:
    // Collect all static geometry from scene
    void    collect_geometry(const SceneManager& scene);

    // Full bake: all tiles
    bool    bake_all(const NavConfig& cfg, rcContext& ctx);

    // Incremental: bake only specified tiles
    bool    bake_tiles(const NavConfig& cfg, rcContext& ctx,
                       const std::vector<std::pair<int,int>>& dirty_tiles);

    // Get result
    dtNavMesh*  get_navmesh();  // ownership transferred

    // Progress callback (for editor progress bar)
    using ProgressFn = std::function<void(float percent, const char* stage)>;
    void    set_progress_callback(ProgressFn fn);

private:
    NavGeomCollector    m_geom;
    ProgressFn          m_progress_fn;
};
```

```cpp
// ===== src/game/navigation/nav_crowd_manager.h =====

class NavCrowdManager {
public:
    bool    init(dtNavMesh* navmesh, int max_agents = 256);
    void    shutdown();

    // Agent lifecycle
    int     add_agent(glm::vec3 position, const AgentProfile& profile);
    void    remove_agent(int agent_id);
    void    set_target(int agent_id, glm::vec3 target);
    void    stop(int agent_id);
    void    warp(int agent_id, glm::vec3 position);

    // Agent state
    AgentState  get_agent_state(int agent_id) const;
    int         agent_count() const;

    // Runtime parameter adjustment
    void    set_speed(int agent_id, float speed);
    void    set_acceleration(int agent_id, float accel);
    void    set_separation_weight(int agent_id, float weight);

    // Batch operations
    std::vector<int> agents_in_radius(glm::vec3 center, float radius) const;

    // Frame update
    void    update(float dt);

    // Callbacks
    using ArrivedFn = std::function<void(int agent_id)>;
    void    on_arrived(int agent_id, ArrivedFn fn);

private:
    dtCrowd*            m_crowd         = nullptr;
    dtNavMeshQuery*     m_query         = nullptr;
    std::unordered_map<int, ArrivedFn>  m_arrived_callbacks;
};

struct AgentProfile {
    std::string name;
    float       radius          = 0.4f;
    float       height          = 1.8f;
    float       max_speed       = 3.5f;
    float       max_acceleration = 8.0f;
    float       max_climb       = 0.4f;
    float       max_slope       = 45.0f;
    float       separation_weight    = 2.0f;
    float       collision_query_range = 4.8f;
    uint8_t     avoidance_quality    = 2;   // 0=none, 1=low, 2=med, 3=high, 4=ultra
};

struct AgentState {
    glm::vec3   position;
    glm::vec3   velocity;
    float       speed;
    bool        has_path;
    bool        reached;
    glm::vec3   target;
};
```

### 10.3 DX12 Debug Draw

The NavMesh debug overlay is rendered as a separate DX12 render pass after the main scene pass but before the editor gizmo pass. It uses translucent polygon rendering for NavMesh surfaces and line rendering for paths, edges, and velocity vectors.

```cpp
// ===== src/game/navigation/editor/navmesh_debug_renderer.h =====
// Compiled only when UZ_EDITOR is defined

#ifdef UZ_EDITOR

class NavMeshDebugRenderer {
public:
    void    init(ID3D12Device* device, DXGI_FORMAT rtv_format);
    void    shutdown();

    // Generate draw data from NavMesh
    void    build_mesh_overlay(const dtNavMesh* navmesh,
                               const AreaTypeTable& areas);
    void    build_path_lines(const std::vector<glm::vec3>& path,
                             uint32_t color);
    void    build_agent_capsules(const NavCrowdManager& crowd);
    void    build_velocity_vectors(const NavCrowdManager& crowd);
    void    build_obstacle_wireframes(const NavObstacleManager& obstacles);

    // Render all debug geometry
    void    render(ID3D12GraphicsCommandList* cmd,
                   const glm::mat4& view_projection);

    // Toggle layers
    void    set_layer_visible(DebugLayer layer, bool visible);

    enum class DebugLayer : uint8_t {
        Polygons,
        Connections,
        Obstacles,
        Areas,
        Paths,
        AgentCapsules,
        Velocities,
        PreferredVelocities,
        RVO2Zones,
        Corridors,
        Targets,
        COUNT
    };

private:
    // GPU resources
    ComPtr<ID3D12PipelineState>     m_polygon_pso;  // translucent triangles
    ComPtr<ID3D12PipelineState>     m_line_pso;     // thick lines
    ComPtr<ID3D12PipelineState>     m_capsule_pso;  // instanced capsules

    // CPU-side buffers rebuilt each frame
    std::vector<NavDebugVertex>     m_polygon_verts;
    std::vector<NavDebugVertex>     m_line_verts;
    std::vector<CapsuleInstance>    m_capsule_instances;

    std::bitset<static_cast<size_t>(DebugLayer::COUNT)> m_visible_layers;
};

struct NavDebugVertex {
    glm::vec3   position;
    uint32_t    color;      // RGBA packed
};

struct CapsuleInstance {
    glm::vec3   position;
    float       radius;
    float       height;
    uint32_t    color;
};

#endif // UZ_EDITOR
```

### 10.4 Incremental Rebuild

To keep iteration time fast, the editor tracks which tiles are affected by geometry changes and only rebuilds those.

**Dirty tile detection**:
1. When a `StaticMesh` or `StaticBody` node's transform or mesh changes, compute its AABB in navmesh tile space.
2. Mark all tiles overlapping the AABB as dirty.
3. On next build (auto or manual), only rebake dirty tiles via `NavBaker::bake_tiles()`.
4. Dirty tiles are tracked in a `std::unordered_set<TileCoord>` where `TileCoord = {int tx, int ty}`.

**Auto-Rebuild**:
- When the `Auto-Rebuild` toggle is ON, dirty tiles are rebuilt automatically after a 500 ms debounce from the last geometry change.
- A progress indicator appears in the Debug Stats bar during rebuild.
- Maximum rebuild budget: 50 ms per frame (to avoid editor stutter). Remaining tiles are queued for the next frame.

### 10.5 VSCode Extension Integration

The NavMesh Editor panels are implemented as VSCode webview panels. Communication with the engine follows the existing IPC pattern.

**IPC Commands**:

| Command | Args | Returns | Description |
|---------|------|---------|-------------|
| `nav.bake` | `{ profile?: string }` | streams progress events, emits `nav.bakeComplete` | Bake NavMesh (all profiles or specific profile) |
| `nav.bakeSelection` | `{ tiles: [[tx,ty], ...] }` | `{ ok, rebuilt_count }` | Bake only specified tiles |
| `nav.clear` | `{}` | `{ ok }` | Clear current NavMesh |
| `nav.getConfig` | `{}` | `{ config: NavConfig }` | Get current build parameters |
| `nav.setConfig` | `{ config: NavConfig }` | `{ ok }` | Update build parameters |
| `nav.getAreas` | `{}` | `{ areas: [...] }` | Get area type definitions |
| `nav.setAreas` | `{ areas: [...] }` | `{ ok }` | Update area type definitions |
| `nav.getProfiles` | `{}` | `{ profiles: [...] }` | Get agent profile definitions |
| `nav.setProfiles` | `{ profiles: [...] }` | `{ ok }` | Update agent profile definitions |
| `nav.getStats` | `{}` | `{ polys, tiles, mem_bytes, build_time_ms }` | Get NavMesh statistics |
| `nav.setDebugLayer` | `{ layer: string, visible: bool }` | `{ ok }` | Toggle debug visualization layer |
| `nav.queryPath` | `{ start, end, area_mask? }` | `{ path: [...], cost, polys }` | Run path query for debug |
| `nav.queryNearest` | `{ position, radius? }` | `{ point, poly_id, distance }` | Find nearest NavMesh point |
| `nav.queryRaycast` | `{ start, end }` | `{ hit, point, distance, poly_id }` | NavMesh surface raycast |
| `nav.spawnTestAgent` | `{ position, profile, target? }` | `{ agent_id }` | Spawn a test agent in editor |
| `nav.removeTestAgent` | `{ agent_id }` | `{ ok }` | Remove a test agent |
| `nav.setAutoRebuild` | `{ enabled: bool }` | `{ ok }` | Toggle auto-rebuild |
| `nav.paintArea` | `{ poly_ids: [...], area: string }` | `{ ok }` | Paint area type on polygons |

### 10.6 Threading Model

| Operation | Thread | Notes |
|-----------|--------|-------|
| `NavBaker::bake_all` | Worker thread | Blocking; editor shows progress bar. Tiles can be baked in parallel via `std::async`. |
| `NavBaker::bake_tiles` | Worker thread | Incremental rebuild on background thread. |
| `dtCrowd::update` | Game thread | Must run on game thread (Detour is not thread-safe for crowd). |
| `dtNavMeshQuery::findPath` | Any thread | Thread-safe if each thread owns its own `dtNavMeshQuery` instance. |
| `dtTileCache::update` | Game thread | Tile rebuilds are budgeted (1 ms/frame). |
| Debug draw build | Game thread | Generates vertex data; GPU upload happens on render thread. |

### 10.7 Memory Budget

| Component | Typical Size | Notes |
|-----------|-------------|-------|
| NavMesh (medium scene) | 1 - 5 MB | Depends on tile count and polygon density |
| NavMesh (large open world) | 10 - 50 MB | Tile streaming recommended for > 20 MB |
| dtCrowd (256 agents) | ~256 KB | Fixed allocation per agent |
| dtTileCache | 2 - 10 MB | Compressed tiles + obstacle data |
| Debug draw buffers | 1 - 4 MB | Only allocated in editor mode |
| Total (editor, large scene) | ~70 MB max | Well within game.dll arena budget |

### 10.8 Integration with Other Systems

**Physics System (Jolt)**:
- `NavGeomCollector` queries physics colliders (via `CColliderBox`, `CColliderMesh`, `CColliderTerrain`) for NavMesh source geometry, in addition to visual meshes.
- NavMesh agents do NOT use Jolt for movement -- they are moved by `dtCrowd` and their transform is synced to ECS. Jolt collision is only used for the player's `CCharacterController`.
- Dynamic obstacles in the physics world (moved via Jolt) automatically update their `CNavObstacle` position, triggering tile cache rebuilds.

**Scene System**:
- NavMesh source geometry is collected from `StaticMesh` and `StaticBody` nodes in the scene tree.
- `navmesh_area` property on scene nodes determines the area type assigned to their geometry.
- Scene save/load triggers NavMesh metadata save/load.

**Lua Scripting**:
- `ctx.navmesh` and `ctx.crowd` tables are registered in `lua_nav_binding.cpp`.
- Callbacks (`on_arrived`) are stored as Lua function references and invoked on the game thread.
- All query functions return `nil` gracefully when NavSystem is not initialized (no-op guard).

**Editor Gizmos**:
- NavMesh Links are rendered with custom gizmos (arcs between endpoints).
- NavMesh Volumes use the standard box gizmo with custom colors.
- NavMesh Obstacles use the standard cylinder/box gizmo with orange color.

---

## Appendix A: Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `N` | Toggle NavMesh polygon overlay |
| `Shift+N` | Toggle connection edges overlay |
| `Ctrl+N` | Toggle obstacle wireframe overlay |
| `Alt+N` | Toggle area color fill overlay |
| `G` | Enter Place Agent mode |
| `Q` | Enter Query Debug mode |
| `P` | Enter Area Paint mode |
| `Escape` | Exit current tool mode |
| `Ctrl+B` | Build All (navmesh bake) |
| `Ctrl+Shift+B` | Build Selection (dirty tiles only) |
| `Delete` | Remove selected test agent or modifier |

---

## Appendix B: Performance Targets

| Operation | Target | Measured On |
|-----------|--------|-------------|
| Full bake (small scene, 100 tiles) | < 2 s | Ryzen 5 5600X, 6 threads |
| Full bake (medium scene, 500 tiles) | < 10 s | Ryzen 5 5600X, 6 threads |
| Incremental rebuild (1 dirty tile) | < 50 ms | Single thread |
| `find_path` (typical 20m path) | < 0.05 ms | Single query |
| `dtCrowd::update` (64 agents, medium) | < 0.5 ms | Game thread |
| `dtCrowd::update` (256 agents, medium) | < 2.0 ms | Game thread |
| Debug draw (full overlay, 5000 polys) | < 1.0 ms GPU | RTX 3060 |
| Editor panel IPC round-trip | < 5 ms | localhost WebSocket |

---

## Appendix C: Error Handling

| Condition | Behavior |
|-----------|----------|
| No NavMesh loaded | All query functions return `nil`/empty. Crowd returns no agents. Log warning once. |
| Destination unreachable | `find_path` returns `nil`. `set_target` returns `{ ok=false, reachable=false }`. Agent stays in place. |
| Agent limit exceeded (256) | `add_agent` returns `-1`. Log error. |
| Obstacle limit exceeded (128) | `add_obstacle` returns `0` (invalid handle). Log error. |
| Bake fails (no geometry) | `bake` returns `false`. Error shown in Debug Stats panel. |
| Stale `.uznavmesh` | Editor shows warning banner: "NavMesh is outdated. Rebuild recommended." |
| Corrupt binary file | `load_binary` returns `false`. Log error with file path. Fall back to runtime bake if enabled. |

---

## Related Documentation

- [specs/navmesh.md](../specs/navmesh.md) -- NavMesh runtime system spec
- [specs/physics-system.md](../specs/physics-system.md) -- Jolt Physics integration
- [specs/scene-system.md](../specs/scene-system.md) -- Scene tree and `.uzscene` format
- [specs/editor-gizmos.md](../specs/editor-gizmos.md) -- Editor gizmo rendering
- [specs/editor-panels.md](../specs/editor-panels.md) -- VSCode panel architecture
- [specs/terrain-system.md](../specs/terrain-system.md) -- Terrain height-field for NavMesh source geometry
- [guides/physics-guide.md](../guides/physics-guide.md) -- Physics system usage guide
