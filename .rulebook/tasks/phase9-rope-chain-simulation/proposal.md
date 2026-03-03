# Proposal: phase4-rope-chain-simulation

## Why
UzEngine has no rope, cable, or chain simulation. These are common mechanics in action games (grabbing ropes), vehicle simulations (tow cables), and environmental design (hanging chains, power lines). Jolt Physics provides `JPH::DistanceConstraint` and `JPH::HingeConstraint` which can form constraint chains, but there is no engine-side ECS component, rendering primitive, or Lua API wrapping them. Without this task, game developers must implement constraint chains manually at the Jolt API level with no engine support.

## What Changes
- `CRope` ECS component storing: segment count, total length, radius, stiffness, damping, and attachment anchor points at each end
- Jolt constraint chain backend: one `JPH::Body` sphere per segment linked by `JPH::DistanceConstraint` pairs
- Catenary initial shape solver to place segments in a realistic hanging curve at spawn time
- Continuous Collision Detection (CCD) enabled per segment body for fast-moving ropes (swinging, cut ropes)
- CPU-side pose readback each frame to update a tube mesh following the segment positions
- Procedural tube mesh renderer: adaptive cross-section subdivisions, UV scrolling for chain texture
- Attachment API: either entity-to-entity or entity-to-world-point, supports one or both ends being free
- Lua API: `ctx.physics:attach_rope(entityA, anchorA, entityB, anchorB, length, options)` and `ctx.physics:cut_rope(ropeEntity)`
- Editor gizmo: visualize segment chain in wireframe with length/stiffness handles

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/editor-gizmos.md`
- Affected code: `src/physics/physics_system.cpp`, `src/physics/components/crope.h`, `src/renderer/rope_renderer.cpp`, `src/scripting/lua_physics_bindings.cpp`
- Breaking change: NO
- User benefit: Game developers can add physically simulated ropes, cables, and chains to scenes via a simple ECS component and Lua API without writing any Jolt Physics code directly.
