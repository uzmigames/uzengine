# Lighting side item — every CVar registered once

Rulebook task `phase0_lighting-ssr-cvars-once` (proposal, design, spec `cvar-registrations`). Branch
`feat/sponza-gi-parity`, 2026-10-08. Block A.8's list, block B.5's note.

## Before

`CVarRegistry::Register` is idempotent: a second registration of a name returns the first CVar, so
whichever translation unit's statics ran first owned the default and the range (block A.4 found
`r.AerialPerspective` with two defaults). The audit (`scripts/rendering/test_cvar_registrations.py`, 268 names)
found nine names registered twice: `r.SSR.MaxRoughness` (ranges 0.05-1 and 0-1), `r.SSR.NumSteps`,
`r.SSR.StartMipLevel`, `r.SSR.SlopeToleranceScale` (frame_gi_passes.cpp, read; frame_lighting.cpp,
never read), `r.SSR.Composite` (frame_postprocess.cpp, read; frame_gi_passes.cpp, never read), `r.Fog`,
`r.Clouds`, `r.SSGI` (fog_renderer.cpp, cloud_pass.cpp, ssgi_renderer.cpp; frame_lighting.cpp, never
read) and `r.RayTracing.GI` (rt_manager.cpp and frame_gi_passes.cpp, both read). `cvar.h`'s usage
example quoted a registration in a comment, which the audit learned to skip.

## What changed

- The eight never-read copies removed, with a comment at each site naming the owner; `frame_gi_passes.cpp`
  reads `r.RayTracing.GI` through `CVarRegistry::Get().Find` at its one use.
- `scripts/rendering/test_cvar_registrations.py`: C1 every name once across `runtime/engine`, `runtime/tools` and
  `runtime/demos` (comments stripped), C2 the fixture with a duplicate is reported, C3 no registration
  in a header.

## Gates (`scripts/rendering/test_cvar_registrations.py`)

| Gate | Criterion | Result |
|---|---|---|
| C1 | every CVar is registered once across runtime/engine, tools and demos | PASS: 269 names; duplicates: none |
| C2 | negative control: the fixture with a name registered twice is reported | PASS: ['r.Fixture.Duplicate'] |
| C3 | no CVar is registered in a header | PASS: none |
| C4 | the SSR passes run with the surviving registrations (reference scene, r.SSR 1): SSR_* timings present and <= 3.0 ms | PASS: {'SSR_CopyPrevColor': 0.012, 'SSR_PrepareIndirect': 0.001, 'SSR_Resolve': 0.016, 'SSR_SpatialDenoise': 0.017, 'SSR_Temporal': 0.017, 'SSR_TileClassify': 0.027, 'SSR_Trace': 0.029} total 0.119 ms |
| C4b | the r.SSR.* values the engine reports are the surviving registrations' defaults | PASS: {'r.SSR.MaxRoughness': 0.35, 'r.SSR.NumSteps': 16.0, 'r.SSR.StartMipLevel': 1.0, 'r.SSR.SlopeToleranceScale': 4.0, 'r.SSR.Composite': 0.0} |

## Still open

- The audit is a source scan: a registration built from a macro or a string variable would escape it.
