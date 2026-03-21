# Proposal: Advanced Material System — Shading Models, Permutations, Decals & Domains (Phase 3a)

## Why

UzEngine currently has a single shading model (DefaultLit) hardcoded into the GBuffer and deferred lighting passes. Every surface — skin, metal, cloth, foliage, glass — evaluates the same Cook-Torrance BRDF. This means:

1. **No skin/SSS** — characters look plastic. Subsurface scattering requires a different diffuse model (wrap lighting + subsurface color) that cannot be expressed with the current single-BRDF pipeline.
2. **No hair rendering** — Marschner/Kajiya-Kay anisotropic specular requires tangent-aligned dual lobes, impossible with isotropic GGX.
3. **No cloth** — velvet, silk, and fabric need the Ashikhmin/Charlie sheen model; GGX produces hard specular highlights that look wrong on soft materials.
4. **No clear coat** — car paint, lacquered surfaces, and wet materials need dual-layer specular (base + coating), which the single-BRDF path cannot do.
5. **No decals** — blood splatter, bullet holes, graffiti, tire marks all require a decal projection system that blends selectively into GBuffer channels.
6. **No unlit materials** — UI elements, holographic effects, and emissive-only surfaces still go through full PBR lighting.
7. **No shader permutation system** — adding new features requires manual `#ifdef` management. There is no formal `ShaderPermutationKey` → PSO cache pipeline.
8. **No material domain separation** — Surface, Decal, PostProcess, UI, and Volume materials all share the same code path.

This task is the **single largest blocker** for rendering content beyond simple PBR props. Every complex model (characters, vehicles, environments with decals, foliage) requires at least 2-3 of these features.

**Supersedes**: `phase3a-3.34.0-shading-models` (no specs, incomplete scope — this task covers shading models + permutations + decals + domains + texture slots as a unified system).

## What Changes

### New Systems

1. **EShadingModel enum** — 10 shading models (Unlit, DefaultLit, Subsurface, SubsurfaceProfile, ClearCoat, TwoSidedFoliage, Hair, Cloth, Eye, ThinTranslucent) stored per-material and encoded in GBuffer.
2. **EMaterialDomain enum** — 5 material domains (Surface, Decal, PostProcess, LightFunction, UI) controlling which render pass processes the material.
3. **ShaderPermutationKey** — 64-bit key encoding shading model + blend mode + feature flags (POM, bindless, instanced, alpha-to-coverage, etc.). Drives PSO cache lookup.
4. **Shader Permutation Compiler** — on-demand compilation of shader variants from permutation keys. Disk cache for compiled PSOs.
5. **GBuffer Shading Model ID** — 4-bit ID packed into GBuffer (stencil or dedicated channel). Deferred lighting branches per shading model.
6. **Decal Render Pass** — projects decal materials onto existing GBuffer surfaces. Per-channel blend control (albedo-only, albedo+normal, full PBR).
7. **Subsurface Scattering Pass** — screen-space separable SSS blur (Jimenez 2015) for Subsurface/SubsurfaceProfile shading models.
8. **New texture slots** — OpacityMask, Tangent, SubsurfaceColor, ClearCoatNormal, Thickness, DetailNormal, DetailAlbedo added to MaterialInterface.
9. **Dithered Opacity** — screen-door transparency for LOD transitions using Bayer matrix dither pattern in the GBuffer pass.

### Refactoring

1. `materialFlags` bits [1:0] blend mode encoding → replaced by `EShadingModel shadingModel` + `EBlendMode blendMode` as separate fields in DrawCommand and PerObjectCB.
2. `PipelineLibrary` fixed PSO getters → replaced by permutation-driven PSO cache (`GetOrCreatePSO(ShaderPermutationKey)`).
3. Deferred lighting pass → add `switch(shadingModelID)` dispatch for per-model BRDF evaluation.
4. Forward pass (mesh_ps) → same shading model dispatch via uniform.

## Impact

- Affected specs: `docs/specs/rendering/shader-variants.md`, `docs/specs/rendering/shader-library.md`, `docs/specs/rendering/ibl-pipeline.md`
- Affected code: `runtime/engine/renderer/material/`, `runtime/engine/renderer/core/pipeline/`, `runtime/engine/renderer/core/gbuffer/`, `runtime/engine/renderer/passes/geometry/`, `runtime/shaders/runtime/geometry/`, `runtime/shaders/runtime/lighting/`, `runtime/engine/renderer/core/params/`
- Breaking change: YES — `materialFlags` encoding changes, PerObjectCB layout expands, GBuffer gains shading model ID channel, PipelineLibrary API changes from fixed getters to permutation-based cache.
- User benefit: Render characters (skin, hair, eyes, cloth), vehicles (clear coat), environments (decals, foliage), UI (unlit), and any AAA-quality content.
- Enables: skeletal mesh characters, hair/fur rendering, vehicle rendering, decal system, material editor in VSCode extension, LOD cross-fade, foliage system, screen-space SSS.
