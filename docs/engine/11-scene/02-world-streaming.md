# World Streaming System

> **Status**: Planned (Phase 3)
> **Source**: `engine/world/`, `engine/subsystem/world_streaming.h`
> **References**: UE5 World Partition, Level Instances, Data Layers; Godot (no built-in streaming)

## Purpose

World Streaming loads and unloads world cells dynamically based on camera position. Large open worlds would consume infinite memory if loaded all at once; streaming divides the world into manageable chunks that load on demand. Transitions between cells are seamless — no loading screens.

## World Partition Grid

The world is conceptually divided into a regular grid of **cells**:

```cpp
/// Extent of a single world cell.
constexpr uint32_t WORLD_CELL_SIZE = 100;  // 100 × 100 × 100 meters

/// World partition divides space into cells.
/// Cell (x, y, z) covers space [x*100, (x+1)*100) in each axis.
struct WorldCell {
    int32_t  cell_x;
    int32_t  cell_y;
    int32_t  cell_z;

    AABB Bounds() const {
        Vec3 min = Vec3(cell_x, cell_y, cell_z) * WORLD_CELL_SIZE;
        Vec3 max = min + Vec3(WORLD_CELL_SIZE);
        return AABB(min, max);
    }
};

/// World streaming state machine per cell.
enum class CellState {
    Unloaded,       // not in memory
    Loading,        // async load in progress
    Loaded,         // in memory, actors inactive
    Activating,     // calling on_start callbacks
    Active,         // streaming updates, frame rate coupled
    Deactivating,   // calling on_stop callbacks
    Unloading,      // async unload in progress
};
```

**Streaming radius:** The engine maintains a radius around the camera. Cells within radius are loaded; cells outside are unloaded:

```
Camera (center)
├─ Streaming radius = 300m
│  ├─ Cell (-1, 0, 0)  ← loaded
│  ├─ Cell (0, 0, 0)   ← loaded
│  ├─ Cell (1, 0, 0)   ← loaded
│  └─ Cell (0, 1, 0)   ← loaded
│
└─ Beyond 300m: Cell (5, 0, 0) ← not loaded
```

## Cell Loading Pipeline

Cells are loaded asynchronously to avoid frame rate spikes:

```
1. Determine cells needed (frustum + radius)
2. Priority queue: cells closer to camera load first
3. Background thread:
   a. Open .uzpak archive
   b. Deserialize actor hierarchy
   c. Instantiate components
   d. Load referenced assets (streaming, not blocking)
4. Transfer to main thread:
   a. Register actors in world
   b. Register physics bodies
   c. Register render proxies
5. Call on_start callbacks (now gameplay-safe)
6. Add to active list (physics/logic updates)
```

**Memory budget:** The engine enforces a maximum loaded cell count:
```cpp
struct StreamingSettings {
    uint32_t max_loaded_cells;     // e.g., 16 (1.6 km × 1.6 km at 100m cells)
    uint32_t target_active_cells;  // e.g., 9 (center 3×3 grid)
    float    streaming_radius;     // 300m typical
    bool     preload_adjacent;     // preload 1 ring ahead of target
};
```

If max cells are loaded, **priority eviction** removes the farthest cell.

## Streaming Volumes

Not all level content fits into a regular grid. Interior spaces (caves, dungeons, buildings) use **streaming volumes**:

```cpp
/// Trigger-based loading for non-grid content.
struct StreamingVolume {
    AABB           bounds;
    std::string    level_name;           // which level to load ("cave_entrance_v2")
    bool           is_blocking;          // if true, pause until fully loaded
    float          priority;             // affects load order
    bool           preload_on_nearby;    // load early if camera approaches
};
```

**Usage example:**
- Player approaches cave entrance (enters StreamingVolume)
- StreamingVolume triggers cave level load
- Cave loads in background (or blocks if `is_blocking = true`)
- Player enters cave
- Cave level now active
- Player exits cave (leaves StreamingVolume)
- Cave unloads over time (after unload_delay)

**Why separate from grid?** Interiors have irregular shapes; a regular grid would waste memory loading half-empty cells.

## Level Layers

A **level** (`.uzlevel` file) can contain multiple **layers**:

```cpp
/// Layers within a level enable independent streaming of gameplay, rendering, and AI.
enum class DataLayer {
    Persistent,      // always loaded with level
    Rendering,       // visual-only (decorative assets, lighting)
    Gameplay,        // interactive gameplay (triggers, enemies, items)
    Navigation,      // pathfinding graph (AI navigation mesh)
};

struct Level {
    std::string name;
    std::vector<ActorSubLevel> sublevel_by_layer[4];
};
```

**Independent streaming:** Each layer loads separately:
- Level loads with `Persistent` layer only
- `Rendering` layer loads next (no gameplay blocking)
- `Gameplay` layer follows (gameplay now active)
- `Navigation` layer last (AI can use navmesh)

Example:
```
Cave Level Loading
├─ Load Persistent (geometry, materials, lighting data)
├─ Load Rendering (detail meshes, VFX volumes)
├─ Load Gameplay (chests, enemies, switches)
└─ Load Navigation (AI navmesh, pathfinding graph)
```

## Editor: Cell Visualization & Debugging

In the editor, developers can:
- Visualize grid (draw cell boundaries)
- See cell state (loaded, unloaded, loading)
- Simulate camera movement and observe streaming
- Debug streaming priority (which cells load first)
- Profile memory per cell (actor count, asset size)

```cpp
struct StreamingDebugOverlay {
    bool  show_cell_grid;
    bool  show_cell_bounds;
    bool  show_cell_states;        // color: unloaded=gray, loading=yellow, active=green
    bool  show_streaming_radius;
    bool  show_memory_per_cell;
};
```

## Save/Load Integration

Each cell's state is serializable:

```cpp
struct CellSaveState {
    int32_t cell_x, cell_y, cell_z;
    std::vector<ActorSaveState> actors;
    std::map<ActorHandle, std::vector<uint8_t>> custom_data;
};
```

When saving a game:
1. Only save state of loaded cells (not unloaded ones)
2. Per-actor: save transform, destroyed flag, component state
3. On load: restore cell state before calling on_start

**Seamless loading:** Player saves in cell (0,0,0), loads in cell (0,0,0) — no cell switching.

## Networking: Server-Authoritative Streaming

In multiplayer, the **server** controls what clients load:

```
Server (Authoritative)
├─ Maintains full world state
├─ Computes streaming radius for each player
├─ Broadcasts: "load cells A,B,C / unload cells D,E"
├─ Tells clients which actors are relevant
└─ Continues physics simulation even if client hasn't loaded yet

Client (Replicator)
├─ Receives load/unload messages
├─ Loads cells on schedule
├─ Spawns/destroys actors per server updates
├─ Never initiates streaming itself
└─ If streaming is slow, updates lag (acceptable)
```

## Level of Detail (LOD) for Distant Cells

Distant cells use impostor geometry instead of full detail:

```cpp
/// Level cells can represent distant detail as impostors.
struct CellLOD {
    LODLevel current_lod;

    enum LODLevel {
        Full,          // all geometry, all details (distance < 100m)
        Reduced,       // merged meshes, no small props (100-300m)
        Impostor,      // single billboard or proxy mesh (>300m)
    };
};
```

**How impostors work:**
- Bake distant cell into single texture (captured from camera direction)
- Render impostor billboard instead of full geometry
- Massive memory/bandwidth savings for far cells
- No visibility pop (LOD transitions smooth in distance fog)

## Data Layers — Independent Streaming

Gameplay, Rendering, Navigation, and Persistence stream independently:

```
Level Load Order (detailed)
├─ Persistent (100 ms)
│  ├─ Static geometry
│  ├─ Global lighting data
│  └─ Physics colliders
├─ Rendering (50 ms parallel)
│  ├─ Material/texture assets
│  ├─ Decorative meshes
│  └─ VFX volumes
├─ Gameplay (100 ms parallel)
│  ├─ Triggers
│  ├─ Enemies
│  ├─ Interactive objects
│  └─ Pickups
└─ Navigation (50 ms parallel)
   ├─ AI pathfinding mesh
   └─ Crowd simulation graph

Total: ~300 ms for typical 100m³ cell (no blocking)
```

## Seamless Transitions

Cells load/unload without "loading screen":

```
Player in cell (0,0,0), approaching boundary to (1,0,0)
├─ (1,0,0) starts loading (distance 50m)
├─ (1,0,0) finishes loading (~200ms)
├─ Player crosses boundary
├─ (1,0,0) becomes active
├─ (0,0,0) becomes inactive (after unload_delay = 30 sec)
├─ (0,0,0) unloads asynchronously
└─ No frame stall or loading screen
```

Cells maintain **physics boundary synchronization** — forces/contacts sync across cell boundaries even during transitions.

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Grid-based world partition (100m³ cells), streaming radius (configurable), background cell loading, max-cell memory budget + eviction, level layers (Persistent/Rendering/Gameplay/Navigation), save/load integration, editor visualization. |

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine** |
|---------|----------|---------|--------------|
| World partition | Built-in grid | Manual scene loading | **Built-in grid Phase 3** |
| Cell size | Variable (customizable) | N/A | **100m × 100m × 100m Phase 3** |
| Streaming volumes | Supported | N/A | **Supported Phase 3** |
| Level layers | DataLayers (4+) | Canvases (2D only) | **DataLayers (4) Phase 3** |
| Async loading | Background thread | Single-threaded | **Background thread Phase 3** |
| Save/load | Per-cell state | Manual | **Per-cell state Phase 3** |
| LOD for cells | Nanite (automatic) | Manual meshes | **Impostor LOD Phase 3** |
| Networking | Replication graph | Custom | **Server-authoritative Phase 4+** |

## Rules

1. **NEVER** load more cells than `max_loaded_cells` — memory budget is hard limit
2. **ALWAYS** store per-cell state in save file — enable seamless save/load transitions
3. **NEVER** synchronously load a cell — always use background thread
4. **ALWAYS** preload adjacent cells (distance < streaming_radius) — smooth transitions
5. **NEVER** call on_start/on_stop callbacks outside of cell activation/deactivation
6. **ALWAYS** track physics contacts across cell boundaries — seamless collisions
