# Proposal: phase1_falcor-layered-materials

## Why
UzEngine supports a single-layer PBR material (GGX specular + Burley diffuse). Falcor has `LayeredBSDF.slang` for two-layer materials: a dielectric coat on top of a base layer. This is required for car paint (metallic base + lacquer coat), lacquered wood, wet surfaces (rough diffuse base + smooth water coat), coated metals, and varnished plastics. Single-layer PBR cannot represent these because it has no way to model the energy attenuation as light passes through the coat and into the base, nor the secondary specular highlight from the coat surface. Requires: shader variant system (for `materialType=4` ClearCoat dispatch path).

## What Changes
- `include/clearcoat.hlsli`: `EvaluateClearCoatBRDF(N, V, L, coatRoughness, coatIOR) → ClearCoatResult { float3 coatSpecular; float coatFresnel; }`. Clear-coat uses GGX specular with coat-specific roughness and IOR 1.5 (dielectric glass). Energy compensation between layers via Kulla-Conty 2017. @ref Kulla & Conty 2017 "Revisiting Physically Based Shading at Imageworks"
- Base layer attenuation: `baseContribution *= (1.0 - coatFresnel)²` — light passes through coat twice (entry and exit). Attenuated base uses existing GGX + Burley evaluation from `brdf.hlsli`.
- Combined output: `totalContrib = coatSpecular + baseContribution`. This is the two-layer separable BSDF.
- GBuffer extension: store `coatRoughness` (R8) and `coatIOR` (encoded as `(IOR - 1.0) / 2.0` in R8) in spare GBuffer channel. `materialType=4` (ClearCoat) stored in existing material type bits.
- Deferred lighting: `materialType=4` dispatch in `deferred_lighting_ps.hlsl` reads GBuffer coat parameters and calls `EvaluateClearCoatBRDF` + attenuated base evaluation. Guarded by `CLEARCOAT` variant define.
- Material system: `ClearCoatMaterial` extends `StandardMaterial` with `coatRoughness = 0.0` and `coatIOR = 1.5` (physical defaults for glass coat)
- Affected files: new `include/clearcoat.hlsli`, `deferred_lighting_ps.hlsl`, `material.h`, `gbuffer_ps.hlsl` (GBuffer layout), `gbuffer.h` (layout constants)

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/shader-system.md`
- Affected code: `include/clearcoat.hlsli`, `deferred_lighting_ps.hlsl`, `material.h`, GBuffer layout files
- Breaking change: MINOR — GBuffer layout changes to add coat channels; all existing passes must handle the new layout without reading uninitialized data
- User benefit: Car paint, wet surfaces, lacquered wood, coated metals all render correctly with physically accurate two-layer response; coat IOR and roughness are artist-controllable per material
