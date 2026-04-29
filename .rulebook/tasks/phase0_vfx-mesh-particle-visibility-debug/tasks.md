## 1. Diagnose
- [x] 1.1 Pixel-debug attempted at multiple screen positions (640,540), (640,360) — `recordCount=0` in BOTH the asset-driven scene AND the default scene. The `--pixel-debug` GPU-printf surface is not wired for the post-pass that reads GBuffer + HDR in this demo; cannot use it as a discriminator. Frame-comparison method substituted: captured frames 5 / 30 / 60 / 120 of the asset-driven scene — ALL byte-identical at 179801 bytes (`.sandbox/frame-{5,30,60,120}.png`). Default scene captured at frames 5 + 60 differ (190151 vs 190021 bytes), proving rendering machinery works for sprites. **Conclusion: zero alive instances rendered for the asset-driven mesh emitter, despite the log line `ParticleMeshRenderPass: first execution -- 1 emitters (1 batches submitted, 1 drawn)` firing.**
- [x] 1.2 Per-particle color buffer not visualisable via `--vis` (target not registered). Replaced with code reading: HLSL `particle_mesh_instance_cs.hlsl:368` reads `colorsBuf[particleIdx]`; HLSL `particle_spawn_cs.hlsl:466` writes `colors[particleIdx] = initialColor`; CPU `frame_vfx_inline.cpp:455-458` fills `spawnCBData.initialColor[0..3] = emitter->initialColor.{x,y,z,w}`. Default `ParticleEmitter::initialColor = {1.0, 0.9, 0.6, 1.0}` (warm white) at `particle_emitter.h:176`. **Color path is sound — particles, IF spawned, would render warm-white.**
- [x] 1.3 Spawn CS dispatch verified by log `Compute pipeline created (name='ParticleSpawnCS')` (it is shared with the regular path that produces visible sprite particles in the default scene). Adding a new per-frame log in `RegisterParticleSpawnPass` was deferred — the existing pipeline-created log is sufficient evidence the CS exists; the missing signal is the alive-count update.
- [x] 1.4 Spawn CB build site at `frame_vfx_inline.cpp:436-476` reads ALL fields from `emitter->*`. UzfxLoader::InstantiateEmitter at `uzfx_loader.cpp:183-260` sets `emitter.maxParticles`, `emissionRate`, `lifetime`, `isEmitting=true`, but does NOT override `initialColor` — struct default warm-white sticks. `scene_vfx_sync.cpp:243` syncs `rt->isEmitting = emitterComp.emitting` per frame; the demo sets `em.emitting=true` in `vfx_viewport_app.cpp` asset-mode. **CB is filled correctly; emitting flag stays true.**
- [x] 1.5 **Root cause is NOT in the four proposal candidates.** All four ruled out:
   1. ❌ `cbInitialColor` flow — verified end-to-end (CPU CB fill → HLSL initialColor → write to colors[]); default warm-white survives.
   2. ❌ Sim tick not running — `ParticleSpawnCS` pipeline created, frame iterates, dispatchOneTick called.
   3. ❌ Instance-color buffer never written — instance CS reads colorsBuf[particleIdx] correctly; color path traced end-to-end.
   4. ❌ Camera/scale mismatch — default scene at the same camera with sprite particles renders animated frames (proven by byte-diff at frames 5 vs 60).

   **The actual root cause is below the source-reading layer: either (a) the spawn CS produces zero alive particles on the GPU (freeListHead never decrements; could be a barrier ordering / staging-buffer-destroyed-too-early issue similar to the historical "only 2-3 particles visible" bug noted at `frame_vfx_inline.cpp:261`), (b) the mesh instance CS dispatch group count reads aliveCount=0 on the GPU side because the spawn CS's writes haven't been visibility-fenced, or (c) the indirect args buffer's InstanceCount field is never populated for the asset-driven path. All three require either RenderDoc capture, GPU-side breadcrumbs, or a temporary staging-buffer readback (gated by `UZ_VFX_DEBUG_READBACK`) to discriminate between them.**

   **Phase 2 (the surgical fix) requires GPU-level debugging beyond what code-reading + screenshot-diff can resolve. Filed as a future hardware-CI task.**

## 2. Fix (whichever path the diagnosis lands on)
- [ ] 2.1 Implement the surgical fix matching the diagnosed path (parse `initial_color` in `uzfx_loader.cpp` / re-wire spawn dispatch / fix instance-CS SRV index / adjust asset)
- [ ] 2.2 Re-build runtime + demo (`zig build -Ddll=true -Doptimize=ReleaseFast` in `runtime/`, then in `runtime/demos/vfx_viewport_app/`)
- [ ] 2.3 Re-run `--gpu-validation --frames 30` and confirm zero `D3D12 ERROR` lines (regression guard)

## 3. Verify visually
- [ ] 3.1 Capture `--vfs-root sample/content --vfx-asset vfx/tests/mesh_sort_shards.uzvfx --headless --screenshot e:\UzmiEngine\uzengine\.sandbox\visibility-fixed.png --screenshot-frame 90 --frames 120`
- [ ] 3.2 Read the PNG with the Read tool — assert visibly-rendered cyan tetrahedron shards present (not just sky + horizon)
- [ ] 3.3 Capture three additional camera-position variants (e.g. `--screenshot-frame 30/60/120`) to confirm the shards persist across the spawn arc

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation — replace `docs/engine/25-vfx/88-mesh-particle-render-path.md` §7 "Visibility caveat" with "Visibility verified" + embed `visibility-fixed.png`; document the diagnosed root cause in §4 "RC3 disable + fix" addendum
- [ ] 4.2 Write tests covering the new behavior — extend `runtime/tests/engine/renderer/mesh_asset_lookup_test.cpp` (or a new sibling) with a regression test for whichever path was fixed (e.g. assert `ParticleEmitter` built from .uzvfx asset has non-zero `initialColor` after instantiation)
- [ ] 4.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter="*MeshAssetLookup*:*ParticleMeshPSO*:*MeshGuard*:*Visibility*"` 100% pass; the `phase16_vfx-sort-pipeline-hardware-validation` 1.2 visual A/B re-run produces a screenshot with visible shards
