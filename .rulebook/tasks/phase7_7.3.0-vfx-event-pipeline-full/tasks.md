# Phase 7-7.3.0 - VFX Event Pipeline (Generators + Receivers)

Full event lifecycle: GPU-side emit, framework-level readback, CPU dispatch to receivers. Unblocks secondary-effect patterns.

Reference: UE5 FNiagaraEventScript + UNiagaraEventReceiverModule

## 1. Event types

- [ ] 1.1 Define VfxEventKind enum (Spawn, Death, Collision, Custom).
- [ ] 1.2 Define VfxEventPayload struct (position, velocity, normal, color, sourceParticleID).

## 2. GPU emit path

- [ ] 2.1 Allocate VfxEventBuffer (RWStructuredBuffer<VfxEventPayload> + append counter) per emitter with emitsEvents = true.
- [ ] 2.2 Update CS death path writes VfxEventPayload when particle dies.
- [ ] 2.3 Collision CS writes on hit (wires into the existing 3.85.0 emitCollisionEvent flag).

## 3. Readback

- [ ] 3.1 Register the event buffer with the 4.26.0 readback framework.
- [ ] 3.2 Callback delivers the event array to CPU on frame N+1.

## 4. Receiver dispatch

- [ ] 4.1 Add VfxEventReceiverConfig (sourceEmitterName, action: SpawnBurst | InheritVelocity | SpawnAtPosition).
- [ ] 4.2 Wire receivers in VfxSystemInstance::Tick - queue events to target emitter's spawn CS next frame.
- [ ] 4.3 Payload inheritance: spawn position = event.position, initial velocity = event.velocity * factor.

## 5. Demo

- [ ] 5.1 Extend fireball compound: spawn embers on head-particle-death via the event pipeline.
- [ ] 5.2 Screenshot verification.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
