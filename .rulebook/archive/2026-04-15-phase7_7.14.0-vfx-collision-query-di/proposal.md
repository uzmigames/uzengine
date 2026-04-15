# Proposal: phase7_7.14.0-vfx-collision-query-di

## Why
Beyond depth-buffer collision (3.85.0) and async GPU trace (4.23.0), a generic CollisionQueryDI lets modules fire raycasts or shape-casts against the physics world during simulation. Niagara exposes PhysicsRaycast, PhysicsSphereCast, returning hit point/normal/material. UzEngine has no in-sim physics query.

## What Changes
Integrate the engine's physics (Jolt) scene query into a GPU-compatible CPU-side path: collect raycast requests in a GPU buffer during update CS, readback via 4.26.0 framework, run CPU raycasts, upload results for next frame. 1-frame latency.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/data_interfaces/, integration with Jolt physics (phase4-4.2.0)
- Breaking change: NO
- User benefit: Proper physics collision for particles (raycast obstacles, material-aware impacts).

## Source
Reference: UE5 UNiagaraDataInterfaceCollisionQuery. Depends on: 4.26.0 readback, 4.2.0 Jolt physics.
