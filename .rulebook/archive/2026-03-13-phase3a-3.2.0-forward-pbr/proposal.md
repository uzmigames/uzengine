# Proposal: 3.2.0 — Forward PBR

## Why
Validates the full material → mesh → draw → light pipeline with a single directional light before adding deferred complexity. Proves Cook-Torrance BRDF and sort-key rendering work end-to-end.

## What Changes
- SimplePBRMaterial: albedo + normal + metallic/roughness textures + scalar params
- Cook-Torrance BRDF pixel shader: GGX NDF, Smith G, Schlick Fresnel
- Single directional light in forward pass
- 64-bit sort-key rendering (material, mesh, distance) — no switch-on-type in draw loop

## Impact
- Affected specs: `docs/engine/12-rendering/`
- Affected code: `runtime/core/renderer/materials/`, `runtime/core/renderer/draw/`, shaders
- Breaking change: NO
- User benefit: First physically-based lit rendering, material system foundation
