# Proposal: phase0_falcor-critical-shader-bugs

## Why
Falcor analysis audit (2026-03-30) discovered 14 critical shader bugs that produce wrong output or silently fail at runtime. Two compute shaders (`draw_compact_cs`, `mipgen_cs`) use `CSMain` entry points instead of `main` — ShaderManager compiles with `entryPoint="main"` so both silently fail. Particle mesh instancing has a data race (missing `GroupMemoryBarrierWithGroupSync`). Aerial perspective returns hardcoded `float4(0,0,0,1)`. Shadow debug grid functions are all stubs. SSGI reads undefined mip data. SDF cone trace has wrong ray direction. These bugs cause incorrect rendering, missing features, and intermittent visual artifacts.

## What Changes
- Fix 3 entry point naming violations (`CSMain` → `main`)
- Fix 1 data race in particle mesh instancing (add barrier)
- Implement 2 stub functions (aerial perspective, shadow debug grid)
- Fix 4 data flow bugs (SSGI mip chain, SDF ray direction, global SDF AABB, cone trace scale)
- Fix 3 code quality issues (GBuffer OctEncode duplicate, particle sort index, cubemap axis)

## Impact
- Affected specs: `docs/specs/rendering/gi-system.md`, `docs/specs/rendering/shadow-maps.md`
- Affected code: 14 shader files across `compute/`, `vfx/`, `include/`, `ssgi/`, `gi/`, `geometry/`, `ibl/`
- Breaking change: NO (fixes only — all changes produce correct output where before it was wrong or missing)
- User benefit: Draw compaction and mipgen actually work; aerial perspective haze visible on distant objects; shadow debug grid functional; SSGI reads valid data; particles render in correct order
