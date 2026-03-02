# Proposal: phase2-behavior-trees

## Why

NPC AI requires structured decision logic (patrol, chase, attack, flee) that is more maintainable than nested Lua if/else chains but more predictable than neural networks. Behavior trees are the industry standard for game AI: composable, debuggable, and easy for AI tools to generate. A `.uzbt` data-driven format — parsed by the data-driven framework — combined with a native C++ `BTSystem` ECS system provides both performance and AI-tooling compatibility.

## What Changes

- **`src/engine/ai/behavior_tree.h/cpp`** — `BtNode` hierarchy, `BtBlackboard`, `BtContext`
- **`src/engine/ai/bt_system.h/cpp`** — `BTSystem` ECS system that ticks all `CBehaviorTree` components
- **`src/engine/assets/bt_importer.h/cpp`** — `.uzbt` format parser
- **`ecs/components.h`** — `CBehaviorTree` component, `BlackboardComponent`
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.blackboard:set/get` Lua API for condition/action Lua hooks
- **Debug visualization** — IPC-driven tree state overlay in editor viewport

## Impact

- Affected specs: new `docs/specs/behavior-trees.md`, `docs/specs/data-driven-framework.md` (`.uzbt` format)
- Dependencies: `phase1-ecs-system-scheduler` (BTSystem registration), `phase0-data-driven-framework` (format), `phase1-lua-scripting` (Lua condition/action hooks)
- Breaking change: NO — additive; existing NPC Lua scripts unchanged
- User benefit: structured, debuggable NPC AI; AI tools can generate `.uzbt` files; visual debug tree in editor
