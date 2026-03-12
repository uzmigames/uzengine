# Entity/Component — Tick Architecture

> **Status**: Planned (Milestone 2.0)
> **Source**: Spec in `docs/ENGINE.md` §8
> **Depends on**: World (§10-01), Component Model (§10-02)
> **References**: UE5 `FTickFunction` + Significance Manager, Godot `_process`/`_physics_process`

## Purpose

Hybrid frequency tick system. Instead of one tick rate per entity, each entity's work is split by **urgency** — immediate per-frame updates for smooth visuals, plus independently-scheduled functions for expensive logic at lower frequencies.

## The Problem with Traditional Tick

Every major engine (Unreal, Unity, Godot) couples gameplay tick to frame rate. At 300 FPS, every behavior ticks 300 times per second — even AI that only needs 10Hz decisions. In action games where 50+ enemies are all within 20m, you can't just lower tick rates globally — the player **feels** 100ms AI reaction delay.

## Urgency-Layered Tick

| Urgency | Runs At | Examples | Cost Target |
|---|---|---|---|
| **Immediate** (`OnTick`) | Every frame | Movement, input response, interpolation | < 0.1ms/entity |
| **Reactive** (`UZ_SCHEDULED 30-60Hz`) | Fixed frequency | Animation state machine, combat logic | < 0.5ms/entity |
| **Strategic** (`UZ_SCHEDULED 5-20Hz`) | Fixed frequency | Pathfinding, AI decisions | < 2ms/entity |
| **Background** (`UZ_SCHEDULED 1-5Hz`) | Fixed frequency | Buff timers, aggro decay, stat regen | < 0.5ms/entity |

A single NPC uses ALL layers simultaneously:
- **OnTick (every frame):** apply velocity, interpolate visuals — cheap, smooth
- **CombatUpdate (30Hz):** choose next attack, verify range — medium
- **NavigationUpdate (10Hz):** recalculate navmesh path — expensive, infrequent
- **AggroUpdate (2Hz):** re-evaluate threat table — cheap, rare

## Tick Groups (Frame Phases)

```cpp
enum class TickGroup : uint8_t {
    PrePhysics   = 0,  // input processing, movement intent, AI decisions
    Physics      = 1,  // fixed-timestep physics simulation (engine-internal)
    PostPhysics  = 2,  // react to physics results, animation, combat
    Update       = 3,  // general game logic, camera, VFX
    PostUpdate   = 4,  // cleanup, UI sync, late effects, render prep
};
```

Executed in order with barriers between them. `OnTick` and scheduled callbacks are dispatched within the appropriate group.

## OnTick — Per-Frame Fast Path

Runs **every frame** within the behavior's tick group. Must be lightweight.

```cpp
class MeleeEnemy : public uz::Behavior {
    UZ_BEHAVIOR(MeleeEnemy);
    Vec3 velocity_ = {};

public:
    void OnBeginPlay() override {
        SetTickGroup(uz::TickGroup::PostPhysics);
    }

    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();
        transform.position += velocity_ * dt;
    }
};
```

## UZ_SCHEDULED — Independent Frequency Functions

Scheduled functions run at a fixed frequency independent of frame rate. The engine accumulates time and dispatches them when their interval elapses.

```cpp
class MeleeEnemy : public uz::Behavior {
    UZ_BEHAVIOR(MeleeEnemy);
    Vec3 velocity_ = {};
    uz::Entity target_ = uz::Entity::Invalid();

public:
    void OnTick(float dt) override {
        Get<uz::TransformComponent>().position += velocity_ * dt;
    }

    // 30 Hz — responsive combat without per-frame waste
    UZ_SCHEDULED(CombatUpdate, 30.0f);
    void CombatUpdate(float dt) {
        // Choose attacks, verify range, manage cooldowns
    }

    // 10 Hz — expensive navmesh queries
    UZ_SCHEDULED(NavigationUpdate, 10.0f);
    void NavigationUpdate(float dt) {
        // Recalculate path via navmesh
    }

    // 2 Hz — cheap background updates
    UZ_SCHEDULED(AggroUpdate, 2.0f);
    void AggroUpdate(float dt) {
        // Re-evaluate threat priorities
    }
};
```

## Scene-Level Frequency Override

`.uzscene` can override the default tick rate per instance without recompiling:

```json
{
    "name": "Boss_Dragon",
    "components": {
        "DragonAI": {
            "scheduled_overrides": {
                "CombatUpdate": 60.0,
                "NavigationUpdate": 20.0
            }
        }
    }
}
```

Priority: **scene override > macro default > system default**.

## Tick Budget — Frame Time Guard

Each frame has a configurable **tick budget** (default: 4ms). If scheduled functions exceed it, remaining work is deferred to the next frame (staggered execution). `OnTick` always runs — it's the fast path.

Priority when budget is tight:
1. Entities with `UZ_TICK_PRIORITY(High)` — bosses, player-interacting NPCs
2. Entities visible to the camera
3. Entities close to the player
4. Entities far away or off-screen

## Tick LOD — Distance-Based Rate Scaling

Automatic scaling of scheduled function rates based on camera distance:

| Distance | Rate Multiplier | Effect |
|---|---|---|
| < 20m | 1.0x | Full rate |
| 20–50m | 0.5x | Half rate |
| 50–100m | 0.25x | Quarter rate |
| > 100m | 0.0x | Sleep (wake on event only) |

Tick LOD applies ONLY to scheduled functions, NEVER to `OnTick`.

## Job System — Parallel Execution (Phase 2+)

Behaviors marked with `UZ_TICK_PARALLEL(true)` can execute on worker threads. Rules:
- Each entity accesses ONLY its own components (no cross-entity writes)
- Read-only access to other entities is allowed (`ForEach` queries)
- World mutation is deferred to end-of-frame

## Performance Budget — Action Game Scenario

**50 melee enemies on screen, all within 20m:**

| Layer | Entities/Frame | Cost/Entity | Total |
|---|---|---|---|
| OnTick (every frame) | 50 | 0.05ms | 2.5ms |
| CombatUpdate (30Hz, staggered) | ~15/frame | 0.2ms | 3.0ms |
| NavigationUpdate (10Hz, staggered) | ~5/frame | 0.8ms | 4.0ms |
| AggroUpdate (2Hz, staggered) | ~1/frame | 0.1ms | 0.1ms |
| **Total per frame** | | | **~9.6ms** |

Compare to naive per-frame tick: 50 × 1.5ms = **75ms** — unplayable.

## MVP vs Full Implementation

| Phase | What Ships |
|-------|-----------|
| **Phase 2 (MVP)** | TickGroup phases, OnTick, UZ_SCHEDULED with time accumulator (single-threaded) |
| **Phase 2+** | Job system with worker pool, UZ_TICK_PARALLEL |
| **Phase 3** | Tick budget + stagger, UZ_TICK_PRIORITY, Tick LOD |

## Engine Comparison

| | Unreal 5 | Godot 4 | **UzEngine** |
|---|---|---|---|
| Tick coupled to frame | Yes | Yes | **No — OnTick + independent frequencies** |
| Per-function frequency | No (per-component only) | No (Timer nodes) | **Yes — UZ_SCHEDULED per method** |
| Parallel tick | Limited | No | **Job system, opt-in per behavior** |
| Tick budget | No | No | **Budget + stagger + priority** |
| Tick LOD | Significance Manager (plugin) | No | **Built-in, automatic, opt-out** |

## Rules

1. **ALWAYS keep `OnTick` lightweight** (< 0.1ms) — expensive work goes in `UZ_SCHEDULED`
2. **NEVER modify other entities' components in `OnTick`** if `UZ_TICK_PARALLEL(true)` — only own components
3. **ALWAYS set `UZ_TICK_PRIORITY(High)`** for player-critical entities (bosses, interactive NPCs)
