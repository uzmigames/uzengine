# Behavior Trees (AI Decision Making)

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/gameplay/behavior_tree/`
> **References**: Unreal Behavior Tree system (clean-room), behavior3js, behaviour3

## Purpose

Node-based AI decision trees for NPC behavior. Behaviors are authored as JSON (`.uzbt` files) and executed by a `BehaviorTreeComponent` every frame. Supports: composite nodes (Sequence, Selector, Parallel), decorators (conditions), tasks (actions), and services (periodic updates).

## Architecture

```
BehaviorTreeComponent (per NPC)
  ├── Blackboard (shared key-value store)
  └── Root BehaviorNode
      ├── Selector "Chase or Patrol"
       │   ├── Sequence "Chase if see player"
       │   │   ├── Service "Update target location"
       │   │   ├── Decorator "Has valid target"
       │   │   └── Task "MoveTo target"
       │   └── Task "Patrol waypoints"
       └── Task "Idle"
```

## Node Types

### Composite Nodes

```cpp
namespace uz::behavior {

/// Base node type — all nodes implement Execute(dt) returning Status
enum class Status { Running, Success, Failure };

/// **Sequence** — AND logic. Executes children in order.
/// Returns Success if all succeed, Failure if any fails, Running if any running.
struct Sequence : public BehaviorNode {
    Status Execute(float dt) override;
};

/// **Selector** — OR logic. Tries children in order until one succeeds.
/// Returns Success if any succeed, Failure if all fail, Running if best is running.
struct Selector : public BehaviorNode {
    Status Execute(float dt) override;
};

/// **Parallel** — Executes all children simultaneously.
/// completion_rule: All (success if all succeed) or Any (success if any succeed)
struct Parallel : public BehaviorNode {
    enum CompletionRule { All, Any } completion_rule = CompletionRule::All;
    Status Execute(float dt) override;
};

}  // namespace uz::behavior
```

### Decorator Nodes

Decorators wrap a single child and modify its behavior:

```cpp
namespace uz::behavior {

/// Base decorator — modifies child's return status or execution
struct Decorator : public BehaviorNode {
    BehaviorNode* child = nullptr;
    virtual Status Execute(float dt) override;
};

/// **Condition Decorator** — Child only executes if blackboard condition is true
struct Condition : public Decorator {
    std::string key;           ///< Blackboard key to check
    std::string expected_value; ///< Expected value (true, false, or string)
    Status Execute(float dt) override;
};

/// **Cooldown Decorator** — Prevents child execution for duration after completion
struct Cooldown : public Decorator {
    float duration = 1.0f;     ///< Cooldown in seconds
    float remaining = 0.0f;    ///< Remaining cooldown
    Status Execute(float dt) override;
};

/// **Loop Decorator** — Repeats child execution N times or forever
struct Loop : public Decorator {
    int count = -1;            ///< -1 = infinite, >0 = N times
    int executed = 0;
    Status Execute(float dt) override;
};

/// **Force Success/Failure** — Always return Success/Failure regardless of child result
struct ForceSuccess : public Decorator {
    Status Execute(float dt) override { return (child->Execute(dt), Status::Success); }
};

}  // namespace uz::behavior
```

### Task Nodes

Leaf nodes that perform actual actions:

```cpp
namespace uz::behavior {

/// Base task — performs action and returns status
struct Task : public BehaviorNode {
    virtual Status Execute(float dt) override = 0;
};

/// **MoveTo Task** — Navigate agent to destination using NavMesh
struct MoveTo : public Task {
    std::string destination_key;  ///< Blackboard key containing Vec3 destination
    float arrival_distance = 100.0f;
    Status Execute(float dt) override;
};

/// **Wait Task** — Delay for duration
struct Wait : public Task {
    float duration = 1.0f;
    float elapsed = 0.0f;
    Status Execute(float dt) override;
};

/// **PlayAnimation Task** — Trigger animation on entity
struct PlayAnimation : public Task {
    std::string animation_name;
    bool wait_for_completion = false;
    Status Execute(float dt) override;
};

/// **Custom Task** — User-defined behavior via Lua callback
struct CustomTask : public Task {
    std::string lua_function;  ///< Lua function name to call
    Status Execute(float dt) override;
};

}  // namespace uz::behavior
```

### Service Nodes

Nodes that update blackboard values periodically:

```cpp
namespace uz::behavior {

/// Base service — executes every N seconds to update blackboard
struct Service : public BehaviorNode {
    float tick_interval = 0.5f;
    float time_since_last_tick = 0.0f;

    virtual void OnTick() = 0;  ///< Override to update blackboard

    void Execute(float dt) override;
};

/// **Update Target Location Service** — Every 0.5s, refresh target location
struct UpdateTargetService : public Service {
    std::string target_key;
    std::string target_location_key;
    void OnTick() override;
};

}  // namespace uz::behavior
```

## Blackboard

Shared per-behavior-tree key-value store. All nodes read/write to it:

```cpp
namespace uz::behavior {

/// Shared state for all nodes in a behavior tree
class Blackboard {
public:
    /// Set value (templated for any type)
    template<typename T>
    void Set(StringView key, const T& value);

    template<typename T>
    T Get(StringView key) const;

    bool Has(StringView key) const;
    void Clear(StringView key);

    /// Used internally for serialization
    void DebugPrint() const;

private:
    std::unordered_map<std::string, std::any> values_;
};

}  // namespace uz::behavior
```

## `.uzbt` Format (JSON)

```json
{
    "behavior_id": "goblin_behavior",
    "root_node": {
        "type": "Selector",
        "children": [
            {
                "type": "Sequence",
                "name": "Chase player if visible",
                "children": [
                    {
                        "type": "Service",
                        "service_type": "UpdateTarget",
                        "tick_interval": 0.5
                    },
                    {
                        "type": "Condition",
                        "key": "has_player_target",
                        "value": true
                    },
                    {
                        "type": "Task",
                        "task_type": "MoveTo",
                        "destination_key": "player_location",
                        "arrival_distance": 100.0
                    }
                ]
            },
            {
                "type": "Task",
                "task_type": "Patrol",
                "waypoints": ["patrol_1", "patrol_2", "patrol_3"]
            }
        ]
    }
}
```

## BehaviorTreeComponent

```cpp
namespace uz {

/// Executes a behavior tree (.uzbt) every frame
class BehaviorTreeComponent : public ActorComponent {
public:
    UZ_COMPONENT(BehaviorTreeComponent);

    /// Load and start behavior tree
    void StartBehavior(StringView behavior_path);

    /// Stop current behavior
    void StopBehavior();

    /// Access blackboard (read/write shared state)
    uz::behavior::Blackboard& GetBlackboard() { return blackboard_; }
    const uz::behavior::Blackboard& GetBlackboard() const { return blackboard_; }

    /// Called by engine each frame
    void OnTick(float dt) override;

    /// Fired when tree completes (root returns Success or Failure)
    MulticastDelegate<void(bool success)> OnBehaviorComplete;

private:
    std::unique_ptr<uz::behavior::BehaviorNode> root_;
    uz::behavior::Blackboard blackboard_;
};

}  // namespace uz
```

## Lua Integration

Behavior trees can call Lua functions for custom logic:

```lua
-- Called by behavior tree's CustomTask
function on_attack()
    print("Attacking player!")
    return "success"  -- success, failure, or running
end

-- Update blackboard in Lua
function on_update_target()
    local player = scene:find_entity_by_name("Player")
    if player then
        entity:behavior_tree():get_blackboard():set("has_player_target", true)
        entity:behavior_tree():get_blackboard():set("player_location", player:position())
    else
        entity:behavior_tree():get_blackboard():set("has_player_target", false)
    end
end
```

## Execution Example

```cpp
// NPC behavior tree execution per frame
void BehaviorTreeComponent::OnTick(float dt) {
    if (!root_) return;

    uz::behavior::Status status = root_->Execute(dt);

    switch (status) {
        case uz::behavior::Status::Running:
            // Tree is still executing, keep going
            break;
        case uz::behavior::Status::Success:
            OnBehaviorComplete.Broadcast(true);
            // Loop behavior or transition to new tree
            break;
        case uz::behavior::Status::Failure:
            OnBehaviorComplete.Broadcast(false);
            break;
    }
}
```

## Common Patterns

### 1. Chase or Patrol AI

```json
{
    "type": "Selector",
    "children": [
        {
            "type": "Sequence",
            "children": [
                {"type": "Condition", "key": "sees_player"},
                {"type": "Task", "task_type": "MoveTo", "destination_key": "player_pos"}
            ]
        },
        {"type": "Task", "task_type": "Patrol"}
    ]
}
```

### 2. Ranged Attack with Cooldown

```json
{
    "type": "Sequence",
    "children": [
        {"type": "Condition", "key": "in_range"},
        {
            "type": "Cooldown",
            "duration": 2.0,
            "child": {"type": "Task", "task_type": "PlayAnimation", "name": "attack"}
        }
    ]
}
```

### 3. Repeating Patrol Loop

```json
{
    "type": "Loop",
    "count": -1,
    "child": {
        "type": "Sequence",
        "children": [
            {"type": "Task", "task_type": "MoveTo", "destination": "waypoint_1"},
            {"type": "Wait", "duration": 1.0},
            {"type": "Task", "task_type": "MoveTo", "destination": "waypoint_2"}
        ]
    }
}
```

## Comparison

| Feature | UE5 Behavior Tree | Godot AINodes | **UzEngine** |
|---------|------------------|---------------|--------------|
| Format | .uasset (binary) | GDScript nodes | JSON .uzbt |
| Execution | Hierarchical task planner | FSM-like | Tree traversal each frame |
| Blackboard | Per-instance | State variables | Key-value store |
| Services | Per-node periodic updates | Signal-based | tick_interval scheduling |
| Decorators | Extensive (10+ types) | Limited | Condition, Cooldown, Loop, Force* |
| Lua integration | Via Blueprint calls | Native | Callback functions |

## Implementation Phase

- **Phase 3**: Composite nodes (Sequence, Selector), basic tasks (MoveTo, Wait, PlayAnimation), Blackboard
- **Phase 4**: Decorators (Condition, Cooldown, Loop), Services, JSON loading, Lua callbacks
- **Phase 5**: Advanced decorators (parallel completion rules), EQS-style spatial queries, visual editor in VSCode
