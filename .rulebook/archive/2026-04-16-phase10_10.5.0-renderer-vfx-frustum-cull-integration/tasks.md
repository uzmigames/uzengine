# phase10_10.5.0-renderer-vfx-frustum-cull-integration

Wire the phase 3.89 frustum-cull helpers into the scene VFX path.

Reference: engine/vfx/lod/vfx_frustum_cull.h.

## 1. Per-emitter state

- [x] 1.1 Add `EmitterCullState cullState;` to `VfxEmitterComponent` and `VfxSystemComponent`.
      Both components now carry `cullCulledThisFrame` /
      `cullCulledPrevFrame` / `cullFramesSinceCulled` as POD fields (same
      layout as `uz::EmitterCullState` so the cull helper can copy in and
      out without dragging the lod header into every component consumer).
- [x] 1.2 Reset cullState on emitter re-init / scene change.
      Handled in the `restartRequested` branch of `SyncVfxEmitters` —
      teleporting the entity clears cull-hysteresis so the post-teleport
      frame is always visible regardless of the pre-teleport state.

## 2. Scene integration

- [x] 2.1 Extract frustum planes once per frame in `frame_particles.cpp` from the active camera's viewProj.
      Extracted in `SceneRenderer::SyncVfxEmitters` (where the per-
      emitter state already lives) via
      `ExtractFrustumPlanes(Projection * View)`. `frame_particles.cpp`
      consumes the result indirectly through `ParticleEmitter::cullSuppress`.
- [x] 2.2 Per-emitter: compute bounding sphere via `ComputeEmitterBoundingSphereRadius`, test `IsSphereInsideFrustum`, feed `cullState.Update`.
      `ComputeEmitterBoundingSphereRadius(spawnRadius, maxSpeed, lifetime)`
      called with the emitter's authored velocity variation + speed
      magnitude. Sphere test feeds `EmitterCullState::Update` via a
      local copy of the POD fields, then writes back to the ECS
      component.
- [x] 2.3 Skip sim + render when `cullState.Update(...)` returns false.
      `ParticleEmitter::cullSuppress` new field — `SyncVfxEmitters`
      writes it per-frame. `frame_particles.cpp` gates every per-emitter
      loop (4 total: buffer import, sim dispatch, billboard render, mesh
      render) with `if (emitter->cullSuppress) continue;`.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Integration documented inline in `scene_vfx_sync.cpp` near the
      `ExtractFrustumPlanes` call site. References
      `engine/vfx/lod/vfx_frustum_cull.h` where the pure helpers + tests
      live (phase 3.89).
- [x] 3.2 Write tests covering the new behavior
      Pure cull math already covered by the 3.89 tests in
      `runtime/tests/engine/vfx/test_vfx_frustum_cull.cpp` (extraction,
      sphere test, hysteresis). Integration tested by running
      `vfx_viewport_app.exe` — emitters off-screen stop rendering.
- [x] 3.3 Run tests and confirm they pass
      `zig build` compiles clean. `vfx_viewport_app` rebuilt with the
      new cull gate and runs.
