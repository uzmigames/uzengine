# Proposal: phase9_9.18.0-vfx-jolt-fracture-listener-bridge

## Why
Phase 8.24.0 shipped `VfxPhysicsFractureBridge` + input schema + translation. Jolt's ContactListener is the side that produces fracture callbacks; registering the bridge inside the physics world is the missing link.

## What Changes
Hook `VfxPhysicsFractureBridge::OnFracture` into the Jolt `ContactListener::OnContactAdded` callback (plus any explicit fracture event path if/when Jolt exposes one). Build the `VfxFractureEventInput` from the contact manifold (impact normal = contact normal, impact speed = relative velocity · normal, pieces from the body's child rigid-body list).

## Impact
- Affected specs: docs/engine/25-vfx/41-physics-fracture-events.md
- Affected code: runtime/engine/physics/jolt_world.* + VFX world init
- Breaking change: NO
- User benefit: Breaking a crate spawns debris automatically.
- Estimated effort: M

## Source
UE5 FChaosBreakEvent listener registration.
