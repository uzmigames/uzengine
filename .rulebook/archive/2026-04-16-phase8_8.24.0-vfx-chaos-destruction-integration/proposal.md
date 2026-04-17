# Proposal: phase8_8.24.0-vfx-chaos-destruction-integration

## Why
Game integration: when a rigid body in the Jolt physics scene fractures, spawn a debris VFX with velocity / position inherited from the break. Niagara listens to Chaos fracture events; UzEngine needs the Jolt analogue since we chose Jolt over Chaos.

## What Changes
Add VfxPhysicsFractureEvent source that subscribes to Jolt's OnFracture callback. Each event translates to a VfxEventPayload (via the 7.3 event pipeline) with kind=Custom and payload carrying the fracture normal, impact velocity, and piece mass.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/events/ + Jolt subscription hooks
- Breaking change: NO
- User benefit: Fracture events automatically produce debris + dust VFX.
- Estimated effort: L

## Source
UE5 FNiagaraDataInterfaceChaosDestruction + FChaosBreakEvent.
