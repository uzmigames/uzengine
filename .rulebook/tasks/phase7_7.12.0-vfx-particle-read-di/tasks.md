# Phase 7-7.12.0 - VFX Particle Read Data Interface

Cross-emitter particle attribute read (follower emitter samples master's positions/velocities).

Reference: UE5 UNiagaraDataInterfaceParticleRead

## 1. Emitter handles

- [ ] 1.1 VfxEmitterHandle stable identifier across the runtime registry.
- [ ] 1.2 Reader declares readEmitters list; dispatcher binds source SRV indices into the reader CB.

## 2. HLSL helpers

- [ ] 2.1 ReadSourcePosition(emitterSlot, particleID) / ReadSourceVelocity / ReadSourceColor.
- [ ] 2.2 Safety: dead source particles return sentinel (NaN or configurable default).

## 3. Module and demo

- [ ] 3.1 AttractToMasterModule - secondary particles track a selected master particle's position.
- [ ] 3.2 Demo: fireball sparks attracted to head core using cross-emitter read.

## 4. Docs and tests

- [ ] 4.1 Documentation on dispatch-order requirements (master must run before follower in the frame).
- [ ] 4.2 Tests: stable read across frames, sentinel handling for dead particles.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
