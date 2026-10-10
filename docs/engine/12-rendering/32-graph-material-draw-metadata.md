# Graph Material Draw Metadata (Sponza parity, step 2)

<!-- doc-meta
title: Graph Material Draw Metadata
category: reference
engine: uzengine
theme: rendering
created: 2026-09-07
updated: 2026-09-07
status: active
author: sponza-parity-02
-->

## Overview

A mesh whose material is a `.uzmat` graph has no `MaterialInterface`. The graph PSO evaluates the
node network when filling the GBuffer, but every other consumer — the shadow pass, the distance-field
occluder set, the deferred BRDF — reads the ordinary `DrawCommand` fields, and those were left at
engine fallbacks. Sponza is entirely graph-authored (28 `.uzmat`, 138 instances), so:

- authored translucency never reached the shadow or GI paths, and glass occluded like stone;
- authored `specularMultiplier` never reached shading, so every dielectric shaded at a fixed 4 % F0;
- authored base colour, roughness, metallic and AO constants were replaced by defaults;
- the forward translucent shader ignored its emission inputs.

This page describes the metadata contract that closes that gap.

## Authored keys

`MaterialGraph` now stores and round-trips two keys it previously dropped:

```json
{ "blendMode": "Opaque" | "Masked" | "Translucent" | "Additive", "twoSided": false }
```

Both participate in `ComputeContentHash()`, because they change the PSO (blend state, cull mode) and
the occluder set. Absent keys default to `Opaque` / `false`; an unrecognised `blendMode` is a load
error rather than a silent fallback.

## materialFlags layout

`scene_sync.cpp` resolves the graph into the draw command. The bit layout is shared with the
`MaterialInterface` path, so downstream consumers do not care which produced it:

| Bits | Meaning |
|---|---|
| 0-1 | blend mode (0 Opaque, 1 Masked, 2 Translucent, 3 Additive) |
| 2 | ORM-packed: metallic sampled from the B channel, texture bound as AO/ORM |
| 3 | two-sided |
| 8-15 | specular × 255 (graph multiplier 1.0 maps to Specular 0.5, i.e. F0 = 0.04) |
| 16-23 | environment multiplier × 127.5 |
| 24-27 | shading-model id |
| 28-31 | clear-coat roughness, or transmission for ThinTranslucent |

`gbuffer_ps.hlsl` folds bits 8-15 and 24-27 into RT3.a as `specular + smID * 16`, and the deferred
pass rebuilds both by subtracting the id.

## Constant inputs

`GraphConstantInput()` reads a PBR output input that resolves to a compile-time constant
(`ScalarConstant`, `Vector2/3/4Constant`, `ColorConstant`), returning the pin default when the input
is absent or unconnected. When the input is driven by anything else — a texture sample, a math node —
it returns `nullopt` and the caller leaves the field alone rather than substituting a wrong constant.

Texture inputs are resolved by channel: `BaseColor` only binds as albedo for an `RGB`/`RGBA` output,
and `Metallic` sampled from `B` means an ORM-packed map (sets bit 2 and binds it as the AO texture)
while `R`/`RGBA` binds the metallic slot.

## Specular energy in the deferred pass

```hlsl
float  materialSpecular = saturate(packedAlpha - float(smID) * 16.0);
float3 dielectricF0  = 0.08 * materialSpecular;   // Specular 0.5 → F0 0.04
float  dielectricF90 = saturate(50.0 * dielectricF0.g);
float3 F0  = lerp(dielectricF0, baseColor, metallic);
float3 F90 = lerp(dielectricF90, 1.0, metallic);
```

`F90` is Filament's specular micro-occlusion: below roughly 2 % reflectance the grazing response is
extinguished instead of rising to white. Sponza's dirt decal authors `specularMultiplier: 0` and now
has no rim at all, where the previous fixed F0 = 0.04 with F90 = 1 gave it a fresnel edge it never
asked for. The same `F0`/`F90` feed the sun, point and spot loops, the split-sum ambient
(`FssEss = F0*A + F90*B`) and the multi-scatter compensation (`Favg = F0 + (F90 - F0) * 0.047619`).

Two behaviours were retired with it:

- The chrome bypass (`metallic > 0.95 && roughness < 0.1` skipping DFG, multi-scatter and specular
  occlusion) existed to hide a warm ring artifact caused by the incorrect envBRDF fit that
  `31-energy-and-bias-corrections.md` replaced. With the published fit there is nothing to hide.
- `r.IndirectAO.Exponent` now defaults to **1.0** (plain AO). Values above 1 are an artistic
  crevice-darkening choice a scene opts into; the pre-port Sponza calibration used 3.0.

## Translucency

Translucent and additive materials (`materialFlags & 0x3 >= 2`) are skipped in the shadow draw loops
(`frame_shadows.cpp`, two sites) and in `shadow_pass.cpp`. The instanced shadow path draws a batch
without consulting per-draw flags, so it is disabled whenever the list contains such a material and
the per-draw path takes over.

`translucent_ps.hlsl` now adds emission (constant × intensity, optionally modulated by an emissive
texture) to both its glass and its lit output. Sponza's window glass carries a `WindowSunGlow`
constant of `(8.0, 6.5, 4.5)` that was previously discarded.

## Directional light fields

`ApplyDirectionalLightFields` read only `color` and `intensity`. It now also accepts
`atmosphere_sun_light`, `light_source_angle` (degrees, full angular diameter), `shadow_cascades`,
`cascade_distribution_exponent` and `dynamic_shadow_distance`, each with a camelCase alias, and warns
on unrecognised keys instead of ignoring them. Sponza authors `light_source_angle: 0.53` — the sun's
real angular diameter — and was silently getting the 1.2 degree component default, which made every
PCSS penumbra more than twice as wide as authored.

## Re-verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/rendering/test_material_specular.ps1   # 686/686
```

The harness extracts the graph-metadata block from `scene_sync.cpp` and the F0/F90 assignments from
`deferred_lighting_ps.hlsl`, then evaluates them against the reference formulation — it validates the
engine source, not a copy of it.

## Related

- `docs/engine/12-rendering/31-energy-and-bias-corrections.md` — the envBRDF fit this builds on
- `docs/engine/12-rendering/16-material-system.md`, `06-pbr-material-system.md`
- `.rulebook/archive/2026-10-10-phase0_sponza-parity-02-material-graph-energy/`
