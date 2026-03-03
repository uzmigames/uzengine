## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-ecs-system-scheduler` is complete — `SystemRegistry` and `TickGroup::Update` exist
- [ ] 1.2 Confirm `phase0-data-driven-framework` spec is complete — `.uzbt` format defined
- [ ] 1.3 Confirm `phase1-lua-scripting` is complete — Lua condition/action hooks can be stored as function refs
- [ ] 1.4 Read `docs/specs/data-driven-framework.md` — understand `IAssetImporter` interface

## 2. .uzbt Format

- [ ] 2.1 Define `[meta]` block: `type = uzbt`, `version = 1`, `uuid`
- [ ] 2.2 Define `[tree]` section: `root = <node_id>` — references root node
- [ ] 2.3 Define `[node.<id>]` sections: `type`, `children[]`, type-specific params
  - Composite types: `Sequence`, `Selector`, `Parallel` (mode: `all`/`any`), `RandomSelector`
  - Decorator types: `Inverter`, `Repeater` (count), `RepeatUntilFail`, `Cooldown` (interval_s), `Timeout` (max_s)
  - Leaf types: `Action` (lua_fn: string), `Condition` (lua_fn: string), `Wait` (duration_s), `Log` (message)
- [ ] 2.4 Write example `.uzbt` file: patrol/chase/attack NPC tree

## 3. BtNode Hierarchy

- [ ] 3.1 Create `src/engine/ai/behavior_tree.h`
- [ ] 3.2 Define `BtStatus` enum: `Running`, `Success`, `Failure`
- [ ] 3.3 Define `BtNode` abstract base: `virtual BtStatus tick(BtContext&) = 0`, `virtual void reset() = 0`
- [ ] 3.4 Implement `BtSequence` — ticks children left to right; returns Failure on first child Failure, Success when all succeed
- [ ] 3.5 Implement `BtSelector` — ticks children left to right; returns Success on first child Success, Failure when all fail
- [ ] 3.6 Implement `BtParallel` — ticks all children; mode=all → Success only if all succeed; mode=any → Success if any succeeds
- [ ] 3.7 Implement `BtRandomSelector` — shuffle children each entry, then behave as Selector
- [ ] 3.8 Implement `BtInverter` — inverts child result (Success↔Failure; Running unchanged)
- [ ] 3.9 Implement `BtRepeater(count)` — runs child N times; returns child result on last run
- [ ] 3.10 Implement `BtRepeatUntilFail` — runs child until Failure; returns Success
- [ ] 3.11 Implement `BtCooldown(interval_s)` — returns Failure if called before cooldown expires; else ticks child
- [ ] 3.12 Implement `BtTimeout(max_s)` — returns Failure if child is Running for longer than max_s
- [ ] 3.13 Implement `BtWait(duration_s)` — returns Running for duration, then Success
- [ ] 3.14 Implement `BtCondition(lua_fn_ref)` — calls Lua function; returns Success if truthy, Failure otherwise
- [ ] 3.15 Implement `BtAction(lua_fn_ref)` — calls Lua function; must return `"success"`, `"failure"`, or `"running"` string
- [ ] 3.16 Implement `BtLog(message)` — logs message, returns Success

## 4. BtBlackboard

- [ ] 4.1 Define `BtBlackboard` — `unordered_map<string, variant<bool, int, float, string, Vec3, entt::entity>>`
- [ ] 4.2 Implement `get<T>(key)`, `set<T>(key, value)`, `has(key)`, `remove(key)`
- [ ] 4.3 Blackboard shared per entity; accessible from all nodes via `BtContext`

## 5. BtContext

- [ ] 5.1 Define `BtContext` struct: `registry`, `entity`, `blackboard`, `dt`, `lua_state*`
- [ ] 5.2 `BtContext` passed by reference to every `BtNode::tick` call
- [ ] 5.3 Provide `BtContext::set_blackboard_value(key, lua_value)` for Lua condition/action hooks

## 6. CBehaviorTree Component and BlackboardComponent

- [ ] 6.1 Add `CBehaviorTree` to `ecs/components.h`: `bt_path: string`, `running_node_id: uint32`, `last_status: BtStatus`
- [ ] 6.2 Add `BlackboardComponent` to `ecs/components.h`: wraps `BtBlackboard`
- [ ] 6.3 Update `.uzscene` parser — serialize/deserialize `CBehaviorTree.bt_path`

## 7. BtImporter

- [ ] 7.1 Create `src/engine/assets/bt_importer.h/cpp` — `BtImporter : IAssetImporter`
- [ ] 7.2 Implement `parse(path, ParsedDoc&)` — build `BtNode` tree from `[node.*]` sections
- [ ] 7.3 Implement `BtImporter::get(path) -> shared_ptr<BtNode>` — returns cached root node
- [ ] 7.4 Implement `reimport(path)` — rebuilds node tree; running instances reset to root

## 8. BTSystem ECS System

- [ ] 8.1 Create `src/engine/ai/bt_system.h/cpp` — `BTSystem : IEcsSystem`
- [ ] 8.2 Register with `SystemRegistry` in `Update` group (after physics, before render)
- [ ] 8.3 Implement `BTSystem::tick(registry, dt)`:
  - Iterate all entities with `CBehaviorTree` + `BlackboardComponent`
  - Load root node from `BtImporter::get(bt_path)`
  - Build `BtContext` for entity
  - Call `root->tick(ctx)`
  - Update `CBehaviorTree.last_status`

## 9. Lua API — ctx.blackboard

- [ ] 9.1 Implement `ctx.blackboard:set(key, value)`
- [ ] 9.2 Implement `ctx.blackboard:get(key) -> value?`
- [ ] 9.3 Implement `ctx.blackboard:has(key) -> bool`
- [ ] 9.4 Implement `ctx.blackboard:remove(key)`
- [ ] 9.5 Lua `Action` node return values: `"success"` → Success, `"failure"` → Failure, `"running"` → Running, nil → Failure

## 10. Debug Visualization

- [ ] 10.1 Implement `scene.get_bt_state` IPC handler — returns current node execution path as JSON tree with status per node
- [ ] 10.2 VSCode extension: "BT Debugger" panel WebView — renders tree with color-coded node status (green/red/yellow)
- [ ] 10.3 Update every 100ms while editor connected

## 11. Testing

- [ ] 11.1 Unit test: `BtSequence` — all children succeed → Success; first failure → Failure
- [ ] 11.2 Unit test: `BtSelector` — first child succeeds → Success; all fail → Failure
- [ ] 11.3 Unit test: `BtInverter` — wraps Failure → Success
- [ ] 11.4 Unit test: `BtCooldown` — second tick within interval → Failure without running child
- [ ] 11.5 Unit test: `BtCondition` lua fn returning false → Failure
- [ ] 11.6 Unit test: `BtAction` lua fn returning `"running"` → Running; child `reset()` called on parent re-entry
- [ ] 11.7 Integration test: parse patrol/chase tree from `.uzbt` → BTSystem ticks correctly for 3 frames

## 12. Documentation

- [ ] 12.1 Create `docs/specs/behavior-trees.md` — full spec: node types, `.uzbt` format, blackboard API, debug visualization
- [ ] 12.2 Update `docs/guides/lua-scripting-guide.md` — "Behavior Trees" section with common NPC pattern
