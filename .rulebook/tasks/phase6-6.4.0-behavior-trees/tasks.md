## 1. BT Core Framework

- [ ] 1.1 Create `runtime/engine/ai/bt_node.h` — BTNode base: virtual Tick(entity, blackboard, dt) → BTStatus { Success, Failure, Running }. virtual OnEnter(), OnExit(). node_name (string), unique_id. Children owned via vector<unique_ptr<BTNode>>.
- [ ] 1.2 Create `runtime/engine/ai/bt_tree.h/.cpp` — BehaviorTree: owns root BTNode*. Tick(entity, blackboard, dt): ticks root, manages Running state across frames. Reset(): clears all node state.

## 2. Composite Nodes

- [ ] 2.1 Create `runtime/engine/ai/nodes/bt_sequence.h/.cpp` — Sequence: ticks children left-to-right. If child returns Failure → return Failure. If Running → return Running (resume from same child next frame). If all Success → return Success.
- [ ] 2.2 Create `runtime/engine/ai/nodes/bt_selector.h/.cpp` — Selector: ticks children left-to-right. If child returns Success → return Success. If Running → return Running. If all Failure → return Failure.
- [ ] 2.3 Create `runtime/engine/ai/nodes/bt_parallel.h/.cpp` — Parallel: ticks ALL children. Policy: RequireOne (any success → success), RequireAll (all success → success). Failure policy mirrors success policy.

## 3. Decorator Nodes

- [ ] 3.1 Create `runtime/engine/ai/nodes/bt_cooldown.h/.cpp` — Cooldown(duration): after child returns Success/Failure, blocks for N seconds. Returns Failure during cooldown.
- [ ] 3.2 Create `runtime/engine/ai/nodes/bt_loop.h/.cpp` — Loop(count): repeats child N times (0=infinite). Returns Success after count completions. Returns Failure if child fails.
- [ ] 3.3 Create `runtime/engine/ai/nodes/bt_conditional.h/.cpp` — Conditional(key, op, value): checks blackboard. If condition false → return Failure without ticking child. Ops: GT, LT, EQ, NEQ, IsSet.
- [ ] 3.4 Create `runtime/engine/ai/nodes/bt_force_result.h/.cpp` — ForceSuccess: wraps child, always returns Success. ForceFailure: wraps child, always returns Failure. Inverter: swaps Success↔Failure.

## 4. Blackboard

- [ ] 4.1 Create `runtime/engine/ai/blackboard.h/.cpp` — Blackboard: hash map of string→Variant. Variant: Float, Int, Bool, Vec3, EntityHandle, String. SetFloat/GetFloat, SetBool/GetBool, SetVec3/GetVec3, SetEntity/GetEntity, SetString/GetString. HasKey(name), ClearKey(name). Observer callbacks: OnValueChanged(key, callback).

## 5. Built-in Action Nodes

- [ ] 5.1 Create MoveTo node — `bt_move_to.h/.cpp`: reads "target_position" (Vec3) or "target_entity" (EntityHandle) from blackboard. Calls CNavMeshAgent::SetDestination(). Returns Running while moving, Success on arrival, Failure if no path.
- [ ] 5.2 Create Wait node — `bt_wait.h/.cpp`: waits for duration seconds. Returns Running until elapsed, then Success.
- [ ] 5.3 Create PlayAnimation node — `bt_play_animation.h/.cpp`: reads "animation_name" from blackboard or node param. Calls AnimationPlayer::Play(). Returns Running until clip ends, then Success.
- [ ] 5.4 Create PlaySound node — `bt_play_sound.h/.cpp`: reads "sound_path" from blackboard. Calls AudioEngine::PlayOneShot(). Returns Success immediately (fire-and-forget).
- [ ] 5.5 Create SetBlackboard node — `bt_set_blackboard.h/.cpp`: sets a blackboard key to a specified value. Returns Success.

## 6. Built-in Condition Nodes

- [ ] 6.1 Create IsInRange condition — checks distance between entity and blackboard "target" < range. Returns Success if in range, Failure otherwise.
- [ ] 6.2 Create HasLineOfSight condition — raycasts from entity to target. Returns Success if clear, Failure if blocked.
- [ ] 6.3 Create IsBlackboardSet condition — checks if a blackboard key exists and is non-default.

## 7. .uzbt Format & Loader

- [ ] 7.1 Create `runtime/engine/ai/bt_loader.h/.cpp` — Load .uzbt JSON: recursive parse of node tree. Each node: { "type": "Sequence", "children": [...], "params": { ... } }. Factory pattern: type string → BTNode constructor. Hot-reload via FileWatcher.

## 8. BT Component & Subsystem

- [ ] 8.1 Create `runtime/engine/ai/components/behavior_tree_component.h` — CBehaviorTree: tree_path (string), BehaviorTree* (loaded), Blackboard. auto_start (bool).
- [ ] 8.2 Create `runtime/engine/ai/ai_subsystem.h/.cpp` — AISubsystem (WorldSubsystem, TickGroup::Update): for each CBehaviorTree entity, tick the tree with entity + blackboard + dt.
- [ ] 8.3 Add to ComponentSerializerRegistry — JSON: tree_path, auto_start, initial blackboard values.

## 9. Service Nodes

- [ ] 9.1 Create `runtime/engine/ai/nodes/bt_service.h/.cpp` — Service: runs alongside parent at interval (e.g., every 0.5s). Updates blackboard values (e.g., "target = FindClosestEnemy()"). Does NOT affect parent's return value.

## 10. Lua Custom Nodes

- [ ] 10.1 Create `runtime/engine/scripting/api/lua_bt.h/.cpp` — `uz.bt.register_action(type_name, function(entity, bb) end)`: registers Lua function as BT action node. `uz.bt.register_condition(type_name, function(entity, bb) end)`. Return "success"/"failure"/"running".
- [ ] 10.2 Add BT control Lua API — `uz.bt.set_tree(entity, path)`, `uz.bt.set_blackboard(entity, key, value)`, `uz.bt.get_blackboard(entity, key)`, `uz.bt.pause(entity)`, `uz.bt.resume(entity)`.

## 11. Testing

- [ ] 11.1 Unit tests for Sequence/Selector/Parallel — all return value combinations.
- [ ] 11.2 Unit tests for decorators — cooldown timing, loop count, conditional evaluation, inverter.
- [ ] 11.3 Unit tests for Blackboard — set/get types, HasKey, ClearKey, observer callbacks.
- [ ] 11.4 Unit tests for .uzbt loader — parse tree from JSON, verify structure.
- [ ] 11.5 Unit tests for built-in actions — MoveTo state machine, Wait timing.
- [ ] 11.6 Integration test: NPC with BT patrols between points, chases player when visible.

## 12. Demo

- [ ] 12.1 Create `demos/behavior_tree/` — 3 NPCs with patrol BT (MoveTo → Wait → MoveTo next). When player enters range: switch to chase behavior. Navmesh + BT visualization. Headless + screenshot.

## 13. Documentation

- [ ] 13.1 Update `docs/engine/20-gameplay/` with BT architecture, node types, blackboard.
- [ ] 13.2 Add `uz.bt` to `docs/lua-api-reference.md`.
- [ ] 13.3 Document .uzbt format with examples.

## 14. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 14.1 Update or create documentation covering the implementation
- [ ] 14.2 Write tests covering the new behavior
- [ ] 14.3 Run tests and confirm they pass
