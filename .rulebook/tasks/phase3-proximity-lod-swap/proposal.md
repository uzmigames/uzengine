# Proposal: phase3-proximity-lod-swap

## Why

Open-world MMO games are populated by harvestable resources: ore nodes, herb clusters, lumber trees, fishing spots, gathering points. In a single biome, hundreds of these objects are scattered across the landscape. Each one, when the player is near, is a fully interactive entity: it has a collision shape for click detection, a trigger volume for range checking, a network sync channel to query availability from the server, and a loot table. This is the correct behavior within 5 meters of the player.

The problem arises at distance. A zone with 5,000 harvestable trees, if all treated as fully interactive entities at all times, would require 5,000 Jolt Physics collision shapes in the simulation, 5,000 trigger volumes polling for player proximity, and 5,000 server network sync channels — all simultaneously active. This is not a theoretical concern: it is exactly the performance profile that causes MMO zone servers to fall over during launch weekend when population density spikes.

The standard MMO solution, implemented in virtually every production MMO engine, is proximity-based LOD swapping: harvestable objects exist as GPU instanced static meshes with zero physics presence at distance. Only when a player is within interaction range does the interactive representation materialize. At any given moment in a zone with 5,000 trees and 50 players, roughly 500 trees total are within interaction range of any player — 10% of the total. The other 4,500 cost nothing in physics or network terms.

UzEngine's `CProximitySwap` component implements this pattern generically. It is not specific to harvestable resources: the same component handles interactive doors (distant = decoration, close = physics door with open/close interaction), wildlife (distant = background animation, close = AI agent with pathfinding), and environmental hazards (distant = visual only, close = damage volume active).

## What Changes

- **`src/engine/components/proximity_swap.h`**: New component. Defines `CProximitySwap { inner_radius: float, outer_radius: float, current_state: State, server_validate: optional callback, distant_mesh: Handle<StaticMesh>, interactive_mesh: Handle<SkeletalMesh>, material: Handle<Material> }`. State enum: `DISTANT=0, APPROACHING=1, INTERACTIVE=2`.
- **`src/engine/systems/proximity_swap_system.cpp`**: New ECS system. Per frame: for each entity with `CProximitySwap`, compute distance to nearest player entity (entities with `CPlayer` tag). Apply state machine transitions: `DISTANT → APPROACHING` when distance crosses `outer_radius`, `APPROACHING → INTERACTIVE` when distance crosses `inner_radius` AND server validation succeeds (or no server_validate callback set), `INTERACTIVE → APPROACHING` when distance exceeds `inner_radius`, `APPROACHING → DISTANT` when distance exceeds `outer_radius`.
- **State transition side effects**:
  - `DISTANT → APPROACHING`: begin async load of `interactive_mesh` if not resident. Dequeue entity from GPU instance batch (it will render individually now). If `server_validate` is set, call it asynchronously.
  - `APPROACHING → INTERACTIVE`: activate `CCollider` shapes. Activate trigger volume. Enable server sync. Switch rendered mesh to `interactive_mesh`.
  - `INTERACTIVE → APPROACHING`: deactivate `CCollider` shapes. Deactivate trigger volume. Disable server sync.
  - `APPROACHING → DISTANT`: unload `interactive_mesh` if ref count drops to zero. Re-enqueue entity into GPU instance batch with `distant_mesh`.
- **GPU instancing integration**: In `DISTANT` state, the entity is rendered by the `BatchBuilder` as part of the GPU instance batch (shares mesh+material with all other DISTANT instances of the same resource type). In `APPROACHING` and `INTERACTIVE` states, it is rendered as an individual draw call.
- **`src/engine/api/proximity_swap_api.cpp`**: Expose `ProximitySwapAPI::force_interactive(entity_id)` (for editor preview of interactive state) and `ProximitySwapAPI::force_distant(entity_id)` (for debugging). IPC handlers: `proximity.forceState { entity_id, state }`.
- **Lua integration**: `ctx.events:on("proximity_swap.became_interactive", callback(entity_id))`, `ctx.events:on("proximity_swap.became_distant", callback(entity_id))`. The `server_validate` callback is registered as: `CProximitySwap.server_validate = "game.validate_resource"` in `.uzscene`, resolved to a Lua function at runtime.
- **Server validation hook**: `server_validate(entity_id: uint32, respond: function(approved: bool))`. The callback is responsible for calling `respond` asynchronously. If `respond(true)` is not called within 5 seconds (configurable), the entity stays in `APPROACHING` state and retries on the next proximity check cycle.
- **`.uzscene` format**: Document `[CProximitySwap]` INI section with all fields. Example in `docs/specs/engine/optimization-systems.md` section 8.
- **Documentation**: Add `ProximitySwapComponent` to `docs/specs/gameplay/proximity-lod-swap.md` (new spec file). Update `docs/specs/engine/optimization-systems.md` section 8.
- **Tests**: Integration test: create a scene with 10 `CProximitySwap` entities. Simulate player moving from distance 100m → 25m → 3m. Verify state transitions occur at correct distances. Verify `CCollider` is added/removed from physics simulation on transitions. Verify `server_validate` callback is invoked on `APPROACHING → INTERACTIVE` transition. Verify GPU instance batch count decrements as entities enter APPROACHING state.

## Impact

- **Affected specs**: `docs/specs/engine/optimization-systems.md`, `docs/specs/rendering/gpu-instancing.md`, `docs/specs/physics/physics-system.md`
- **Affected code**: `src/engine/components/proximity_swap.h` (new), `src/engine/systems/proximity_swap_system.cpp` (new), `src/engine/api/proximity_swap_api.cpp` (new), `src/renderer/instancing/batch_builder.cpp`, `src/engine/systems/physics_system.cpp`
- **Breaking change**: NO — additive component
- **User benefit**: Open-world MMO zones with thousands of interactive objects maintain constant physics and network costs bounded by player proximity radius, not total object count. Enables zones with 10,000+ interactive objects without scaling physics/network overhead linearly with total count.
