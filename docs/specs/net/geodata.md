# GeoData Specification

**Status**: Draft
**Phase**: 4
**Scope**: Server-side height map and 2D collision grid derived automatically from scene geometry, enabling lightweight XY-only movement packets, server-authoritative Z resolution, and fast line-of-sight checks without full 3D physics queries.

---

## 1. Problem Statement (from production experience)

In previous MMORPG versions, the server validated player movement using full 3D position packets (`x, y, z, yaw`). This created:

1. **Larger movement packets** — 12 bytes of position instead of 8.
2. **Client-controllable Z** — a modified client could send an arbitrary Z and stay airborne or clip underground.
3. **Expensive server-side validation** — 3D navmesh or physics queries for every player position update.
4. **LoS queries were slow** — ray tests against the Jolt physics world at zone-tick rate.

The solution: maintain a compact **GeoData grid** — a 2D grid of height layers and surface flags baked from scene collision geometry. The server resolves Z deterministically from XY, the client sends XY only, and movement validation reduces to a 2D cell walk.

This is the same approach used by Lineage 2 (GeoData), Ultima Online (tile heights), and most classic MMORPGs at scale.

---

## 2. Coordinate Model

The world uses a standard right-handed coordinate system:

```
X — east
Y — north  (horizontal, same as X; together form the 2D ground plane)
Z — up
```

The GeoData grid covers the XY plane at a fixed resolution (default **0.5 m/cell**). Each grid cell stores one or more **height layers** — the Z values where walkable surfaces exist.

```
World AABB example: X[-4096, 4096], Y[-4096, 4096]
Grid dimensions:    16384 × 16384 cells  @ 0.5 m/cell
Memory:             see §3.4
```

Cell coordinates:

```c
int32_t cell_x = (int32_t)floorf((world_x - grid_origin_x) / cell_size);
int32_t cell_y = (int32_t)floorf((world_y - grid_origin_y) / cell_size);
```

---

## 3. GeoData Format

### 3.1 Cell Structure

Each cell stores up to **N height layers** (default N=4, configurable at bake time):

```c
#define GEO_MAX_LAYERS 4

typedef struct {
    int16_t  z;          // height in centimeters (range ±327 m, sufficient for all zones)
    uint8_t  flags;      // surface flags (see §3.2)
    uint8_t  _pad;
} uz_geo_layer_t;

typedef struct {
    uint8_t        layer_count;          // 0 = no walkable surface (void / underwater wall)
    uz_geo_layer_t layers[GEO_MAX_LAYERS];
} uz_geo_cell_t;
```

Height stored as **int16 centimeters** → ±327.67 m range, 1 cm precision, 2 bytes. No float needed.

### 3.2 Surface Flags

```c
#define GEO_FLAG_WALKABLE   0x01   // surface is walkable (filtered by agent slope/height during bake)
#define GEO_FLAG_WATER      0x02   // submerged — swimming rules apply
#define GEO_FLAG_NOCOLLIDE  0x04   // pass-through geometry (foliage marked NAV_EXCLUDE)
#define GEO_FLAG_SLOPE_WARN 0x08   // slope > agent_max_slope but < hard limit (slideable)
#define GEO_FLAG_DOOR       0x10   // dynamic surface (door open/close changes walkability)
#define GEO_FLAG_AERIAL     0x20   // aerial zone — entity must be in aerial mode to traverse
```

### 3.3 Grid Header

```c
typedef struct {
    uint32_t  magic;           // 'UZGD' (0x44475A55)
    uint16_t  version;         // format version
    uint16_t  flags;           // reserved
    float     origin_x;        // world X of cell (0,0)
    float     origin_y;        // world Y of cell (0,0)
    float     cell_size;       // meters per cell (default 0.5)
    uint32_t  cells_x;         // grid width  in cells
    uint32_t  cells_y;         // grid height in cells
    uint8_t   max_layers;      // GEO_MAX_LAYERS used in this file
    uint8_t   _pad[3];
    uint64_t  bake_timestamp;  // unix timestamp
    uint32_t  checksum;        // CRC32 of all cell data
} uz_geo_header_t;
```

### 3.4 Memory Budget

| Zone size | Cell size | Grid dims | Cells | Bytes/cell (4 layers) | Total |
|---|---|---|---|---|---|
| 2×2 km | 0.5 m | 4000×4000 | 16 M | 9 B | ~144 MB |
| 2×2 km | 1.0 m | 2000×2000 | 4 M | 9 B | ~36 MB |
| 512×512 m | 0.5 m | 1024×1024 | 1 M | 9 B | ~9 MB |

**Recommendation**: use **1.0 m cells** for large open-world zones (36 MB, 10 cm Z precision adequate for outdoor terrain). Use **0.5 m cells** for dense dungeon zones requiring precise step validation.

Cell size is configurable per zone in `.uzscene`:

```ini
[geodata]
cell_size   = 1.0      ; meters per cell
max_layers  = 4        ; max floors at any one cell
bake_height = 200.0    ; raycast distance downward (m)
```

### 3.5 File Layout

```
scenes/zone_talking_island/
  zone.uzscene
  zone.uznavmesh
  zone.uzgeodata        ← GeoData grid
  zone.uzgeodata.meta   ← bake params snapshot (cache invalidation)
```

---

## 4. Bake Pipeline

### 4.1 Input Geometry

Same source as the navmesh bake (§3.1 of navmesh-shared.md): Jolt collision geometry marked `NAV_INCLUDE`. No separate mesh needed.

### 4.2 Bake Algorithm

```
For each cell (cx, cy):
    world_pos = (origin_x + cx * cell_size + 0.5*cell_size,
                 origin_y + cy * cell_size + 0.5*cell_size,
                 bake_height)   ← start above max terrain height

    Cast ray downward (0, 0, -1) through Jolt physics world.
    Collect ALL hit surfaces within bake_height range.

    Filter hits:
        - Exclude NAV_EXCLUDE colliders
        - Exclude backfaces (upward normals only: dot(normal, up) > 0)
        - Exclude slopes > hard_slope_limit (45° + 5° margin)

    Sort hits by Z descending (highest layer first).

    Store up to max_layers hits as cell layers:
        layer.z     = hit.point.z in centimeters
        layer.flags = derive from surface properties
                        (slope angle → SLOPE_WARN,
                         water volume overlap → WATER,
                         door component → DOOR)
```

Multiple hits at the same approximate Z (within 10 cm) are merged into one layer. This handles thin meshes (floor + ceiling of the same surface within epsilon).

### 4.3 Bake Trigger

Same triggers as navmesh bake:

| Trigger | Action |
|---|---|
| Scene save | Background incremental bake (only dirty cells whose AABB overlaps changed geometry) |
| Geometry moved/deleted | Mark overlapping cells dirty |
| Bake parameters changed | Full bake |
| Manual `Build GeoData` button | Full bake, blocking with progress bar |

GeoData bake runs on the same background thread pool as the navmesh bake. Both can run simultaneously (independent pipelines). Status shown in status bar:

- `GeoData: Up to date` (green)
- `GeoData: N cells dirty — baking…` (yellow)
- `GeoData: Error — see Output` (red)

### 4.4 Dynamic Updates

For **door open/close**: cells with `GEO_FLAG_DOOR` are toggled at runtime via:

```c
uz_geo_set_door(geo, cell_x, cell_y, layer_idx, bool open);
// Clears or sets GEO_FLAG_WALKABLE on that layer.
// Door state is broadcast to clients via NavObstacleAdded/Removed (same RPC as navmesh obstacles).
```

For **destructible terrain**: cells in the affected AABB are marked dirty, triggering an async re-bake of those cells only (same as navmesh tile rebuild).

---

## 5. Runtime API

### 5.1 Query Functions

```c
// Load GeoData for a zone
uz_geodata_t* uz_geo_load(const char* path);
void          uz_geo_free(uz_geodata_t* geo);

// Resolve Z from XY — returns the best layer Z for a given world XY and reference Z
// reference_z: current entity Z (selects closest layer below)
// Returns false if no walkable surface found at (x, y)
bool uz_geo_resolve_z(
    const uz_geodata_t* geo,
    float  world_x,
    float  world_y,
    float  reference_z,   // entity's current Z (for layer selection)
    float* out_z,         // resolved surface Z
    uint8_t* out_flags    // surface flags at the resolved layer
);

// Check if a straight-line 2D path is clear of blocking geometry
// Walks cells along Bresenham line from (x0,y0) to (x1,y1)
// Checks each cell for walkable layer near reference_z (±layer_tolerance_m)
// Returns false on first blocking cell (no walkable layer within tolerance)
bool uz_geo_los_2d(
    const uz_geodata_t* geo,
    float x0, float y0, float z0,
    float x1, float y1,
    float layer_tolerance_m   // how far above/below reference Z to look for a layer
);

// Get cell data directly
const uz_geo_cell_t* uz_geo_cell_at(
    const uz_geodata_t* geo,
    float world_x, float world_y
);

// Update door state
void uz_geo_set_door(uz_geodata_t* geo, int32_t cell_x, int32_t cell_y,
                     uint8_t layer_idx, bool open);
```

### 5.2 Layer Selection Logic

When an entity is at position `(x, y, z)` and the server needs to resolve the canonical Z:

```c
bool uz_geo_resolve_z(geo, x, y, reference_z, out_z, out_flags) {
    cell = uz_geo_cell_at(geo, x, y);
    if (!cell || cell->layer_count == 0) return false;

    // Find the highest walkable layer at or below reference_z + STEP_UP_TOLERANCE
    float best_z = -FLT_MAX;
    bool  found  = false;
    float step_up = AGENT_MAX_CLIMB_M;  // e.g. 0.4 m

    for (int i = 0; i < cell->layer_count; i++) {
        float layer_z = cell->layers[i].z / 100.0f;  // cm → m
        if (!(cell->layers[i].flags & GEO_FLAG_WALKABLE)) continue;
        if (layer_z > reference_z + step_up) continue;  // too high to step up to
        if (layer_z > best_z) {
            best_z = layer_z;
            *out_z    = layer_z;
            *out_flags = cell->layers[i].flags;
            found = true;
        }
    }
    return found;
}
```

This naturally handles multi-floor: an entity on the bridge (Z=10 m) will always resolve to the bridge layer, not the ground below (Z=0), because the bridge layer is the highest one ≤ `reference_z + step_up`.

---

## 6. Movement Protocol Integration

### 6.1 Ground Movement (default mode)

```c
// C2S: client sends XY destination only
UZ_RPC_C2S(PlayerMoveTo,
    (UZ_WPOS_XY, target_xy)   // 8 bytes: 2× fixed-point world XY (0.1 m precision)
)

// Server processing:
void on_player_move_to(uz_player_t* player, uz_wpos_xy_t target_xy) {
    float tx = target_xy.x, ty = target_xy.y;

    // 1. Resolve Z at destination
    float dest_z;
    uint8_t dest_flags;
    if (!uz_geo_resolve_z(geo, tx, ty, player->pos.z, &dest_z, &dest_flags)) {
        // No walkable surface — reject silently (anti-teleport)
        return;
    }

    // 2. Validate path via navmesh (XY path, Z ignored in navmesh poly check)
    uz_path_result_t path;
    uz_wpos_t dest3 = { tx, ty, dest_z };
    uz_navmesh_find_path(q, player->pos, dest3, &filter, &path);
    if (path.count == 0) return;  // unreachable

    // 3. Accept — update server state
    player->target_pos = dest3;
    player->path       = path;
}
```

**Packet size comparison:**

| Mode | Fields | Wire size |
|---|---|---|
| Full XYZ (old) | x f32, y f32, z f32, yaw f16 | 14 bytes |
| GeoData XY (new) | x fixed16, y fixed16 | 8 bytes (−43%) |

### 6.2 Position Tick Validation

Each zone tick, the server advances the player along their path and resamples Z from GeoData:

```c
void player_tick(uz_player_t* player, float dt, uz_geodata_t* geo) {
    if (!player->is_moving) return;

    // Advance position along path
    uz_advance_path(player, dt);

    // Resample Z from GeoData every tick (handles slopes and steps smoothly)
    float new_z;
    uint8_t flags;
    if (uz_geo_resolve_z(geo, player->pos.x, player->pos.y, player->pos.z, &new_z, &flags)) {
        player->pos.z = new_z;
    }
    // else: player is briefly airborne (knockup, jump off edge) — maintain current Z
}
```

### 6.3 Aerial Mode

Entities that enter aerial mode (flying mount, knockup, falling off a cliff) switch to **XYZ mode**:

```c
// Flag per entity
typedef enum {
    MOVE_MODE_GROUND  = 0,   // GeoData Z resolution active
    MOVE_MODE_AERIAL  = 1,   // Full XYZ physics; GeoData suspended
    MOVE_MODE_SWIM    = 2,   // Water volume; Z constrained by water surface + depth
} uz_move_mode_t;
```

When `move_mode == MOVE_MODE_AERIAL`:
- Client sends full XYZ destination.
- Server uses Jolt CharacterVirtual for position validation (no GeoData).
- Server broadcasts position at full rate (20 Hz vs 10 Hz ground).
- On landing (`layer_z` found within 0.5 m below entity Z): transition back to `MOVE_MODE_GROUND`, snap Z to GeoData.

Mode transitions are broadcast:

```c
UZ_RPC_S2C(EntityMoveMode,
    (UZ_U32, entity_id)
    (UZ_U8,  move_mode)   // MOVE_MODE_GROUND / AERIAL / SWIM
)
```

### 6.4 S2C Position Replication

Server-to-client entity position uses the same `EntityMove` RPC from the navmesh spec. The Z resolved from GeoData is included so the client can smoothly lerp — the client does not independently resolve Z (it trusts the server):

```c
UZ_RPC_S2C(EntityMove,
    (UZ_U32,     entity_id)
    (UZ_WPOS,    target_pos)     // full XYZ from server (Z resolved via GeoData)
    (UZ_F32Q_8,  speed)
    (UZ_BOOL,    is_moving)
    (UZ_F32Q_16, yaw)
)
```

The bandwidth saving comes from `PlayerMoveTo` (C2S) — the client sends only XY. The server sends full XYZ in `EntityMove` (S2C) since it's authoritative and clients need Z for smooth rendering.

---

## 7. Line-of-Sight and Skill Range Checks

### 7.1 2D LoS Walk

For skills and aggro range checks, the server needs LoS between two positions. Full 3D Jolt raycasts are expensive at zone-tick rate. The GeoData LoS walk is O(distance/cell_size) with no physics overhead:

```c
bool uz_geo_los_2d(geo, x0, y0, z0, x1, y1, layer_tolerance_m) {
    // Bresenham line from cell(x0,y0) to cell(x1,y1)
    // For each cell along the line:
    //   - if cell has no walkable layer within (z0 ± layer_tolerance_m): blocked
    //   - track Z progression: update z0 as we step along the line
    //     using the resolved Z of each intermediate cell
    // Return true only if every cell along the path has a walkable layer
}
```

`layer_tolerance_m` is typically `2 × agent_height` (3.6 m) — enough to detect walls and overhangs but not flag multi-floor as blocked.

### 7.2 When to Use Jolt vs GeoData LoS

| Query type | Method | Cost |
|---|---|---|
| Skill range + LoS (same floor) | GeoData 2D walk | ~0.01 ms |
| Skill LoS across floors / complex geometry | GeoData 2D walk with layer check | ~0.02 ms |
| Spell projectile trajectory | Jolt ray (only for projectile spells) | ~0.1 ms |
| Physics overlap (AoE hit detection) | Jolt sphere/cylinder overlap | ~0.05 ms |

The server uses GeoData LoS for all **aggro, targeting, and skill range** checks. Jolt is reserved for **projectile travel** and **AoE hit detection** (see spell-system.md).

---

## 8. Client-Side Usage

The client loads the same `.uzgeodata` file and uses it for:

1. **Visual Z resolution** — smooth terrain-hugging when walking (client resolves Z locally at 60 Hz without waiting for server).
2. **Click-to-move destination** — show valid/invalid cursor icon based on GeoData walkability before sending `PlayerMoveTo`.
3. **NPC interpolation** — same as server: advance NPC toward `target_pos`, snap Z via GeoData each frame.

The client trusts the server's Z in `EntityMove` as the authoritative value and lerps to it smoothly.

---

## 9. Editor Visualisation

The editor renders the GeoData as an optional overlay in the scene viewport:

| Overlay | Key binding | Description |
|---|---|---|
| GeoData cell grid (height-colored) | `Alt+H` | Cells colored by height (green = walkable, red = blocked/void, blue = water) |
| Layer count overlay | `Alt+L` | Cells colored by layer count (white=1, yellow=2, orange=3, red=4) |
| GeoData LoS test | `Alt+Click` source + `Alt+Click` dest | Draws Bresenham walk, highlights blocking cells in red |

---

## 10. Integration with Other Systems

| System | Integration |
|---|---|
| **NavMesh** | GeoData bake runs in parallel with navmesh bake. NavMesh used for pathfinding (waypoint list). GeoData used for Z resolution and LoS checks during path execution. |
| **Spell System** | Skill range and LoS validation uses GeoData 2D walk. AoE hit overlap uses Jolt. |
| **Creature AI** | NPC Z resolved via GeoData each AI tick (same as player). LoS for aggro range check uses GeoData walk. |
| **Jolt Physics** | GeoData is not a physics body — it is a lookup table derived from Jolt geometry. Both exist simultaneously. |
| **Terrain System** | CTerrain heightmap is a primary source for GeoData bake (sampled at GeoData resolution). |

---

## 11. File Layout

```
src/engine/net/geodata/
├── uz_geodata.h/.c             — public API (load, resolve_z, los_2d, set_door)
├── uz_geodata_bake.h/.c        — bake pipeline (Jolt raycast grid, layer merging)
├── uz_geodata_format.h/.c      — .uzgeodata binary read/write
├── uz_geodata_debug.h/.c       — editor overlay rendering (height map, layer count)
└── uz_geodata_lua.h/.c         — Lua ctx.geo.* bindings (optional, for scripted queries)

tests/net/geodata/
├── test_geodata_bake.c         — bake from simple geometry, verify layer count and Z values
├── test_geodata_resolve_z.c    — single-layer, multi-layer, no surface cases
├── test_geodata_layer_select.c — bridge over ground: correct layer chosen by reference_z
├── test_geodata_los.c          — clear LoS, wall blocked, multi-floor tolerance
├── test_geodata_door.c         — door flag toggle, walkability changes
├── test_geodata_aerial.c       — aerial mode transition, landing Z snap
├── test_geodata_format.c       — binary round-trip, checksum validation
└── test_geodata_perf.c         — 10 000 resolve_z queries < 1 ms; 1 000 LoS walks < 5 ms
```

---

## 12. Dependencies

| Library | Purpose |
|---|---|
| Jolt Physics | Raycast source for bake pipeline (shared with physics system) |

No additional dependencies. GeoData is a self-contained lookup table using only integer math at runtime.

---

## 13. Definition of Done

### Bake
- [ ] GeoData bake runs automatically on scene save alongside navmesh bake; no manual step.
- [ ] Bake collects up to `max_layers` (default 4) hits per cell via Jolt downward raycasts.
- [ ] NAV_EXCLUDE geometry correctly excluded from bake.
- [ ] Slope > `agent_max_slope` sets `GEO_FLAG_WALKABLE` clear; steep surfaces not walkable.
- [ ] Water volumes set `GEO_FLAG_WATER` on affected layers.
- [ ] Cells with no walkable surface store `layer_count = 0`.
- [ ] Incremental bake: only cells overlapping changed geometry rebuild; full bake on parameter change.
- [ ] `.uzgeodata` checksum validated on load; corrupted file rejected with error.

### Runtime Query
- [ ] `uz_geo_resolve_z` selects correct layer for single-floor, multi-floor (bridge over ground), and aerial→ground transition.
- [ ] `uz_geo_resolve_z` returns false for void cells (no walkable surface).
- [ ] `uz_geo_los_2d` correctly identifies clear LoS, wall-blocked LoS, and multi-floor scenarios.
- [ ] 10 000 `resolve_z` queries complete in < 1 ms (pure table lookup, no physics).
- [ ] 1 000 `los_2d` walks (50 m path at 1 m/cell) complete in < 5 ms.

### Movement Protocol
- [ ] `PlayerMoveTo` C2S RPC carries XY only (8 bytes); server resolves Z via GeoData.
- [ ] Server rejects `PlayerMoveTo` to cell with no walkable surface.
- [ ] Server advances player along path each tick and resamples Z from GeoData.
- [ ] `EntityMove` S2C RPC includes server-resolved Z; client lerps smoothly.
- [ ] Wire size for `PlayerMoveTo`: 8 bytes (vs 14 bytes for XYZ — 43% reduction verified by test).

### Multi-Floor
- [ ] Bridge scenario: entity on bridge (Z=10 m) resolves to bridge layer, not ground (Z=0).
- [ ] Entity drops off bridge: aerial mode activates; on landing, Z snaps to GeoData ground.
- [ ] `layer_tolerance_m` in LoS walk correctly handles same-floor targets through walls vs cross-floor.

### Aerial Mode
- [ ] `EntityMoveMode` RPC broadcast on ground↔aerial↔swim transitions.
- [ ] Aerial entities accept full XYZ destination; GeoData resolution suspended.
- [ ] Landing detection: Z within 0.5 m of GeoData surface → snap to ground, `MOVE_MODE_GROUND`.

### Dynamic Updates
- [ ] Door `GEO_FLAG_DOOR` toggle: walkability changes within 1 tick; clients notified via NavObstacleAdded/Removed.

### Client
- [ ] Client loads `.uzgeodata`, resolves Z locally at 60 Hz for smooth character grounding.
- [ ] Client cursor shows walkable/invalid based on GeoData before sending `PlayerMoveTo`.

### Tests
- [ ] All tests pass. Coverage ≥ 95%.
