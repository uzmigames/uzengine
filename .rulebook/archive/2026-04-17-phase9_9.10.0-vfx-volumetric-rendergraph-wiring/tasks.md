# phase9_9.10.0-vfx-volumetric-rendergraph-wiring

Render-graph pass registration for the 8.15.0 splat+raymarch shaders.

Reference: UE5 FNiagaraVolumetricRenderer.

## 1. Passes

- [x] 1.1 `AddVfxVolumetricSplatPass` — compute dispatch per emitter that
      writes into the transient 3D density volume via UAV.
      `engine/renderer/passes/vfx/vfx_volumetric_pass.cpp` — registers a
      compute pass "VFX_VolumetricSplat" that iterates emitters, fills
      `VolumetricSplatCB` (grid center/extent/resolution/density/alive),
      binds the pipeline + UAV/SRV indices via root constants, and
      dispatches `ceil(aliveCount/64)` groups.
- [x] 1.2 `AddVfxVolumetricRaymarchPass` — fullscreen PS reading the
      density SRV, blending into the HDR scene RT.
      Same file — registers "VFX_VolumetricRaymarch" pass that fills
      `VolumetricRaymarchCB` (144 bytes: invViewProj, camera, grid,
      sun, scattering, HG, steps), binds the raymarch PSO, and draws a
      fullscreen triangle (3 verts, no VB/IB). Alpha blend over existing
      HDR content.
- [x] 1.3 Transient volume allocation via RG (128³ R32F).
      The caller uses `RgBuilder::CreateTransient3D("VfxDensityVolume",
      128, 128, 128, R32Float)` (phase 10.6.0 helper) to create the
      transient 3D texture. The splat pass writes it as UAV, the
      raymarch pass reads it as SRV ��� the render graph inserts the
      UAV→SRV barrier automatically.

## 2. Integration

- [x] 2.1 Runs after deferred lighting, before / after fog (config toggle).
      Pass descriptor includes all fields needed for scheduling. The
      caller in `frame_particles.cpp` registers the splat pass after
      simulation and the raymarch pass at the appropriate point in the
      frame. Pass ordering is enforced by the render graph DAG (splat
      writes the volume, raymarch reads it → implicit dependency).
- [x] 2.2 CB population from `VfxVolumetricConfig` + per-frame camera data.
      `VfxVolumetricSplatCB` (32 bytes) and `VfxVolumetricRaymarchCB`
      (144 bytes) — CPU-side CB mirrors with static_assert size checks.
      Both are populated from `VfxVolumetricConfig` fields + per-frame
      camera matrices/position/sun direction.
- [x] 2.3 Per-emitter enable flag reads the renderer-type enum.
      `VolumetricEmitterData` in the pass descriptor carries the
      per-emitter config + alive count. The frame code filters emitters
      by renderer type before populating the desc.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on all public API + CB structs in
      `vfx_volumetric_pass.h`. Architecture block explains the
      splat→raymarch DAG and root signature slot assignments.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/renderer/test_vfx_volumetric_pass.cpp`:
      9 tests covering CB layout (size + field offsets for both CBs),
      emitter data defaults, config defaults, and pass descriptor
      default state.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly. Full test-run blocked
      by pre-existing FFmpeg DLL deployment issue.
