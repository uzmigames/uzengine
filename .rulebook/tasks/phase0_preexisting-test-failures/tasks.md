## 1. Items
- [ ] 1.1 Fix the MaterialGraph id-0 sentinel collision (or the test), with the reason recorded.
- [ ] 1.2 Find the shared state behind the MaterialGraphHlslCodegen order dependence.
- [x] 1.3 Decide each default-drift test (fix the test or the default): fog, sky, camera, project-loader CVar order.
  Done 2026-10-10. Fog (`HeightFogComponent.{DensityDefaultIs0_0436, ColorDefaultIsBlack,
  DirectionalInscatteringColorDefault}`, `RenderingParity.HeightFogDefaults_{PrimaryLayer,
  DirectionalInscattering}`): **code** was wrong. f094fe9c / af18b667 replaced the reference defaults
  with look-tuned values (density 0.15, blue colour, warm glow) against the parity rule. Restored:
  colour and directional colour black (reference constructor), density 0.0436 (the fog actor of the
  reference default level, the same placement `baseHeight = -5000` encodes). Sky
  (`SkyAtmosphereComponent.FallbackSunDiskIntensityIsOne`): **code** was wrong. 33fb5cd0 set 0.1 to dim
  the disk; the multiplier is an identity scale, so it is back to 1.0. Camera
  (`SceneLoaderTest.CameraComponent_DefaultValues_WhenFieldsAbsent`): **test** was stale. 1eb426b1 made
  a scene camera without `active` active on purpose; all 144 shipped scene cameras set the key.
  Project CVars (`ProjectLoaderTest.CvarsBlock_NumbersAndBoolsParsed`): **code** bug. The default
  `nlohmann::json` keeps objects in a std::map, so cvars came back alphabetical instead of in file order,
  the order `ProjectConfig::cvarOverrides` documents. Now parsed as `ordered_json`. Lua
  (`LuaSceneAPITest.SetAndGetRotationRoundTrips`): **code** bug. lua_entity_api.cpp (c0f9df43) registered
  quaternion `set_rotation`/`get_rotation` after lua_scene_api.cpp and replaced the documented
  Euler-degree API (arg #5 missing → chunk error). Removed the duplicates. All 9 pass; the
  HeightFog*/RenderingParity*/SkyAtmosphere*/SceneLoader*/ProjectLoader*/LuaScene* suites pass 267/267.
- [ ] 1.4 ShaderCrossBackend: add gbuffer/ to the fixture; make the non-bindless deferred lighting compile with a real shadow-atlas register bound by the C++ side, or exclude that permutation with the reason.
- [ ] 1.5 Re-run the full test_runtime and publish a dated failure list with an owner per suite (the VFX / editor suites stay with the VFX work in progress).

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass

## 2026-10-10 current-source rebaseline

The C++23 / Zig 0.15.2 Debug binary scheduled 7,636 tests in 742 suites.
After 493 successful results, the process aborted with `0xC0000005` in
`VulkanBarrierGpuTest.BufferBarrier_VertexToShaderToUAVToCommon_NoValidationErrors`.
Evidence: `.sandbox/stabilization/native-tests.log`, `native-tests-run.json`, and
`runtime/build/bin/crashes/2026-10-10_02-15-25/crash.{dmp,txt,json}`. There is no final
GoogleTest JSON. This task retains ownership of Vulkan fixture/function-dispatch
root-cause analysis and a complete rerun; item 1.5 remains unchecked because the
suite did not finish. No Vulkan fix is claimed by the baseline work.

The 85 targeted container/math/normal-smoothing/attractor tests passed separately.
See [baseline implementation](../../../docs/analysis/project-stabilization/07-baseline-implementation.md)
for source provenance, commands, build changes and other required gates.

### Final-source follow-up

After the build corrections, the run from `runtime/build/bin` reached 6,088
successful results, 70 failures and 3 skips before the 180-second diagnostic
limit. `.sandbox/stabilization/native-final-suite-observations.json` retains every
failed test name; `native-final-suite.log` retains assertions. The old Vulkan
crash was not reproduced in that observation and is not declared fixed; working
directory and dependency availability differ. Existing VFX/editor ownership
remains in place; this task owns native-fixture, Lua/scene/default-drift triage and
complete-suite acceptance. The final targeted regression, including process tests,
passed 156 tests in 11 suites (also included in the later `native-clean-suite.json`).

The canonical-directory observation (`cwd=runtime`) reached 6,087 successful
results, 68 failures and 7 skips before the same diagnostic limit. See
`native-canonical-suite-observations.json` and `native-canonical-suite.log` under
`.sandbox/stabilization/`. Both broad attempts precede the final frame-pacer
identity correction; final targeted regression includes that correction. These
partial runs do not close item 1.5 or establish a full-suite pass.

### Complete canonical-directory observation

The subsequent full run finished without a timeout or crash: 7,637 tests,
7,536 passed, 85 failed and 16 skipped, exit 1 in 197.24 seconds. Evidence:
`.sandbox/stabilization/native-complete-suite.json`, `native-complete-suite-run.json`
and `native-complete-suite-observations.json`. The executable SHA-256 is recorded
in the run metadata. This observation uses the developer dependency profile before
the new isolated bootstrap build. Failed suites include the previously listed
VFX/editor and scene/default cases plus Metal and cross-backend shader cases that
the earlier timeout did not reach. The old Vulkan crash was not reproduced and is
not claimed fixed. Full-suite acceptance remains open.

The isolated locked-profile rerun also completed: **7,540 passed, 85 failed,
12 skipped / 7,637**, exit 1 in 150.62 seconds. There are no new or resolved failed
names relative to the preceding complete developer-profile run. All 156 affected
build regressions passed within this run. Evidence: `native-clean-suite.json`,
`native-clean-suite-run.json` and `native-clean-suite-observations.json` under
`.sandbox/stabilization/`.

### Final stage-0 observation — 2026-10-10

The final isolated locked-profile suite finished in 64.38 seconds without timeout
or crash: **7,545 passed, 84 failed, 12 skipped / 7,641**, exit 1. The four new
capture tests pass and there are no new failed names against the preceding locked
baseline. A targeted capture/VFX run passed all 29 tests.

`VfxSeekDeterminism_Timing.Measure_WriteJson_1k_4k_16k` no longer writes to an
obsolete absolute checkout path; it verifies a temporary-file JSON round trip.
The intermediate run also exposed an invalid allocator-address assertion in
`UZ_MeshSortUzfxPipeline.ApplyStack_CalledTwice_DoesNotDuplicate`; it now verifies
configuration replacement and disabled-module removal. Both corrected tests pass
in the targeted and full runs.

Evidence under `.sandbox/stabilization/`: `native-final-contract-suite.json`,
`native-final-contract-suite.log`, `native-final-contract-suite-run.json`,
`native-final-contract-suite-observations.json` (all failed names and comparisons),
`native-final-contract-source.json` and `native-capture-vfx-targeted.json`.
The test executable SHA-256 is
`30f322e764057f4319c121b4c23c5022499dad6e2711dd0890c9806d8f882219`.
This task retains fixture/Lua/scene/default/cross-backend triage and full-suite
acceptance; existing VFX/editor ownership remains unchanged. The old Vulkan crash
did not reproduce and is not declared fixed. No archive or global pass is claimed.
