# Design: environment-MIS specular

## Before
`EnvMISPass::Execute` sets the pipeline, the 16 root constants and the UAV table and dispatches; the
DX12 command list routes `SetRootCBV` to the compute root arguments only while a compute pipeline is
bound, and nobody called it for this pass, so b1 (`cameraPos`, `skyLightIntensity`, `skyLightColor*`)
was the previous compute pass's slot-1 buffer. The shader then drew 4 environment samples per pixel,
weighted each by `p_env / (p_env + p_brdf)` with `p_brdf = D · NoH / (4 VoH)` (the NDF-sampling density,
not of any strategy it ran), divided by a pdf without the solid angle's sin(theta), and wrote
`float4(result, 1.0)` on every lit pixel. The lighting replaced the split-sum specular wherever
alpha > 0.

## After
- The estimator (`env_mis_temporal_cs.hlsl`): for the covered pixels, `coverage = smoothstep(0.3, 0.5,
  metallic) · (1 − smoothstep(0.55, 0.7, roughness))`; strategy 1 draws `kEnvSamples` directions through
  the alias table (`SampleEnvironment`, pdf per solid angle), strategy 2 draws `kBrdfSamples` visible
  normals (`SampleGGX_VNDF` in the tangent frame of `BuildOrthonormalBasis`) and reflects V about them;
  both evaluate `D · Vis · F · NoL` with the BRDF of `brdf.hlsli`, the balance heuristic with
  `p_brdf(L) = G1(V) · D(H) / (4 NoV)` (the density of strategy 2, used on both sides) and `p_env(L)`
  from the pyramid (`EvaluateEnvironmentPDF`); the contributions are `f · L · NoL · w / p` averaged per
  strategy and summed, times the sky light tint. The roughness floor is 0.04 (the lighting's default
  clamp), so a mirror's lobe has a finite density. The EMA (alpha 0.05, ping-pong, no reprojection)
  stays; the output alpha is the coverage.
- The pdf (`envmap_sampling.hlsli::EnvTexelPdf`): `(w / total) · texelCount / (2π² sin(theta))` with
  `w = luminance · sin(theta)` as stored, so `p(omega) = luminance · texelCount / (2π² total)`; the
  sampling and the evaluation paths share it.
- The frame (`EnvWorldToCubemap` / `EnvCubemapToWorld`, the swizzle of `WorldToCubemap`): the pyramid
  and the alias table stay in cubemap space; `SampleEnvironment` returns world directions and
  `EvaluateEnvironmentPDF` takes them; the BRDF strategy samples the cube through the conversion. The
  diagnostics dump stays in cubemap space and says so; the harness swaps.
- The alias table (`SampleAliasTable`): the bin is `u.x · texelCount`, the accept test uses `u.y`;
  the classic trick of reusing the fractional part of `u.x · texelCount` needs more than the 17 bits
  the table takes from `u.x`, and the pass's 16-bit randoms left it at 0 (every sample accepted its
  own even texel: uniform sampling divided by the luminance pdf). `Rand2` now draws one hash per
  number as a 32-bit float.
- The binding: `EnvMISPass::DispatchParams` carries the frame's `CBAllocation` fields and `Execute`
  binds them at root slot 1 after `SetPipeline` (compute root arguments); `frame_lighting.cpp` passes
  `ctx.perFrameAlloc`.
- Not changed: the lighting reads the previous frame's estimate (`GetAccumulatedSrv` returns the last
  written history; the pass and the lighting share no declared graph resource, the ping-pong keeps
  them apart). One frame of latency; under the TAA jitter a silhouette pixel can carry a neighbour's
  coverage. Importing the history into the graph and reading the current texture is the follow-up.
- The lighting (`deferred_lighting_ps.hlsl`): `specular = lerp(splitSum, lerp(mis, ssr · F_ss,
  ssr.a) · specOcclusion, coverage)`; the dump's `specular_indirect` records the blended value and
  `emissive_and_mis` the estimate's share. `r.EnvMIS` (frame_constants.cpp, one registration, read
  into `m_envMisEnabled` once per frame) gates the pass and the lighting's read of it; default on.
- Diagnostics: `EnvMapSampler::SetDumpDirectory` (forwarded from the region-dump directory every
  frame; the sampler rebuilds after `SetRegionDump` ran); `Rebuild` then also creates an RGBA32F
  equirect the mip-0 shader fills through a sixth root constant, reads it back and writes
  `env_equirect.f32` / `.json`. The harness integrates the GGX BRDF over it on the CPU.

## Harness
`scripts/rendering/test_env_mis.py`: the mirrored view, the chrome floor (`r.EnvMIS` 1 / 0), the roughness 0.3
and 0.5 metal floors (1 / 0), the chrome floor under the ballroom PMREM (1 / 0), `chrome_test`
(reported: its sphere mesh renders inside-out), Sponza (default, repeat; frame 120 against the
capture of build 5470d37f); E1-E7 as pre-registered (E3 on the boxes' interior, E4 on the PMREM floor);
`--before` runs the capture set the old binary supports (no `r.EnvMIS`, no dump) and reports the gates
that need them as unavailable. The CPU ground truth: the dumped equirect (2 × 2 averaged for the rough
floors), `sum D · Vis · F · NoL · L · dOmega` per sampled pixel, with the dump's normal, roughness and
world position; the mirror limit `F(NoV) · L(R)` for the chrome floor and sphere.
