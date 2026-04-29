## 1. Diagnose
- [ ] 1.1 Run `--pixel-debug 640 540` (and several screen positions in the spawn arc) on `vfx_viewport_app.exe --vfs-root sample/content --vfx-asset vfx/tests/mesh_sort_shards.uzvfx --headless --frames 90`; capture the JSON and decide path 1/2/3 (no rendering) vs path 4 (rendering but invisible)
- [ ] 1.2 Visualise the per-particle colors buffer via `--vis` (or add a diag target if not exposed). If alive particles show RGBA(0,0,0,0): root cause is path 1 or 2 (spawn / CB)
- [ ] 1.3 Add a one-shot `UZ_LOG(VFX, Info)` in `RegisterParticleSpawnPass` (or wherever the spawn CS is dispatched) printing groupCount + CB binding for the asset-driven emitter; verify the log fires
- [ ] 1.4 Locate the spawn CB build site (`cbInitialColor`, `SpawnConstants`); confirm whether `ParticleEmitter::initialColor` is forwarded for asset emitters
- [ ] 1.5 Document the confirmed root cause with file:line evidence in the doc updated by item 4.1

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
