# Proposal: Anisotropic GGX BRDF (Phase 2)

## Why
Brushed metal, fabric, hair, and carbon fiber all exhibit anisotropic reflection. UzEngine only supports isotropic GGX — materials with directional microstructure look incorrect. Anisotropic GGX is a glTF standard (KHR_materials_anisotropy), implemented in UE5, Godot 4, Three.js, and Falcor. This is the most visible PBR gap identified in the renderer AAA analysis (docs/analysis/renderer-aaa/gap-analysis.md P0.2).

## What Changes

### New Systems
1. `D_GGXAniso(ax, ay, NoH, XoH, YoH)` — Anisotropic GGX NDF in brdf.hlsli
2. `V_SmithGGXCorrelated_Aniso(ax, ay, NoV, NoL, XoV, XoL, YoV, YoL)` — Anisotropic Smith visibility
3. Tangent/bitangent support in GBuffer — encode anisotropy direction in material system
4. `SHADING_MODEL_ANISOTROPIC` variant flag in standard_material.hlsli

### Refactoring
- brdf.hlsli: add aniso variants alongside isotropic functions
- standard_material.hlsli: add anisotropic shading model dispatch
- gbuffer_ps.hlsl: pack anisotropy direction + strength into GBuffer
- PBRMaterial struct: add `anisotropy` (float) and `anisotropyRotation` (float) fields

## Impact
- Affected specs: docs/specs/rendering/shader-library.md, docs/specs/rendering/shader-variants.md
- Affected code: runtime/shaders/runtime/include/brdf.hlsli, runtime/shaders/runtime/include/standard_material.hlsli, runtime/shaders/runtime/geometry/gbuffer_ps.hlsl, runtime/engine/renderer/material/
- Breaking change: NO — additive, existing materials unaffected
- User benefit: Correct rendering of brushed metal, fabric, hair, carbon fiber
- Enables: glTF KHR_materials_anisotropy import
