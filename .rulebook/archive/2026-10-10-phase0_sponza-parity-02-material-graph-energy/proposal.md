# Proposal: phase0_sponza-parity-02-material-graph-energy

## Why

Sponza's prefab uses `material_graph` references (28 materials: 26 Opaque, 1 Translucent, 1 Masked).
SponzaTest (`docs/analysis/rendering/sponza-graph-material-evidence.md`, commit `97acd374`) found
that our runtime discards most of what those graphs author:

- `MaterialGraph` has no `blendMode`/`twoSided` storage; `FromJson` only reads domain and
  shadingModel, so the Translucent glass enters the GDF and the shadow pass as a solid occluder.
- `scene_sync.cpp` fills `materialGraphPipeline`/`materialTexIndices` for graph materials but leaves
  the ordinary PBR draw fields at fallback values → GI and shadow paths see default white diffuse.
- `specularMultiplier` never reaches composition: every dielectric shades with fixed F0 = 0.04 and
  F90 = 1.0. The dirt decal authors specular 0 and should have no grazing reflectance
  (Filament micro-occlusion: `F90 = saturate(50 * F0.g)`).
- `translucent_ps.hlsl` ignores its emission inputs, so the `WindowSunGlow` glass constant is lost.

With the manual `roughnessMultiplier 0.55 / metallicMultiplier 0.0 / warm baseColorTint` removed
(2026-09-07, backup in `sample/reference/sponza-manual-calibration/`), materials are now neutral
pass-through and the renderer must get its energy from the authored data.

## What Changes

- `runtime/engine/material/material_graph.{h,cpp}`: `MaterialGraphBlendMode` enum, `blendMode`,
  `twoSided`, JSON round-trip.
- `runtime/engine/material/compiler/hlsl_material_translator.cpp`: emit specular into the GBuffer
  packed alpha slot (`packedAlpha = shadingModelID*16 + specular`).
- `runtime/engine/renderer/scene/scene_sync.cpp` (+132 in SponzaTest): resolve ordinary draw metadata
  (texture roles, constants, blend mode, two-sided) for graph-only materials.
- `runtime/engine/renderer/core/pipeline/pipeline_geometry.cpp`, `pipeline_shadow.cpp`,
  `material/material_pipeline.cpp`: two-sided raster state; translucent excluded from shadow casters.
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`: `materialSpecular = saturate(packedAlpha - smID*16)`,
  `dielectricF0 = 0.08 * specular`, `dielectricF90 = saturate(50 * F0.g)`, `F0/F90 = lerp(dielectric, baseColor/1, metallic)`,
  `F_Schlick(F0, F90, VoH)` for sun/point/spot, multi-scatter terms with F90, and the simplified
  ambient combine (`ambient = diffuseAmbient * ao * skyTintDiffuse + specularAmbient * specOcclusion * skyTintSpecular`).
- `runtime/shaders/runtime/gbuffer/translucent_ps.hlsl`: honour emission inputs.
- Port `scripts/test_material_specular.{cpp,ps1}` (686/686) and the BRDF check (200/200).

Preserve on our side: local-light shadow sampling in the spot loop, `r.IndirectAO.Exponent` CVar
(verify its default is 1.0 so removing the `pow` from the combine is behaviour-neutral for other
scenes, or keep the `pow` guarded by `exponent != 1`).

## Impact
- Affected specs: material graph JSON contract gains `blendMode`/`twoSided` keys (additive)
- Affected code: material graph, translator, scene sync, geometry/shadow pipelines, deferred + translucent shaders
- Breaking change: NO (defaults reproduce current behaviour for graphs that omit the keys)
- User benefit: glass is translucent and emissive, dirt decal has no fake fresnel rim, every dielectric uses its authored specular
