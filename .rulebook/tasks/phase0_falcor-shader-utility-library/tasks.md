## Shader Utility Library — Consolidation + New Functions (from Falcor analysis)
> Source: `docs/analysis/falcor/unified-shader-improvements.md` + `shaders-and-materials.md`
> Estimate: 3 sessions

## Session 1: Math constants + PCG hash + branchless ONB

- [ ] 1.1 `include/math_util.hlsli` — add `M_2PI`, `M_1_PI`, `M_PI_2`, `M_PI_4`. Replace all inline `3.14159265` in codebase.
- [ ] 1.2 `include/math_util.hlsli` — implement `uint pcgHash(uint seed)` and `uint2 pcgHash2D(uint2 v)`. @ref O'Neill 2014.
- [ ] 1.3 `include/math_util.hlsli` — implement `branchlessONB(float3 n, out float3 t, out float3 b)`. @ref Duff et al. 2017. No sqrt, no pole singularity.
- [ ] 1.4 `include/math_util.hlsli` — add `CartesianToSpherical(float3 dir)` → `float2(theta, phi)` and `SphericalToCartesian(float theta, float phi)` → `float3`.
- [ ] 1.5 `include/math_util.hlsli` — add `LatLongToDirection(float2 uv)` and `DirectionToLatLong(float3 dir)`.
- [ ] 1.6 `include/math_util.hlsli` — add `jenkinsHash(uint seed)`. @ref Jenkins 1997.
- [ ] 1.7 Build all demos + grep verify no remaining inline PI literals.

## Session 2: Shared sampling library

- [ ] 2.1 Create `include/sampling.hlsli`. Extract `RadicalInverseVdC`, `Hammersley` from `ibl.hlsli`.
- [ ] 2.2 `sampling.hlsli` — extract `UniformSampleDiskConcentric` from `pcss.hlsli`. @ref Shirley & Chiu 1997.
- [ ] 2.3 `sampling.hlsli` — implement `CosineSampleHemisphere(float2 u)` → `float3`. @ref Shirley & Chiu 1997.
- [ ] 2.4 `sampling.hlsli` — implement `UniformSampleSphere(float2 u)` → `float3`.
- [ ] 2.5 `sampling.hlsli` — implement `UniformSampleCone(float2 u, float cosThetaMax)` → `float3`.
- [ ] 2.6 `sampling.hlsli` — implement `SampleTriangle(float2 u)` → `float2(baryU, baryV)`. Needed for emissive mesh sampling.
- [ ] 2.7 `sampling.hlsli` — implement `VonMisesFisherSample(float3 mu, float kappa, float2 u)`. Needed for volumetric phase.
- [ ] 2.8 Update `ibl.hlsli`, `pcss.hlsli`, `ssgi_trace_cs.hlsl` to `#include "sampling.hlsli"` — remove duplicated functions. Build + verify no regressions.

## Session 3: Octahedral + sheen extraction + IBL helpers

- [ ] 3.1 `include/octahedral.hlsli` — implement `OctEncodeEqualArea(float3 n)` and `OctDecodeEqualArea(float2 e)`. @ref Meyer et al. 2010. Keep non-equal-area for backward compat.
- [ ] 3.2 Extract `D_Charlie` + `V_Neubelt` from `deferred_lighting_ps.hlsl` into new `include/sheen.hlsli`. @ref Estevez & Kulla 2017.
- [ ] 3.3 `deferred_lighting_ps.hlsl` — `#include "sheen.hlsli"`, remove local copies. Build + verify cloth shading unchanged.
- [ ] 3.4 `include/ibl.hlsli` — implement `ComputeEnvLOD(float pdf, uint texWidth, uint numSamples)` for log2-based mip selection. Replace linear roughness-to-mip.
- [ ] 3.5 `include/ibl.hlsli` — implement `SolidAngleSample(uint numSamples, float pdf)`.
- [ ] 3.6 Create `include/packing.hlsli` — implement R10G10B10A2 pack/unpack, R16G16 pack/unpack, snorm/unorm converters. @ref Falcor `PackedFormats.slang`.
- [ ] 3.7 Build all demos + capture screenshots at multiple positions. Verify zero visual regressions.
