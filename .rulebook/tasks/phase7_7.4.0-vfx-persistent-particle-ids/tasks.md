# Phase 7-7.4.0 - VFX Persistent Particle IDs

Stable uint64 IDs per particle surviving slot recycle. Prereq for event correlation (7.3) and cross-emitter particle reads (7.12).

Reference: UE5 FNiagaraEmitter::bRequiresPersistentIDs

## 1. Storage

- [ ] 1.1 Add RHIBufferHandle persistentIDs SoA channel (stride=8, per-emitter opt-in via emitsPersistentIDs flag).
- [ ] 1.2 Allocate + destroy in CreateAttributeSet / DestroyAttributeSet.

## 2. ID assignment

- [ ] 2.1 CPU: uint64 nextPersistentID atomic counter per emitter.
- [ ] 2.2 Upload baseID + spawnCount to spawn CS each frame; GPU computes id = baseID + threadIndex.
- [ ] 2.3 Spawn CS writes persistentIDs[slot] = assignedID; death CS clears to 0.

## 3. Query API

- [ ] 3.1 Helper VfxEmitter::QueryParticleByID(uint64 id, out ParticleSnapshot) via readback framework (async, 1-frame latency).
- [ ] 3.2 Wire into event pipeline (7.3.0): VfxEventPayload carries sourceID.

## 4. Documentation and tests

- [ ] 4.1 Docs: extend 17-particle-flipbook-pipeline.md with the persistent-ID SoA channel.
- [ ] 4.2 Tests: monotonic ID sequence, death and respawn does not reuse IDs, 64-bit space sufficient.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
