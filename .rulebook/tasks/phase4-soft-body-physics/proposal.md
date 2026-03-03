# Proposal: phase4-soft-body-physics

## Why
UzEngine has no soft body simulation, making cloth-like objects, inflatable bodies, and deformable props impossible. These are pervasive in modern games: capes and flags in God of War, organic enemy bodies in horror titles, and deformable terrain in sandbox games all rely on soft body physics. Jolt Physics added first-class soft body support in v4.0 via `JPH::SoftBodyCreationSettings`. Without this integration, game developers have no path to these visual and gameplay effects.

## What Changes
- `CSoftBody` ECS component wrapping `JPH::SoftBodyCreationSettings`: vertex positions, edge constraints, volume constraints, stiffness, damping, pressure (for inflatables)
- Jolt soft body world registration and per-frame pose readback from `JPH::SoftBodyMotionProperties`
- Vertex-based skinned mesh renderer: instead of skeleton joints, mesh vertices follow soft body vertex positions directly
- Attachment constraints linking soft body vertices to rigid body anchor points (e.g., cape attached to character shoulders)
- Self-collision flag on `JPH::SoftBodyCreationSettings` for cloth that folds on itself
- Wind force integration: reads from `WindSystem` global wind field (requires `phase4-wind-system-unified`)
- Configurable per-component: stiffness, damping, pressure, iteration count, gravity scale
- Lua API: `ctx.physics:apply_soft_force(entity, vertex_index, force_vec)` and `ctx.physics:reset_soft_body(entity)`
- Asset pipeline: `.uzmesh` can store a soft body rest-pose vertex mesh alongside the render mesh

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/renderer.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/physics/physics_system.cpp`, `src/physics/components/csoftbody.h`, `src/renderer/soft_body_renderer.cpp`, `src/scripting/lua_physics_bindings.cpp`
- Breaking change: NO
- User benefit: Game developers can add deformable cloth, flags, capes, and inflatable objects through a simple ECS component with no manual Jolt API integration required.
