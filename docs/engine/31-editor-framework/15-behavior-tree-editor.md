# Behavior Tree Editor — AI Decision Authoring

> **Status**: Planned (Phase 3–4)
> **Source**: `extension/editor/bt-editor.ts`, `engine/runtime/ai/behavior_tree.h`
> **References**: Node Graph Framework (§40.09), AI Systems, UE5 Behavior Tree Editor, Godot (none built-in), GDevelop Behavior Trees

## Purpose

The Behavior Tree Editor allows game designers to visually compose AI decision logic without scripting. Trees are hierarchical: Composites (Sequence, Selector, Parallel) control flow, Decorators add conditions, and Tasks perform actions. The editor supports real-time debugging with breakpoints, blackboard inspection, and runtime visualization of which nodes are active.

## Architecture

```
Behavior Tree Editor (TypeScript/Webview)
    │
    ├─ Tree Visualization (center canvas, top-down layout)
    │   ├─ Root node (top)
    │   ├─ Child nodes (left-to-right arrangement)
    │   └─ Connections (lines between parent↔child)
    │
    ├─ Node Palette (left sidebar, drag-drop)
    │   ├─ Composites
    │   │   ├─ Sequence (all children in order, stop on first failure)
    │   │   ├─ Selector (try children until one succeeds)
    │   │   └─ Parallel (run all children simultaneously)
    │   │
    │   ├─ Decorators
    │   │   ├─ Cooldown (throttle execution)
    │   │   ├─ Loop (repeat N times)
    │   │   ├─ Blackboard Check (condition: if key == value)
    │   │   ├─ Not (invert child result)
    │   │   └─ Until (repeat until condition true)
    │   │
    │   ├─ Tasks
    │   │   ├─ MoveTo (pathfind to position/entity)
    │   │   ├─ PlayAnimation (play skeletal anim)
    │   │   ├─ Wait (idle for duration)
    │   │   ├─ SetBlackboard (write key=value)
    │   │   └─ Custom (game-specific tasks)
    │   │
    │   └─ Custom Tasks (game can register new ones)
    │
    ├─ Properties Panel (right sidebar)
    │   ├─ Selected node properties
    │   ├─ Execution result (Success/Running/Failure)
    │   └─ Breakpoint toggle
    │
    ├─ Blackboard Panel (bottom)
    │   ├─ Key definitions (type: Object/Float/Bool/String/Vector/Enum)
    │   ├─ Current values (live during runtime)
    │   └─ Add/Delete key buttons
    │
    └─ Debugging Panel (right, runtime)
        ├─ Breakpoints (pause on node execution)
        ├─ Call stack (active nodes being executed)
        ├─ Execution flow (highlight active nodes on tree)
        └─ Blackboard values (live inspection)

Runtime: Behavior Tree (.uzbt JSON) → Load → Tick every frame → Execute nodes → Update blackboard
```

## Node Types

### Composites

Control flow containers:

```cpp
/// Base class for composite nodes (have children).
class CompositeNode : public BehaviorNode {
public:
    std::vector<BehaviorNode*> children;

    virtual void AddChild(BehaviorNode* child) { children.push_back(child); }
    virtual BehaviorResult Execute(BehaviorContext& ctx) = 0;
};

/// Sequence: execute children in order, stop on first failure.
class SequenceNode : public CompositeNode {
public:
    /// Execute children left-to-right.
    /// Returns Success only if all children succeed.
    BehaviorResult Execute(BehaviorContext& ctx) override {
        for (auto child : children) {
            BehaviorResult result = child->Tick(ctx);
            if (result != BehaviorResult::Success) {
                return result;  // Stop on failure or running
            }
        }
        return BehaviorResult::Success;
    }
};

/// Selector: execute children until one succeeds.
class SelectorNode : public CompositeNode {
public:
    /// Try children left-to-right.
    /// Returns Success if any child succeeds.
    BehaviorResult Execute(BehaviorContext& ctx) override {
        for (auto child : children) {
            BehaviorResult result = child->Tick(ctx);
            if (result == BehaviorResult::Success) {
                return BehaviorResult::Success;
            }
            if (result == BehaviorResult::Running) {
                return BehaviorResult::Running;
            }
        }
        return BehaviorResult::Failure;
    }
};

/// Parallel: execute all children simultaneously.
class ParallelNode : public CompositeNode {
public:
    enum class FailurePolicy { FailOnOne, FailOnAll };
    FailurePolicy failure_policy = FailurePolicy::FailOnOne;

    /// Tick all children, report overall result.
    BehaviorResult Execute(BehaviorContext& ctx) override {
        int success_count = 0, failure_count = 0, running_count = 0;

        for (auto child : children) {
            BehaviorResult result = child->Tick(ctx);
            if (result == BehaviorResult::Success) success_count++;
            else if (result == BehaviorResult::Failure) failure_count++;
            else running_count++;
        }

        if (failure_count > 0 && failure_policy == FailurePolicy::FailOnOne) {
            return BehaviorResult::Failure;
        }
        if (running_count > 0) {
            return BehaviorResult::Running;
        }
        return BehaviorResult::Success;
    }
};
```

### Decorators

Modify or gate child execution:

```cpp
/// Base class for decorators (single child with condition/modification).
class DecoratorNode : public BehaviorNode {
public:
    BehaviorNode* child = nullptr;

    virtual void SetChild(BehaviorNode* c) { child = c; }
};

/// Cooldown decorator: throttle how often child executes.
class CooldownDecorator : public DecoratorNode {
public:
    float cooldown_duration = 1.0f;
    float last_execution_time = -999.0f;

    BehaviorResult Execute(BehaviorContext& ctx) override {
        float current_time = ctx.world_time;
        if (current_time - last_execution_time < cooldown_duration) {
            return BehaviorResult::Running;  // Still cooling down
        }

        last_execution_time = current_time;
        return child->Tick(ctx);
    }
};

/// Loop decorator: repeat child N times.
class LoopDecorator : public DecoratorNode {
public:
    int loop_count = 3;
    int current_iteration = 0;

    BehaviorResult Execute(BehaviorContext& ctx) override {
        BehaviorResult result = child->Tick(ctx);

        if (result == BehaviorResult::Success) {
            current_iteration++;
            if (current_iteration >= loop_count) {
                current_iteration = 0;
                return BehaviorResult::Success;
            }
            return BehaviorResult::Running;  // Loop again
        }

        return result;
    }
};

/// Blackboard Check: condition on blackboard value.
class BlackboardCheckDecorator : public DecoratorNode {
public:
    std::string key;
    std::string expected_value;

    BehaviorResult Execute(BehaviorContext& ctx) override {
        std::string actual = ctx.blackboard.Get<std::string>(key);
        if (actual != expected_value) {
            return BehaviorResult::Failure;  // Condition not met
        }

        return child->Tick(ctx);
    }
};

/// Not decorator: invert child result.
class NotDecorator : public DecoratorNode {
public:
    BehaviorResult Execute(BehaviorContext& ctx) override {
        BehaviorResult result = child->Tick(ctx);
        if (result == BehaviorResult::Success) return BehaviorResult::Failure;
        if (result == BehaviorResult::Failure) return BehaviorResult::Success;
        return result;  // Running stays running
    }
};
```

### Tasks

Leaf nodes that perform actions:

```cpp
/// Base class for tasks (leaf nodes, perform actions).
class TaskNode : public BehaviorNode {
public:
    virtual BehaviorResult Execute(BehaviorContext& ctx) = 0;
};

/// Move task: pathfind to position.
class MoveToTask : public TaskNode {
public:
    Vec3 target_position;
    EntityHandle target_entity;  // If set, follow entity instead
    float stop_distance = 1.0f;

    BehaviorResult Execute(BehaviorContext& ctx) override {
        if (target_entity.IsValid()) {
            target_position = ctx.scene->GetEntity(target_entity)->GetPosition();
        }

        Vec3 current_pos = ctx.actor->GetPosition();
        Vec3 direction = (target_position - current_pos).Normalize();
        float distance = (target_position - current_pos).Length();

        if (distance < stop_distance) {
            return BehaviorResult::Success;
        }

        // Request pathfind (non-blocking)
        ctx.actor->MoveTo(target_position, 5.0f);  // 5 m/s speed

        return BehaviorResult::Running;  // Still moving
    }
};

/// Play Animation task.
class PlayAnimationTask : public TaskNode {
public:
    AssetRef animation;
    float play_rate = 1.0f;
    bool wait_for_completion = true;

    BehaviorResult Execute(BehaviorContext& ctx) override {
        if (ctx.actor->GetCurrentAnimation() != animation) {
            ctx.actor->PlayAnimation(animation, play_rate);
        }

        if (!wait_for_completion) {
            return BehaviorResult::Success;
        }

        if (ctx.actor->IsAnimationPlaying()) {
            return BehaviorResult::Running;
        }

        return BehaviorResult::Success;
    }
};

/// Wait task: idle for duration.
class WaitTask : public TaskNode {
public:
    float wait_duration = 1.0f;
    float elapsed_time = 0.0f;

    BehaviorResult Execute(BehaviorContext& ctx) override {
        elapsed_time += ctx.delta_time;

        if (elapsed_time >= wait_duration) {
            elapsed_time = 0.0f;
            return BehaviorResult::Success;
        }

        return BehaviorResult::Running;
    }
};

/// Set Blackboard task: write value to blackboard.
class SetBlackboardTask : public TaskNode {
public:
    std::string key;
    std::string value;

    BehaviorResult Execute(BehaviorContext& ctx) override {
        ctx.blackboard.Set(key, value);
        return BehaviorResult::Success;
    }
};
```

## Blackboard

Shared memory for AI decision-making:

```cpp
/// Blackboard: key-value store for AI state.
class Blackboard {
public:
    /// Define a key (type info for validation).
    void DefineKey(const std::string& key, BlackboardType type) {
        key_types[key] = type;
    }

    /// Set value (any type, runtime checked).
    template<typename T>
    void Set(const std::string& key, const T& value) {
        values[key] = std::any(value);
    }

    /// Get value.
    template<typename T>
    T Get(const std::string& key, const T& default_value = T()) {
        auto it = values.find(key);
        if (it == values.end()) return default_value;

        try {
            return std::any_cast<T>(it->second);
        } catch (const std::bad_any_cast&) {
            UZ_LOG(AI, Warning, "Blackboard type mismatch for key '{}'", key);
            return default_value;
        }
    }

    /// All keys for inspection.
    const std::map<std::string, std::any>& GetAllValues() const {
        return values;
    }

private:
    std::map<std::string, BlackboardType> key_types;
    std::map<std::string, std::any> values;
};
```

## .uzbt File Format

Trees save as hierarchical JSON:

```json
{
  "name": "goblin_patrol_ai",
  "description": "Patrol area, chase player on sight, retreat if health low",
  "blackboard_keys": [
    {
      "name": "patrol_waypoints",
      "type": "Vector",
      "default": [0, 0, 0]
    },
    {
      "name": "patrol_index",
      "type": "Int",
      "default": 0
    },
    {
      "name": "target_entity",
      "type": "Object",
      "default": null
    },
    {
      "name": "player_visible",
      "type": "Bool",
      "default": false
    },
    {
      "name": "health_low",
      "type": "Bool",
      "default": false
    }
  ],
  "root_node": {
    "type": "Selector",
    "id": "root",
    "children": [
      {
        "type": "Sequence",
        "id": "retreat_sequence",
        "children": [
          {
            "type": "BlackboardCheck",
            "id": "check_low_health",
            "key": "health_low",
            "expected_value": "true"
          },
          {
            "type": "MoveTo",
            "id": "retreat_move",
            "target_position": [0, 0, -20],
            "stop_distance": 1.0
          },
          {
            "type": "PlayAnimation",
            "id": "retreat_anim",
            "animation": "res://animations/goblin_run.uzanim"
          }
        ]
      },
      {
        "type": "Sequence",
        "id": "chase_sequence",
        "children": [
          {
            "type": "BlackboardCheck",
            "id": "check_see_player",
            "key": "player_visible",
            "expected_value": "true"
          },
          {
            "type": "MoveTo",
            "id": "chase_move",
            "target_entity": "player",
            "stop_distance": 2.0
          },
          {
            "type": "PlayAnimation",
            "id": "attack_anim",
            "animation": "res://animations/goblin_attack.uzanim",
            "wait_for_completion": true
          }
        ]
      },
      {
        "type": "Sequence",
        "id": "patrol_sequence",
        "children": [
          {
            "type": "MoveTo",
            "id": "patrol_move",
            "target_position": [0, 0, 10],
            "stop_distance": 1.0
          },
          {
            "type": "Wait",
            "id": "patrol_wait",
            "wait_duration": 3.0
          }
        ]
      }
    ]
  }
}
```

## Debugging Features

Real-time inspection during gameplay:

```typescript
/// Behavior tree debugger (runtime inspection).
export class BehaviorTreeDebugger {
    private breakpoints: Set<string> = new Set();  // node IDs
    private callStack: string[] = [];              // Active node path
    private blackboardSnapshot: Map<string, any> = new Map();

    /// Set breakpoint on node.
    toggleBreakpoint(nodeId: string) {
        if (this.breakpoints.has(nodeId)) {
            this.breakpoints.delete(nodeId);
        } else {
            this.breakpoints.add(nodeId);
        }
        this.ipc.call("bt.set_breakpoint", [nodeId, this.breakpoints.has(nodeId)]);
    }

    /// Update call stack on each node execution.
    async onNodeExecute(nodeId: string, result: ExecutionResult) {
        // Check breakpoint
        if (this.breakpoints.has(nodeId)) {
            await this.ipc.call("bt.pause", []);
            console.log(`[Breakpoint] ${nodeId} → ${result}`);
        }

        // Update call stack visualization
        this.updateCallStackUI(nodeId, result);

        // Fetch blackboard snapshot
        this.blackboardSnapshot = await this.ipc.call("bt.get_blackboard", []);
        this.updateBlackboardUI();

        // Highlight active node on tree
        this.highlightNode(nodeId);
    }

    private updateCallStackUI(nodeId: string, result: ExecutionResult) {
        const stack = document.querySelector('.call-stack')!;
        const item = document.createElement('div');
        item.textContent = `${nodeId} → ${result}`;
        item.style.color = result === 'Success' ? '#0f0' : result === 'Failure' ? '#f00' : '#ff0';
        stack.appendChild(item);
    }

    private updateBlackboardUI() {
        const panel = document.querySelector('.blackboard-panel')!;
        panel.innerHTML = '<h3>Blackboard</h3>';

        for (const [key, value] of this.blackboardSnapshot) {
            const row = document.createElement('div');
            row.className = 'blackboard-row';
            row.innerHTML = `<strong>${key}:</strong> ${JSON.stringify(value)}`;
            panel.appendChild(row);
        }
    }

    private highlightNode(nodeId: string) {
        // Remove old highlights
        document.querySelectorAll('.bt-node.active').forEach(el => {
            el.classList.remove('active');
        });

        // Highlight current node
        const node = document.getElementById(`node_${nodeId}`);
        if (node) {
            node.classList.add('active');
        }
    }
}
```

## Tree Visualization

Canvas-based tree layout:

```typescript
/// Visual tree layout and rendering.
export class BehaviorTreeRenderer {
    private canvas: HTMLCanvasElement;
    private nodes: Map<string, BehaviorNode> = new Map();
    private selectedNode: BehaviorNode | null = null;

    constructor(container: HTMLElement) {
        this.canvas = document.createElement('canvas');
        this.canvas.width = 1200;
        this.canvas.height = 800;
        this.canvas.className = 'bt-canvas';
        container.appendChild(this.canvas);

        this.setupInput();
    }

    /// Render tree (recursive layout).
    render() {
        const ctx = this.canvas.getContext('2d')!;
        ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

        if (!this.rootNode) return;

        // Layout nodes using recursive positioning
        this.layoutNode(this.rootNode, this.canvas.width / 2, 40, this.canvas.width / 4);

        // Draw edges
        ctx.strokeStyle = '#666';
        ctx.lineWidth = 2;
        for (const [id, node] of this.nodes) {
            if (!node.parent) continue;
            ctx.beginPath();
            ctx.moveTo(node.parent.x, node.parent.y);
            ctx.lineTo(node.x, node.y);
            ctx.stroke();
        }

        // Draw nodes
        for (const [id, node] of this.nodes) {
            this.drawNode(node);
        }
    }

    private layoutNode(node: BehaviorNode, x: number, y: number, offset: number) {
        node.x = x;
        node.y = y;

        if (node.children && node.children.length > 0) {
            const childOffset = offset / 2;
            const childSpacing = (offset * 2) / node.children.length;

            node.children.forEach((child, i) => {
                const childX = x - offset + childSpacing * i;
                const childY = y + 60;
                this.layoutNode(child, childX, childY, childOffset);
            });
        }
    }

    private drawNode(node: BehaviorNode) {
        const ctx = this.canvas.getContext('2d')!;

        // Color by node type
        let color = '#444';
        if (node instanceof SequenceNode) color = '#0066ff';
        else if (node instanceof SelectorNode) color = '#ff6600';
        else if (node instanceof ParallelNode) color = '#00ff66';
        else if (node instanceof DecoratorNode) color = '#ff00ff';
        else if (node instanceof TaskNode) color = '#ffff00';

        // Highlight if active
        if (node.classList?.contains('active')) {
            color = '#00ff00';
            ctx.lineWidth = 3;
        } else {
            ctx.lineWidth = 2;
        }

        // Draw circle
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(node.x, node.y, 25, 0, Math.PI * 2);
        ctx.fill();

        // Draw border
        ctx.strokeStyle = '#fff';
        ctx.stroke();

        // Draw text label
        ctx.fillStyle = '#000';
        ctx.font = '11px Arial';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(node.type.substring(0, 3).toUpperCase(), node.x, node.y);
    }

    private setupInput() {
        this.canvas.onmousedown = (e) => {
            const rect = this.canvas.getBoundingClientRect();
            const x = e.clientX - rect.left;
            const y = e.clientY - rect.top;

            // Hit-test nodes
            for (const [id, node] of this.nodes) {
                const dist = Math.hypot(node.x - x, node.y - y);
                if (dist < 25) {
                    this.selectNode(node);
                    return;
                }
            }
        };

        // Drag canvas to pan
        this.canvas.onmousedown = (e) => {
            const startX = e.clientX;
            const startY = e.clientY;
            const offsetX = this.canvas.style.left || '0';
            const offsetY = this.canvas.style.top || '0';

            const onMove = (e: MouseEvent) => {
                const dx = e.clientX - startX;
                const dy = e.clientY - startY;
                this.canvas.style.left = `${parseInt(offsetX) + dx}px`;
                this.canvas.style.top = `${parseInt(offsetY) + dy}px`;
            };

            const onUp = () => {
                document.removeEventListener('mousemove', onMove);
                document.removeEventListener('mouseup', onUp);
            };

            document.addEventListener('mousemove', onMove);
            document.addEventListener('mouseup', onUp);
        };
    }

    private selectNode(node: BehaviorNode) {
        this.selectedNode = node;
        this.showPropertiesPanel(node);
        this.render();  // Highlight selected
    }

    private showPropertiesPanel(node: BehaviorNode) {
        const panel = document.querySelector('.properties-panel')!;
        panel.innerHTML = `
            <h3>${node.type}</h3>
            <p>ID: ${node.id}</p>
            <p>Result: ${node.lastResult || 'Not executed'}</p>
            <button onclick="toggleBreakpoint('${node.id}')">Toggle Breakpoint</button>
        `;
    }
}
```

## Comparison

| Feature | UE5 (Behavior Tree) | Godot (none built-in) | **UzEngine** |
|---|---|---|---|
| **Visual editor** | Yes (similar layout) | N/A (use GDScript) | **Yes (canvas-based)** |
| **Node types** | 15+ (Services, Decorators, Tasks) | N/A | **9 types (all essentials)** |
| **Blackboard** | Full key-value store | N/A | **Yes (type-safe)** |
| **Breakpoints** | Limited | N/A | **Yes (full debugging)** |
| **Execution visualization** | Yes, shows active nodes | N/A | **Yes (highlight + call stack)** |
| **File format** | Binary .uasset | N/A | **.uzbt (JSON)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Tree visualization, 5 node types (Sequence, Selector, Parallel, Wait, MoveTo), blackboard panel, .uzbt JSON serialization |
| **Phase 4** | All 9 node types, breakpoints, runtime debugging (call stack + blackboard inspection), execution flow highlighting, custom task registration, decorator nodes |
| **Phase 4+** | Service nodes (background tasks), tree composition/inheritance, visual behavior tree debugging overlays, performance profiling (node execution time) |

