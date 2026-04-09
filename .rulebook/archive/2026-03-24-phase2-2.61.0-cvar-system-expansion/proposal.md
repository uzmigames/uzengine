# CVar System Expansion — Full Runtime Configurability

## Why

The engine had 47 CVars but ~95 hardcoded constants that should be runtime-configurable.
Artists and developers cannot tune shadow quality, AO radius, SSR steps, exposure speed,
or POM depth without recompiling. This blocks iteration on visual quality and performance
profiling. Expanding CVars from 47→118 enables full runtime tuning via CLI, Lua, or
future editor UI.

## Context

Analysis of the runtime codebase identified ~95 hardcoded constants that should be
exposed as CVars. Currently 47 CVars exist. This task adds Tier 1 (32 high-impact)
and Tier 2 (28 tuning) CVars, plus fixes 3 bugs found during analysis.

## Bugs to Fix First
1. **Bloom defaults inconsistent** — bloom_pass.h struct defaults differ from CVar registration
2. **MaxDeltaTime duplicated** — TimeManager (0.25) vs Application (0.1), consolidate
3. **GTAO shader hardcoded** — shader constants not overridden by CVar values via CB

## Scope
- Register ~60 new CVars across renderer, shadows, post-process, system
- Wire shader-side constants to constant buffers fed by CVars
- Update cvar-reference.md documentation
- Expose all new CVars to Lua via existing uz.cvar API (automatic — no code needed)
