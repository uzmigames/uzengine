# Own and fix the pre-existing test_runtime failures

## Why

Every task closure since 2026-09-09 lists the same test_runtime failures as "pre-existing", and no task owns
them (91 of 7562 failing on 2026-10-08; the two stale per-frame CB tests were fixed in 8246ab5c). Diagnosed but not
fixed: MaterialGraphSerialization.RoundTrip_* (expression id 0 collides with the IsConnected() sentinel
`connectedExprId != 0`, material_expression.h:55; parity-02 notes.md:80-86); MaterialGraphHlslCodegen.Add_/Multiply_*
(pass alone, fail under wider filters: shared state); RenderingParity.HeightFogDefaults_* and
SkyAtmosphereComponent.FallbackSunDiskIntensityIsOne (superseded defaults, lighting-block-a-2026-10-05.md:171-174);
SceneLoaderTest.CameraComponent_DefaultValues_WhenFieldsAbsent; ProjectLoaderTest.CvarsBlock_NumbersAndBoolsParsed
(CVar order); ShaderCrossBackend: the fixture's subdirectory list lacks gbuffer/ (test_shader_cross_backend.cpp:56-57),
and the non-bindless deferred_lighting_ps never compiled with shadows (shadow.hlsli uses shadowAtlasTexture, which
the non-bindless block does not declare; t5-t7 are the IBL textures there, so the atlas needs a free register and a
C++ binding, not t5) and reads gBufIndices unguarded at :1381.

Origin: followup-09 and parity-06 close-outs (2026-10-08): the failures every task since 2026-09-09 reported as "pre-existing" had no owner.

## What Changes

- Fix the MaterialGraph id-0 sentinel collision (or the test), with the reason recorded.
- Find the shared state behind the MaterialGraphHlslCodegen order dependence.
- Decide each default-drift test (fix the test or the default): fog, sky, camera, project-loader CVar order.
- ShaderCrossBackend: add gbuffer/ to the fixture; make the non-bindless deferred lighting compile with a real shadow-atlas register bound by the C++ side, or exclude that permutation with the reason.
- Re-run the full test_runtime and publish a dated failure list with an owner per suite (the VFX / editor suites stay with the VFX work in progress).

## Impact
- Affected specs: preexisting-test-failures (new)
- Affected code: see the items; gates are pre-registered per item before its first run
- Breaking change: NO until a gate passes
- User benefit: the open points of the closed followups have an owner and a plan.

**Update 2026-10-09** (full run on the Debug build of 8fb13fa1 + 9735d401: 89 of 7649 failing). Two failures were
not listed above: `ShaderPipelineMetal.*` (4; the fixture sets no DXC compiler, "no DXC compiler set") and
`AverageRootConstants.SizeIs32Bytes` (stale since a13003d8, block A, which grew the struct to 14 DWORDs; fixed
in 8fb13fa1 as `SizeIs56Bytes` with the new offsets). MaterialGraph* did not fail in this run.
