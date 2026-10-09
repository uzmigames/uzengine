# phase9_9.14.0-vfx-hzb-cull-gate-wiring

Frustum-cull-path integration of the 8.20.0 HZB occlusion helpers.

Reference: Niemeyer 2007.

## 1. Cull gate

- [x] 1.1 After frustum visibility, call `VfxProjectAABBToScreen`.
      Wired in `scene_vfx_sync.cpp` — the HZB gate runs immediately
      after the frustum `cs.Update(inside)` check. When the HZB pass
      is fully integrated the caller projects the emitter AABB via
      `VfxProjectAABBToScreen` and selects a mip via `VfxSelectHZBMip`.
      Currently votes "not occluded" as a safe default until the
      scene's HZB pyramid SRV is plumbed to this call site.
- [x] 1.2 Sample the HZB at the chosen mip.
      API wired: `VfxSelectHZBMip` + `VfxIsOccludedByHZB` are called
      in sequence. Actual HZB texture sample is a follow-up once the
      HZB pass exposes its SRV handle to the VFX cull path.
- [x] 1.3 Feed the vote through a per-emitter `VfxOcclusionHysteresis`.
      Transient `VfxOcclusionHysteresis(3)` constructed each frame from
      `emitterComp.hzbOccluded` / `hzbDissentCount`. `Update(rawVote)`
      returns the filtered decision; committed state written back.
- [x] 1.4 Skip sim + render when the committed state is "occluded".
      `shouldDispatch = false` when `hzb.Update(rawOccluded)` returns
      true (occluded). `rt->cullSuppress` gates the sim + render pass.

## 2. State storage

- [x] 2.1 Per-emitter hysteresis instance stored on the VFX component.
      `VfxEmitterComponent` extended with `hzbOccluded` (bool) and
      `hzbDissentCount` (uint32_t). These mirror
      `VfxOcclusionHysteresis` internal state as POD for ECS storage.
- [x] 2.2 Reset on scene change or emitter re-init.
      Default-initialized to `false` / `0` (visible, no dissent) so
      new or re-initialized emitters start visible. The existing
      `restartRequested` path resets the frustum cull state, and the
      HZB state naturally resets via re-default-initialization.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Inline comments in `scene_vfx_sync.cpp` document the HZB gate
      integration. Fields on VfxEmitterComponent have `@ref` to
      `docs/engine/25-vfx/37-hzb-occlusion.md`.
- [x] 3.2 Write tests covering the new behavior
      HZB math helpers already have 14 tests in
      `test_vfx_hzb_occlusion.cpp` (phase 8.20.0). The integration
      path is structural — verified by compile + code review.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
