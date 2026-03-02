# Gameplay Systems Spec

> **Status**: Planned (Phase 4)
> **Owner**: `game.dll` (C++ system scheduler, GAS, FSM, BehaviorTree)
> **Lua API**: Full integration via `ctx.gameplay`
> **Thread safety**: Game Logic thread only

---

## Overview

The Gameplay Systems layer provides advanced high-level mechanics for game development: a flexible **System Scheduler** with execution groups and priorities, a **Gameplay Ability System** (GAS-equivalent) for managing player and NPC abilities with cooldowns and costs, **Finite State Machines** for AI and state management, and **Behavior Trees** for complex NPC behavior.

These systems sit above the core ECS (EnTT registry) and leverage the Event Bus for decoupled communication. All systems are designed for hot-reload and Lua scripting-first workflow.

**Key principles**:
- System Scheduler (`SystemGroup` enum) enforces execution order (PrePhysics → Physics → PostPhysics → PreRender → Render → PostRender)
- Gameplay Ability System manages grant/activation lifecycle with cooldowns, costs, and tag-based filtering
- FSM and BehaviorTree are data-driven assets (`.uzfsm`, `.uzbt` INI files) with hot-reload support
- All state machines dispatch events via EventBus for reaction from other systems
- Behavior trees use blackboard components for data sharing between nodes

---

## System Scheduler / TickGroups

### Architecture

The System Scheduler is the central orchestrator for all game logic. It maintains an explicit execution order across 6 logical phases, each with independent priority ordering.

```cpp
// src/game/system_scheduler.h

enum class SystemGroup : uint8_t {
    PrePhysics = 0,      // Input processing, pre-physics setup
    Physics,             // Physics simulation (fixed 60 Hz)
    PostPhysics,         // Physics result processing, damage
    PreRender,           // Animation, visibility culling, sound updates
    Render,              // Particle emission, VFX
    PostRender,          // Cleanup, post-frame accounting
    COUNT
};

struct SystemHandle {
    uint32_t id;  // Opaque handle, unique per system
};

class SystemScheduler {
public:
    // Register a system with a tick group and priority
    // Lower priority_value executes first within the group
    SystemHandle register_system(
        std::string_view name,
        SystemGroup group,
        int priority_value = 0,
        const std::vector<SystemHandle>& dependencies = {}
    );

    // Unregister a system (automatic on destruction of owning class)
    void unregister_system(SystemHandle handle);

    // Execute all registered systems in order
    // Called once per frame by GameContext::on_frame()
    void tick(float dt);

    // Get current system count (debugging)
    size_t system_count() const;

    // Query system state (for editor inspector)
    struct SystemInfo {
        std::string name;
        SystemGroup group;
        int priority;
        float last_tick_ms;  // Duration of last tick in milliseconds
    };
    std::vector<SystemInfo> get_system_info() const;
};
```

### Registration Example

```cpp
// In game.dll initialization (e.g., GameEngine::init())

class InputSystem {
public:
    InputSystem(GameContext& ctx, SystemScheduler& sched) : ctx_(ctx) {
        handle_ = sched.register_system(
            "InputSystem",
            SystemGroup::PrePhysics,
            0  // Run first in PrePhysics
        );
    }

    ~InputSystem() {
        // Scheduler automatically cleans up on destruction
    }

    void tick(float dt) {
        // Process input, dispatch input events
    }

private:
    GameContext& ctx_;
    SystemHandle handle_;
};

class PhysicsSystem {
public:
    PhysicsSystem(GameContext& ctx, SystemScheduler& sched) : ctx_(ctx) {
        handle_ = sched.register_system(
            "PhysicsSystem",
            SystemGroup::Physics,
            0
        );
    }

    void tick(float dt) {
        ctx_.physics_world.step(dt);
    }

private:
    GameContext& ctx_;
    SystemHandle handle_;
};

class DamageResolveSystem {
public:
    DamageResolveSystem(GameContext& ctx, SystemScheduler& sched) : ctx_(ctx) {
        // Depends on PhysicsSystem — will run after Physics group completes
        handle_ = sched.register_system(
            "DamageResolveSystem",
            SystemGroup::PostPhysics,
            0,
            { /* physics_system_handle */ }
        );
    }

    void tick(float dt) {
        // Process collision events and apply damage
    }

private:
    GameContext& ctx_;
    SystemHandle handle_;
};
```

### Execution Flow

```
Frame Start
    │
    ├─ SystemGroup::PrePhysics (priority 0, 1, 2, ...)
    │   ├─ InputSystem
    │   └─ AnimationBlendSystem
    │
    ├─ SystemGroup::Physics (synchronized)
    │   └─ PhysicsSystem (fixed 60 Hz step)
    │
    ├─ SystemGroup::PostPhysics
    │   ├─ DamageResolveSystem
    │   └─ CameraFollowSystem
    │
    ├─ SystemGroup::PreRender
    │   ├─ CullingSystem
    │   └─ AudioUpdateSystem
    │
    ├─ SystemGroup::Render
    │   └─ ParticleEmitterSystem
    │
    └─ SystemGroup::PostRender
        └─ CleanupSystem
```

### Scheduler Statistics (Editor IPC)

The editor can query scheduler performance:

```python
# VSCode Command: /uz call scheduler.get_stats

{
    "systems": [
        {
            "name": "InputSystem",
            "group": "PrePhysics",
            "last_tick_ms": 0.15,
            "avg_tick_ms": 0.14,
        },
        {
            "name": "PhysicsSystem",
            "group": "Physics",
            "last_tick_ms": 2.3,
            "avg_tick_ms": 2.1,
        }
    ],
    "frame_time_ms": 16.7
}
```

---

## Gameplay Ability System (GAS)

### Overview

The Gameplay Ability System manages player and NPC abilities — granting, activating, and canceling them with support for cooldowns, costs (health, mana, etc.), and ability tags for filtering.

This is inspired by Unreal Engine's GAS but simplified for UzEngine. Key differences:
- No attribute system (use ECS components for character stats)
- No containers (use EnTT directly for ability holders)
- Ability specs are loadable data assets (`.uzability` INI files)

### Core Concepts

**AbilitySpec**: Data definition of an ability (name, cooldown, cost, tags)
**AbilityComponent**: ECS component holding granted abilities on a node
**AbilityInstance**: Runtime state of a granted ability (cooldown timer, tags)
**AbilityActivation**: Request to activate an ability (can fail if on cooldown or cost unavailable)

### C++ API

```cpp
// src/game/ability_system.h

struct AbilityCost {
    std::string_view stat_name;  // e.g., "mana", "stamina"
    float amount;
};

struct AbilityTag {
    uint32_t id;  // Tag ID from TagRegistry (see tags-groups.md)
};

// Ability definition (loaded from .uzability assets)
struct AbilitySpec {
    StringId id;                           // Unique ability identifier
    std::string display_name;              // "Fireball", "Dash", etc.
    float cooldown_duration;               // Cooldown in seconds (0 = no cooldown)
    std::vector<AbilityCost> costs;        // Resource costs (mana, stamina, etc.)
    std::vector<AbilityTag> required_tags; // Node must have all these tags to use
    std::vector<AbilityTag> ability_tags;  // Tags this ability grants (for filtering)
    uint32_t max_level;                    // Ability leveling (0 = no leveling)
};

// Runtime instance of a granted ability
struct AbilityInstance {
    StringId spec_id;
    float cooldown_timer;  // Counts down to 0; ability cannot activate if > 0
    uint32_t level;        // Current ability level
};

// ECS component: holds all granted abilities for a node
struct CAbilityComponent {
    std::vector<AbilityInstance> granted_abilities;
    std::unordered_map<std::string, float> current_stats;  // "mana", "health", etc.
};

// Ability activation request
struct AbilityActivationRequest {
    NodeHandle activator;
    StringId ability_id;
    Vec3 activate_at_position;  // World position (for targeting)
};

// Result of activation attempt
enum class AbilityActivationResult : uint8_t {
    Success,               // Ability activated
    NotGranted,           // Node doesn't have this ability
    OnCooldown,           // Cooldown timer not ready
    InsufficientCost,     // Can't afford the cost
    FailedTagRequirement, // Node missing required tags
    Blocked,              // Another system blocked activation (via event)
};

// Ability System
class AbilitySystem {
public:
    explicit AbilitySystem(GameContext& ctx);

    // Load ability spec from .uzability asset
    bool load_spec(std::string_view asset_path);

    // Grant an ability to a node (adds AbilityInstance to CAbilityComponent)
    void grant_ability(NodeHandle node, StringId ability_id, uint32_t level = 1);

    // Revoke a granted ability
    void revoke_ability(NodeHandle node, StringId ability_id);

    // Try to activate an ability
    AbilityActivationResult try_activate(const AbilityActivationRequest& req);

    // Apply cooldown to an ability (internal, called after activate)
    void apply_cooldown(NodeHandle node, StringId ability_id);

    // Update cooldown timers (called once per frame from SystemScheduler)
    void tick_cooldowns(float dt);

    // Query ability state
    const AbilityInstance* get_ability(NodeHandle node, StringId ability_id) const;
    bool has_ability(NodeHandle node, StringId ability_id) const;
    bool is_on_cooldown(NodeHandle node, StringId ability_id) const;
    float get_cooldown_remaining(NodeHandle node, StringId ability_id) const;
    float get_cooldown_percent(NodeHandle node, StringId ability_id) const;  // 0.0-1.0
};
```

### Ability Spec File Format (.uzability)

Ability definitions are loadable assets in INI format:

```ini
# res://abilities/fireball.uzability
[ability]
id = fireball
display_name = Fireball
cooldown_duration = 3.0
max_level = 3

[cost.0]
stat = mana
amount = 25.0

[cost.1]
stat = stamina
amount = 10.0

[required_tag.0]
tag = is_magic_user

[ability_tag.0]
tag = spell
[ability_tag.1]
tag = fire_damage
[ability_tag.2]
tag = aoe

[level_scaling]
# Optional: cooldown scales with level
cooldown_at_level = [3.0, 2.5, 2.0]
# Cost per level
cost_mana_at_level = [25.0, 30.0, 35.0]
```

### Events

The AbilitySystem emits events via EventBus:

```cpp
// Predefined events
struct AbilityGranted {
    NodeHandle node;
    StringId ability_id;
    uint32_t level;
};

struct AbilityRevoked {
    NodeHandle node;
    StringId ability_id;
};

struct AbilityActivated {
    NodeHandle activator;
    StringId ability_id;
    Vec3 position;
};

struct AbilityActivationFailed {
    NodeHandle activator;
    StringId ability_id;
    AbilityActivationResult reason;
};

struct AbilityCooldownStarted {
    NodeHandle node;
    StringId ability_id;
    float duration;
};

struct AbilityCooldownEnded {
    NodeHandle node;
    StringId ability_id;
};
```

### Lua API

```lua
-- In game.dll Lua scripts

-- Grant an ability to self
ctx.gameplay:grant_ability(self.id, "fireball")

-- Check if ability is on cooldown
local is_ready = not ctx.gameplay:is_on_cooldown(self.id, "fireball")
if is_ready then
    -- Attempt activation
    local result = ctx.gameplay:try_activate({
        activator = self.id,
        ability_id = "fireball",
        position = target_pos
    })

    if result == "Success" then
        -- Play attack animation, emit particles, etc.
        self:emit_ability_vfx()
    else
        print("Activation failed: " .. result)
    end
end

-- Listen for ability events
local sub = ctx.events:subscribe("AbilityActivated", function(ev)
    if ev.ability_id == "fireball" then
        ctx.world:query_sphere(ev.position, 5.0, function(hit_node)
            -- Apply damage to hit_node
        end)
    end
end)

-- Check cooldown percentage (for UI)
local cooldown_pct = ctx.gameplay:get_cooldown_percent(self.id, "fireball")
-- UI: show cooldown ring (0% = off cooldown, 100% = full cooldown)
```

---

## Finite State Machine (FSM)

### Overview

FSMs are essential for AI and game mode management. UzEngine provides a data-driven FSM system where state machines are defined in `.uzfsm` files and instantiated as ECS components.

States are defined as functions (C++ or Lua) that execute on enter, update, and exit. Transitions are triggered by events.

### Architecture

```cpp
// src/game/fsm.h

// State machine definition (loaded from .uzfsm asset)
struct FSMStateDef {
    std::string name;
    std::function<void()> on_enter;        // Called when entering state
    std::function<void(float)> on_update;  // Called each frame while active
    std::function<void()> on_exit;         // Called when leaving state
};

struct FSMTransitionDef {
    std::string from_state;
    std::string to_state;
    std::function<bool()> guard;           // Transition only if guard returns true
    std::function<void()> on_transition;   // Called during transition
};

// Runtime FSM instance (ECS component)
struct CFSMComponent {
    StringId fsm_spec_id;           // Which FSM spec is loaded
    std::string current_state_name;
    float state_time_elapsed;       // Time spent in current state
    std::vector<std::string> history; // Recent states (for debugging)
};

// FSM State Machine class
template<typename StateEnum, typename EventEnum>
class FSM {
public:
    // Define a state
    void state(StateEnum s, std::function<void()> on_enter = nullptr,
               std::function<void(float dt)> on_update = nullptr,
               std::function<void()> on_exit = nullptr);

    // Define a transition
    void transition(StateEnum from, EventEnum event, StateEnum to,
                    std::function<bool()> guard = nullptr);

    // Trigger a transition (emit event)
    void emit_event(EventEnum e);

    // Get current state
    StateEnum current_state() const;

    // Tick the FSM (update current state, check transitions)
    void tick(float dt);
};
```

### Simple Enum-Based FSM

For simple state machines, use a strongly-typed enum approach:

```cpp
// AI enemy state machine
enum class EnemyState {
    Idle,
    Patrol,
    Alerted,
    Chase,
    Attack,
    Damaged,
    Dead,
};

enum class EnemyEvent {
    PlayerSpotted,
    PlayerLost,
    PlayerAttacked,
    TakeDamage,
    ReachDestination,
    OnDeath,
};

class EnemyAISystem : public System {
public:
    EnemyAISystem(GameContext& ctx) : ctx_(ctx), fsm_(EnemyState::Idle) {
        setup_fsm();
    }

private:
    void setup_fsm() {
        // Idle state
        fsm_.state(EnemyState::Idle,
            [this]() { ctx_.log_info("Enemy idle"); },
            [this](float dt) { update_idle(dt); },
            [this]() { ctx_.log_info("Enemy exiting idle"); }
        );

        // Chase state
        fsm_.state(EnemyState::Chase,
            [this]() { ctx_.log_info("Enemy chasing player"); },
            [this](float dt) { update_chase(dt); },
            nullptr
        );

        // Transitions
        fsm_.transition(EnemyState::Idle, EnemyEvent::PlayerSpotted, EnemyState::Chase,
            [this]() { return can_see_player(); }
        );

        fsm_.transition(EnemyState::Chase, EnemyEvent::PlayerLost, EnemyState::Patrol,
            [this]() { return !can_see_player(); }
        );
    }

    void update_idle(float dt) {
        // Check for player
        if (can_see_player()) {
            fsm_.emit_event(EnemyEvent::PlayerSpotted);
        }
    }

    void update_chase(float dt) {
        Vec3 player_pos = get_player_position();
        move_toward(player_pos);

        if (!can_see_player()) {
            fsm_.emit_event(EnemyEvent::PlayerLost);
        }
    }

    FSM<EnemyState, EnemyEvent> fsm_;
    GameContext& ctx_;
};
```

### FSM Asset Format (.uzfsm)

```ini
# res://ai/enemy_ai.uzfsm
[fsm]
id = enemy_ai
initial_state = Idle

[state.Idle]
on_enter_script = res://ai/scripts/enemy_idle.lua
on_update_script = res://ai/scripts/enemy_idle_update.lua
on_exit_script = res://ai/scripts/enemy_idle_exit.lua

[state.Chase]
on_enter_script = res://ai/scripts/enemy_chase.lua
on_update_script = res://ai/scripts/enemy_chase_update.lua

[transition.Idle_to_Chase]
from = Idle
to = Chase
event = PlayerSpotted
guard_script = res://ai/scripts/guard_can_chase.lua

[transition.Chase_to_Patrol]
from = Chase
to = Patrol
event = PlayerLost
```

### Lua FSM API

```lua
-- Define FSM in Lua script
local my_fsm = ctx.gameplay:create_fsm({
    initial_state = "Idle",

    states = {
        Idle = {
            on_enter = function() print("Entering Idle") end,
            on_update = function(dt)
                -- Check for player
                if should_chase() then
                    my_fsm:emit("PlayerSpotted")
                end
            end,
            on_exit = function() print("Leaving Idle") end,
        },

        Chase = {
            on_enter = function() print("Chasing player") end,
            on_update = function(dt)
                move_toward_player()
            end,
        },
    },

    transitions = {
        { from = "Idle", event = "PlayerSpotted", to = "Chase" },
        { from = "Chase", event = "PlayerLost", to = "Idle" },
    },
})

-- Tick FSM each frame
function on_update(dt)
    my_fsm:tick(dt)
end

-- Emit event (trigger transition)
my_fsm:emit("PlayerSpotted")
```

---

## Behavior Trees

### Overview

Behavior Trees (BTs) are a hierarchical, data-driven approach to AI behavior. Unlike FSMs (which are flat state machines), BTs are trees where each node represents a task or decision.

BT nodes execute in a top-down, left-to-right order and return a **status**: Running, Success, or Failure. Parent nodes (Selector, Sequence) decide what to do based on child results.

### Node Types

| Node | Behavior | Returns |
|------|----------|---------|
| **Sequence** | Execute children left-to-right. Return Failure if any child fails; Success if all succeed | Failure on first failure; Running if child is running; Success if all succeed |
| **Selector** | Execute children until one succeeds. Return Success immediately. | Success on first success; Running if child is running; Failure if all fail |
| **Parallel** | Execute all children simultaneously. Return Success if N children succeed | Running if executing; Success if ≥N succeed; Failure if >N fail |
| **Decorator** (invert, repeat, timeout) | Modify child behavior | Depends on decorator |
| **Leaf** | Action or condition (e.g., "move to target", "is enemy in sight") | Running, Success, or Failure |

### C++ API

```cpp
// src/game/behavior_tree.h

enum class BTStatus {
    Running = 0,
    Success = 1,
    Failure = 2,
};

class BehaviorTreeNode {
public:
    virtual ~BehaviorTreeNode() = default;
    virtual BTStatus tick(float dt) = 0;
    virtual void reset() {}  // Called when exiting the node
};

class BTreeSequence : public BehaviorTreeNode {
    // Children execute in order. Fail on first failure.
    std::vector<std::unique_ptr<BehaviorTreeNode>> children;
    BTStatus tick(float dt) override;
};

class BTreeSelector : public BehaviorTreeNode {
    // Children execute in order. Succeed on first success.
    std::vector<std::unique_ptr<BehaviorTreeNode>> children;
    BTStatus tick(float dt) override;
};

class BTreeLeaf : public BehaviorTreeNode {
    // Action or condition node
    std::function<BTStatus(float dt)> callback;
    BTStatus tick(float dt) override;
};

class BehaviorTree {
public:
    explicit BehaviorTree(std::unique_ptr<BehaviorTreeNode> root);
    BTStatus tick(float dt);
    void reset();

private:
    std::unique_ptr<BehaviorTreeNode> root_;
};
```

### Example: NPC Patrol & Chase BT

```cpp
// Pseudocode for patrol + chase behavior tree

auto root = std::make_unique<BTreeSelector>();

// Branch 1: Chase player if visible
auto chase_branch = std::make_unique<BTreeSequence>();
chase_branch->add_child(std::make_unique<BTreeLeaf>(
    [this](float dt) {
        return can_see_player() ? BTStatus::Success : BTStatus::Failure;
    }
));
chase_branch->add_child(std::make_unique<BTreeLeaf>(
    [this](float dt) {
        move_toward_player();
        return BTStatus::Running;  // Chase is continuous
    }
));
root->add_child(std::move(chase_branch));

// Branch 2: Patrol (fallback)
auto patrol_branch = std::make_unique<BTreeLeaf>(
    [this](float dt) {
        patrol_next_waypoint();
        return BTStatus::Running;
    }
);
root->add_child(std::move(patrol_branch));

bt_ = std::make_unique<BehaviorTree>(std::move(root));
```

### BT Asset Format (.uzbt)

Behavior trees are defined in INI format and loaded as assets:

```ini
# res://ai/npc_patrol.uzbt
[tree]
id = npc_patrol
root_node = Selector_Root

[node.Selector_Root]
type = Selector
children = [ChaseIfVisible, Patrol]

[node.ChaseIfVisible]
type = Sequence
children = [CanSeePlayer, ChasePlayer]

[node.CanSeePlayer]
type = Leaf
action = is_player_visible
# Guard: only execute if player is visible

[node.ChasePlayer]
type = Leaf
action = move_to_player
# Continuous action; returns Running

[node.Patrol]
type = Leaf
action = patrol_next_waypoint
```

### Lua BT API

BTs are typically defined in C++ (`.uzbt` files), but can be instantiated and ticked from Lua:

```lua
-- In NPC script
local bt = ctx.gameplay:load_behavior_tree("res://ai/npc_patrol.uzbt")

function on_update(dt)
    local status = bt:tick(dt)

    if status == "Success" then
        print("Behavior complete")
    elseif status == "Failure" then
        print("Behavior failed")
    else
        print("Behavior running")
    end
end
```

### Blackboard Component

BTs share data via a Blackboard — a simple key-value store on the entity:

```cpp
struct CBlackboardComponent {
    std::unordered_map<std::string, Variant> data;  // "target_pos", "enemy_count", etc.
};

// In BT leaf node:
auto move_to_target = std::make_unique<BTreeLeaf>(
    [this](float dt) -> BTStatus {
        auto& bb = this->entity.get<CBlackboardComponent>();
        Vec3 target_pos = bb.data["target_pos"].as<Vec3>();

        move_toward(target_pos);

        if (distance_to(target_pos) < 0.1f) {
            return BTStatus::Success;
        }
        return BTStatus::Running;
    }
);
```

---

## Performance & Hot-Reload

### System Scheduler Hot-Reload

When game.dll is reloaded:
1. All SystemHandle pointers become invalid
2. SystemScheduler is recreated in the new game.dll
3. Systems re-register on construction

No loss of entity data — only the C++ system instances are recreated.

### FSM & BT Hot-Reload

FSM and BT assets (`.uzfsm`, `.uzbt` files) are hot-reloadable. When a file changes:
1. The asset is re-parsed
2. New FSM/BT instances are created for affected entities
3. State/position is preserved if the state names match

### Profiling

The editor can query system performance via IPC:

```python
# VSCode Command: /uz call scheduler.profile

{
    "systems": [
        {"name": "InputSystem", "time_ms": 0.15},
        {"name": "PhysicsSystem", "time_ms": 2.3},
        {"name": "DamageResolveSystem", "time_ms": 0.42},
    ],
    "total_frame_time_ms": 16.7,
    "frame_budget_exceeded": false
}
```

---

## Test Coverage

### Unit Tests (C++)

```cpp
// test/gameplay_systems_test.cpp

TEST(SystemScheduler, RegistersAndExecutesInOrder) {
    SystemScheduler sched;

    std::vector<std::string> exec_order;
    auto system_a = sched.register_system("A", SystemGroup::PrePhysics, 0);
    auto system_b = sched.register_system("B", SystemGroup::PrePhysics, 1);

    // A should execute before B (priority 0 < 1)
    EXPECT_THAT(exec_order, ElementsAre("A", "B"));
}

TEST(AbilitySystem, ActivationFailsOnCooldown) {
    AbilitySystem ability_sys(ctx_);
    NodeHandle node = create_test_node();

    ability_sys.grant_ability(node, "fireball");
    auto result1 = ability_sys.try_activate({node, "fireball", Vec3(0,0,0)});
    EXPECT_EQ(result1, AbilityActivationResult::Success);

    auto result2 = ability_sys.try_activate({node, "fireball", Vec3(0,0,0)});
    EXPECT_EQ(result2, AbilityActivationResult::OnCooldown);
}

TEST(FSM, TransitionsOnEvent) {
    FSM<EnemyState, EnemyEvent> fsm(EnemyState::Idle);
    fsm.transition(EnemyState::Idle, EnemyEvent::PlayerSpotted, EnemyState::Chase);

    EXPECT_EQ(fsm.current_state(), EnemyState::Idle);
    fsm.emit_event(EnemyEvent::PlayerSpotted);
    EXPECT_EQ(fsm.current_state(), EnemyState::Chase);
}

TEST(BehaviorTree, SelectorSucceedsOnFirstSuccess) {
    auto selector = std::make_unique<BTreeSelector>();

    // First child fails
    selector->add_child(std::make_unique<BTreeLeaf>(
        [](float) { return BTStatus::Failure; }
    ));

    // Second child succeeds
    selector->add_child(std::make_unique<BTreeLeaf>(
        [](float) { return BTStatus::Success; }
    ));

    BehaviorTree bt(std::move(selector));
    EXPECT_EQ(bt.tick(0.016f), BTStatus::Success);
}
```

### Integration Tests (Lua)

```lua
-- test/gameplay_systems_test.lua

describe("Gameplay Systems", function()
    it("should grant and activate ability", function()
        local node = create_test_node()
        ctx.gameplay:grant_ability(node, "fireball")

        local result = ctx.gameplay:try_activate({
            activator = node,
            ability_id = "fireball",
            position = Vec3(0, 0, 0),
        })

        assert_equals(result, "Success")
    end)

    it("should trigger AbilityActivated event", function()
        local node = create_test_node()
        ctx.gameplay:grant_ability(node, "fireball")

        local event_fired = false
        ctx.events:subscribe("AbilityActivated", function(ev)
            if ev.ability_id == "fireball" then
                event_fired = true
            end
        end)

        ctx.gameplay:try_activate({
            activator = node,
            ability_id = "fireball",
            position = Vec3(0, 0, 0),
        })

        assert_true(event_fired)
    end)

    it("should fail FSM transition with failed guard", function()
        local fsm = ctx.gameplay:create_fsm({
            initial_state = "Idle",
            states = {
                Idle = {},
                Chase = {},
            },
            transitions = {
                {
                    from = "Idle",
                    to = "Chase",
                    event = "Trigger",
                    guard = function() return false end,
                },
            },
        })

        fsm:emit("Trigger")
        assert_equals(fsm:current_state(), "Idle")
    end)
end)
```

---

## Related Docs

- [ECS & Scene System](ecs-advanced.md) — Entity structure, components, registry
- [Event Bus](event-bus.md) — Event dispatch system used by gameplay systems
- [Lua Scripting](lua-scripting.md) — Lua integration and binding patterns
- [System Scheduling](../guides/gameplay-programming-guide.md) — Practical guide to using these systems
