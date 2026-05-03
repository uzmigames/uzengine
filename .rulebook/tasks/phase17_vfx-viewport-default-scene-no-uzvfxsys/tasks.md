## 1. Implementation

- [ ] 1.1 Audit `vfx_viewport_app.cpp` + `.h` for every reference to `m_vfxEntity` and `m_systemAssetPath`. Catalogue (a) which references are essential to other features, (b) which exist purely to drive the missing compound. Output: short comment in this tasks.md listing the references.
- [ ] 1.2 Remove the `Fireball Compound` block in `OnStart` (lines ~287-305) that creates the `VfxSystemComponent` entity. Remove `m_vfxEntity` member from header if no longer referenced. Remove `m_systemAssetPath` member.
- [ ] 1.3 Remove `--vfxsys` CLI argument parsing in `main.cpp` (or convert it to a deprecation warning that logs and ignores).
- [ ] 1.4 Replace `kPresetFns[0]` (`ApplyPreset_FireballProj`) with a working single-emitter preset. Either alias to `ApplyPreset_Fireball` or write a new `ApplyPreset_FireballProj` body matching the old visual intent. Update `kPresetNames[0]` if necessary.
- [ ] 1.5 If `ApplyPreset_FireballProj` was a stub for the missing compound, remove its declaration from `engine/vfx/components/emitter_presets.h` and any other definition site.
- [ ] 1.6 Update the startup `UZ_LOG(App, Info, ...)` lines that mention `"fireball compound created via VfxSystemComponent"` to reflect the actual standalone-emitter setup.
- [ ] 1.7 Build runtime + demo (`zig build -Ddll=true -Doptimize=ReleaseFast` then in `runtime/demos/vfx_viewport_app/`).
- [ ] 1.8 Launch `vfx_viewport_app.exe` with NO CLI arguments. Verify the log file at `%LOCALAPPDATA%/UzEngine/logs/uzengine_*.log` has ZERO `[VFX] [error]` lines for `vfx/systems/fireball.uzvfxsys` or any other missing asset.
- [ ] 1.9 Verify Tab / Right cycles forward through all 22 preset names; Left cycles backward. The status line in the overlay (or the on-screen overlay text) updates the preset name on each press.

## 2. Tail (mandatory - enforced by rulebook v5.3.0)

- [ ] 2.1 Update or create documentation covering the implementation - update `docs/guides/vfx-engine-integration.md` removing references to the fireball compound, OR add a short section explaining the asset-less default scene. Add a comment in `vfx_viewport_app.h` explaining that the demo is intentionally asset-free for the default scene.
- [ ] 2.2 Write tests covering the new behavior - add a test in `runtime/tests/demos/vfx_viewport_app_test.cpp` (create if absent) that constructs a `VFXViewportApp` config with no asset path and asserts (a) `m_systemAssetPath` member no longer exists / is unset, (b) `m_emitterEntities.size() >= 1` after `OnStart`, (c) preset cycling list contains 22 entries.
- [ ] 2.3 Run tests and confirm they pass - `runtime/build/bin/test_runtime.exe --gtest_filter="*VfxViewportApp*"` 100% pass.
