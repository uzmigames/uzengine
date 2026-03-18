# CHANGELOG v0.2.37 — Material Graph System + Rendering Fixes

**Date:** 2026-03-18

## New Features

### Material Graph System (`runtime/engine/material/`)
- **MaterialExpression** — Base expression (node) class with 37 built-in types across 8 categories: constants, math (20 ops), texture sampling, coordinates, procedural (checker, noise, gradient), tri-planar mapping, utility (fresnel, panner, component mask, append), and runtime parameters.
- **MaterialGraph** — DAG container with typed pin connections, cycle detection, topological sort with dead-code elimination, xxHash64 content hashing, and `.uzmat` JSON serialization via nlohmann/json.
- **HlslMaterialTranslator** — Compiles MaterialGraph to HLSL `ComputeMaterialOutputs()` function. Memoized expression emission, automatic type promotion (float↔float3 splat/truncation), parameter slot allocation, texture slot allocation.
- **MaterialShaderCache** — Content-addressed HLSL cache keyed by xxHash64 of graph topology + parameters. Cache hit skips recompilation.
- **MaterialAssetGraph** — `.uzmat` file loader + compile orchestrator. Bridges the graph system to the renderer.
- **MaterialBaker** — CPU-side graph evaluator that bakes UV-based materials to RGBA8 textures. Handles all 37 expression types. Used for preview and fallback rendering.
- **MI_ProcGrid.uzmat** — UE5 Basic Scene procedural checker floor replicated as a `.uzmat` graph: TexCoord → CheckerPattern(scale=16) → Lerp(light gray 0.85, dark gray 0.65) → BaseColor, with varying roughness (0.3/0.65).

### Rulebook Task
- Created `phase3-3.2.0-material-graph` task with proposal, 35-item checklist, and OpenSpec-compatible spec (8 requirements, 14 Given/When/Then scenarios).

## Rendering Fixes

### Contact Shadows — Smooth Falloff
- **Fixed binary 0/1 contact shadows** — `ComputeContactShadow()` now uses `smoothstep` falloff based on hit distance along the ray (closer hit = stronger shadow). Added camera distance fade at 5000cm (50m). Matches PoC1 visual quality.
- File: `runtime/shaders/runtime/include/contact_shadows.hlsli`

### Sky Atmosphere — Jitter Fix
- **Fixed sky/sun disk jitter at large camera distances** — Ray direction reconstruction via `worldFar - cameraPos` suffered catastrophic cancellation. Now uses near/far clip subtraction (`world0 - world1`) where camera translation cancels exactly.
- File: `runtime/shaders/runtime/atmosphere/sky_atmosphere_ps.hlsl`

### Height Fog — UE5 Parity
- **Fog parameters matched to UE5 Basic Scene** — density=0.0436, baseHeight=-5000, fogColor=black, startDistance=0. Fog inscattering comes from SkyAtmosphere ambient injection (skyLightColor), not from fog color directly.
- **skyLightColor fix** — Fog now uses `SkyLight.color × intensity` directly instead of SH L0 coefficients which were scaled by ~0.28 (diffuse convolution constant).
- File: `runtime/engine/renderer/frame/frame_lighting.cpp`

### Aerial Perspective — Anti-Double-Fog
- **Fixed AP darkening without compensation** — When heightFogContribution=1.0, AP transmittance was darkening the scene but inscattering was zeroed (`apInscatterScale = 1.0 - 1.0 = 0.0`). Now uses `lerp(ap.a, 1.0, hfc)` — AP is fully disabled when height fog handles all atmospheric haze.
- File: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`

## Analysis Documents
- `docs/analysis/ue5-exponential-height-fog-defaults.md` — UE5 ExponentialHeightFog defaults with file:line citations
- `docs/analysis/ue5-material-system-*.md` — 4 documents analyzing UE5 material system architecture, patterns, directory structure

## File Summary
- **12 new C++ files** in `runtime/engine/material/`
- **1 new .uzmat asset** (MI_ProcGrid procedural checker)
- **4 modified shaders** (contact shadows, sky atmosphere, deferred lighting, height fog)
- **3 modified C++ files** (frame_lighting, pbr_test demo, build system)
- **5 new analysis documents**
