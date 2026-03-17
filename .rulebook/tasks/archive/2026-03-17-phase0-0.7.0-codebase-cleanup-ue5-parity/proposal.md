# Codebase Cleanup & UE5 Parity Audit — Phase 0 Hygiene

## Priority: P0 — Technical debt, UE5 parity violations, dead code removal

## Why

Full codebase audit found 44 technical debt items including UE5-incompatible approximations, dead code, architecture violations, and stubs that violate CLAUDE.md rules. Must be cleaned up to maintain code quality and UE5 parity.

## Summary

Full codebase audit identified **44 issues** across 6 categories: UE5-incompatible approximations, dead/orphaned code, architecture violations, stubs, magic numbers without citations, and Y-up documentation errors. These violations directly contradict CLAUDE.md rules (zero-creativity policy, no stubs, no TODOs, no analytical fallbacks). This task systematically resolves every item.

## Motivation

1. **5 P0 violations** of the "UE5 Exact Parity — Zero Creativity Policy" exist in shipping shaders (invented SH fallback, forbidden minimum clamps, compensation factors, empirical multi-scatter)
2. **6 orphaned systems** compile but are never called — dead weight in build times and cognitive load
3. **18 raw `assert()` calls** bypass the engine's `UZ_CHECK`/`UZ_ENSURE` crash reporting
4. **15+ stub functions** pretend to work but silently do nothing (Lua texture APIs, runtime init, async mipgen)
5. Forward and deferred lighting use **different attenuation formulas** — visible inconsistency

## UE5 References

| Issue | UE5 Source | UE5 Approach |
|-------|-----------|--------------|
| Aerial perspective | `SkyAtmosphere.usf` — AerialPerspectiveLUT | 3D volume texture, not per-pixel Rayleigh |
| Multi-scattering | `SkyAtmosphere.usf` — MultipleScatteringTexture | Precomputed LUT, not `*0.5` empirical |
| Light attenuation | `DeferredLightingCommon.ush` | `(1-(d/r)^4)^2` — fourth power windowing |
| ACES tonemap | `ACES.ush` / `PostProcessCombineLUTs.usf` | Full ACES RRT+ODT, not Narkowicz polynomial |
| Shadow transition | `ShadowRendering.cpp` | `kShadowTransitionScale = 60.0` with VS depth bias |
| Ground albedo | `SkyAtmosphereComponent` | Default `(0,0,0)` — no Lambertian ground reflection |
| Ray march steps | `SkyAtmosphere.usf` — `SampleCountMax` | 20 steps, not 16 |
| k-remapping direct | `ShadingModels.ush` | `(roughness+1)^2 / 8` for direct lights |

## Scope

- **44 items** across ~30 files (shaders + C++ engine code)
- **Risk**: LOW per-item (each is a localized fix), MEDIUM aggregate (many files touched)
- **Performance**: Neutral or positive (removing dead code, fixing WaitIdle)
- **Visual**: Fixes forward/deferred attenuation mismatch, corrects atmosphere approximations

## Acceptance Criteria

1. Zero TODO/FIXME/HACK/TEMP comments remain in engine source
2. Zero raw `assert()` in engine code (all converted to `UZ_CHECK`/`UZ_ENSURE`)
3. Zero orphaned/dead systems (removed or properly wired)
4. Zero invented parameter values in shaders (all cite UE5 file/line)
5. Zero minimum clamps or compensation factors without UE5 source
6. Forward and deferred lighting use identical attenuation formula
7. All doc comments use correct Z-up terminology
8. All stub functions either implemented or removed with clear error
9. `vulkan_device.cpp` split to under 1500 lines
