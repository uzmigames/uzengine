# Tasks: phase8-proximity-lod-swap

## 1. CProximitySwap Component

- [ ] 1.1 Create `src/engine/components/proximity_swap.h` defining `CProximitySwap` struct
- [ ] 1.2 Define fields: `inner_radius: float`, `outer_radius: float`, `current_state: State`, `server_validate: string` (Lua callback name, optional), `distant_mesh: Handle<StaticMesh>`, `interactive_mesh: Handle<SkeletalMesh>`, `material: Handle<Material>`
- [ ] 1.3 Define `State` enum: `DISTANT=0`, `APPROACHING=1`, `INTERACTIVE=2`
- [ ] 1.4 Validate at construction: `inner_radius < outer_radius`; assert if violated
- [ ] 1.5 Document the `[CProximitySwap]` INI section format for `.uzscene` files

## 2. ProximitySwapSystem ECS System

- [ ] 2.1 Create `src/engine/systems/proximity_swap_system.h` and `src/engine/systems/proximity_swap_system.cpp`
- [ ] 2.2 Per frame: for each entity with `CProximitySwap`, compute distance to the nearest entity with `CPlayer` tag
- [ ] 2.3 Implement state machine transitions: `DISTANT → APPROACHING` when distance < `outer_radius`; `APPROACHING → INTERACTIVE` when distance < `inner_radius` AND server validation passes; `INTERACTIVE → APPROACHING` when distance > `inner_radius`; `APPROACHING → DISTANT` when distance > `outer_radius`

## 3. State Transition Side Effects

- [ ] 3.1 Implement `DISTANT → APPROACHING` transition: begin async load of `interactive_mesh` if not resident; dequeue entity from GPU instance batch (will render individually)
- [ ] 3.2 If `server_validate` callback is set, call it asynchronously on `DISTANT → APPROACHING`
- [ ] 3.3 Implement `APPROACHING → INTERACTIVE` transition: activate `CCollider` shapes; activate trigger volume; enable server sync; switch rendered mesh to `interactive_mesh`
- [ ] 3.4 Implement `INTERACTIVE → APPROACHING` transition: deactivate `CCollider` shapes; deactivate trigger volume; disable server sync
- [ ] 3.5 Implement `APPROACHING → DISTANT` transition: unload `interactive_mesh` if ref count drops to zero; re-enqueue entity into GPU instance batch with `distant_mesh`

## 4. GPU Instancing Integration

- [ ] 4.1 In `DISTANT` state: entity is rendered by `BatchBuilder` as part of GPU instance batch (shares mesh+material with all other `DISTANT` instances of the same resource type)
- [ ] 4.2 In `APPROACHING` and `INTERACTIVE` states: render as individual draw call using `interactive_mesh`
- [ ] 4.3 Update `src/renderer/instancing/batch_builder.cpp` to remove entities from batches on `DISTANT → APPROACHING` and re-add on `APPROACHING → DISTANT`

## 5. ProximitySwapAPI

- [ ] 5.1 Create `src/engine/api/proximity_swap_api.h` and `src/engine/api/proximity_swap_api.cpp`
- [ ] 5.2 Implement `ProximitySwapAPI::force_interactive(entity_id)`: bypass distance check and transition directly to `INTERACTIVE` state
- [ ] 5.3 Implement `ProximitySwapAPI::force_distant(entity_id)`: bypass distance check and transition directly to `DISTANT` state

## 6. Server Validation Hook

- [ ] 6.1 Implement `server_validate(entity_id, respond_callback)` as a Lua callback called asynchronously on `APPROACHING → INTERACTIVE` transition attempt
- [ ] 6.2 The `respond` callback: if `respond(true)` not called within 5 seconds (configurable timeout), entity stays in `APPROACHING` state and retries on next proximity check cycle
- [ ] 6.3 Resolve `CProximitySwap.server_validate` string to a Lua function via the scripting runtime on first call

## 7. IPC Handlers

- [ ] 7.1 Add `proximity.forceState { entity_id, state: "distant"|"approaching"|"interactive" }` IPC handler in `src/ipc/handlers/entity_handlers.cpp`
- [ ] 7.2 Register the IPC handler in the engine's IPC dispatch table

## 8. Lua Events

- [ ] 8.1 Emit `ctx.events:on("proximity_swap.became_interactive", callback(entity_id))` Lua event on `APPROACHING → INTERACTIVE` transition
- [ ] 8.2 Emit `ctx.events:on("proximity_swap.became_distant", callback(entity_id))` Lua event on `APPROACHING → DISTANT` transition

## 9. Documentation

- [ ] 9.1 Create `docs/specs/gameplay/proximity-lod-swap.md` with the full `CProximitySwap` component specification and a complete `.uzscene` example
- [ ] 9.2 Update `docs/specs/engine/optimization-systems.md` section 8 with proximity LOD swap pattern

## 10. Testing (≥95% line + branch coverage)

- [ ] 10.1 Write `tests/core/test_proximity_swap_state_machine.cpp`: create 10 `CProximitySwap` entities; simulate player moving from 100m → 25m → 3m; assert `DISTANT → APPROACHING` fires at `outer_radius` crossing; assert `APPROACHING → INTERACTIVE` fires at `inner_radius` crossing; assert `CCollider` added to physics simulation on `INTERACTIVE` entry
- [ ] 10.2 Write `tests/core/test_proximity_swap_reverse.cpp`: player moves from 3m → 30m → 110m; assert `INTERACTIVE → APPROACHING` fires; assert `APPROACHING → DISTANT` fires; assert `CCollider` removed from physics; assert entity re-added to GPU instance batch
- [ ] 10.3 Write `tests/core/test_proximity_swap_server_validate.cpp`: set `server_validate` callback; transition to `APPROACHING`; assert Lua callback invoked; call `respond(true)`; assert state becomes `INTERACTIVE`; call `respond(false)`; assert state stays `APPROACHING`
- [ ] 10.4 Write `tests/core/test_proximity_swap_server_validate_timeout.cpp`: set `server_validate` callback; do not call `respond` within timeout period; assert entity stays `APPROACHING` and retry occurs on next cycle
- [ ] 10.5 Write `tests/core/test_proximity_swap_gpu_batch.cpp`: assert that `DISTANT`-state entities are included in `BatchBuilder` instance batch; assert that `APPROACHING`-state entities are NOT in the instance batch
- [ ] 10.6 Write `tests/core/test_proximity_swap_force_state.cpp`: call `ProximitySwapAPI::force_interactive` on a `DISTANT` entity; assert state becomes `INTERACTIVE` immediately; assert `CCollider` activated
- [ ] 10.7 Verify ≥95% line and branch coverage across all `src/engine/systems/proximity_swap_system.cpp` and `src/engine/api/proximity_swap_api.cpp` using OpenCppCoverage
