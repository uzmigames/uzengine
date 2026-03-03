# Dynamic Blockers Specification

**Status**: Draft
**Phase**: 4
**Scope**: Server-side runtime layer of temporary collision blockers — skill walls, dungeon gates, repulsion zones, and traps — with defined lifetimes, server-authoritative movement prevention, and client-side visual feedback.

---

## 1. Design Goals

GeoData and NavMesh are **baked** — they represent the static geometry of the zone. Dynamic blockers are a **separate runtime layer** that overlays them:

- **No bake required** — blockers are created and destroyed at runtime by the server, triggered by skills, events, or scripted zones.
- **Consistent with GeoData LoS** — all movement validation and LoS queries consult the blocker list after GeoData, not instead of it.
- **NavMesh stays baked** — NavMesh `TileCache` obstacles (from navmesh-shared.md) handle NPC pathfinding avoidance separately. Dynamic blockers handle player movement rejection at the server tick level.
- **Short TTL** — blockers have a defined lifetime (seconds to minutes). Permanent changes use the baked geometry pipeline.
- **Four types**: `WALL`, `GATE`, `REPULSION`, `TRAP`.

---

## 2. Blocker Types

### 2.1 WALL — Skill Barrier

A solid geometric surface that prevents passage. Created by skill (Fire Wall, Ice Wall, Wind Barrier) or by the server (boss scripted arena walls). Has a lifetime in seconds.

```
Shape:      LINE segment with thickness (width_m, height_m)
            or CYLINDER (radius_m, height_m)
Lifetime:   1–300 s
Permeates:  nothing — blocks all entities
Destroy:    on expiry, or on HP depletion (destructible walls)
```

### 2.2 GATE — Door / Dungeon Gate

A controlled barrier that can be toggled open/closed by the server (event script or Lua). Typically permanent until toggled.

```
Shape:      RECTANGLE or LINE with thickness
Lifetime:   indefinite (toggled, not timed)
State:      OPEN (passthrough) | CLOSED (blocking)
Destroy:    never auto-expires; server closes/opens it explicitly
```

### 2.3 REPULSION — Force Field / Wind Zone

An area that applies a continuous force (knockback direction and magnitude) to entities inside it. Does not hard-block passage — entities can push through with enough speed, or the force can be large enough to prevent entry.

```
Shape:      CYLINDER or SPHERE
Lifetime:   1–600 s
Force:      direction (vec3) + magnitude (m/s²)
Mode:       PUSH_OUT (force radiates outward from center)
            PUSH_DIR (fixed world-space direction)
            PUSH_UP  (vertical lift — e.g. updraft)
```

### 2.4 TRAP — Triggered Area

A hidden (or visible) area that activates when an entity steps on it. On activation, applies an effect (via spell system EffectDef) and optionally freezes the entity in place for `freeze_s` seconds.

```
Shape:      CYLINDER (trigger area)
Lifetime:   until triggered, or timed
Trigger:    on entity enter (first step into area)
On trigger: apply EffectDef(s) to triggering entity
            optional freeze_s (root-like, prevents movement)
Visibility: HIDDEN (only activator can see it) | VISIBLE_TO_ALL
```

---

## 3. Data Model

### 3.1 Blocker Struct

```c
typedef enum {
    UZ_BLOCKER_WALL      = 0,
    UZ_BLOCKER_GATE      = 1,
    UZ_BLOCKER_REPULSION = 2,
    UZ_BLOCKER_TRAP      = 3,
} uz_blocker_type_t;

typedef enum {
    UZ_BSHAPE_LINE     = 0,   // line segment with width + height
    UZ_BSHAPE_CYLINDER = 1,   // cylinder: radius + height
    UZ_BSHAPE_RECTANGLE= 2,   // oriented rectangle: width + depth + height
    UZ_BSHAPE_SPHERE   = 3,   // sphere: radius
} uz_blocker_shape_t;

typedef struct {
    uint32_t           blocker_id;     // unique ID within zone, assigned by server
    uz_blocker_type_t  type;
    uz_blocker_shape_t shape;

    uz_vec3_t          pos;            // anchor position (world space)
    float              yaw;            // orientation (radians)

    // Shape parameters (interpretation depends on shape):
    float              param_a;        // LINE: length_m  | CYL: radius_m  | RECT: width_m  | SPH: radius_m
    float              param_b;        // LINE: width_m   | CYL: height_m  | RECT: depth_m
    float              param_c;        // LINE: height_m  |                | RECT: height_m

    uint32_t           owner_id;       // entity that created this blocker (0 = server/zone)
    uint32_t           skill_id;       // skill that created it (0 = none)

    // Lifetime
    float              lifetime_s;     // remaining lifetime (-1 = indefinite/gate)
    float              elapsed_s;      // time alive so far

    // Type-specific
    union {
        struct {                        // WALL
            int32_t    hp;             // -1 = indestructible
            int32_t    max_hp;
        } wall;

        struct {                        // GATE
            uint8_t    state;          // 0 = OPEN, 1 = CLOSED
        } gate;

        struct {                        // REPULSION
            uz_vec3_t  force_dir;      // normalized direction (for PUSH_DIR)
            float      force_mag;      // m/s²
            uint8_t    mode;           // PUSH_OUT / PUSH_DIR / PUSH_UP
        } repulsion;

        struct {                        // TRAP
            uint32_t   effect_ids[4];  // EffectDef IDs to apply on trigger
            uint8_t    effect_count;
            float      freeze_s;       // how long triggering entity is rooted
            uint8_t    triggered;      // 1 = already fired (single-trigger traps)
            uint8_t    visible;        // 0 = hidden, 1 = visible to all
        } trap;
    };
} uz_blocker_t;
```

### 3.2 Blocker World

```c
#define UZ_MAX_BLOCKERS 512   // per zone; configurable at compile time

typedef struct {
    uz_blocker_t  blockers[UZ_MAX_BLOCKERS];
    uint16_t      count;
    uint32_t      next_id;    // monotonic blocker ID counter
} uz_blocker_world_t;
```

512 simultaneous blockers per zone is sufficient for typical MMORPG load (skill walls expire in seconds; gates are few). Configurable at compile time via `UZ_MAX_BLOCKERS` define.

---

## 4. Runtime Queries

### 4.1 Movement Block Check

Called by the server movement system after GeoData validation, before accepting a player step:

```c
// Returns true if the straight line from pos_from to pos_to is blocked by any active blocker.
// entity_z is used to filter by height overlap.
bool uz_blockers_check_movement(
    const uz_blocker_world_t* bw,
    uz_vec3_t pos_from,
    uz_vec3_t pos_to,
    float     entity_height_m   // entity's capsule height
);
```

Implementation: for each active blocker of type WALL or GATE(CLOSED):
- Broad phase: skip if entity AABB doesn't overlap blocker AABB.
- Narrow phase: geometry-specific segment intersection test (see §4.4).

Complexity: O(N) where N = active blockers. At N≤512 and query rate of ~100 players/tick, worst case is 51 200 tests per tick. Each test is a few multiplications — acceptable.

### 4.2 LoS Check Integration

`uz_geo_los_2d` from geodata.md already walks cells. After the GeoData walk succeeds, the caller optionally runs a blocker LoS check:

```c
bool uz_blockers_check_los(
    const uz_blocker_world_t* bw,
    uz_vec3_t from,
    uz_vec3_t to
);
```

Used by: skill range validation, aggro acquisition, creature AI LoS. Not called every frame — called at aggro-check rate (1 Hz) and on skill activation.

### 4.3 Spell LoS Validation (full pipeline)

All server-side LoS and skill-target validation runs through a **unified pipeline** that checks GeoData static geometry first, then dynamic blockers. Spells cannot pass through walls — static or dynamic.

```c
// Complete LoS check: GeoData + dynamic blockers
// Called on every skill activation that requires LoS (see SkillDef.require_los)
bool uz_los_check(
    const uz_geodata_t*      geo,
    const uz_blocker_world_t* bw,
    uz_vec3_t from,           // caster eye position (pos + eye_height_m)
    uz_vec3_t to,             // target eye position or ground point
    float     layer_tolerance_m
) {
    // Step 1: GeoData 2D LoS walk (static geometry + baked walls)
    if (!uz_geo_los_2d(geo, from.x, from.y, from.z,
                            to.x,   to.y, layer_tolerance_m)) {
        return false;  // blocked by static geometry
    }
    // Step 2: Dynamic blocker segment test (WALL + GATE CLOSED only)
    if (!uz_blockers_check_los(bw, from, to)) {
        return false;  // blocked by runtime wall/gate
    }
    return true;
}
```

**SkillDef `require_los` flag** (added to spell-system.md spec):

```json
{
  "skill_id": "fireball",
  "require_los":       true,   // default true for all offensive skills
  "los_check_point":   "eye"   // "eye" (default) | "center" | "feet"
}
```

| `require_los` | Behaviour |
|---|---|
| `true` (default for TARGET_ENTITY, GROUND_CIRCLE, CAMERA_FORWARD) | `uz_los_check()` runs at skill activation. Fails → `SkillFailed { reason: NO_LOS }` |
| `false` (e.g. SELF buffs, chain heals through walls — intentional design) | No LoS check |

**GROUND_POINT / GROUND_CIRCLE** skills — location LoS rule:

```
Server validates:
  1. Range: distance(caster, target_pos) ≤ skill.range_m
  2. LoS:   uz_los_check(caster_eye, target_pos + (0, 0, 0.5))
            → cannot place AoE behind a wall or inside a room the caster cannot see into
  3. NavMesh surface: target_pos must be on a walkable surface (uz_geo_resolve_z succeeds)
```

This prevents the common exploit of placing ground AoE skills through walls by clicking on the geometry behind them.

**New SkillFailed reason code:**

```c
UZ_RPC_S2C(SkillFailed,
    (UZ_U32, skill_id)
    (UZ_U8,  reason)
    // reason values (appended to spell-system.md):
    // SILENCED=0, STUNNED=1, CASTING=2, NO_TARGET=3, OUT_OF_RANGE=4,
    // RESOURCE=5, COOLDOWN=6, NO_LOS=7   ← NEW
)
```

### 4.4 Repulsion Tick

Each zone tick, entities inside REPULSION blockers receive velocity impulses:

```c
void uz_blockers_apply_repulsion(
    uz_blocker_world_t* bw,
    uz_entity_t*        entities,
    uint32_t            entity_count,
    float               dt
);
```

For each entity: check if inside any REPULSION blocker (point-in-shape test). If yes, compute force vector (outward or fixed direction), add to entity velocity. Physics integration applies the displacement next tick. For strong forces, the entity may never enter (effective "soft wall"). For weaker forces, entities can push through slowly.

### 4.4 Intersection Tests

```c
// LINE blocker: oriented box segment test
// Model as oriented bounding box (OBB) with:
//   half-extents: (param_a/2, param_b/2, param_c/2)
//   center:       blocker.pos + half_height_up
//   orientation:  blocker.yaw
// Segment-OBB intersection using separating axis theorem (SAT)

// CYLINDER blocker:
// Project segment to XY. Circle-segment intersection test (2D).
// Height check: segment Z range must overlap [pos.z, pos.z + param_b].

// RECTANGLE blocker:
// Same as LINE (OBB).

// SPHERE blocker (REPULSION only — no hard movement block):
// Point-in-sphere test per entity each tick.
```

### 4.5 Trap Trigger

Each zone tick, for each TRAP blocker not yet triggered:

```c
void uz_blockers_check_traps(
    uz_blocker_world_t*  bw,
    uz_zone_t*           zone,
    float                dt
);
```

For each non-triggered trap: iterate entities, check if any entity's XY position falls inside the trap cylinder (2D circle test + Z range). On first trigger:
1. Apply all `effect_ids` via the spell system (`uz_effect_apply`).
2. Apply `freeze_s` root effect to triggering entity.
3. Mark `triggered = 1` (single-trigger) or reset for multi-trigger traps (configured via skill SkillDef `trap_retrigger: true`).
4. Broadcast `BlockerTriggered` RPC.

---

## 5. Blocker Lifecycle

### 5.1 Creation

Blockers are created by:

**a) Skill system** — when a skill with `built_in_handler: wall | gate | repulsion | trap` executes:

```json
{
  "skill_id": "ice_wall",
  "target_type": "GROUND_POINT",
  "built_in_handler": "wall",
  "wall": {
    "shape":      "LINE",
    "length_m":   6.0,
    "width_m":    0.5,
    "height_m":   3.0,
    "lifetime_s": 12.0,
    "hp":         -1
  }
}
```

The spell system calls `uz_blocker_create` after the skill executes at the ground point.

**b) Zone script (Lua)** — for event-driven gates and boss scripted walls:

```lua
-- Create a gate (indefinite lifetime)
local gate_id = ctx.blockers.create_gate({
    pos      = vec3(100, 200, 0),
    yaw      = math.pi / 2,
    width_m  = 4.0,
    depth_m  = 0.5,
    height_m = 3.0,
})

-- Open / close it
ctx.blockers.set_gate(gate_id, "CLOSED")
ctx.blockers.set_gate(gate_id, "OPEN")

-- Create a timed wall
local wall_id = ctx.blockers.create_wall({
    pos        = vec3(50, 100, 0),
    yaw        = 0,
    length_m   = 8.0,
    width_m    = 0.5,
    height_m   = 2.5,
    lifetime_s = 30.0,
})

-- Destroy early
ctx.blockers.destroy(wall_id)

-- Create a trap
local trap_id = ctx.blockers.create_trap({
    pos        = vec3(80, 150, 0),
    radius_m   = 1.0,
    effect_ids = { "slow_50pct", "dot_poison" },
    freeze_s   = 0.5,
    visible    = false,
})
```

**c) Server C code** — direct API call for low-level zone logic:

```c
uz_blocker_t b = {
    .type       = UZ_BLOCKER_GATE,
    .shape      = UZ_BSHAPE_RECTANGLE,
    .pos        = { 100.0f, 200.0f, 0.0f },
    .yaw        = UZ_PI_HALF,
    .param_a    = 4.0f,  // width
    .param_b    = 0.5f,  // depth
    .param_c    = 3.0f,  // height
    .lifetime_s = -1.0f, // indefinite
    .gate       = { .state = 1 }, // CLOSED
};
uint32_t gate_id = uz_blocker_create(zone->blockers, &b);
```

### 5.2 Tick (expiry + repulsion)

```c
void uz_blockers_tick(uz_blocker_world_t* bw, uz_zone_t* zone, float dt) {
    for (int i = 0; i < bw->count; ) {
        uz_blocker_t* b = &bw->blockers[i];

        // Expiry
        if (b->lifetime_s >= 0.0f) {
            b->elapsed_s += dt;
            if (b->elapsed_s >= b->lifetime_s) {
                uz_blocker_destroy(bw, b->blocker_id);
                // bw->blockers[i] is now the former last element (swap-remove)
                continue;
            }
        }

        i++;
    }

    // Repulsion forces
    uz_blockers_apply_repulsion(bw, zone->entities, zone->entity_count, dt);

    // Trap triggers
    uz_blockers_check_traps(bw, zone, dt);
}
```

### 5.3 Destruction

Blockers are destroyed by:
- Lifetime expiry (WALL, REPULSION, TRAP with `lifetime_s ≥ 0`)
- `uz_blocker_destroy(bw, id)` call (Lua, spell system, C)
- WALL HP depletion: skills/attacks deal damage to destructible walls via `uz_blocker_damage(bw, id, amount)`

On destruction, `BlockerRemoved` RPC is broadcast to clients in the blocker's AoI cell.

---

## 6. Movement Validation Integration

Full server-side movement validation pipeline (all checks in sequence):

```
PlayerMoveTo (XY) received:
    1. GeoData: uz_geo_resolve_z() — surface exists?
    2. NavMesh:  uz_navmesh_find_path() — path reachable?
    3. Blockers: uz_blockers_check_movement(from, to) — no wall/gate in the way?
    4. Accept: update player target_pos, begin path execution

Each tick (path step):
    1. Advance player along path by speed × dt
    2. uz_geo_resolve_z() — resample Z
    3. uz_blockers_check_movement(prev_pos, new_pos) — blocker appeared mid-path?
       → if blocked: stop player at last valid position, send EntityMove(current_pos, speed=0)
    4. uz_blockers_check_traps() — stepped into trap?
```

---

## 7. Client-Side Representation

### 7.1 RPCs

```c
// Server → clients in AoI: blocker appeared
UZ_RPC_S2C(BlockerAdded,
    (UZ_U32,    blocker_id)
    (UZ_U8,     type)          // WALL / GATE / REPULSION / TRAP(visible only)
    (UZ_U8,     shape)
    (UZ_WPOS,   pos)
    (UZ_F32Q_16, yaw)
    (UZ_F32Q_8,  param_a)
    (UZ_F32Q_8,  param_b)
    (UZ_F32Q_8,  param_c)
    (UZ_U16,    lifetime_ms)   // 0 = indefinite
    (UZ_U32,    skill_id)      // for client VFX lookup (0 = generic)
)

// Server → clients in AoI: blocker removed
UZ_RPC_S2C(BlockerRemoved,
    (UZ_U32, blocker_id)
)

// Gate state change
UZ_RPC_S2C(GateStateChanged,
    (UZ_U32, blocker_id)
    (UZ_U8,  state)   // 0 = OPEN, 1 = CLOSED
)

// Trap triggered (broadcast to all in AoI, including players who didn't step on it)
UZ_RPC_S2C(BlockerTriggered,
    (UZ_U32, blocker_id)
    (UZ_U32, triggered_by)   // entity_id of entity that triggered it
)

// Destructible wall HP update
UZ_RPC_S2C(BlockerDamaged,
    (UZ_U32, blocker_id)
    (UZ_I16, hp_remaining)
)
```

Hidden TRAP blockers (`visible = false`) are **not sent** to clients via `BlockerAdded`. Only `BlockerTriggered` is sent when the trap fires — revealing its position at that point.

### 7.2 Client Visual Representation

The client maps `skill_id` → visual asset for the blocker. The SkillDef includes blocker visual fields:

```json
{
  "skill_id": "ice_wall",
  "blocker_visual": {
    "mesh":       "effects/ice_wall/ice_wall.uzfbx",
    "material":   "effects/ice_wall/ice_wall.uzmat",
    "vfx_spawn":  "effects/ice_wall/spawn.uzfx",
    "vfx_destroy":"effects/ice_wall/shatter.uzfx",
    "sound_spawn": "audio/skills/ice_wall_spawn.ogg",
    "sound_loop":  "audio/skills/ice_wall_loop.ogg"
  }
}
```

For blockers with `skill_id = 0` (server-placed gates, generic walls), the client uses a default gray barrier mesh.

The client renders the blocker as a scene object parented to nothing — it exists only while the `BlockerAdded` RPC is active. On `BlockerRemoved`, the client plays the destroy VFX and removes the object.

### 7.3 Client-Side Movement Prediction

The client maintains a local copy of all visible blockers. Before sending `PlayerMoveTo`, the client checks:

```lua
-- Client-side blocker check (prediction only — server is authoritative)
if ctx.blockers.is_blocked(from_pos, to_pos) then
    -- Show "path blocked" cursor, do not send PlayerMoveTo
    return
end
```

If the server rejects a movement the client thought was valid (blocker appeared between client input and server tick), the server sends `EntityMove` with the corrected (stopped) position and the client snaps to it.

---

## 8. NavMesh Coordination

Dynamic blockers do **not** modify the NavMesh directly. However, WALL and GATE(CLOSED) blockers should also influence NPC pathfinding. This is handled via the existing `NavMesh TileCache` obstacle API (navmesh-shared.md §5.1):

```c
// When a WALL blocker is created:
uz_obs_ref_t obs = uz_navmesh_add_obstacle(
    nm,
    blocker.pos,
    blocker.param_a / 2.0f,   // radius approximation for WALL
    blocker.param_c            // height
);
blocker.navmesh_obs_ref = obs;

// When the WALL blocker is destroyed:
uz_navmesh_remove_obstacle(nm, blocker.navmesh_obs_ref);
```

GATE blockers use the `dtNavMeshQuery` filter flag approach from navmesh-shared.md §5.3 (off-mesh connection BLOCKED flag).

REPULSION and TRAP blockers do not affect NavMesh — NPCs are not repelled by force fields (or this is handled by creature AI behavior nodes if desired).

---

## 9. Siege Warfare (Barricades)

Siege barricades are WALL blockers with HP, placed by players during siege warfare:

```json
{
  "skill_id": "place_barricade",
  "built_in_handler": "wall",
  "wall": {
    "shape":      "RECTANGLE",
    "length_m":   2.0,
    "width_m":    0.5,
    "height_m":   2.0,
    "lifetime_s": -1,
    "hp":         5000,
    "max_hp":     5000
  }
}
```

Barricades are permanent until destroyed (HP=0). Attacking players deal damage via normal combat → `uz_blocker_damage(bw, id, amount)`. On HP=0, `BlockerRemoved` broadcast, NavMesh obstacle removed.

The number of barricades a player can place is limited via the skill's cooldown and a per-player counter enforced server-side.

---

## 10. File Layout

```
src/engine/net/blockers/
├── uz_blockers.h/.c             — public API (create, destroy, tick, check_movement, check_los)
├── uz_blockers_query.h/.c       — intersection tests (LINE/CYL/RECT/SPHERE), repulsion, trap trigger
├── uz_blockers_navmesh.h/.c     — TileCache obstacle sync on blocker create/destroy
├── uz_blockers_lua.h/.c         — Lua ctx.blockers.* API
└── uz_blockers_rpc.h/.c         — BlockerAdded / BlockerRemoved / GateStateChanged broadcast

tests/net/blockers/
├── test_blocker_wall.c          — wall create, movement blocked, expiry, removal
├── test_blocker_gate.c          — gate CLOSED blocks, OPEN allows, toggle mid-path
├── test_blocker_repulsion.c     — entity in repulsion zone receives force, aerial mode interaction
├── test_blocker_trap.c          — trap trigger on step, effect apply, freeze, hidden not sent to client
├── test_blocker_los.c           — LoS blocked by WALL, not blocked by REPULSION
├── test_blocker_siege.c         — barricade HP damage, destroy on HP=0, navmesh sync
├── test_blocker_navmesh.c       — TileCache obstacle added/removed on blocker lifecycle
└── test_blocker_perf.c          — 512 blockers, 100 movement checks/tick < 1 ms total
```

---

## 11. Dependencies

| Library | Purpose |
|---|---|
| Jolt Physics | Repulsion force integration via CharacterVirtual (shared with physics system) |
| recastnavigation (DetourTileCache) | NavMesh obstacle sync for WALL/GATE blockers |

---

## 12. Definition of Done

### Core Blocker System
- [ ] `UZ_MAX_BLOCKERS` (512) simultaneous blockers per zone; swap-remove O(1) destroy.
- [ ] Blocker lifecycle: create, tick (expiry), destroy — all correct.
- [ ] `uz_blockers_check_movement` returns true for blocked path (WALL, GATE CLOSED).
- [ ] `uz_blockers_check_movement` returns false for open path (GATE OPEN, REPULSION, expired blocker).
- [ ] 512 blockers, 100 movement checks per tick complete in < 1 ms total.

### WALL
- [ ] WALL with `lifetime_s` expires after correct duration; `BlockerRemoved` broadcast.
- [ ] Indestructible wall (`hp = -1`) cannot be damaged.
- [ ] Destructible wall takes damage via `uz_blocker_damage`; `BlockerDamaged` RPC with remaining HP.
- [ ] Wall destroyed at HP=0; `BlockerRemoved` broadcast; NavMesh obstacle removed.
- [ ] Player path blocked mid-movement by newly appeared wall: server stops player at last valid position.

### GATE
- [ ] Gate CLOSED: movement blocked for all entities.
- [ ] Gate OPEN: movement allowed.
- [ ] `ctx.blockers.set_gate(id, "CLOSED")` / `"OPEN"` Lua API works; `GateStateChanged` RPC broadcast.
- [ ] Gate uses NavMesh off-mesh connection BLOCKED flag — NPC does not path through closed gate.

### REPULSION
- [ ] Entity inside PUSH_OUT zone receives outward force proportional to `force_mag`.
- [ ] Entity inside PUSH_DIR zone receives fixed-direction force.
- [ ] PUSH_UP lifts entity into aerial mode at sufficient magnitude.
- [ ] Repulsion force does not hard-block movement (entity can push through if faster than force).

### TRAP
- [ ] Hidden trap (`visible = false`): `BlockerAdded` NOT sent to clients. `BlockerTriggered` IS sent on activation.
- [ ] Visible trap: `BlockerAdded` sent; client shows visual indicator.
- [ ] Trap applies all configured `effect_ids` to triggering entity via spell system.
- [ ] `freeze_s > 0`: root effect applied to triggering entity for correct duration.
- [ ] Single-trigger trap: `triggered = 1` after first fire; subsequent entity entries ignored.

### NavMesh Sync
- [ ] WALL/GATE blocker creation adds NavMesh TileCache cylinder obstacle.
- [ ] WALL/GATE blocker removal removes the TileCache obstacle.
- [ ] NPC re-paths around newly placed wall within 1 tick.

### Client
- [ ] `BlockerAdded` received: client spawns correct mesh/VFX from `skill_id` → SkillDef lookup.
- [ ] `BlockerRemoved` received: client plays destroy VFX, removes object.
- [ ] Client-side `ctx.blockers.is_blocked()` prediction prevents sending invalid `PlayerMoveTo`.
- [ ] Server correction (entity snapped to stopped position) applied smoothly on client.

### Tests
- [ ] All tests pass. Coverage ≥ 95%.
