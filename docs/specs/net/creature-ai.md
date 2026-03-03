# Creature & Boss AI Specification

**Status**: Draft
**Phase**: 4
**Scope**: Server-authoritative behavior system for NPCs and bosses — visual flow editor in VSCode, data-driven behaviors served from Content API, and ability telegraphing so players can read and dodge boss attacks.

---

## 1. Design Goals (from production experience)

In previous MMORPG versions, creature AI had several recurring pain points:

1. **Behavior logic was hardcoded C++** — adding a new boss ability required a server recompile and deploy.
2. **No telegraphing system** — AoE attacks hit without warning; players either memorized patterns or got hit randomly.
3. **Boss scripting lived in separate files** from the creature definition, creating sync bugs after updates.
4. **All movement was "rush the target"** — patrol, phased behavior, and retreat patterns required significant custom code.

Goals for this system:

- **Server-authoritative**: All movement, state transitions, and attack decisions run on the zone server. The client only receives RPCs with results + visual cues.
- **Data-driven behavior trees**: Creatures are defined in JSON served from the Content API. New behaviors take effect without a server restart.
- **Visual flow editor** in VSCode extension: designers see and edit behavior trees graphically, no C++ required.
- **Telegraphing built-in**: Every server-executed ability broadcasts a "telegraph RPC" N seconds before the hit, with shape, position, and timer — clients show ground markers, directional arrows, and AoE circles.
- **Simple common cases**: Patrol, idle, aggro-chase, auto-attack, ranged attack, heal, flee — all zero-code via built-in nodes.

---

## 2. Behavior Tree Architecture

### 2.1 Node Types

Behavior trees use the standard **Composite / Decorator / Leaf** model:

```
Composites (control flow):
  Sequence      — run children left→right; stop on first FAILURE
  Selector      — run children left→right; stop on first SUCCESS
  Parallel      — run all children; stop policy: ALL_SUCCESS or ANY_SUCCESS
  RandomSelector — shuffle children each evaluation, then Selector logic

Decorators (single-child wrappers):
  Inverter      — flip SUCCESS/FAILURE
  Repeat(n)     — repeat child N times (0 = infinite)
  Cooldown(t)   — return FAILURE if child ran within t seconds
  Probability(p)— run child with probability p (0.0–1.0), else FAILURE
  Until(cond)   — repeat child until condition is true

Leaves (actions and conditions):
  [See §2.2 Built-in Actions]
  [See §2.3 Built-in Conditions]
```

### 2.2 Built-in Action Nodes

| Node | Parameters | Description |
|---|---|---|
| `Idle` | `duration_s` | Stand still for duration, then SUCCESS |
| `PatrolPath` | `path_id`, `loop` | Follow waypoint path from scene CNavPath component |
| `PatrolRandom` | `radius_m` | Walk to random navmesh point within radius, wait, repeat |
| `ChaseTarget` | `max_range_m`, `speed_factor` | Move toward aggro target using Detour path; SUCCESS if in range |
| `Flee` | `direction`, `distance_m` | Run away from aggro target (fear-like, but intentional) |
| `ReturnHome` | — | Path back to spawn point; SUCCESS on arrival |
| `LookAt` | — | Face aggro target without moving |
| `PlayAnim` | `anim_id` | Trigger animation on client via `EntityAnimNotify` RPC |
| `Wait` | `min_s`, `max_s` | Wait random time in range, then SUCCESS |
| `Say` | `text_key`, `radius_m` | Broadcast localized chat message to players in radius |
| `ActivateSkill` | `skill_id`, `target` | Execute skill from skill system (see §4) |
| `SummonCreature` | `creature_id`, `count`, `radius_m` | Spawn creatures at positions around self |
| `DespawnSelf` | `delay_s` | Schedule own despawn |
| `SetBlackboard` | `key`, `value` | Write value to behavior blackboard |
| `SetPhase` | `phase_id` | Switch to named behavior phase (see §3.3 Boss Phases) |

### 2.3 Built-in Condition Nodes

| Node | Parameters | Description |
|---|---|---|
| `HasTarget` | — | Is there an entity in aggro list? |
| `TargetInRange` | `range_m` | Is aggro target within range? |
| `TargetOutsideRange` | `range_m` | Is aggro target beyond range? |
| `HealthBelow` | `pct` | Self HP < pct% |
| `HealthAbove` | `pct` | Self HP > pct% |
| `TargetHealthBelow` | `pct` | Target HP < pct% |
| `PlayerCountInArea` | `radius_m`, `min`, `max` | Is player count in area between min and max? |
| `OnCooldown` | `skill_id` | Is skill currently on cooldown? |
| `BlackboardEqual` | `key`, `value` | Blackboard[key] == value? |
| `PhaseIs` | `phase_id` | Current behavior phase matches? |
| `TimeInPhase` | `min_s` | Has been in current phase for at least min_s? |
| `IsDay` | — | Is it currently daytime in the world? |
| `RandomChance` | `pct` | Roll RNG < pct% |

---

## 3. Creature Definition (JSON)

### 3.1 CreatureDef Schema

Served from the Content API: `GET /v1/creatures/{creature_id}`

```json
{
  "creature_id": "field_spider_elite",
  "display_name": "Elite Field Spider",
  "mesh": "creatures/spider/spider_elite.uzfbx",
  "anim_set": "creatures/spider/spider_anim.uzanim",
  "base_stats": {
    "max_hp": 4200,
    "move_speed": 5.5,
    "attack_speed": 1.2,
    "level": 15
  },
  "aggro_range_m": 12.0,
  "leash_range_m": 50.0,
  "faction": "MONSTER",
  "loot_table": "lt_spider_elite",
  "respawn_time_s": 120,
  "behavior": {
    "phases": [
      {
        "phase_id": "default",
        "enter_condition": null,
        "tree": {
          "type": "Selector",
          "children": [
            {
              "type": "Sequence",
              "name": "Combat",
              "children": [
                { "type": "HasTarget" },
                {
                  "type": "Selector",
                  "children": [
                    {
                      "type": "Sequence",
                      "name": "Range attack",
                      "children": [
                        { "type": "TargetInRange", "range_m": 12.0 },
                        { "type": "TargetOutsideRange", "range_m": 3.0 },
                        { "type": "Cooldown", "t": 6.0,
                          "child": { "type": "ActivateSkill", "skill_id": "spider_web_shot", "target": "AGGRO_TARGET" }
                        }
                      ]
                    },
                    {
                      "type": "Sequence",
                      "name": "Melee attack",
                      "children": [
                        { "type": "TargetInRange", "range_m": 2.5 },
                        { "type": "ActivateSkill", "skill_id": "spider_bite", "target": "AGGRO_TARGET" }
                      ]
                    },
                    { "type": "ChaseTarget", "max_range_m": 50.0, "speed_factor": 1.0 }
                  ]
                }
              ]
            },
            {
              "type": "Sequence",
              "name": "Patrol",
              "children": [
                { "type": "PatrolRandom", "radius_m": 20.0 },
                { "type": "Wait", "min_s": 2.0, "max_s": 5.0 }
              ]
            }
          ]
        }
      }
    ]
  }
}
```

### 3.2 Blackboard

Each creature instance has a per-instance **Blackboard** — a string-keyed, dynamically-typed key-value store:

```c
typedef struct {
    char     key[32];
    uint8_t  type;     // BB_INT, BB_FLOAT, BB_BOOL, BB_ENTITY_ID
    union {
        int32_t  i;
        float    f;
        bool     b;
        uint32_t entity_id;
    } value;
} uz_bb_entry_t;

// Per-creature blackboard (max 32 entries)
typedef struct {
    uz_bb_entry_t entries[32];
    uint8_t       count;
} uz_blackboard_t;
```

Used for inter-node communication within the same tree tick:
- Phase 1 enrage sets `blackboard["enraged"] = true`
- Child nodes can read it via `BlackboardEqual { key: "enraged", value: true }`

### 3.3 Boss Phases

Bosses use multiple `phases` in the behavior array. Phase transitions are driven by `enter_condition` — evaluated at tree root:

```json
{
  "phases": [
    {
      "phase_id": "phase_1",
      "enter_condition": null,
      "tree": { ... }
    },
    {
      "phase_id": "phase_2",
      "enter_condition": { "type": "HealthBelow", "pct": 60 },
      "tree": { ... }
    },
    {
      "phase_id": "phase_3",
      "enter_condition": { "type": "HealthBelow", "pct": 30 },
      "tree": { ... }
    }
  ]
}
```

Phase evaluation order: reverse (highest index = highest priority). The AI tick checks phase conditions top-down (phase_3 → phase_2 → phase_1) and activates the first matching phase. `SetPhase` action node overrides this for scripted transitions.

Phase transitions broadcast `EntityPhaseChanged` RPC, allowing the client to trigger cinematics, music changes, or visual effects.

---

## 4. Boss Telegraphing System

### 4.1 The Problem

Without telegraphing, AoE boss attacks are invisible until they fire. Players learn patterns only by dying repeatedly — acceptable in single-player roguelikes, poor UX in MMORPGs where death has real costs.

Reference games handle this differently:
- **Path of Exile**: Ground indicator circles, beams, and targeting lines appear 0.5–2 s before the hit.
- **Diablo 4**: Red AoE zones appear before ground-slam abilities.
- **Albion Online**: Circular/cone indicators appear during cast animations.
- **Classic WoW**: Boss warns via yell + cast bar; no visual ground marker.

UzEngine uses **explicit server-broadcast telegraph RPCs** — the server is always the authority on where the ability will land. Client trusts server position; no client-side prediction of boss abilities needed.

### 4.2 Telegraph Shapes

| Shape | Parameters | Use case |
|---|---|---|
| `CIRCLE` | `center`, `radius_m` | Ground AoE, shockwave |
| `RING` | `center`, `inner_r`, `outer_r` | Annular AoE (safe zone in center) |
| `CONE` | `origin`, `direction`, `angle_deg`, `length_m` | Breath weapon, frontal cleave |
| `LINE` | `start`, `end`, `width_m` | Laser beam, ground rupture |
| `RECTANGLE` | `center`, `width_m`, `height_m`, `yaw` | Stomp, slam |
| `POINT_SPREAD` | `center`, `count`, `spread_r` | Multiple targeted circles (spread mechanic) |
| `ARROW` | `origin`, `direction`, `length_m` | Directional charge, thrust |
| `DONUT_SECTOR` | `center`, `radius_m`, `angle_deg`, `yaw` | Partial ring AoE |

### 4.3 Telegraph RPC

```c
// Server → all clients in AoI of the creature
UZ_RPC_S2C(AbilityTelegraph,
    (UZ_U32,    caster_id)
    (UZ_U32,    telegraph_id)   // unique ID for cancellation
    (UZ_U8,     shape)          // TelegraphShape enum
    (UZ_WPOS,   anchor_pos)     // world position of the shape center/origin
    (UZ_F32Q_8, param_a)        // shape-specific: radius, length, width, etc.
    (UZ_F32Q_8, param_b)        // shape-specific: height, angle, inner_r, etc.
    (UZ_F32Q_16, yaw)           // orientation
    (UZ_U16,    duration_ms)    // how long the indicator shows before the hit
    (UZ_U8,     color_hint)     // DANGER=0, WARNING=1, INFO=2 — client colors
    (UZ_U8,     flags)          // DODGE_POSSIBLE=1, LETHAL=2, PERSISTENT=4
)

// Cancel a telegraph before it fires (e.g., boss interrupted)
UZ_RPC_S2C(AbilityTelegraphCancel,
    (UZ_U32, caster_id)
    (UZ_U32, telegraph_id)
)
```

### 4.4 Telegraph Lifecycle

```
Server tick:
  1. Boss AI decides to cast "Ground Slam" (cast_time_s = 2.0, telegraph_lead_s = 1.5)
  2. Server broadcasts AbilityTelegraph { duration_ms=1500, shape=CIRCLE, ... }
  3. Client renders ground AoE circle + countdown timer bar
  4. Server waits cast_time_s (2.0 s) — movement and interrupt rules apply (same as player spells)
  5. At t=2.0 s: ability fires → SkillExecuted RPC
  6. Client removes telegraph indicator on SkillExecuted or AbilityTelegraphCancel
```

`telegraph_lead_s` is configured per skill in SkillDef:

```json
{
  "skill_id": "ground_slam",
  "target_type": "GROUND_POINT",
  "cast_time_s": 2.0,
  "telegraph_lead_s": 1.5,
  "telegraph_shape": "CIRCLE",
  "telegraph_param_a": 5.0,
  "telegraph_param_b": 0.0,
  "flags": ["DODGE_POSSIBLE", "LETHAL"]
}
```

`telegraph_lead_s` < `cast_time_s`: indicator appears `cast_time_s - telegraph_lead_s` seconds after cast starts (delayed reveal, for harder patterns).

`telegraph_lead_s` == `cast_time_s`: indicator appears immediately when cast starts (standard).

`telegraph_lead_s` > `cast_time_s`: invalid, clamped to `cast_time_s`.

### 4.5 Dynamic Telegraph Tracking

For abilities that track the player during cast (delayed homing AoE):

```c
// Update telegraph position mid-cast (max 10 Hz)
UZ_RPC_S2C(AbilityTelegraphMove,
    (UZ_U32,  caster_id)
    (UZ_U32,  telegraph_id)
    (UZ_WPOS, new_anchor_pos)
    (UZ_F32Q_16, new_yaw)
)
```

The server stops sending `AbilityTelegraphMove` when the cast locks in (typically `cast_time_s * 0.75`). Client lerps the indicator smoothly.

### 4.6 Multi-Phase Boss Example

```json
{
  "skill_id": "lava_burst",
  "target_type": "GROUND_POINT",
  "cast_time_s": 3.0,
  "telegraph_lead_s": 2.5,
  "telegraph_shape": "CIRCLE",
  "telegraph_param_a": 6.0,
  "telegraph_flags": ["DODGE_POSSIBLE", "LETHAL"],
  "hit_shape": { "type": "CYLINDER", "radius": 6.0, "height": 8.0 }
}
```

```json
{
  "skill_id": "flame_breath",
  "target_type": "CAMERA_FORWARD",
  "cast_time_s": 2.0,
  "telegraph_lead_s": 2.0,
  "telegraph_shape": "CONE",
  "telegraph_param_a": 45.0,
  "telegraph_param_b": 12.0,
  "telegraph_flags": ["DODGE_POSSIBLE"],
  "hit_shape": { "type": "CONE", "angle_deg": 45.0, "length_m": 12.0 }
}
```

---

## 5. Aggro System

### 5.1 Threat Table

Each creature maintains a per-target threat table:

```c
typedef struct {
    uint32_t entity_id;
    float    threat;
    uint64_t last_damage_ms;
} uz_threat_entry_t;

typedef struct {
    uz_threat_entry_t entries[64];  // max simultaneous attackers
    uint8_t           count;
    uint32_t          top_threat_id;
} uz_threat_table_t;
```

Threat generation rules (configurable per creature in CreatureDef):

| Event | Threat Added |
|---|---|
| Physical damage | damage × 1.0 |
| Spell damage | damage × 1.1 |
| Healing received by target | heal × 0.5 |
| Taunt CC effect | set threat to top_threat + 10% |
| Fixed threat skill | `fixed_threat` field in EffectDef |

### 5.2 Aggro Range and Leash

```c
// Aggro acquisition
if (distance(creature, player) < aggro_range_m && creature.state == IDLE) {
    uz_threat_add(creature, player, INITIAL_AGGRO_THREAT);
}

// Leash — creature resets if pulled too far from spawn
if (distance(creature, spawn_pos) > leash_range_m) {
    uz_creature_reset(creature);  // full HP, evade state, return home
}
```

During leash reset, the creature enters an `EVADE` state — immune to damage, all path to spawn, reset HP on arrival.

### 5.3 Assist Radius

When a creature is attacked, nearby creatures of the same faction within `assist_radius_m` (default 10 m) are alerted and add the attacker to their own threat tables. Configurable per creature:

```json
"assist_radius_m": 15.0,
"assist_faction": ["SPIDER", "SPIDER_ELITE"]
```

---

## 6. VSCode Behavior Tree Editor

### 6.1 Overview

The VSCode extension provides a graphical flow editor for creature behavior trees, using a canvas-based node graph (similar to Unreal's Blueprint/Behavior Tree editor but simpler and web-native).

```
[VSCode Extension — Creature AI Panel]
┌─────────────────────────────────────────────────────────────┐
│ field_spider_elite  │ Phase: default ▼          [Save] [Test]│
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐                                           │
│  │  Selector    │                                           │
│  └──────┬───────┘                                           │
│         │                                                   │
│   ┌─────┴──────┐                                            │
│   │  Sequence  │  (Combat)                                  │
│   └─────┬──────┘                                            │
│         │                                                   │
│   ┌─────┴──────────────────────┐                            │
│   │ HasTarget │ ActivateSkill  │                            │
│   │           │ spider_bite    │                            │
│   └───────────┴────────────────┘                            │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 IPC Protocol for Editor ↔ Server

The VSCode extension sends behavior definitions to the game server via the Content API. For live-testing in Editor Play Mode:

```
// Push updated creature behavior to the running server (Editor Play Mode only)
POST /v1/creatures/{creature_id}/behavior
Content-Type: application/msgpack
Body: { behavior: { phases: [...] } }

Response: 200 OK { hot_reloaded: true, creature_count: 3 }
// All live instances of creature_id adopt the new behavior tree on next tick
```

The server validates the behavior JSON on receipt (unknown node types → reject with error details). Invalid trees do not overwrite the running behavior.

### 6.3 Node Property Panel

Clicking a node in the editor opens a properties panel:

```
Selected: ActivateSkill
─────────────────────────────
skill_id:   [spider_web_shot ▼]   ← dropdown from Content API skill list
target:     [AGGRO_TARGET    ▼]   ← SELF / AGGRO_TARGET / GROUND_AT_TARGET
on_fail:    [CONTINUE        ▼]   ← CONTINUE / ABORT_BRANCH
─────────────────────────────
[Delete Node]  [Duplicate]
```

### 6.4 Phase Tab Navigation

The editor shows one behavior tree per phase. A phase tab strip allows switching and adding phases:

```
[Phase 1 (default)] [Phase 2 (<60%HP)] [Phase 3 (<30%HP)] [+ Add Phase]
```

Adding a phase opens a dialog to set `enter_condition`.

---

## 7. Patrol System

### 7.1 CNavPath Component

Designers place a **`CNavPath`** scene component — a polyline of waypoints — to define patrol routes:

```
CNavPath
  path_id:   "patrol_a"
  waypoints: [ vec3, vec3, vec3, ... ]
  loop:      true
  loop_mode: RESTART | PING_PONG
```

Waypoints are placed visually in the scene editor (click to add point, drag to move). The path is stored in the `.uzscene` file and available to the navmesh query layer at runtime.

### 7.2 Patrol Behavior at Runtime

```c
// PatrolPath node tick (server)
void patrol_tick(uz_creature_t* c, uz_nav_path_node_t* node) {
    if (reached_waypoint(c)) {
        node->waypoint_idx = (node->waypoint_idx + 1) % path->count;
    }
    uz_wpos_t target = path->waypoints[node->waypoint_idx];
    uz_navmesh_find_path(q, c->pos, target, &filter, &c->current_path);
    uz_move_along_path(c);
}
```

The creature moves at `move_speed × patrol_speed_factor` (default 0.6 — slower patrol gait).

### 7.3 Return-to-Patrol After Combat

When combat ends (threat table clears), the creature returns to its patrol path. The `ReturnHome` node navigates back to the nearest patrol waypoint, then `PatrolPath` resumes from that waypoint.

---

## 8. Server Integration

### 8.1 Creature AI Tick

AI runs on the zone worker thread alongside physics and game logic:

```c
void zone_tick(uz_zone_t* zone, float dt) {
    // ... physics, player movement ...

    // Creature AI batch tick
    uz_ai_tick_all(zone->ai_world, dt);

    // ... broadcast updates ...
}

void uz_ai_tick_all(uz_ai_world_t* w, float dt) {
    // AI ticks at 10 Hz (every 100 ms), not every physics frame
    w->ai_accum += dt;
    if (w->ai_accum < AI_TICK_INTERVAL_S) return;
    w->ai_accum -= AI_TICK_INTERVAL_S;

    for (int i = 0; i < w->creature_count; i++) {
        uz_bt_tick(&w->creatures[i]->bt, w->creatures[i], w);
    }
}
```

AI ticks at **10 Hz** by default (configurable per creature — 20 Hz for elite/boss). Physics runs at fixed 50 Hz. AI piggybacks on the zone tick without a separate thread.

### 8.2 Creature Position Replication

Creature positions are replicated with the same `EntityMove` RPC used for NPCs in the navmesh spec:

```c
UZ_RPC_S2C(EntityMove,
    (UZ_U32,    entity_id)
    (UZ_WPOS,   target_pos)
    (UZ_F32Q_8, speed)
    (UZ_BOOL,   is_moving)
    (UZ_F32Q_16, yaw)
)
```

Broadcast rate follows AoI Interest LOD rings (20/10/5 Hz by distance).

### 8.3 Phase Change RPC

```c
UZ_RPC_S2C(EntityPhaseChanged,
    (UZ_U32, entity_id)
    (UZ_U8,  phase_idx)
    (UZ_STR8, phase_id)   // for client-side VFX trigger mapping
)
```

Client can map `phase_id` → camera shake, particle burst, UI cinematic bar, etc. via the Lua `on_phase_changed(entity, phase_id)` hook.

---

## 9. File Layout

```
src/engine/net/ai/
├── uz_ai_world.h/.c            — AI world: creature registry, tick dispatcher
├── uz_behavior_tree.h/.c       — BT runtime: tick, node dispatch, result propagation
├── uz_bt_nodes_action.h/.c     — Built-in action nodes (Idle, Patrol, ChaseTarget, ...)
├── uz_bt_nodes_condition.h/.c  — Built-in condition nodes (HasTarget, HealthBelow, ...)
├── uz_bt_nodes_composite.h/.c  — Composites (Sequence, Selector, Parallel, ...)
├── uz_bt_loader.h/.c           — JSON → BT deserialization (uses cJSON)
├── uz_blackboard.h/.c          — Per-creature blackboard key-value store
├── uz_aggro.h/.c               — Threat table, aggro acquisition, leash
├── uz_patrol.h/.c              — CNavPath loading, patrol movement
└── uz_telegraph.h/.c           — AbilityTelegraph broadcast, lifecycle management

src/engine/scene/components/
└── CNavPath.h/.c               — Designer-placed patrol path component

src/engine/net/server/
└── zone_ai.h/.c                — Zone-level AI tick integration

tests/net/ai/
├── test_bt_sequence.c          — Sequence/Selector flow, child result propagation
├── test_bt_conditions.c        — Condition nodes, blackboard read/write
├── test_bt_patrol.c            — PatrolPath following, return-to-patrol
├── test_bt_combat.c            — ChaseTarget, ActivateSkill, cooldown decorator
├── test_bt_phases.c            — Phase transitions, enter_condition evaluation
├── test_bt_loader.c            — JSON → BT parse, invalid tree rejection
├── test_aggro.c                — Threat generation, taunt, leash reset, evade
├── test_telegraph.c            — AbilityTelegraph broadcast timing, cancel
└── test_telegraph_tracking.c  — AbilityTelegraphMove rate, lock-in timing
```

---

## 10. Dependencies

| Library | Version | Purpose |
|---|---|---|
| cJSON | HEAD | JSON behavior tree deserialization |
| recastnavigation | HEAD | Pathfinding for creature movement (shared with navmesh-shared spec) |

Both managed via vcpkg.

---

## 11. Definition of Done

### Core Behavior Tree
- [ ] Behavior tree runtime ticks creatures at configurable Hz (default 10 Hz) on zone worker thread.
- [ ] Sequence, Selector, Parallel, RandomSelector composites behave correctly per standard BT semantics.
- [ ] All Cooldown, Probability, Inverter, Repeat, Until decorators work and compose correctly.
- [ ] All built-in action nodes (Idle, PatrolPath, PatrolRandom, ChaseTarget, Flee, ReturnHome, LookAt, ActivateSkill, SummonCreature, Say, Wait) implemented.
- [ ] All built-in condition nodes (HasTarget, TargetInRange, HealthBelow, OnCooldown, PhaseIs, RandomChance, etc.) implemented.
- [ ] JSON behavior tree deserialized at startup and on hot-reload; invalid nodes logged and rejected without crash.
- [ ] Blackboard read/write between nodes within same tick works correctly.

### Boss Phases
- [ ] Multiple phases defined per creature; phase transitions triggered by `enter_condition` on each tick.
- [ ] `SetPhase` action overrides automatic phase selection.
- [ ] `EntityPhaseChanged` RPC broadcast to clients in AoI on transition.
- [ ] Lua `on_phase_changed(entity, phase_id)` hook fires on client.

### Telegraphing
- [ ] `AbilityTelegraph` RPC broadcast to all clients in AoI `telegraph_lead_s` seconds into cast.
- [ ] All 8 telegraph shapes (CIRCLE, RING, CONE, LINE, RECTANGLE, POINT_SPREAD, ARROW, DONUT_SECTOR) broadcast correctly with shape-specific parameters.
- [ ] `AbilityTelegraphCancel` broadcast when ability is interrupted.
- [ ] `AbilityTelegraphMove` broadcast at max 10 Hz for tracking abilities; locks in at `cast_time_s × 0.75`.
- [ ] `telegraph_lead_s` > `cast_time_s` clamped silently.
- [ ] Client renders CIRCLE and CONE indicators correctly using AoE circle mesh and decal system (editor shows wireframe).

### Aggro
- [ ] Threat generated correctly for damage, healing, and taunt.
- [ ] Creature targets entity with highest threat.
- [ ] Leash: creature resets when pulled beyond `leash_range_m`; enters EVADE, returns to spawn, restores full HP.
- [ ] Assist: nearby same-faction creatures within `assist_radius_m` add attacker to threat table.

### Patrol
- [ ] `CNavPath` component placed in editor; waypoints visible as lines in scene viewport.
- [ ] `PatrolPath` node follows waypoints using Detour; `RESTART` and `PING_PONG` loop modes correct.
- [ ] Creature returns to nearest patrol waypoint after combat via `ReturnHome`.
- [ ] Patrol speed = `move_speed × 0.6` by default, configurable.

### Content API / Hot-Reload
- [ ] `GET /v1/creatures/{id}` returns full CreatureDef with behavior JSON.
- [ ] `POST /v1/creatures/{id}/behavior` hot-reloads behavior on all live instances within 1 tick.
- [ ] Invalid behavior JSON rejected with structured error response; running behavior unchanged.

### VSCode Editor
- [ ] Node graph renders behavior tree from CreatureDef JSON.
- [ ] Nodes can be added, connected, deleted, and property-edited via Inspector panel.
- [ ] Phase tabs allow switching phases and adding new phases with `enter_condition`.
- [ ] Save pushes updated behavior to Content API.

### Tests
- [ ] All tests pass. Coverage ≥ 95%.
