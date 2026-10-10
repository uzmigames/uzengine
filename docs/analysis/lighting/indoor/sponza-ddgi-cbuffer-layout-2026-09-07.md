# Sponza DXR/DDGI preset: uniform green frame caused by cbuffer array packing

> **Origin**: `test/windows-sponza-parity @ 25cd0c5f` (`E:\SponzaTest`), ported verbatim on 2026-09-08 by `phase0_sponza-parity-06-qa-harness-validation`.
> Paths such as `scripts/*_artifacts/`, `scripts/sponza-*.png|log|json|sha256.txt` refer to evidence that is **not in Git**; the executable identities quoted are SponzaTest builds, not this repository's.


Tested 2026-09-07 on the first DXR Tier 1.1 host available to this branch. HEAD `25cd0c5f` plus the shader fix below. GPU: NVIDIA GeForce RTX 5080 (16 GB, driver 32.0.16.1088, `RaytracingTier=12`, DXR 1.1 inline supported). Windows 11 Pro 10.0.26200. Device: `D3D_FEATURE_LEVEL_12_2` via Agility 1.619.2, negotiated SM6.9, shader profile `cs_6_8` (bindless), DXC 1.9.2602. Adapter 0 in `DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE` order; both launchers now pass `--adapter 0`.

## Symptom

`scripts/rendering/run_sponza_dxr.bat` (`r.RayTracing=1`, `r.RayTracing.GI=1`, `r.GI.FinalGather=0`) rendered the whole atrium in a single saturated green: floor, columns, vaults and walls, with only the sky through the windows unaffected. `run_sponza_max_quality.bat` (`r.RayTracing=0`) was unaffected. The run log showed no errors: `RTManager: Initialized`, `Created TLAS 'SceneTLAS'`, BLAS built per mesh, `DDGISystem: allocated irrad 2048x128 ... depth 4608x288`, and `PipelineLibrary: created DDGI_ProbeUpdate pipeline` — the pipeline that failed with `0x80070057` on the previous Tier 1.0 host. This is therefore the first documented run on this branch's test hosts in which the DDGI atlas writer executed and fed `deferred_lighting_ps.hlsl:483-589`. The defective declaration has been present since the shader's only commit (`29c83ba5`, 2026-04-07); the v0.9.1 changelog's RTX 4090 DDGI timing predates that commit and does not contradict this finding.

## Root cause

`DDGISystem::DispatchUpdate` (`runtime/engine/renderer/gi/ddgi_system.cpp:108-145`) uploads a `RadianceCacheTraceConstants` (`runtime/engine/renderer/gi/radiance_cache.h:225-268`, `static_assert` 176 bytes) to root CBV slot 2, the same struct the SDF tracer uses. `radiance_cache_trace_cs.hlsl:36-76` mirrors it with scalar fields. `ddgi_probe_update_cs.hlsl:58-61` mirrored the unused GDF clipmap block as `float _gdf0[4]; ... float _gdf3[4];`.

HLSL constant-buffer packing places every array element in its own 16-byte register, so each `float[4]` occupies 64 bytes instead of 16. `dxc -T cs_6_8 -E main -Fc` on the HEAD shader reports the resulting offsets:

| Field | C++ struct writes at | HEAD shader read at | Fixed shader reads at |
|---|---|---|---|
| `numGdfClipmaps` | 128 | 308 | 128 |
| `skyColorR` | 132 | 312 | 132 |
| `lightDirX` | 144 | 324 | 144 |
| `lightIntensity` | 156 | 336 | 156 |
| `lightColorR` | 160 | 340 | 160 |
| `groundAlbedo` | 172 | 352 | 172 |

Every value the tracer uses for miss radiance (`skyColor*`) and single-bounce hit radiance (`lightDir*`, `lightIntensity`, `lightColor*`, `groundAlbedo`) was read 180 bytes past the 176-byte upload, i.e. from the neighbouring allocation in the CB ring. The atlas therefore accumulated arbitrary radiance, and because `frame_constants.cpp:663-676` publishes the atlas SRVs once `HasCompletedFirstUpdate()` is true, deferred lighting applied that radiance as `irradiance = ddgiIrrad * _ddgiIntensity` on every lit surface. The green hue is whatever those bytes held on this host; it is not a channel or format defect of the atlases (`RGBA16F` irradiance, `RG16F` depth are created correctly).

The fields before the arrays (`gridOrigin*`, `probeSpacing`, `probesPerAxis`, `totalProbes`, `raysPerProbe`, `frameIndex`, `maxTraceDistance`, `temporalBlendAlpha`) were unaffected, which is why probe placement, ray count and blending behaved normally and the image was uniformly wrong rather than structurally wrong.

## Fix

`runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl`: the four `float _gdfN[4]` declarations are replaced by the same scalar quadruplets the C++ struct and the SDF shader use (`_gdfCenterNX, _gdfCenterNY, _gdfCenterNZ, _gdfExtentN`), with a comment stating the packing rule. No other shader or C++ change. Shaders are embedded by `uzembed` at build time, so `zig build` is required; the incremental rebuild took 48 s and again produced 27/30 steps with the 23 known `test_runtime` compilation errors.

## Verification

`dxc -Fc` on the fixed source reports `skyColorR` at 132 through `groundAlbedo` at 172, matching the struct.

Before/after captures were taken with the same binary arguments (`--width 1280 --height 720 --adapter 0 --screenshot ... --log-level info` plus the DXR preset CVars), same scene camera, automatic capture after `Pre-load complete`:

| Run | Result | Frames incl. load | ms/frame | Exit |
|---|---|---|---|---|
| HEAD shader | uniform green atrium | 537 | 51.00 (20 FPS) | 0 |
| fixed shader | sunlit floor patch, shaded arcades, ambient vaults, no tint | 537 | 28.21 (35 FPS) | 0 |

Both timings include scene load and SDF bake and are not a benchmark. Captures are kept locally under `scripts/sponza_ddgi_artifacts/` (ignored by Git) as `ddgi_before.png` and `ddgi_after.png`. The only errors in either log are the known `rt_uv_test_lib.hlsl` `[raypayload]` compile failures of the debug RT UV pass.

## Scope and remaining limitations

- This restores the intended inputs to the DDGI writer; it does not validate DDGI quality. `DDGISystem::DispatchUpdate` still hard-codes sky `(0.6, 0.7, 0.9)`, sun direction `(0, 0, 1)`, intensity 1, white light and `groundAlbedo` 0.18 instead of the scene sun and sky, and `TraceProbeRay` uses `-dir` as the hit normal. The handoff's statement that DDGI "retains simplified sky/sun/material inputs and needs separate evaluation" still stands.
- `r.GI.FinalGather=0` in the DXR preset means diffuse GI is DDGI only; reflections remain SDF/SSR and AO remains GTAO.
- No Falcor comparison was possible on this host; PSNR is unverified.
- `rt_uv_test_lib.hlsl` needs `struct [raypayload] RayPayload { float4 color : read(caller, closesthit, miss) : write(closesthit, miss); };` for `lib_6_7+` targets; it is a debug pass and non-fatal.
