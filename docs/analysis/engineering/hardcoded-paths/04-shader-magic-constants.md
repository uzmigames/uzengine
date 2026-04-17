# 04 — Shader Magic Constants Without `@ref` Citation

**Severity:** MEDIUM
**File focus:** `runtime/shaders/runtime/`

Per [AGENTS.override.md](../../../AGENTS.override.md) rendering rules: **every shader numeric constant must be cited with `@ref Author Year "Title"` and `@constants` annotation.** Many shaders have duplicated magic numbers (`0.04` for F0 in 5+ places) with no citation, or the citation exists but the constant is inlined instead of named via `#define`.

## Findings

| # | Category | File:Line | Snippet | Why Bad | Fix |
|---|----------|-----------|---------|---------|-----|
| 4.1 | Dielectric F0 (deferred) | [deferred_lighting_ps.hlsl:823](../../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl#L823) | `float3 dielectricF0 = float3(0.04, 0.04, 0.04)` | F0 hardcoded 5+ times, no `@ref Schlick 1994` | Centralize in `material_constants.hlsli` |
| 4.2 | Roughness floor (deferred) | [deferred_lighting_ps.hlsl:261](../../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl#L261) | `roughness = max(rt1.a, 0.04)` | Inline magic without define | `#define kMinRoughness 0.04` |
| 4.3 | Roughness floor mismatch | [path_tracer_ps.hlsl:163](../../../runtime/shaders/runtime/pathtracing/path_tracer_ps.hlsl#L163) | `roughness = max(0.045, normalData.a)` | Uses `0.045` vs deferred's `0.04` — values differ | Unify in shared include |
| 4.4 | SSR roughness fade | [ssr_resolve_cs.hlsl:97](../../../runtime/shaders/runtime/ssr/ssr_resolve_cs.hlsl#L97) | `saturate(roughness * mask + 2.0)` | Magic `2.0` offset; no Stachowiak 2015 cite | Add `@constants ROUGHNESS_FADE_BIAS = 2.0` |
| 4.5 | SSR normal power | [ssr_resolve_cs.hlsl:178](../../../runtime/shaders/runtime/ssr/ssr_resolve_cs.hlsl#L178) | `pow(max(dot(centerN, sN), 0.0), 512.0)` | `512.0` inline; AMD SSSR paper referenced in comment, not `@ref` | `#define PREFILTER_NORMAL_SIGMA 512` |
| 4.6 | SSR depth weight | [ssr_resolve_cs.hlsl:183](../../../runtime/shaders/runtime/ssr/ssr_resolve_cs.hlsl#L183) | `exp(-depthDiff * centerZ * 4.0)` | `4.0` sigma inline; comment names it but code uses literal | Extract `#define PREFILTER_DEPTH_SIGMA 4.0` |
| 4.7 | GTAO aspect ratio | [gtao_cs.hlsl:252](../../../runtime/shaders/runtime/ao/gtao_cs.hlsl#L252) | Comment: `"1.78× at 16:9"` | Hardcoded 16:9 assumption in shader math | Pass `invAspectRatio` via CB |
| 4.8 | Cloud extinction | [cloud_raymarch_cs.hlsl:469](../../../runtime/shaders/runtime/atmosphere/cloud_raymarch_cs.hlsl#L469) | `1.0f - exp(-cloudDist * 0.04f)` | Extinction coeff `0.04` inline, no citation | `@constants cloudExtinction = 0.04` + Bouthors 2008 ref |
| 4.9 | GBuffer smID pack | [deferred_lighting_ps.hlsl:265](../../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl#L265) | `packedAlpha = specular + smID * 16.0` | `16.0` shading-model scale inline | `#define SHADING_MODEL_SCALE 16.0` |
| 4.10 | Thread group GTAO | [gtao_cs.hlsl:197](../../../runtime/shaders/runtime/ao/gtao_cs.hlsl#L197) | `[numthreads(8, 8, 1)]` | 64 threads; undocumented choice | Comment citing GPU wave size |
| 4.11 | Thread group histogram | [exposure_histogram_cs.hlsl:39](../../../runtime/shaders/runtime/post/exposure_histogram_cs.hlsl#L39) | `[numthreads(16, 16, 1)]` | 256 threads may exceed optimal; no justification | Reduce to 8×8 or cite histogram bin requirement |
| 4.12 | Radiance cache thread | [radiance_cache_trace_cs.hlsl:230](../../../runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl#L230) | `[numthreads(64, 1, 1)]` | 1D 64 threads; differs from 2D 8×8 convention | Document why spatial cache needs 1D |
| 4.13 | SM5 register slots | [debug_view_ps.hlsl:21-26](../../../runtime/shaders/runtime/debug/debug_view_ps.hlsl#L21) | `: register(t0)...: register(t5)` | Fixed slots brittle across variants | Use `BINDLESS_SLOT()` macro |
| 4.14 | MRT slot assumption | [deferred_lighting_ps.hlsl:197-200](../../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl#L197-L200) | `SV_Target0..SV_Target3` (4 MRTs) | 4 slots assumed; not all GPUs guarantee 8+ | Validate MRT count at PSO creation |

## Proposed Shared Header

Create [shaders/runtime/common/material_constants.hlsli](../../../runtime/shaders/runtime/common/material_constants.hlsli) with:

```hlsl
#pragma once

/// PBR material constants — single source of truth across all lighting shaders.
/// @ref Burley 2012 "Physically Based Shading at Disney"
/// @ref Schlick 1994 "An Inexpensive BRDF Model"

/// Dielectric baseline reflectance (F0) for plastics, skin, fabrics.
/// @constants F0_DIELECTRIC = 0.04 (4% reflectance, Karis 2013 UE4 baseline)
#define F0_DIELECTRIC 0.04

/// Minimum GGX alpha to prevent specular aliasing on normal-mapped surfaces.
/// @constants MIN_GGX_ALPHA = 0.0064 (sqrt(0.005) rounded)
#define MIN_GGX_ALPHA 0.0064

/// Shading model ID packed into GBuffer specular alpha channel.
/// @constants SHADING_MODEL_SCALE = 16.0 — allows 16 discrete models (0..15)
#define SHADING_MODEL_SCALE 16.0

/// SSR prefilter coefficients.
/// @ref Stachowiak 2015 "Stochastic Screen-Space Reflections" (SIGGRAPH)
/// @ref AMD 2020 "FidelityFX Stochastic Screen-Space Reflections"
#define PREFILTER_NORMAL_SIGMA 512.0
#define PREFILTER_DEPTH_SIGMA  4.0
#define ROUGHNESS_FADE_BIAS    2.0
```

Then replace all inlined literals (`deferred_lighting_ps.hlsl`, `path_tracer_ps.hlsl`, `ssr_resolve_cs.hlsl`, etc.) with the `#define` names.

## Unresolved Physical Discrepancies

| Shader | Constant | Value | Note |
|--------|----------|-------|------|
| `deferred_lighting_ps.hlsl:261` | roughness floor | `0.04` | Inconsistent with path tracer |
| `path_tracer_ps.hlsl:163` | roughness floor | `0.045` | — |

These must agree. Pick one (physically-motivated value) and document rationale.

## Thread Group Size Justification

Every `[numthreads(X,Y,Z)]` should have a comment explaining the choice:

- `(8,8,1) = 64` — matches AMD RDNA wave32×2 / NV warp32×2; cache-friendly tile
- `(16,16,1) = 256` — only when shared memory requires it (e.g. tile-based lighting)
- `(64,1,1)` — 1D dispatch (particles, probes); justify why not 2D

Currently most files lack this justification.

## Shader Entry Point Rule

Per [.claude/rules/shader-entry-point.md](../../../.claude/rules/shader-entry-point.md): **all HLSL entry points MUST be named `main`**. No violations found in the quick grep — but this rule should be enforced in CI (lint step checking HLSL files).
