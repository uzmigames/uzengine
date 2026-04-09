# UE5 Rendering Parameter Parity — Exact Numeric Match

## Priority: P0 — Visual quality gap traced to 12 specific parameter/formula mismatches

## Why

Comprehensive analysis (`docs/analysis/ue5-uzengine-rendering-parity-comparison.md`) identified 12 concrete parameter and formula mismatches between UzEngine and UE5 5.6. Current parity is 81%. The PBR demo produces visibly darker, flatter, hazier output compared to an equivalent UE5 scene with default settings. Root causes are specific numeric constants, formula choices, and missing post-process stages — each traced to exact UE5 source file:line.

## What Changes

### Critical Fixes (1-line changes, immediate visual impact)
1. **Mie Absorption 10x Error** — `4.4e-3` should be `4.44e-4` per UE5 SkyAtmosphereComponent.cpp:114
2. **Burley Diffuse BRDF** — replace Lambertian with Disney Burley per UE5 BRDF.ush:177-189
3. **Directional Light Intensity** — default `1.0` should be `10.0` per UE5 DirectionalLightComponent.cpp:1004

### High Priority (Formula replacements)
4. **UE5 ACES FilmToneMap** — replace Narkowicz polynomial with full RRT+ODT + 5 artist params
5. **Smith Joint Geometry** — replace separate Schlick with SmithJointApprox per UE5 BRDF.ush:346
6. **k-remapping per context** — IBL uses `sqrt(a2)*0.5`, direct uses `(r+1)^2/8` per UE5 BRDF.ush:329
7. **GTAO Screen-Space AO** — implement Ground Truth Ambient Occlusion

### Medium Priority (Polish)
8. **Multi-scatter energy compensation** — analytical Karis 2013 approximation
9. **PCF adaptive radius** — per-cascade filter width scaling
10. **Bloom post-process** — multi-pass gaussian bloom with threshold
11. **Color Grading LUT** — 32x32x32 3D LUT from artist parameters
12. **Shadow bias per cascade** — scale depth bias by cascade resolution

## Impact
- Affected specs: `docs/specs/rendering/renderer.md`, `docs/specs/rendering/ibl-pipeline.md`
- Affected code: `runtime/shaders/`, `runtime/engine/renderer/`, `runtime/engine/ecs/`
- Breaking change: YES — directional light intensity 10x changes all existing scenes
- User benefit: Visual output matches UE5 default scene appearance (95%+ parity)
