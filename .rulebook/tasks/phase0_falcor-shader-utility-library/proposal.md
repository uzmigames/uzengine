# Proposal: phase0_falcor-shader-utility-library

## Why
Falcor analysis revealed UzEngine's shader utility functions are scattered across per-pass files (Hammersley in `ibl.hlsli`, disk sampling in `pcss.hlsli`, cosine hemisphere inline in SSGI) instead of centralized in shared headers. This causes duplication, inconsistency, and makes new passes harder to write. Critical utilities are missing entirely: PCG hash (per-pixel RNG), branchless ONB (Pixar — no pole singularity), equal-area octahedral encoding (better GBuffer normals), cosine hemisphere / uniform sphere / triangle sampling functions, and format packing helpers. Falcor centralizes all of these in `Utils/Math/`.

## What Changes
- Create `include/sampling.hlsli` — 7 centralized sampling functions (Hammersley, disk, hemisphere, sphere, cone, triangle, VMF)
- Create `include/packing.hlsli` — R10G10B10A2, R16G16, snorm/unorm pack/unpack
- Extend `include/math_util.hlsli` — PI variants (`M_2PI`, `M_1_PI`), `pcgHash`, `branchlessONB`, spherical/lat-long coords, `jenkinsHash`
- Extend `include/octahedral.hlsli` — equal-area octahedral encode/decode (@ref Meyer et al. 2010)
- Extract `D_Charlie` + `V_Neubelt` into new `include/sheen.hlsli` (@ref Estevez & Kulla 2017)
- Add `ComputeEnvLOD` (log2-based) and `SolidAngleSample` to `include/ibl.hlsli`
- Update all consumers to use shared functions, remove duplicates

## Impact
- Affected specs: `docs/specs/rendering/shader-library.md`
- Affected code: `math_util.hlsli`, `octahedral.hlsli`, `ibl.hlsli`, `pcss.hlsli`, `ssgi_trace_cs.hlsl`, `deferred_lighting_ps.hlsl`, + 3 new headers
- Breaking change: NO (additive — existing functions kept, consumers updated)
- User benefit: Higher quality GBuffer normals, better RNG, cleaner shader authoring, prerequisite for path tracing and advanced material sampling
