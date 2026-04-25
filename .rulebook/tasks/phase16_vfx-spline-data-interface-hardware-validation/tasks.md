## 1. CB plumbing (frame_vfx_inline.cpp + shaders)
- [ ] 1.1 Detect spline modules on each emitter's stack via `dynamic_cast` over spawn/update module pointers
- [ ] 1.2 Lazy-create `VfxSplineBuffer` on first dispatch when a spline module is present (capacity = `kVfxSplineDefaultResolution`)
- [ ] 1.3 Call `VfxSplineBuffer::RebuildAndUpload(emitter->splineSource, device)` once per frame before simulation
- [ ] 1.4 Extend `ParticleSpawnConstants` (CPU + HLSL) with `cbSplineSrvIndex`, `cbSplinePointCount`, `cbSplineTotalLength`, `cbSplineSpawnStratified`, `cbSplineSpawnJitterRadius`, `cbSplineSpawnTangentOffset`
- [ ] 1.5 Extend `ParticleUpdateConstants` with the same trio plus `cbSplineForceStiffness`, `cbSplineForceMaxAccel`, `cbSplineForceAttractDistance`, `cbSplineFollowSpeed`, `cbSplineFollowLoop`, `cbSplineFollowStartU`
- [ ] 1.6 Update `static_assert(sizeof(ParticleSpawnConstants) == ...)` / `(ParticleUpdateConstants == ...)` to the new sizes; mirror in HLSL cbuffers

## 2. GPU integration tests
- [ ] 2.1 `test_spline_spawn_distribution.cpp` — 1024 particles spawned with `stratified=true`; chord-distance variance < 5% of mean
- [ ] 2.2 `test_spline_force_convergence.cpp` — 100 particles 200 cm off-spline; after 60 ticks mean nearest-distance < 5 cm
- [ ] 2.3 `test_spline_follow_endpoints.cpp` — `lifeFrac=0` matches `controlPoints[0]`, `lifeFrac=1` matches `controlPoints.back()`, tolerance 1e-3 cm

## 3. Editor + ECS integration
- [ ] 3.1 New `SplineComponent` ECS type (`engine/ecs/environment/spline_component.{h,cpp}`) with `controlPoints` array, `tension`, `looping`
- [ ] 3.2 `SyncVfxEmitters` reads `VfxEmitterComponent::splineTargetEntity`, copies `SplineComponent::controlPoints` to runtime emitter's `splineSource`
- [ ] 3.3 Niagara Parameters panel: "Spline Target" entity-picker row filtered by `SplineComponent` presence

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
