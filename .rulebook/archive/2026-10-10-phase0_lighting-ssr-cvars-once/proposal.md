# The `r.SSR.*` CVars registered twice

## Why

Block A.4 found `r.AerialPerspective` registered with two defaults; the registry is idempotent (a
second registration returns the first), so the effective default depended on static-initialisation
order. Block A.8 listed the remaining doubles and block B.5 recorded five of them still present:
`r.SSR.MaxRoughness`, `r.SSR.NumSteps`, `r.SSR.StartMipLevel` and `r.SSR.SlopeToleranceScale` are
registered in `frame_gi_passes.cpp` (where they are read) and again in `frame_lighting.cpp` (never read
there; `r.SSR.MaxRoughness` with a different range, 0-1 against 0.05-1); `r.SSR.Composite` is registered
in `frame_postprocess.cpp` (read there) and again in `frame_gi_passes.cpp` (never read there). Harmless
today because the defaults agree, and a trap the moment one copy is edited. The user asked for the
pending items to be finished (2026-10-08).

## What changes

- The unused second registrations are removed; a comment at each site says where the CVar lives.
- `scripts/rendering/test_cvar_registrations.py`: a static audit that every `UZ_CVAR_*("name", ...)` under
  `runtime/engine`, `runtime/tools` and `runtime/demos` appears once, with a negative-control fixture
  (`scripts/rendering/test_cvar_registrations_fixture/duplicate.cpp`, a name registered twice, not compiled) and
  the header rule of `cvar.h`.

## Gates (pre-registered)

C1 no CVar name is registered twice across the engine, tools and demos (before: the five above). C2
the fixture's duplicate is reported. C3 no CVar is registered in a header. C4 the SSR passes run with
the surviving registrations: the reference scene captured with `r.SSR 1` reports SSR_* timings within
block B.5's R5 bound (3.0 ms), and (C4b) the `r.SSR.*` values the engine reports are the defaults of the
registrations that survive, parsed from the source (`test_cvar_registrations.py --gpu`); the renderer
gtests in task 3.3.

## Impact
- Affected specs: cvar-registrations (new)
- Affected code: `runtime/engine/renderer/frame/frame_lighting.cpp`, `frame_gi_passes.cpp`;
  `scripts/rendering/test_cvar_registrations.py`, the fixture
- Breaking change: NO (the surviving registrations carry the same defaults)
- User benefit: one place per CVar; the audit guards the next double.

## Out of scope

Other doubles the audit may list outside the lighting files are reported, not changed here.
