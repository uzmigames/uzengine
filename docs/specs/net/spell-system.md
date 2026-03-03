# Spell & Ability System Specification

**Status**: Draft
**Phase**: 4
**Scope**: Data-driven, server-authoritative spell/ability system for MMORPG. Hybrid targeting (ArcheAge-style). Zero client rebuild for new spells.

---

## 1. Problem Statement (from production experience)

Implementing spells in a traditional MMORPG involves **both** client and server work coordinated and shipped together:

- Client: visual effect, animation, targeting preview, ground indicator
- Server: damage formula, cooldown, resource cost, hit validation, AoE query

Every new spell means a build, QA, and a Steam patch. With hundreds of spells in a typical MMORPG this is the main velocity bottleneck for content teams.

The second problem: **implementing hybrid targeting in UE Blueprint is painful**. Mixing tab-target entity locking, ground-targeted placement, camera-directional skills, and portals each requires its own replication pattern, its own authority handoff, and its own preview system. Without a clean abstraction, each new targeting mode explodes in Blueprint graph complexity.

### Why Not Use UE's Gameplay Ability System (GAS)

GAS was designed for single-server games (Fortnite, ARPGs):
- Gameplay Cues are `UObject` assets — adding a new cue requires a Blueprint/C++ asset and a rebuild.
- `FGameplayTag` registry is compiled — new tags require rebuild.
- Ability state on `UAbilitySystemComponent` is tightly coupled to UE's replication model.
- Not designed for 5 000 concurrent activations per tick.

UzEngine's system takes GAS concepts (Ability, Effect, Cue, Tag) and makes them fully data-driven.

---

## 2. Targeting Model — Hybrid (ArcheAge Style)

The system supports **seven targeting modes**. A `SkillDef` specifies exactly one. The client and server both read the same `target_type` from the Content API — no hardcoded targeting logic in either binary.

| `target_type` | Description | Examples |
|---|---|---|
| `SELF` | Caster is the target. No selection required. | Self-buff, dash, stance |
| `TARGET_ENTITY` | Requires a locked entity target. | Single-target heal, curse, debuff, hard CC |
| `TARGET_ENTITY_CHAIN` | Requires locked target with prerequisite effect. | Combo finishers, execute skills |
| `GROUND_POINT` | Player places an indicator on the ground. | AoE nuke, trap, portal exit |
| `GROUND_CIRCLE` | Like GROUND_POINT but shows a circle preview. | Healing circle, AoE CC |
| `CAMERA_FORWARD` | Fires in the direction the camera faces. | Dash-attack, cone spray, charge |
| `CHAIN_TARGET` | Bounces between N closest enemies to original target. | Chain lightning, multi-heal |

### 2.1 Hard vs Soft Lock

Two locking modes are configurable per connection (player preference):

- **HARD_LOCK** (default): clicking an entity or pressing Tab latches onto it. The character auto-faces the target when skills fire. All `TARGET_ENTITY` skills are aimed at the locked target without additional input.
- **SOFT_LOCK**: a reticle in the camera center soft-locks the nearest entity it hovers. Lock is held only while the reticle stays on target. Useful for action-oriented players.

Both modes use the same server-side validation — only the client-side targeting UX differs.

### 2.2 Movement During Skills

ArcheAge's defining combat feel: **players move freely during most skill casts**. This is a deliberate design choice. Movement is **not interrupted** by casting unless the skill explicitly sets `interrupt_on_move: true`.

Interruption sources (configured per skill):
- `interrupt_on_move`: cast cancels if the player moves.
- `interrupt_on_damage_chance`: 0.0–1.0 base chance to interrupt when hit.
- `interrupt_on_damage_delay_s`: cast time added per hit (ArcheAge model, default 0.5 s for first two hits).

The server tracks cast progress and interrupt events independently. Movement and casting share the same tick without blocking each other.

---

## 3. Data Model

All definitions served from Content API, cached in memory (see `live-content.md`).

### 3.1 SkillDef

```json
{
  "id": 501,
  "version": 3,
  "name": "Fireball",
  "icon_url": "icons/skills/501.webp",
  "description": "Launches a fireball that explodes on impact.",
  "type": "active",

  "target_type": "GROUND_POINT",
  "range": 25.0,
  "indicator_radius": 4.0,
  "snap_to_navmesh": true,

  "cast_time_s": 1.5,
  "interrupt_on_move": false,
  "interrupt_on_damage_chance": 0.35,
  "interrupt_on_damage_delay_s": 0.5,
  "interrupt_on_damage_max_delays": 2,

  "cooldown_s": 8.0,
  "global_cooldown_s": 1.0,
  "cost": { "resource": "mana", "amount": 40 },

  "require_los": true,
  "can_cast_while_moving": true,
  "can_cast_while_in_air": false,

  "built_in_handler": null,
  "script": "scripts/skills/fireball.lua",

  "hit": {
    "shape": "SPHERE",
    "radius": 4.0,
    "max_targets": 8,
    "friendly_fire": false,
    "damage": {
      "base_min": 80, "base_max": 120,
      "scale_stat": "int", "scale_factor": 1.5,
      "element": "fire"
    },
    "effects_on_hit": [601]
  },

  "cue": {
    "cast_vfx":   "vfx/cast_fire_charge",
    "cast_sfx":   "sfx/spells/fireball_cast",
    "cast_anim":  "anim/cast_overhand",
    "proj_mesh":  "mesh/projectile_fireball",
    "proj_speed": 18.0,
    "impact_vfx": "vfx/explosion_fire_small",
    "impact_sfx": "sfx/spells/fireball_impact",
    "indicator":  "vfx/indicator_fire_circle"
  },

  "tags": ["fire", "projectile", "aoe", "interruptible"],
  "combo_requires_effect": null
}
```

#### Per `target_type` — required SkillDef fields

| target_type | Required extra fields |
|---|---|
| `SELF` | — |
| `TARGET_ENTITY` | `range` |
| `TARGET_ENTITY_CHAIN` | `range`, `combo_requires_effect` (effect ID or null for unconditional) |
| `GROUND_POINT` | `range`, `snap_to_navmesh` |
| `GROUND_CIRCLE` | `range`, `indicator_radius`, `snap_to_navmesh` |
| `CAMERA_FORWARD` | `range`, `hit.shape` = CONE or CYLINDER, `hit.cone_angle_deg` |
| `CHAIN_TARGET` | `range`, `hit.chain_count`, `hit.chain_radius` |

### 3.2 EffectDef

```json
{
  "id": 601,
  "version": 1,
  "name": "Burning",
  "type": "dot",
  "duration_s": 5.0,
  "tick_interval_s": 1.0,
  "stack_max": 3,
  "stack_mode": "refresh",
  "modifiers": [
    { "stat": "hp", "op": "add_flat", "value": -12.0, "per_tick": true }
  ],
  "icon_url": "icons/effects/601.webp",
  "vfx": "vfx/effect_burning_char",
  "tags": ["fire", "dot", "dispellable"],
  "script": null
}
```

#### Effect `type` enumeration — complete list

| `type` | Description | Blocks player control? |
|---|---|---|
| `instant` | Applied once, no duration. Stat change, heal burst, instant damage. | No |
| `dot` | Damage over time. Ticks every `tick_interval_s`. | No |
| `hot` | Heal over time. Same structure as dot. | No |
| `buff` | Stat modifier with duration. Positive. | No |
| `debuff` | Stat modifier with duration. Negative. | No |
| `stun` | Target cannot move, cast, or use skills for `duration_s`. | Full |
| `root` | Target cannot move. Can still cast and use non-movement skills. | Movement only |
| `silence` | Target cannot cast spells or use skills. Can still move. | Skills only |
| `sleep` | Target cannot move or act. Broken by any damage received. | Full |
| `fear` | Target moves in a random or server-computed flee direction. Cannot act. See §3.5. | Full + forced move |
| `polymorph` | Target transformed into a creature. Cannot act. Visual and stat override. See §3.6. | Full + visual replace |
| `slow` | Reduces movement speed by `cc.slow_factor` (0.0–1.0). Player retains full control. | Partial (speed) |
| `knockback` | One-time physics impulse. No duration. See §3.7. | Transient |
| `knockup` | One-time upward impulse. No duration. | Transient |
| `snare` | Synonym for root. Interchangeable tag. | Movement only |
| `taunt` | Forces target to attack the caster for `duration_s`. PvE only by default. | Target selection |
| `charm` | Target fights for the caster's faction for `duration_s`. | Full |

### 3.3 Crowd Control (CC) Block

All CC effect types carry a `cc` sub-object in EffectDef. Fields not relevant to the type are ignored.

```json
{
  "id": 700,
  "name": "Fear",
  "type": "fear",
  "duration_s": 3.0,
  "stack_max": 1,
  "stack_mode": "replace",
  "cc": {
    "fear_mode": "random_walk",   // random_walk | flee_from_caster | fixed_dir
    "fear_speed_factor": 0.8,     // fraction of base move speed while feared
    "break_on_damage": false,     // if true, any damage removes the effect
    "break_on_damage_threshold": 0.0  // min damage to trigger break (0 = any)
  },
  "modifiers": [],
  "icon_url": "icons/effects/700.webp",
  "vfx": "vfx/effect_fear_spiral",
  "vfx_on_entity": true,         // play vfx attached to the entity (not ground)
  "tags": ["cc", "fear", "magic", "dispellable"],
  "script": null
}
```

```json
{
  "id": 710,
  "name": "Polymorph",
  "type": "polymorph",
  "duration_s": 8.0,
  "stack_max": 1,
  "stack_mode": "replace",
  "cc": {
    "break_on_damage": true,
    "break_on_damage_threshold": 0.0
  },
  "polymorph": {
    "mesh_override":   "mesh/creatures/sheep",
    "anim_set":        "anim/creatures/sheep",
    "vfx_transform":  "vfx/polymorph_transform_in",
    "vfx_revert":     "vfx/polymorph_transform_out",
    "stat_override": {
      "move_speed": 2.5,
      "atk": 0, "def": 0,
      "hp_max_factor": 1.0
    },
    "suppress_skills": true,      // all skills blocked
    "suppress_items":  true       // no item use
  },
  "modifiers": [],
  "icon_url": "icons/effects/710.webp",
  "vfx": "vfx/effect_polymorph_aura",
  "tags": ["cc", "polymorph", "magic", "dispellable"],
  "script": null
}
```

```json
{
  "id": 720,
  "name": "Stun",
  "type": "stun",
  "duration_s": 2.5,
  "stack_max": 1,
  "stack_mode": "replace",
  "cc": {
    "break_on_damage": false,
    "break_on_damage_threshold": 0.0
  },
  "modifiers": [],
  "icon_url": "icons/effects/720.webp",
  "vfx": "vfx/effect_stun_stars",
  "tags": ["cc", "stun", "physical", "dispellable"],
  "script": null
}
```

### 3.3 Hit Shape Definitions

```json
"hit": {
  "shape": "SPHERE",      // SPHERE | CONE | CYLINDER | BOX | CHAIN
  "radius": 4.0,          // SPHERE, CYLINDER
  "height": 3.0,          // CYLINDER
  "cone_angle_deg": 60.0, // CONE
  "cone_length": 8.0,     // CONE
  "box_w": 2.0,           // BOX
  "box_h": 2.0,           // BOX
  "box_l": 8.0,           // BOX
  "chain_count": 3,       // CHAIN — number of bounce targets
  "chain_radius": 6.0,    // CHAIN — search radius for next target
  "max_targets": 8,
  "friendly_fire": false
}
```

### 3.4 Tags — String-Based, No Compiled Registry

Tags are plain strings in hash maps built at startup from SkillDef/EffectDef data. No `FGameplayTag` compiled registry.

Immunity expressed in entity definitions:
```json
{ "immune_tags": ["fire", "stun", "knockback"] }
```

CC resistance can be partial (reduces duration) or full (immune):

```json
{
  "cc_resistance": [
    { "tag": "stun",      "reduce_duration_factor": 0.5 },
    { "tag": "polymorph", "reduce_duration_factor": 0.0 }
  ]
}
```

`reduce_duration_factor: 0.0` = full immunity. `0.5` = half duration. Applied after the immunity tag check.

### 3.5 Fear — Server-Controlled Forced Movement

Fear is the most complex CC type because the server must drive movement for the affected entity — the player cannot provide input while feared.

#### Server behaviour during Fear

```
On EffectApplied (type=fear):
  1. Lock entity: ignore all C2S movement RPCs from this connection
  2. Compute initial flee direction:
       fear_mode = "random_walk"      → random angle, re-randomised every 0.8 s
       fear_mode = "flee_from_caster" → direction away from caster at cast time
       fear_mode = "fixed_dir"        → direction field from cc block
  3. Each zone tick while feared:
       a. Advance entity along flee direction at (base_speed × fear_speed_factor)
       b. Navmesh steering: if next position is off navmesh, steer along edge
          (Detour `findStraightPath` with the flee vector as preferred direction)
       c. If random_walk and time since last direction change > 0.8 s: new random dir
       d. If break_on_damage and damage event received: remove effect immediately
  4. Server sends EntityMove RPCs from fear movement (same path as normal NPC movement)
  5. Client receives EntityMove and moves the player character accordingly
     (client cannot override; input is discarded server-side)
  6. On effect expire or break: unlock C2S movement, send EntityStopCC RPC
```

#### Client behaviour during Fear

```
On EffectApplied (type=fear):
  - Play fear VFX on character
  - Show fear icon on status bar + countdown
  - Disable movement input keys (grey out WASD / click-to-move)
  - Character animation switches to "fear_run" anim state

On EntityMove RPC during fear:
  - Move character toward target_pos at speed (same as NPC interpolation)
  - Camera follows character freely (not locked to fear direction)

On EntityStopCC RPC:
  - Re-enable movement input
  - Play "vfx/effect_fear_end"
  - Return to idle animation state
```

#### Why NavMesh steering matters for Fear

Without navmesh steering, feared players would run into walls and become stuck, which feels broken and can be exploited to CC players into corners where they can be freely attacked. Detour's path-following ensures the feared entity moves along valid walkable terrain, bouncing naturally off obstacles.

### 3.6 Polymorph — Visual and Stat Override

Polymorph transforms the target into a creature for its duration. It requires both server-side stat suppression and client-side visual replacement.

#### Server behaviour during Polymorph

```
On EffectApplied (type=polymorph):
  1. Store entity's current stats snapshot
  2. Apply polymorph.stat_override fields (move_speed, atk, def, etc.)
  3. Set ability_component.skills_suppressed = true  (if suppress_skills)
  4. Set ability_component.items_suppressed  = true  (if suppress_items)
  5. If break_on_damage and damage received: remove effect, restore stats
  6. S2C RPC: EffectApplied { entity_id, effect_id, duration_s }
     + S2C RPC: EntityPolymorphed { entity_id, polymorph_def_id }

On effect expire or break:
  1. Restore stat snapshot
  2. Clear suppression flags
  3. S2C RPC: EntityPolymorphReverted { entity_id }
```

#### Client behaviour during Polymorph

```
On EntityPolymorphed RPC:
  - Replace entity's skeletal mesh with polymorph.mesh_override
  - Replace entity's animation set with polymorph.anim_set
  - Play polymorph.vfx_transform at entity position
  - Show polymorph icon on status bar + countdown
  - Suppress hotbar UI (skills greyed out)

On EntityPolymorphReverted RPC:
  - Restore original mesh and animation set
  - Play polymorph.vfx_revert
  - Re-enable hotbar UI
```

The `mesh_override` and `anim_set` are keys into the client's asset pak. If the asset is missing, the client shows the entity as invisible-but-still-collidable (flagged with a floating "?" placeholder) rather than crashing. This allows adding new polymorph forms in a content update before the client pak is updated — rare but safe.

#### Polymorph and Damage Break

`break_on_damage: true` with `break_on_damage_threshold: 0.0` means any damage, including DoT ticks, breaks the polymorph. If the threshold is > 0, only hits above that value break it. This distinction matters for balance: a DoT applied before polymorph can soft-extend CC if it ticks below the threshold.

### 3.7 Knockback and Knockup — Physics Impulse

Knockback and knockup are **instant, one-shot effects** with no duration. They apply a single physics impulse to the target entity and are immediately done. No `stack_mode` or `duration_s` needed (set to 0).

```json
{
  "id": 730,
  "name": "Knockback",
  "type": "knockback",
  "duration_s": 0.0,
  "stack_max": 1,
  "stack_mode": "replace",
  "cc": {
    "knockback_force": 12.0,        // impulse magnitude in m/s
    "knockback_direction": "away",  // away | fixed | toward
    "knockback_arc": 0.3,           // 0.0 = horizontal, 1.0 = straight up (mix)
    "break_on_damage": false
  },
  "modifiers": [],
  "icon_url": null,                 // no icon — instant, not displayed on status bar
  "vfx": "vfx/effect_knockback_dust",
  "tags": ["cc", "knockback", "physical"],
  "script": null
}
```

#### Server behaviour

```
On EffectApplied (type=knockback or knockup):
  1. Compute impulse vector:
       direction = (away)  → normalize(entity.pos - caster.pos)
                  (toward) → normalize(caster.pos - entity.pos)
                  (fixed)  → cc.knockback_fixed_dir
       arc = cc.knockback_arc
       impulse = direction × (1 - arc) + vec3(0, 1, 0) × arc
       impulse = normalize(impulse) × cc.knockback_force
  2. Apply impulse to entity's physics body (Jolt CharacterVirtual or RigidBody)
  3. For the next N ticks (estimated from force / deceleration):
       - Server simulates entity position via physics
       - Sends EntityMove RPCs from physics result
       - Ignores C2S movement input until velocity < threshold
  4. When entity lands / velocity drops below 0.5 m/s: return control to player
```

#### Client behaviour

```
On EntityPhysicsImpulse RPC (broadcast on SkillExecuted):
  - Apply same impulse to client-side character controller
  - Play knockback VFX at hit point
  - Camera does not shake unless SkillDef specifies screen_shake cue
  - Character animation: "knockback_launch" → "knockback_land" state machine transition
```

#### NavMesh and Knockback

Physics impulse may carry the entity off the navmesh surface momentarily. The server simulates this as a ballistic arc using Jolt's CharacterVirtual with gravity, landing on the terrain collider. Once landed, navmesh snapping resumes. This means knockback over cliffs is intentionally possible — the entity lands at the terrain below.

---

## 4. Client-Side Targeting Flow

The client handles **all UI and preview** before sending anything to the server. Server only sees the final resolved activation.

### 4.1 `SELF` — Instant, no preview

```
Player presses skill key
→ Client sends SkillActivate { skill_id, target_type=SELF } immediately
→ Client plays cast_vfx at caster position (optimistic)
```

### 4.2 `TARGET_ENTITY` — Locked target required

```
Player presses skill key
→ If no locked target: play "no target" sound, abort
→ Client sends SkillActivate { skill_id, target_entity_id }
→ Client plays cast_anim + cast_vfx immediately (optimistic)
→ Server validates and replies with SkillExecuted or SkillFailed
```

### 4.3 `TARGET_ENTITY_CHAIN` — Locked target + combo check

Same as TARGET_ENTITY. The server checks `combo_requires_effect` on the target. If the prerequisite effect is missing, server replies `SkillFailed { reason=COMBO_PREREQ }`. Client plays a "combo miss" sound.

### 4.4 `GROUND_POINT` / `GROUND_CIRCLE` — Ground placement

```
Player presses skill key
→ Client enters placement mode (cursor changes, indicator appears)
   - Raycast from camera through mouse cursor position onto terrain
   - Show indicator mesh (cue.indicator) at raycast hit point
   - If snap_to_navmesh: snap to nearest navmesh polygon
   - Colour indicator RED if distance > range; GREEN if valid
   - Show radius ring if GROUND_CIRCLE

Player clicks to confirm placement
→ Client sends SkillActivate { skill_id, target_pos }
→ Client exits placement mode
→ Client plays cast_anim + cast_vfx at caster (optimistic)

Player presses Escape / right-click to cancel
→ Client exits placement mode silently (no RPC sent)
```

**Indicator mesh asset** is specified in `cue.indicator`. It is a flat circle/ring mesh loaded from the client pak. Its scale is set at runtime to `indicator_radius`. If the asset key is missing (new spell before client update), a generic white circle is used.

### 4.5 `CAMERA_FORWARD` — Direction from camera

```
Player presses skill key
→ Client captures camera_forward vector at the moment of keypress
→ Client sends SkillActivate { skill_id, client_direction: camera_forward }
→ Client plays cast_anim + directional vfx immediately (optimistic)
```

The server validates `client_direction` against the character's current facing. If the angle diverges by more than `CAMERA_FORWARD_MAX_DEVIATION_DEG` (default 60°), the server clamps it to the character facing direction rather than rejecting the skill. This handles natural camera-ahead-of-body situations without punishing the player.

### 4.6 `CHAIN_TARGET` — Bouncing chain

```
Player presses skill key
→ Requires a locked primary target (same as TARGET_ENTITY)
→ Client sends SkillActivate { skill_id, target_entity_id }
→ Server computes chain sequence: primary target + N nearest enemies within chain_radius
→ Server broadcasts SkillExecuted with all chain hit entity IDs
→ Client plays bounce VFX sequentially between each hit entity
```

---

## 5. Server-Side Activation Flow

```
Recv SkillActivate RPC from conn
│
├── 1. Existence & state checks
│     entity alive, entity owns skill_id, entity not stunned/silenced,
│     entity not already casting another skill
│
├── 2. Resource check
│     resource >= cost.amount  →  deduct optimistically (refund on fail)
│
├── 3. Cooldown check
│     cooldown_ready_at[skill_id] <= now_ns
│
├── 4. Target validation  (varies by target_type — see §5.1)
│
├── 5. Cast time
│     cast_time_s == 0  →  skip to step 7
│     cast_time_s > 0   →  begin cast state, proceed to step 6
│
├── 6. Cast phase (per zone tick)
│     advance cast_progress += delta_s
│     check interrupt events:
│       move event received + interrupt_on_move → cancel → SkillCastInterrupted
│       damage event received:
│         roll interrupt_on_damage_chance → cancel or add delay
│         (max interrupt_on_damage_max_delays delays)
│     cast_progress >= cast_time_s + accumulated_delays → proceed to step 7
│
├── 7. Execute
│     set cooldown_ready_at = now_ns + cooldown_s × 1e9
│     set global cooldown until now_ns + global_cooldown_s × 1e9
│     run Lua script on_execute() OR built_in_handler
│     → collect hit_results[]
│
├── 8. Apply effects
│     for each hit entity in hit_results:
│       apply damage / heal (calc_spell_damage)
│       for each effect in effects_on_hit: EffectSystem.apply(entity, effect_id, caster)
│
└── 9. Broadcast
      S2C RPC SkillExecuted { caster_id, skill_id, hits[] }
      → clients play impact_vfx, sfx, projectile
```

### 5.1 Target Validation Per `target_type`

#### SELF
```
No target validation. Check caster is alive and able to cast.
```

#### TARGET_ENTITY
```
target = get_entity(target_entity_id)
if !target or !target.alive          → FAIL: TARGET_INVALID
if dist(caster, target) > range      → FAIL: OUT_OF_RANGE
if require_los and !los(caster, target) → FAIL: NO_LOS
if target.immune_tags ∩ skill.tags ≠ ∅  → FAIL: IMMUNE
```

#### TARGET_ENTITY_CHAIN
```
Same as TARGET_ENTITY, plus:
if combo_requires_effect != null:
  if !target.has_effect(combo_requires_effect) → FAIL: COMBO_PREREQ
```

#### GROUND_POINT / GROUND_CIRCLE
```
// Server re-validates the client-provided position
ground_pos = client target_pos
if dist(caster, ground_pos) > range   → FAIL: OUT_OF_RANGE
if !on_terrain(ground_pos)            → FAIL: INVALID_POSITION
if snap_to_navmesh and !on_navmesh(ground_pos):
  ground_pos = nearest_navmesh_point(ground_pos)
  if dist(caster, ground_pos) > range → FAIL: OUT_OF_RANGE

// LoS check: caster must be able to see the ground position
// Prevents placing AoE behind walls, inside rooms, through doors
// Uses unified pipeline: GeoData static LoS + dynamic blocker check (§4.3 dynamic-blockers.md)
if require_los:
  caster_eye = caster_pos + (0, 0, caster_eye_height_m)
  target_eye = ground_pos + (0, 0, 0.5)   // slightly above ground to avoid floor self-block
  if !uz_los_check(geo, blockers, caster_eye, target_eye) → FAIL: NO_LOS

// ground_pos is now authoritative; client position is discarded
```

**Why LoS is mandatory for ground-targeted skills**: a player clicking through a wall model can accidentally (or deliberately) cast an AoE inside a room they cannot see into. Without server-side LoS, the AoE lands correctly on the server even though the client indicator was blocked. Validating `uz_los_check` against both the static GeoData and active dynamic blockers (walls, closed gates) eliminates this class of exploit.

**Client-side indicator feedback**: the client runs the same LoS check locally using its GeoData copy before confirming placement:
```
- Indicator RED + blocked icon: target position is out of range OR no LoS from caster
- Indicator YELLOW:             in range but LoS partially obstructed (geometry in the way)
- Indicator GREEN:              valid — in range, walkable surface, clear LoS
```
The server is always authoritative; client feedback is prediction only.

#### CAMERA_FORWARD
```
direction = clamp_to_character_facing(client_direction, max_deviation=60°)
// direction is now authoritative
// hit detection will use this direction
```

#### CHAIN_TARGET
```
// Same as TARGET_ENTITY for primary target
// Chain sequence built during execute step (§5.2)
```

### 5.2 Hit Detection Per Shape

All queries performed server-side using Jolt Physics overlap queries (reuses the physics world):

```c
// SPHERE at ground_pos
hits = physics_overlap_sphere(ground_pos, hit.radius);

// CONE in direction from caster
hits = physics_overlap_cone(caster_pos, direction, hit.cone_angle_deg, hit.cone_length);

// CYLINDER at ground_pos
hits = physics_overlap_cylinder(ground_pos, hit.radius, hit.height);

// BOX in direction from caster
hits = physics_overlap_box(caster_pos, direction, hit.box_w, hit.box_h, hit.box_l);

// CHAIN — find primary target, then bounce
targets = [primary_target];
last = primary_target;
for i in 1..chain_count:
  candidates = physics_overlap_sphere(last.pos, chain_radius)
               .filter(e => e != caster && !targets.contains(e) && valid_target(e))
               .sort_by(dist_to_last);
  if candidates.empty: break
  targets.push(candidates[0]);
  last = candidates[0];
```

After shape query, each candidate entity is filtered:
1. Not the caster (unless `friendly_fire` or `SELF` targets allow it).
2. Not on same faction as caster (unless `friendly_fire`).
3. `immune_tags` does not intersect `skill.tags`.
4. If `require_los`: `los(caster, entity)` must pass.
5. Respect `max_targets` (sorted by distance, closest first).

---

## 6. Lua Skill Scripts

For skills beyond what `built_in_handler` covers, a Lua script runs server-side:

```lua
-- scripts/skills/fireball.lua
return {
  on_execute = function(ctx, caster, skill, target_pos)
    -- Spawn a server-simulated projectile
    local proj = ctx.world:spawn_projectile({
      owner      = caster,
      origin     = caster:pos() + vec3(0, 1.2, 0),  -- offset to hand height
      target_pos = target_pos,
      speed      = skill.cue.proj_speed,
      radius     = 0.25,
      on_impact  = function(proj, hit_pos)
        -- Server calls the built-in sphere query from the skill def
        local hits = ctx.skill:aoe_query(skill, hit_pos, caster)
        for _, victim in ipairs(hits) do
          local dmg = ctx.combat:calc_spell_damage(caster, victim, skill.hit.damage)
          ctx.combat:apply_damage(caster, victim, dmg)
          ctx.effect:apply(victim, 601, caster)
        end
      end,
    })
  end,
}
```

### 6.1 Pure-Data Skills (no Lua)

Skills with `"script": null` use a `built_in_handler` string that maps to a C handler:

| `built_in_handler` | Behaviour |
|---|---|
| `"melee_single"` | TARGET_ENTITY, damage, apply effects_on_hit |
| `"ranged_single"` | TARGET_ENTITY, spawns a projectile, damage on impact |
| `"ground_aoe"` | GROUND_POINT, sphere query, damage + effects |
| `"ground_heal_aoe"` | GROUND_CIRCLE, sphere query, heal allies |
| `"self_buff"` | SELF, apply effects_on_hit to caster |
| `"chain_lightning"` | CHAIN_TARGET, damage each target in chain |
| `"teleport_self"` | GROUND_POINT, teleport caster to validated position |
| `"portal_place"` | GROUND_POINT, place portal entity at position |
| `"cone_spray"` | CAMERA_FORWARD, cone query, damage |

Adding `Power Strike` = POST the JSON to Content API. No script, no rebuild.

---

## 7. Portal Skill

Portals are a first-class built-in handler (`portal_place`), given how common they are in MMORPG design.

```json
{
  "id": 601,
  "name": "Recall Portal",
  "target_type": "GROUND_POINT",
  "range": 30.0,
  "cast_time_s": 3.0,
  "snap_to_navmesh": true,
  "interrupt_on_move": true,
  "built_in_handler": "portal_place",
  "portal": {
    "duration_s": 30.0,
    "radius": 1.5,
    "destination_type": "PLACED",   // PLACED | ZONE_SPAWN | BIND_POINT
    "use_count": 10,
    "team_only": true
  }
}
```

Server behaviour:
1. Validate ground position on navmesh.
2. Spawn a `portal_entity` at `target_pos` owned by caster.
3. Portal entity persists for `duration_s` or `use_count` uses, whichever comes first.
4. Any entity entering the portal radius triggers a zone-safe teleport to `destination`.
5. S2C `SpawnEntity` RPC broadcasts portal to all connections in interest area.

---

## 8. Client Prediction & Rollback

The client plays cast animations and cosmetic VFX **immediately on keypress** without waiting for server confirmation. This is purely visual — no damage, no effects, no cooldown is applied client-side.

On server reply:
- `SkillExecuted`: cooldown starts, impact VFX plays, damage numbers appear.
- `SkillFailed`: cancel cast animation, play "fail" sound, no cooldown consumed.

If the server `SkillFailed` arrives after the client already started a cast animation (cast time > 0), the animation is interrupted at whatever frame it is on.

The **Global Cooldown** is applied client-side optimistically (prevents double-queuing). If `SkillFailed` arrives, the GCD is cancelled.

---

## 9. Interrupt Resistance

ArcheAge's cast interruption model is reproduced exactly:

```c
typedef struct {
    float    base_interrupt_chance;   // from skill_def
    float    interrupt_resistance;    // stat on caster (0.0–1.0, 0 = full interruptible)
    float    accumulated_delay_s;     // sum of applied delays this cast
    uint8_t  delay_count;             // number of delays applied
    uint8_t  max_delays;              // from skill_def
} uz_cast_state_t;

void uz_cast_on_damage_event(uz_cast_state_t* cs, uz_ability_comp_t* ab) {
    float effective_chance = cs->base_interrupt_chance
                             * (1.0f - ab->interrupt_resistance);

    if (frand() < effective_chance) {
        // Full interrupt
        ab->casting = false;
        return;
    }

    if (cs->delay_count < cs->max_delays) {
        cs->accumulated_delay_s += cs->base_delay_s;  // default 0.5s
        cs->delay_count++;
    }
    // Beyond max_delays: additional hits have no further effect
}
```

Interrupt resistance is a float stat on the entity (0.0 = full vulnerability, 1.0 = immune to delay/interrupt from damage). Heavy-armour classes have higher interrupt resistance by default in their base stats.

---

## 10. Combo System

`TARGET_ENTITY_CHAIN` skills define `combo_requires_effect` (an effect ID). The server checks whether the target currently has that effect before proceeding.

```lua
-- Combo finisher example: "Shatter" — requires target has "Frozen" effect (id 701)
{
  "id": 550,
  "name": "Shatter",
  "target_type": "TARGET_ENTITY_CHAIN",
  "combo_requires_effect": 701,   -- Frozen
  "built_in_handler": "melee_single",
  "hit": {
    "damage": { "base_min": 200, "base_max": 280, "scale_stat": "str", "scale_factor": 2.0 }
  }
}
```

If combo condition is not met, the server sends `SkillFailed { reason=COMBO_PREREQ }`. The client can show a "Combo: target not Frozen" toast notification.

Combo multipliers can also be expressed without hard-gating using a Lua script that reads target effects:

```lua
on_execute = function(ctx, caster, skill, target)
    local base_dmg = ctx.combat:calc_spell_damage(caster, target, skill.hit.damage)
    local frozen = target:has_effect(701)
    local final  = frozen and base_dmg * 2.0 or base_dmg
    ctx.combat:apply_damage(caster, target, final)
    if frozen then ctx.effect:remove(target, 701) end
end
```

---

## 11. Animation System Integration

Skill animations are specified in `cue.cast_anim` (a key into the character's animation state machine). The client drives animations; the server does not know about animation states.

| Cue field | Client behaviour |
|---|---|
| `cast_anim` | Transition animation state machine to this state at cast start |
| `cast_vfx` | Play VFX at caster's hand bone (or root if no bone specified) |
| `proj_mesh` | Spawn projectile mesh entity, move toward target_pos at proj_speed |
| `impact_vfx` | Play VFX at impact point on SkillExecuted RPC |
| `indicator` | Show during ground-placement mode |

On `SkillCastInterrupted`, the client transitions back to the `idle` animation state immediately.

Players can **move freely** during cast animations unless `interrupt_on_move: true`. The movement system runs independently of the animation system on both client and server — movement and skill ticking never block each other.

---

## 12. RPC Definitions

```c
// Client → Server
UZ_RPC_C2S(SkillActivate,
    (UZ_U32,     skill_id)
    (UZ_U8,      target_type)           // mirrors SkillDef target_type enum
    (UZ_U32,     target_entity_id)      // 0 if not TARGET_ENTITY/CHAIN
    (UZ_WPOS,    target_pos)            // zero-vec if not GROUND / CAMERA
    (UZ_ZIGZAG16, dir_x)               // CAMERA_FORWARD only: normalised direction × 32767
    (UZ_ZIGZAG16, dir_z)
)

// Server → Client (to caster only)
UZ_RPC_S2C(SkillFailed,
    (UZ_U32, skill_id)
    (UZ_U8,  reason)     // COOLDOWN | NO_MANA | OUT_OF_RANGE | NO_TARGET
                         // NO_LOS | IMMUNE | COMBO_PREREQ | INVALID_POS
                         // SILENCED | STUNNED | CASTING
)

UZ_RPC_S2C(SkillCastStart,
    (UZ_U32,     caster_entity_id)
    (UZ_U32,     skill_id)
    (UZ_F32Q_16, cast_time_s)        // total cast time (may grow with delays)
    (UZ_WPOS,    resolved_target_pos) // authoritative ground pos for ground skills
)

UZ_RPC_S2C(SkillCastProgress,
    (UZ_U32,     caster_entity_id)
    (UZ_U32,     skill_id)
    (UZ_F32Q_16, total_cast_s)       // updated total time (after interrupt delays)
)

UZ_RPC_S2C(SkillCastInterrupted,
    (UZ_U32, caster_entity_id)
    (UZ_U32, skill_id)
    (UZ_U8,  reason)     // MOVE | DAMAGE | DEATH | STUN | SILENCE
)

// Server → broadcast to interest area
UZ_RPC_S2C(SkillExecuted,
    (UZ_U32, caster_entity_id)
    (UZ_U32, skill_id)
    (UZ_WPOS, resolved_pos)          // ground pos or caster pos (for camera skills)
    (UZ_ZIGZAG16, resolved_dir_x)    // authoritative direction (camera skills)
    (UZ_ZIGZAG16, resolved_dir_z)
    (UZ_U8,  hit_count)
    // repeated hit_count times:
    (UZ_U32,    hit_entity_id)
    (UZ_WPOS,   hit_pos)
    (UZ_ZIGZAG, damage_or_heal)      // negative = damage
    (UZ_BOOL,   is_crit)
    (UZ_BOOL,   is_combo)
)

// Effect events (targeted to owning connection for own entity;
//               broadcast via RELIABLE_UNO for visible entities)
UZ_RPC_S2C(EffectApplied,
    (UZ_U32, target_entity_id)
    (UZ_U32, effect_id)
    (UZ_U8,  stacks)
    (UZ_F32Q_8, duration_s)
)
UZ_RPC_S2C(EffectRemoved,
    (UZ_U32, target_entity_id)
    (UZ_U32, effect_id)
    (UZ_U8,  reason)     // EXPIRED | DISPELLED | CLEANSED | DEATH
)

// CC-specific RPCs
UZ_RPC_S2C(EntityPolymorphed,
    (UZ_U32, entity_id)
    (UZ_U32, effect_id)           // EffectDef with polymorph block
    (UZ_F32Q_8, duration_s)
)
UZ_RPC_S2C(EntityPolymorphReverted,
    (UZ_U32, entity_id)
)

// Knockback/knockup: sent inside SkillExecuted hits[] as a hit_flag,
// but also broadcast separately for entities that see the impact but
// are not in the original SkillExecuted receiver set.
UZ_RPC_S2C(EntityPhysicsImpulse,
    (UZ_U32,     entity_id)
    (UZ_ZIGZAG16, dir_x)          // normalised impulse direction × 32767
    (UZ_ZIGZAG16, dir_y)
    (UZ_ZIGZAG16, dir_z)
    (UZ_F32Q_8,  force)           // magnitude m/s, 0–30 range
)

// Sent when CC ends and player regains control (fear, stun, polymorph, sleep)
UZ_RPC_S2C(EntityStopCC,
    (UZ_U32, entity_id)
    (UZ_U8,  cc_type)             // FEAR | STUN | SLEEP | POLYMORPH | CHARM
)

// Reconnect sync
UZ_RPC_S2C(CooldownSync,
    (UZ_U8, count)
    // repeated count times:
    (UZ_U32,     skill_id)
    (UZ_F32Q_16, remaining_s)
)
UZ_RPC_S2C(ActiveEffectsSync,
    (UZ_U8, count)
    // repeated count times:
    (UZ_U32,     effect_id)
    (UZ_U8,      stacks)
    (UZ_F32Q_8,  remaining_s)
)
```

---

## 13. Extending the System

### New pure-data skill (zero rebuild)
1. Author `SkillDef` JSON → choose `built_in_handler`.
2. Author `EffectDef` if new status effect needed.
3. Upload icon to CDN.
4. POST to Content API → live in < 5 s.

### New Lua-scripted skill (zero client rebuild)
1. Write `scripts/skills/my_spell.lua` → push to Content API.
2. Author `SkillDef` with `"script": "scripts/skills/my_spell.lua"`.
3. POST → server hot-reloads script in < 2 s.

### New targeting mode (engine change required)
New `target_type` values require:
- Server: new validation branch in the activation flow.
- Client: new preview/input mode (indicator mesh, cursor change).
This is a C code change, but the data model (`SkillDef`) naturally extends with new `target_type` values and new fields without breaking existing skills.

### New VFX (client rebuild required)
New particle/mesh assets must be bundled in the client pak. Client falls back to a generic VFX if the cue key is missing — no crash, no black screen.

---

## 14. File Layout

```
src/engine/net/abilities/
├── ability_system.h/.c       — AbilityComponent, activation FSM, cooldown, GCD
├── effect_system.h/.c        — EffectComponent, tick, stack modes, expire
├── skill_def.h/.c            — SkillDef + HitDef struct, Content API cache
├── effect_def.h/.c           — EffectDef struct, Content API cache
├── tag_registry.h/.c         — string tag hash map, immunity check
├── damage_calc.h/.c          — spell damage formula (stat scale, element, crit)
├── hit_detect.h/.c           — SPHERE / CONE / CYLINDER / BOX / CHAIN queries
├── cast_state.h/.c           — cast progress, interrupt delay, move interrupt
├── combat_log.h/.c           — server event log
└── ability_lua_api.h/.c      — ctx.combat / ctx.effect / ctx.skill Lua bindings

src/engine/client/targeting/
├── targeting_manager.h/.c    — hard-lock / soft-lock state, current target entity
├── ground_indicator.h/.c     — raycast, indicator mesh, navmesh snap, range colour
└── camera_forward.h/.c       — camera forward vector capture, direction packing

tests/net/abilities/
├── test_ability_activation.c — all target_type paths, failure modes
├── test_cast_interrupt.c     — damage interrupt, move interrupt, delay accumulation
├── test_hit_detect.c         — SPHERE / CONE / BOX / CHAIN shape correctness
├── test_effect_system.c      — dot tick, stack modes, expire, dispel
├── test_combo.c              — combo prereq check, chain bounce order
├── test_skill_rpc.c          — serialisation round-trip all fields
├── test_portal.c             — portal spawn, use_count, team_only filter
└── test_pure_data_skill.c    — all built_in_handler coverage
```

---

## 15. Definition of Done

- [ ] All seven `target_type` values activate correctly end-to-end (client → server → broadcast).
- [ ] `GROUND_POINT/CIRCLE`: indicator renders, snaps to navmesh, colour changes at range boundary; indicator RED when GeoData LoS blocked on client.
- [ ] `GROUND_POINT/CIRCLE`: server rejects activation with `NO_LOS` when caster cannot see target_pos through static geometry OR dynamic blockers (wall, closed gate).
- [ ] `TARGET_ENTITY` with `require_los: true`: `NO_LOS` returned when target is behind a dynamic wall blocker.
- [ ] Spell cast through wall that appears mid-cast (blocker created during cast_time): spell fires but hit detection uses correct LoS at execute time (hits only entities with LoS to impact point).
- [ ] `CAMERA_FORWARD`: direction clamped to 60° of character facing; CONE hit detects correctly.
- [ ] `CHAIN_TARGET`: bounces to nearest N enemies; stops gracefully when fewer targets available.
- [ ] Cast interrupt on move cancels cast and sends RPC when `interrupt_on_move: true`.
- [ ] Interrupt-on-damage: first two hits add 0.5 s delay; third hit onward has no effect (default config).
- [ ] Interrupt resistance stat at 1.0 blocks all interrupts and delays.
- [ ] Combo prereq missing: `SkillFailed { COMBO_PREREQ }` sent; no damage applied.
- [ ] Portal spawns entity, teleports team members, expires after duration/use_count.
- [ ] Pure-data skill via `built_in_handler` activates with no Lua, no script file.
- [ ] Lua skill hot-reload: new script live in < 2 s, old script evicted.
- [ ] New skill added via Content API dashboard is playable in < 5 s.
- [ ] Missing VFX cue key falls back to generic VFX; no crash.
- [ ] CooldownSync restores all cooldowns correctly on reconnect.
- [ ] ActiveEffectsSync restores all active effects on reconnect.
- [ ] **CC — Stun**: entity receives stun → C2S movement RPCs are ignored → after duration, entity regains control and `EntityStopCC` is sent.
- [ ] **CC — Root**: entity receives root → C2S movement RPCs ignored → can still cast skills → after duration, movement restored.
- [ ] **CC — Silence**: entity receives silence → `SkillActivate` RPC returns `SkillFailed { SILENCED }` → movement unaffected.
- [ ] **CC — Fear**: entity receives fear → server drives movement via navmesh steering at correct speed → `EntityMove` RPCs sent to clients → C2S movement ignored → after duration `EntityStopCC`.
- [ ] **CC — Fear `break_on_damage`**: any damage while feared removes the effect immediately; `EffectRemoved { CLEANSED }` sent.
- [ ] **CC — Polymorph**: `EntityPolymorphed` triggers client mesh/anim swap → stats overridden server-side → damage breaks if `break_on_damage: true` → `EntityPolymorphReverted` restores client mesh.
- [ ] **CC — Sleep**: entity cannot move or cast → first damage > 0 removes effect.
- [ ] **CC — Knockback**: `EntityPhysicsImpulse` sent → client character launches in computed direction → navmesh resumes on land.
- [ ] **CC resistance**: entity with `reduce_duration_factor: 0.5` for "stun" receives half-duration stun; entity with `0.0` is immune and receives no CC effect.
- [ ] **Immune tag**: entity with `immune_tags: ["polymorph"]` is unaffected by polymorph skill; `SkillFailed { IMMUNE }` sent.
- [ ] **CC + polymorph asset missing**: client receives `EntityPolymorphed` with unknown mesh_override → placeholder shown, no crash.
- [ ] 5 000 simultaneous skill activations processed in < 1 tick (50 ms).
- [ ] All RPC serialisation round-trips pass bit-exact comparison.
- [ ] All tests pass. Coverage ≥ 95%.
