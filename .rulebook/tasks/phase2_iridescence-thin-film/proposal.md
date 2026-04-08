# Proposal: Iridescence / Thin-Film Interference (Phase 2)

## Why
Iridescence (soap bubbles, oil slicks, beetle shells, car paint) is a glTF standard feature (KHR_materials_iridescence) implemented in Three.js but missing from UzEngine, UE5, and Godot. Adding it differentiates UzEngine's material system. The technique (Belcour & Barla 2017) modifies the Fresnel term based on thin-film thickness and viewing angle — a 3-4 day implementation.

## What Changes

### New Systems
1. `EvalIridescence(outsideIOR, iridescenceIOR, filmThickness, cosTheta)` — Thin-film Fresnel modification in new `iridescence.hlsli`
2. `SHADING_MODEL_IRIDESCENT` material variant flag

### Refactoring
- standard_material.hlsli: add iridescence dispatch path that replaces standard Fresnel with thin-film Fresnel
- PBRMaterial struct: add `iridescence` (0-1), `iridescenceIOR` (default 1.3), `iridescenceThicknessMin/Max`

## Impact
- Affected specs: docs/specs/rendering/shader-library.md, docs/specs/rendering/shader-variants.md
- Affected code: NEW runtime/shaders/runtime/include/iridescence.hlsli, runtime/shaders/runtime/include/standard_material.hlsli, runtime/engine/renderer/material/
- Breaking change: NO — additive material feature
- User benefit: Soap bubbles, oil slicks, beetle shells, car paint effects
- Enables: Full glTF KHR_materials_iridescence import support
