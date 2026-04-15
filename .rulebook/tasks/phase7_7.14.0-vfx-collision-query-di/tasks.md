# Phase 7-7.14.0 - VFX Physics Collision Query DI

Async GPU->CPU->GPU raycast roundtrip for particle collision against the physics world.

Reference: UE5 UNiagaraDataInterfaceCollisionQuery

## 1. Request buffer

- [ ] 1.1 VfxRaycastRequest struct (origin, direction, maxDist, particleID).
- [ ] 1.2 GPU append buffer + count.

## 2. Readback and resolve

- [ ] 2.1 Register request buffer with 4.26.0 readback framework.
- [ ] 2.2 CPU worker pool resolves raycasts against the active Jolt physics scene.

## 3. Result upload

- [ ] 3.1 VfxRaycastResult struct (hit, hitPos, hitNormal, hitMaterialID).
- [ ] 3.2 Upload result buffer bound back into the sim CB for next frame.

## 4. HLSL helpers

- [ ] 4.1 RequestRaycast(origin, dir, maxDist) appends to request buffer.
- [ ] 4.2 Next-frame ReadRaycastResult(particleID) reads result.

## 5. Demo

- [ ] 5.1 Particles raycast downward and stick at first hit.

## 6. Docs and tests

- [ ] 6.1 Documentation on 1-frame latency semantics and safety guarantees.
- [ ] 6.2 Tests: request/result round-trip, stale-result handling.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
