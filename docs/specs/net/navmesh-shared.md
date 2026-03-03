# Shared NavMesh Specification

**Status**: Draft
**Phase**: 4
**Scope**: Automatic navmesh generation from scene collision geometry, offline baking to a shared binary, transparent client/server parity, dynamic obstacle support.

---

## 1. Problem Statement (from production experience)

In previous MMORPG versions, the navmesh pipeline required designers to manually:

1. Script a separate tool to scan terrain and generate a mapping file.
2. Run the tool, review the output, fix boundary errors.
3. Handle dynamic objects separately — the static bake did not account for them.
4. Repeat every time the level changed.

This was slow, error-prone, and created a class of bugs where the server's collision model diverged from what players saw on the client.

The goals for this system:

- **Zero manual navmesh authoring.** Designers place scene geometry and collision volumes; the navmesh is derived automatically.
- **One baked file, used by both client and server.** The same `.uznavmesh` binary is loaded identically by the game client (for local pathfinding queries and NPC movement display) and the game server (for authoritative pathfinding and movement validation).
- **Dynamic obstacles work correctly** without rebuilding the entire navmesh.
- **Level designers iterate fast** — bake runs automatically on scene save in the editor; no separate tooling step.

---

## 2. Technology: Recast/Detour

UzEngine uses **Recast Navigation** (open source, MIT), the same library used by Unreal Engine, Unity, Godot, and O3DE.

| Module | Role |
|---|---|
| **Recast** | Offline bake: voxelises geometry → walkability filter → region detection → polygon mesh |
| **Detour** | Runtime pathfinding: `findPath`, `findNearestPoly`, `raycast`, distance queries |
| **DetourTileCache** | Dynamic obstacle API: cylindrical obstacle add/remove without tile rebuild |
| **DetourCrowd** | Multi-agent local avoidance (RVO) for NPC crowd simulation |

---

## 3. Bake Pipeline (Editor)

### 3.1 Input Geometry Source

The navmesh bake uses **collision geometry** as its sole input, not render meshes. This ensures the server's navmesh exactly matches the physics world, and the designer does not maintain a separate "navmesh mesh":

```
Scene Geometry
│
├── CColliderBox / CColliderMesh / CColliderCapsule (Jolt physics)
│     marked as NAV_INCLUDE (default for static geometry)
│
├── CColliderMesh with NAV_EXCLUDE
│     e.g. foliage, small props — too small to affect pathfinding
│
└── CTerrain heightmap
      sampled at navmesh resolution (default 25 cm/sample)
```

Designers tag collision components with `nav_include` (default) or `nav_exclude`. No separate navmesh proxy mesh needed.

### 3.2 Bake Parameters

Stored per-zone in the `.uzscene` file under `[navmesh]`:

```ini
[navmesh]
cell_size   = 0.25      ; horizontal voxel size in meters (affects precision vs bake time)
cell_height = 0.20      ; vertical voxel size in meters
agent_height    = 1.80  ; minimum walkable ceiling height (m)
agent_radius    = 0.35  ; agent body radius; paths stay this far from walls (m)
agent_max_climb = 0.40  ; maximum step height (m)
agent_max_slope = 45.0  ; maximum walkable slope (degrees)
tile_size   = 64        ; tile size in voxels (larger = slower per-tile rebuild)
detail_sample_dist  = 6.0
detail_sample_error = 1.0
```

These are tunable per zone — a dense dungeon uses a smaller `cell_size` for precision; a large open world uses a larger value for performance.

### 3.3 Bake Trigger

The bake runs automatically in the editor:

| Trigger | Action |
|---|---|
| Scene save | Background incremental bake (only dirty tiles) |
| Geometry moved/deleted | Mark affected tiles dirty |
| Bake parameters changed | Full bake |
| Manual `Build NavMesh` button | Full bake, blocking with progress bar |

Bake runs on a background thread pool to avoid blocking the editor UI. Progress is shown in the status bar.

### 3.4 Output: `.uznavmesh`

A binary file stored alongside the scene:

```
scenes/
  zone_talking_island/
    zone.uzscene
    zone.uznavmesh        ← baked navmesh
    zone.uznavmesh.meta   ← bake parameters snapshot (for cache invalidation)
```

The `.uznavmesh` format is the Recast `dtNavMesh` binary serialisation format extended with a UzEngine header:

```
[UZ_NAVMESH_MAGIC: 4B]  "UZNM"
[version: uint16]        format version
[flags: uint16]          reserved
[tile_count: uint32]
[bake_timestamp: uint64] unix timestamp of last full bake
[agent_params: 40B]      copy of bake parameters
[tile_data: variable]    standard dtNavMesh tile binary layout
```

The file is **platform-independent** (endian-safe). The same file runs on the Windows client and a Linux game server.

---

## 4. Runtime Loading

### 4.1 Server (authoritative)

```c
// Zone startup
uz_navmesh_t* nm = uz_navmesh_load("zones/talking_island/zone.uznavmesh");
uz_navmesh_query_t* q = uz_navmesh_query_create(nm, /*max_path_nodes=*/512);

// Path query — called from zone AI tick
uz_path_result_t result;
uz_navmesh_find_path(q, start, end, &nav_filter, &result);
// result.waypoints[] — waypoint list for NPC movement
```

### 4.2 Client (display + local queries)

The client loads the same `.uznavmesh` file (shipped in the client pak, or downloaded as a patch). Uses:

- Local pathfinding for **player movement preview** (click-to-move destination line).
- **NPC interpolation validation** — verify that a received NPC waypoint is on the navmesh before interpolating (basic cheat resistance for third-party clients).
- **Debug visualisation** in editor/play mode (navmesh overlay, path display).

The client never sends pathfinding results to the server. The server computes all paths authoritatively.

### 4.3 NavMesh Version Check

On connection, the server sends the `navmesh_version` (hash of `.uznavmesh` content) to the client. If the client's cached version does not match:

```
Server → Client: NavMeshVersion { zone_id, version_hash }
Client: hash mismatch → request patch via Content API CDN
         GET /v1/navmesh/{zone_id}/{version_hash}.uznavmesh
```

This allows navmesh updates to reach clients without a Steam patch (navmesh is not baked into the main pak — it is a separate downloadable asset).

---

## 5. Dynamic Obstacles

### 5.1 Temporary Cylinder Obstacles (DetourTileCache)

For short-lived blocking geometry (closed gate, dropped boulder, siege engine, barricade):

```c
// Server: add obstacle (cylinder: position, radius, height)
uz_obs_ref_t obs = uz_navmesh_add_obstacle(nm, pos, radius_m, height_m);

// Server: remove obstacle
uz_navmesh_remove_obstacle(nm, obs);

// Must call each zone tick to process pending obstacle changes
uz_navmesh_update(nm, delta_s);
```

- Maximum 64 simultaneous obstacles per navmesh (TileCache limit; configurable via compile flag).
- Add/remove cost: < 0.1 ms.
- Shape: cylinder only. For complex shapes: use off-mesh connection or tile rebuild.
- The server broadcasts `ObstacleAdded` / `ObstacleRemoved` RPCs to clients in the area. The client applies the same API to its local navmesh copy so NPC interpolation and path preview stay in sync.

```c
UZ_RPC_S2C(NavObstacleAdded,
    (UZ_U32,  obs_id)
    (UZ_WPOS, pos)
    (UZ_F32Q_8, radius_m)   // 0–10 m range, 8 bits
    (UZ_F32Q_8, height_m)   // 0–10 m range, 8 bits
)
UZ_RPC_S2C(NavObstacleRemoved,
    (UZ_U32, obs_id)
)
```

### 5.2 Tile Rebuild (Major Dynamic Changes)

For destructible terrain, collapsed buildings, or other permanent geometry changes that affect large areas:

```c
// Server: mark tiles covering the AABB as dirty, schedule async rebuild
uz_navmesh_dirty_aabb(nm, min_pos, max_pos);

// Rebuild runs on a background navmesh worker thread (not the zone tick thread)
// On completion: swaps tile data atomically, zone thread resumes using new tiles
```

Tile rebuild is **async** — the zone continues ticking with the old navmesh during the rebuild. Agents re-path after the tile swap. Rebuild time: ~ 8–20 ms per tile (zone thread is not stalled).

### 5.3 Off-Mesh Connections (Portals, Jumps, Ladders)

Off-mesh connections are set at bake time by placing **`CNavLink`** components in the scene:

```
CNavLink
  start_pos: vec3  (editor gizmo)
  end_pos:   vec3
  radius:    float
  direction: BIDIRECTIONAL | UNIDIRECTIONAL
  area_type: JUMP | TELEPORT | LADDER | DOOR
```

The editor displays nav links as arrows in the navmesh visualisation overlay. Designers place them visually; the bake automatically includes them in the navmesh tile data. No scripting required.

For **dynamic doors** (open/close at runtime): the door bakes as a `DOOR`-area off-mesh link with a customisable traversal cost. When the door is closed, the server sets the link's traversal flags to BLOCKED via the `dtNavMeshQuery` filter; no tile rebuild needed.

---

## 6. NPC Pathfinding and Movement Protocol

### 6.1 Server Path Computation

All pathfinding is computed server-side by the zone's `NavMeshQuery`. The result is a **waypoint list**, not the full polygon corridor:

```c
typedef struct {
    uz_vec3  waypoints[UZ_PATH_MAX_WAYPOINTS];  // 64 waypoints max
    uint8_t  count;
    uint8_t  partial;   // true if path could not reach the destination fully
} uz_path_result_t;
```

Path queries run on the zone tick thread as part of AI processing. Full path recomputation per NPC is done at most every 500 ms (configurable); steering (obstacle avoidance) runs every tick via DetourCrowd.

### 6.2 Movement Replication to Client

The server does **not** send the full waypoint list every tick. Instead it sends:

```c
UZ_RPC_S2C(EntityMove,
    (UZ_U32,     entity_id)
    (UZ_WPOS,    target_pos)       // current movement destination
    (UZ_F32Q_8,  speed)            // m/s, 0–20 range
    (UZ_BOOL,    is_moving)
    (UZ_F32Q_16, yaw)              // facing direction
)
```

The client receives `EntityMove` and moves the entity toward `target_pos` at `speed` using its own local navmesh for smooth interpolation. The local navmesh ensures the entity does not clip through walls during client interpolation.

When the entity reaches `target_pos` or the server issues a new `EntityMove`, the client updates accordingly. This is the same model used by MaNGOS (the classic WoW open-source server).

### 6.3 Player Movement Validation

Player movement is validated server-side using the navmesh:

```c
// C2S: client sends intended destination
UZ_RPC_C2S(PlayerMoveTo,
    (UZ_WPOS, target_pos)
)

// Server validates: is target_pos reachable from current pos on navmesh?
uz_path_result_t path;
uz_navmesh_find_path(q, player_pos, target_pos, &filter, &path);
if (path.count == 0) {
    // Client suggested an unreachable position — ignore
    return;
}
// Authoritative: store path, begin movement simulation on server
```

For **click-to-move** games (classic MMORPG style), the server follows the computed path. For action games with analog input, the server validates that each incremental position update stays on the navmesh (within `agent_radius` tolerance).

---

## 7. Editor Visualisation

The editor renders the navmesh as a transparent overlay in the scene viewport using the `DebugUtils` module from Recast:

| Overlay | Key binding |
|---|---|
| Navmesh polygons (green = walkable, red = obstacle) | `Alt+N` |
| AoI grid cell boundaries | `Alt+G` |
| NPC paths (live, from server) | `Alt+P` |
| Off-mesh connections (arrows) | always on when navmesh shown |

Bake status is shown in the status bar:
- `NavMesh: Up to date` (green)
- `NavMesh: N tiles dirty — baking…` (yellow, spinning)
- `NavMesh: Error — see Output panel` (red)

---

## 8. File Layout

```
src/engine/net/navmesh/
├── uz_navmesh.h/.c              — public API (load, query, obstacle, update)
├── uz_navmesh_bake.h/.c         — offline bake (Recast wrapper, geometry input)
├── uz_navmesh_format.h/.c       — .uznavmesh binary read/write
├── uz_navmesh_debug.h/.c        — DebugUtils overlay rendering (editor only)
├── uz_navmesh_crowd.h/.c        — DetourCrowd multi-agent local avoidance
└── uz_navmesh_link.h/.c         — CNavLink scene component + off-mesh bake integration

src/engine/scene/components/
└── CNavLink.h/.c                — designer-placed nav link component

src/engine/net/server/
└── zone_pathfinder.h/.c         — per-zone path query + AI steering integration

tests/net/navmesh/
├── test_navmesh_load.c           — load .uznavmesh, query walkable poly
├── test_navmesh_path.c           — findPath correctness, partial paths
├── test_navmesh_obstacle.c       — add/remove cylinder obstacle, path changes
├── test_navmesh_tile_rebuild.c   — dirty AABB → async rebuild → verify new tile
├── test_navmesh_offmesh.c        — CNavLink bake → Detour query traversal
└── test_navmesh_version.c        — version hash mismatch → CDN patch flow
```

---

## 9. Dependencies

| Library | Version | Purpose |
|---|---|---|
| recastnavigation | HEAD | Recast bake + Detour pathfinding + DetourTileCache + DetourCrowd |

Managed via vcpkg.

---

## 10. Definition of Done

- [ ] Navmesh bakes automatically from scene collision geometry on scene save in editor; no separate tool step.
- [ ] `.uznavmesh` file loaded identically by client and server; bit-exact parity on all path queries.
- [ ] Designer tags `NAV_EXCLUDE` on a collider → polygon excluded from bake without touching other geometry.
- [ ] `CNavLink` component placed in editor → off-mesh connection appears in baked navmesh; NPC traverses it.
- [ ] Dynamic cylinder obstacle: add/remove in < 0.1 ms; NPC re-paths around it within 1 tick.
- [ ] Tile rebuild for destroyed wall: completes async in < 20 ms; zone tick thread unblocked during rebuild.
- [ ] Player movement validation: server rejects teleport to position not reachable on navmesh.
- [ ] NPC path computed in < 1 ms for 100 m paths; 1 000 simultaneous path queries in < 50 ms.
- [ ] Editor navmesh overlay renders in < 2 ms for a typical zone.
- [ ] Version mismatch triggers CDN download; client resumes with correct navmesh without restart.
- [ ] NavObstacleAdded/Removed RPCs keep client navmesh copy in sync; NPC interpolation does not clip walls.
- [ ] All tests pass. Coverage ≥ 95%.
