## Shader Utility Library — Consolidation + New Functions (from Falcor analysis)
> Source: `docs/analysis/falcor/unified-shader-improvements.md` + `shaders-and-materials.md`
> Estimate: 3 sessions

## Session 1: Math constants + PCG hash + branchless ONB

- [x] 1.1 `math_util.hlsli` — added `M_2PI`, `M_1_PI`, `M_PI_2`, `M_PI_4`. Replaced all inline `3.14159265` across 16 shader files (removed local #define PI, static const PI, and inline literals). Zero inline PI remaining in codebase.
- [x] 1.2 `math_util.hlsli` — implemented `pcgHash(uint)` and `pcgHash2D(uint2)`. @ref O'Neill 2014.
- [x] 1.3 `math_util.hlsli` — implemented `branchlessONB(float3 n, out float3 t, out float3 b)`. @ref Duff et al. 2017. No sqrt, no pole singularity.
- [x] 1.4 `math_util.hlsli` — added `CartesianToSpherical(float3)` → `float2(theta,phi)` and `SphericalToCartesian(float,float)` → `float3`. Z-up convention.
- [x] 1.5 `math_util.hlsli` — added `LatLongToDirection(float2 uv)` and `DirectionToLatLong(float3 dir)`.
- [x] 1.6 `math_util.hlsli` — added `jenkinsHash(uint seed)`. @ref Jenkins 1997.
- [x] 1.7 Build passes. Grep confirms zero inline PI literals in code (only in @constants doc comments).

## Session 2: Shared sampling library

- [x] 2.1 Created `include/sampling.hlsli`. Extracted `RadicalInverseVdC`, `Hammersley` from `ibl.hlsli`.
- [x] 2.2 Extracted `UniformSampleDiskConcentricApprox` from `pcss.hlsli` into `sampling.hlsli`. @ref Reynolds 2017.
- [x] 2.3 Implemented `CosineSampleHemisphere(float2 u)`. @ref Shirley & Chiu 1997 / Malley's method.
- [x] 2.4 Implemented `UniformSampleSphere(float2 u)`. @ref Marsaglia 1972.
- [x] 2.5 Implemented `UniformSampleCone(float2 u, float cosThetaMax)`. @ref PBR Book Section 13.6.3.
- [x] 2.6 Implemented `SampleTriangle(float2 u)` → `float2(baryU, baryV)`. @ref PBR Book Section 13.6.5.
- [x] 2.7 Implemented `VonMisesFisherSample(float3 mu, float kappa, float2 u)` using branchlessONB. @ref Fisher 1953, Jakob 2012.
- [x] 2.8 Updated `ibl.hlsli` (`#include "sampling.hlsli"`, removed local RadicalInverseVdC/Hammersley), `pcss.hlsli` (`#include "sampling.hlsli"`, removed local disk sampling). SSGI has custom overloads (randomSeed param) — left as-is. Build passes.

## Session 3: Octahedral + sheen extraction + IBL helpers

- [x] 3.1 `octahedral.hlsli` — implemented `OctEncodeEqualArea` + `OctDecodeEqualArea`. @ref Meyer et al. 2010. Added `#include "math_util.hlsli"` for M_1_PI/M_PI_4. Non-equal-area variants preserved.
- [x] 3.2 Created `include/sheen.hlsli` — extracted `D_Charlie` + `V_Neubelt`. @ref Estevez & Kulla 2017, Neubelt 2013.
- [x] 3.3 `deferred_lighting_ps.hlsl` — `#include "sheen.hlsli"`, removed 18-line local D_Charlie/V_Neubelt.
- [x] 3.4 `ibl.hlsli` — implemented `ComputeEnvLOD(pdf, texWidth, numSamples)` using log2(omegaS/omegaP). @ref PBR Book Section 13.10.
- [x] 3.5 `ibl.hlsli` — implemented `SolidAngleSample(numSamples, pdf)` = 1/(N*pdf).
- [x] 3.6 Created `include/packing.hlsli` — R10G10B10A2, R16G16_FLOAT, R16G16_UNORM, R16G16_SNORM pack/unpack + generic FloatToUnorm/Snorm/UnormToFloat/SnormToFloat.
- [x] 3.7 Build passes. Zero errors.
