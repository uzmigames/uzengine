# Design: CVars registered once

## Before
`frame_lighting.cpp` registered `r.SSR.MaxRoughness` (range 0-1), `r.SSR.NumSteps`,
`r.SSR.StartMipLevel` and `r.SSR.SlopeToleranceScale` next to `r.SSR`, and read none of them;
`frame_gi_passes.cpp` registered the same four (where they are read) and `r.SSR.Composite`, which only
`frame_postprocess.cpp` reads. `CVarRegistry::Register` returns the existing CVar for a repeated name,
so whichever translation unit's statics ran first owned the default and the range.

## After
- `frame_lighting.cpp` keeps `r.SSR` only; `frame_gi_passes.cpp` keeps its four and drops
  `r.SSR.Composite`; each site carries a comment naming the owner.
- `scripts/test_cvar_registrations.py` scans the sources for `UZ_CVAR_(FLOAT|BOOL|INT)("name"` and
  reports names with more than one site; the fixture proves the detection; headers are rejected.

## Harness
`scripts/test_cvar_registrations.py` (C1-C3; `--gpu` for C4: the reference scene captured with
`r.SSR 1`, the SSR_* timings within block B.5's R5 bound and the reported `r.SSR.*` values equal to the
surviving registrations' defaults parsed from the source) and the renderer gtests.
