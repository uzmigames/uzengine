# Phase 7-7.10.0 - VFX Static Mesh Sampling DI

Surface sampling of an imported mesh (uniform by area) for mesh-aligned particle spawning.

Reference: UE5 UNiagaraDataInterfaceStaticMesh

## 1. CPU preprocessing

- [ ] 1.1 VfxStaticMeshDI caches vertex, index, and triangle-area prefix sum (CDF).
- [ ] 1.2 Accept MeshAsset handle + optional filter (material slot, bounding volume).

## 2. GPU upload

- [ ] 2.1 Three structured buffers (verts, indices, area CDF) with bindless SRV indices.
- [ ] 2.2 Emitter CB carries SRV index triplet.

## 3. HLSL helper

- [ ] 3.1 SampleMeshSurface(rand2) returning {pos, normal, uv} via inverse-CDF sampling.

## 4. Module and demo

- [ ] 4.1 MeshSpawnModule replaces emitter origin with SampleMeshSurface(hash(threadID)).
- [ ] 4.2 Demo: spawn sparks uniformly across a dragon mesh surface.

## 5. Docs and tests

- [ ] 5.1 Documentation on sampling model (uniform-by-area, not uniform-by-vertex).
- [ ] 5.2 Tests: CDF monotonicity, area-weighted distribution statistical check.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
