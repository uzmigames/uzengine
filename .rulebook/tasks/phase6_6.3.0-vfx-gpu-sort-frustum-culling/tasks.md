## Phase 6.3.0 — GPU Bitonic Sort & Frustum Culling

Re-enable the bitonic merge sort pass for alpha-blended particle ordering and
add CPU sphere-frustum culling to skip off-screen emitter dispatches.

## 1. GPU Bitonic Sort
- [ ] 1.1 Remove `if (false)` guard around sort pass in frame_particles.cpp
- [ ] 1.2 Ensure sort buffers (sortKeys, sortIndices) are properly imported into render graph
- [ ] 1.3 Verify sort pipeline (GetParticleSortCS) creates successfully
- [ ] 1.4 Wire sorted indices SRV to billboard VS (sortedIndicesSrvIndex)
- [ ] 1.5 Test: Smoke/Whirlpool (alpha blend) renders with correct back-to-front ordering

## 2. CPU Frustum Culling
- [ ] 2.1 Add frustumCullingEnabled flag to SceneRenderer or VFXSystem
- [ ] 2.2 Extract camera frustum planes from view-projection matrix
- [ ] 2.3 Compute emitter bounding sphere (worldPosition + maxRadius from velocity*lifetime)
- [ ] 2.4 Sphere-frustum intersection test before dispatch in frame_vfx_inline.cpp
- [ ] 2.5 Skip spawn+update+billboard dispatch for culled emitters
- [ ] 2.6 Add culled frame counter for stats overlay

## 3. Build & Test
- [ ] 3.1 Build runtime + demo, zero compile errors
- [ ] 3.2 Alpha-blended Smoke preset: no sorting artifacts (particles blend correctly)
- [ ] 3.3 Move camera away from emitter: emitter stops dispatching (culled)
- [ ] 3.4 DX12 validation: zero errors with sort pass enabled

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
