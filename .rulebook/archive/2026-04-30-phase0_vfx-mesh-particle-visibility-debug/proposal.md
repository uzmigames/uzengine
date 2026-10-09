# Proposal: phase0_vfx-mesh-particle-visibility-debug

## Why

`phase0_re-enable-mesh-particle-render-path-rc3` (archived 2026-04-29)
re-enabled the VFX mesh-particle render path end-to-end at the
structural level, but its 3.4 visual A/B step ("confirm the 200 cyan
tetrahedron shards are visible") could not produce a screenshot
showing the shards. The pipeline is structurally clean — proven by:

- `[VFX] ParticleMeshRenderPass: first execution -- 1 emitters (1 batches submitted, 1 drawn, transparent=false)` log line fires every run.
- `--gpu-validation` headless run reports zero `D3D12 ERROR` lines.
- `--diag` JSON shows HDR target `avg=0.813` non-zero (sky composites correctly).
- All 27 unit tests covering the mesh-cache lookup, PSO resolver, and
  billboard guard pass green.

But the screenshot (`e:/UzmiEngine/uzengine/.sandbox/post-fix.png`)
shows only sky + ground horizon — no visible cyan tetrahedron shards
even after 90 frames of emission. Something downstream of the draw
call (or in the particle simulation feeding it) is producing
imperceptible output. This task isolates and fixes that residual
visibility issue.

The most likely root causes, ranked:

1. **`cbInitialColor` not flowing to `ParticleSpawnCS` for
   asset-loaded mesh emitters.** Spawn writes
   `Colors[slotIdx] = cbInitialColor` (`spawn_module.cpp:32`), but
   there is no JSON parsing of `initial_color` in `uzfx_loader.cpp`
   and the `.uzvfx` schema does not name the field. The default
   `ParticleEmitter::initialColor = {1.0f, 0.9f, 0.6f, 1.0f}`
   (`particle_emitter.h:176`) should still produce warm-white
   particles, but if the CB is not being filled per-emitter for the
   asset-loaded path, every particle could read RGBA(0,0,0,0) and
   fail visibility through the alpha test or just blend invisibly
   against the sky.
2. **Sim tick not running** — `[VFX] InitializeData` reports
   `aliveCount=0` at frame 0, but the per-frame log doesn't surface
   alive-count updates. Possible the spawn CS dispatch is missing
   from the per-frame loop for the asset-driven emitter path (a
   parallel of the bindless-migration regression fixed at
   `frame_particles.cpp:155` for the render-graph imports).
3. **Instance-color buffer never written** —
   `particle_mesh_instance_cs.hlsl` writes `instanceColors[u3]` for
   each alive instance from the per-particle `colors[t4]` SoA buffer.
   If the spawn never wrote `colors` (path 1) or the instance CS
   reads from the wrong SRV index, the PS reads zeroed colors →
   multiplied by lighting → black → blends invisibly.
4. **Camera/scale mismatch** — already partially mitigated by tuning
   the asset (scale=3.0, vel=200) but the camera may simply look past
   the spawn region. Less likely given 200 particles with 60° spread,
   but cheap to verify.

The fix is one of these. The investigation needs the engine's
diagnostic flags (`--pixel-debug`, `--vis hdr`, GPU breadcrumbs)
plus targeted reads of the spawn / instance-CS code paths.

## What Changes

### Diagnose

1. **Pixel-debug at the centroid of the visible spawn zone.** Run
   `--pixel-debug 640 540` (screen-centre, slightly lower because
   shards rise from origin), inspect the JSON for albedo, HDR, and
   per-channel values. If HDR is non-zero where shards should be:
   particles ARE rendering but the color is washing into the sky
   gradient (path 4). If HDR is identical to a non-spawn pixel:
   particles are NOT rendering (path 1, 2, or 3).
2. **GPU debugger snapshot of the per-particle color buffer.** After
   pinpointing whether colors are written, read the buffer back via
   `--vis vfx.particle_colors` (if registered; otherwise add the
   target to the diag list). All-zero RGBA on alive particles
   confirms path 1 or 2.
3. **Verify spawn CS dispatch.** Add a one-time `UZ_LOG(VFX, Info)`
   inside `RegisterParticleSpawnPass` (or the equivalent) that prints
   the dispatched group count and CB binding for the asset emitter.
   Run; log should appear once per emitter creation.
4. **Read the spawn CB build site.** If `cbInitialColor` is not being
   filled from `ParticleEmitter::initialColor` for the asset-loaded
   path, that's path 1. Locate the CB build (search
   `cbInitialColor`, `SpawnConstants`, `spawnCB`) and verify the
   feeding member is the right one.

### Fix

5. **Whichever root cause the diagnosis lands on**, fix it surgically:
   - Path 1: extend `uzfx_loader.cpp` to parse `initial_color`
     (RGBA float4 array) AND/OR ensure the CB build uses
     `ParticleEmitter::initialColor` for asset emitters as well as
     preset emitters.
   - Path 2: re-enable / re-wire the spawn dispatch for asset-driven
     emitters in `frame_particles.cpp` (mirror the fix from item 3.3
     of the predecessor task).
   - Path 3: add the missing SRV import or fix the descriptor index
     in the per-frame instance-CS dispatch.
   - Path 4: adjust the test asset's spawn velocity / scale or the
     demo's camera position to bring the shards into frame.
6. **Re-capture the visual A/B.** Replace
   `e:/UzmiEngine/uzengine/.sandbox/post-fix.png` with a screenshot
   showing visibly-rendered cyan shards. Update
   `docs/engine/25-vfx/88-mesh-particle-render-path.md` §7 — change
   "Visibility caveat" to "Visibility verified" and embed the new
   screenshot.

## Impact

- Affected specs: `vfx-particle-mesh-render` (existing — enables the
  spec scenario "Sort hardware-validation unblocked").
- Affected code: probably one of `runtime/engine/asset/formats/uzfx_loader.cpp`
  (path 1), `runtime/engine/renderer/frame/frame_particles.cpp` (path
  2/3), or `sample/content/vfx/tests/mesh_sort_shards.uzvfx` (path 4).
  Scope remains tight — focused bug fix, not a new feature.
- Breaking change: NO. CPU surface unchanged; default behaviour for
  preset emitters preserved.
- User benefit: closes the last remaining gap in the mesh-particle
  pipeline — unblocks the `phase16_vfx-sort-pipeline-hardware-validation`
  final visual A/B step (item 1.2) and any future feature work that
  wants visible mesh-particle output.

## Dependencies

- Builds on `phase0_re-enable-mesh-particle-render-path-rc3`
  (archived) — the structural pipeline is already in place.
- Blocks: `phase16_vfx-sort-pipeline-hardware-validation` items 1.2,
  1.3, 1.4 (visual A/B, perf budget at 16k particles, CVar fallback).
- Surfaced by: this task's own predecessor 2026-04-29.
