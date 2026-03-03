# Tasks: phase8-companion-mount-pattern

## 1. CCompanion Component

- [ ] 1.1 Create `src/engine/components/companion.h` defining `CCompanion` struct
- [ ] 1.2 Define fields: `owner_id: EntityId`, `type: CompanionType`, `allow_collision_with_owner: bool` (default `false`)
- [ ] 1.3 Define `CompanionType` enum: `PET=0`, `MOUNT=1`, `SUMMON=2`, `FAMILIAR=3`
- [ ] 1.4 Add `suppress_warnings: vector<string>` field for per-entity warning suppression
- [ ] 1.5 Default `allow_collision_with_owner: true` only for `MOUNT` type; all other types default to `false`

## 2. PhysicsSystem Integration

- [ ] 2.1 Add early-return check in `PhysicsSystem::register_entity(EntityId)` in `src/engine/systems/physics_system.cpp`
- [ ] 2.2 Check: `if (ecs.has<CCompanion>(id)) { LOG_DEBUG("CCompanion entity {} — no physics body created", id); return; }`
- [ ] 2.3 This prevents any `CCollider` shapes from being registered to Jolt for companion entities, regardless of other components present

## 3. COMPANION Collision Layer

- [ ] 3.1 Add `COMPANION = 0x0040` to the physics collision layer enum in the physics system header
- [ ] 3.2 Configure the default layer matrix: `COMPANION` collides with nothing (empty `collides_with` list)
- [ ] 3.3 For MOUNT entities with `allow_collision_with_owner: true`: create a temporary contact pair at runtime between the specific mount entity and its owner's `PLAYER` layer body

## 4. Entity Validator Warning

- [ ] 4.1 Add validation rule to `src/asset/validation/entity_validator.cpp`: if entity has both `CCompanion` and `CCollider` (or `CRigidBody`), emit warning `W_COMPANION_HAS_COLLIDER`
- [ ] 4.2 Warning message: "CCompanion entity has CCollider. This is almost always unintentional. Companion entities should have no collision geometry. Add suppress: companion_collision_warning to CCompanion to suppress."
- [ ] 4.3 Suppress the warning if `CCompanion.suppress_warnings` contains `"companion_collision_warning"`

## 5. CompanionFollowSystem

- [ ] 5.1 Create `src/engine/systems/companion_follow_system.h` and `src/engine/systems/companion_follow_system.cpp`
- [ ] 5.2 Define `CFollowAI` component: `{target: EntityId, follow_radius: float, offset: float3, smoothing: float}`
- [ ] 5.3 Per frame: for each entity with `CCompanion` + `CFollowAI`, read `CTransform` of `target` entity and update companion `CTransform` via lerp
- [ ] 5.4 Since companions have no physics body, set position directly on `CTransform` each frame (no physics impulses)
- [ ] 5.5 Keep companions within `follow_radius` of owner; use configurable `smoothing` factor for lerp (default 0.1)
- [ ] 5.6 Do not use navmesh by default — pets teleport through walls rather than navigating around them (visual-only entities)

## 6. Owner-Only Interaction Filter

- [ ] 6.1 In the event bus dispatch for `interaction.begin` events: if the interacting entity has `CCompanion`, filter receivers to `companion.owner_id` only
- [ ] 6.2 Implement the filter in the event dispatch layer, not in individual interaction handlers

## 7. Lua API

- [ ] 7.1 Add `ctx.entity:is_companion(entity_id) -> bool` Lua binding
- [ ] 7.2 Add `ctx.companion:get_owner(entity_id) -> EntityId` Lua binding
- [ ] 7.3 Add `ctx.companion:set_follow_target(entity_id, target_id)` Lua binding: updates `CFollowAI.target`
- [ ] 7.4 Add `ctx.companion:detach(entity_id)` Lua binding: clears `CFollowAI.target` and sets companion position to its current world position (stops following)

## 8. IPC Handler

- [ ] 8.1 Add `entity.setCompanionOwner { entity_id, owner_id, companion_type }` IPC handler in `src/ipc/handlers/entity_handlers.cpp` for editor companion preview
- [ ] 8.2 Register the IPC handler in the engine's IPC dispatch table

## 9. Documentation

- [ ] 9.1 Create `docs/specs/gameplay/companion-mount-pattern.md` with the full companion entity pattern including a complete example `.uzscene` snippet (skeletal mesh + CFollowAI + CCompanion, explicitly without CCollider/CRigidBody)
- [ ] 9.2 Update `docs/specs/engine/optimization-systems.md` section 9 with COMPANION layer exclusion rationale and physics cost savings

## 10. Testing (≥95% line + branch coverage)

- [ ] 10.1 Write `tests/core/test_companion_no_physics.cpp`: spawn a `CCompanion` entity with a `CCollider` component; call `PhysicsSystem::register_entity`; assert Jolt body count is unchanged (no body created); assert debug log message contains expected text
- [ ] 10.2 Write `tests/core/test_companion_validator_warning.cpp`: create entity with both `CCompanion` and `CCollider`; run entity validator; assert `W_COMPANION_HAS_COLLIDER` warning emitted; add `"companion_collision_warning"` to `suppress_warnings`; run validator again; assert no warning
- [ ] 10.3 Write `tests/core/test_companion_follow_system.cpp`: spawn 100 `CCompanion` PET entities each with a unique owner entity; run `CompanionFollowSystem` for 10 frames; assert all companions are within `follow_radius` of their respective owners; assert Jolt body count is unchanged from pre-spawn
- [ ] 10.4 Write `tests/core/test_companion_owner_interaction.cpp`: spawn companion entity; dispatch `interaction.begin` event; assert only the `owner_id` entity receives the interaction event (other entities do not)
- [ ] 10.5 Write `tests/core/test_companion_lua_api.cpp`: call `ctx.companion:set_follow_target`; assert `CFollowAI.target` updated; call `ctx.companion:detach`; assert `CFollowAI.target` cleared
- [ ] 10.6 Verify ≥95% line and branch coverage across all `src/engine/systems/companion_follow_system.cpp`, `src/engine/components/companion.h`, and `src/asset/validation/entity_validator.cpp` companion-related paths using OpenCppCoverage
