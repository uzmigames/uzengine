# Proposal: phase3-area-lights-ltc

## Why
The engine currently supports only point, spot, and directional lights — all infinitely small sources that produce hard-edged highlights regardless of the light's apparent size. Real-world lighting is dominated by area sources: fluorescent panels on office ceilings, LED strips under shelves, monitor screens, windows, and car headlights all have a physical extent that produces soft, elongated specular highlights and gradual falloff. Without area lights, architectural visualization scenes and modern game interiors immediately look wrong. The Linearly Transformed Cosines (LTC) technique by Heitz et al. approximates the GGX BRDF integral over an area light source using two 64x64 LUT lookups, giving accurate soft highlights at a cost of roughly two texture samples per pixel — making it the standard real-time area light solution.

## What Changes
- Precompute two LTC coefficient LUTs (64x64 RG16F each): `ltc_mat.dds` (3x3 matrix inverse coefficients encoded as RGBA) and `ltc_amp.dds` (amplitude + Fresnel normalization), stored as engine data assets and loaded at startup
- Implement `AreaLightEval.hlsli` in `shaders/include/`: functions `EvalRectLight`, `EvalTubeLight`, `EvalDiskLight` that transform the area shape into the LTC-normalized hemisphere and integrate using polygon solid-angle formulas
- `CAreaLight` ECS component: `type` (rect/tube/disk), `width`, `height` (rect/disk), `length` (tube), `color`, `intensity`, `two_sided` (bool for rect), `cast_specular`, `cast_diffuse`
- Integrate area light evaluation into the PBR pixel shader: for each `CAreaLight` in the clustered light list, call the appropriate `Eval*` function and accumulate diffuse + specular contributions
- Editor gizmos: `AreaLightGizmo.cpp` renders a wireframe rectangle, tube, or disk matching the light's physical shape in the scene viewport; uses the existing gizmo DX12 editor pass
- Inspector panel: expose all `CAreaLight` fields with type-appropriate controls (type enum dropdown, width/height/length sliders, color picker, intensity float, checkboxes)
- Area lights participate in the 3D clustered shading system: their AABB bounding volume is computed from the shape extents + range and inserted into the cluster assignment compute pass
- Area lights do not cast shadows in this initial implementation; a `no_shadows` flag is set automatically and documented as a known limitation

## Impact
- Affected specs: `renderer.md`, `shader-library.md`, `editor-panels.md`, `editor-gizmos.md`, `point-spot-light-shadows.md`
- Affected code: `src/renderer/passes/PBRLightingPass.cpp`, `shaders/include/AreaLightEval.hlsli`, `shaders/pbr/PBR.hlsl`, `src/ecs/components/CAreaLight.h`, `src/editor/gizmos/AreaLightGizmo.cpp`, `data/engine/ltc_mat.dds`, `data/engine/ltc_amp.dds`
- Breaking change: NO
- User benefit: Artists can place rectangular panel lights, tube lights, and disk lights to produce the soft, physically correct area lighting seen in architectural visualization and modern AAA game interiors. Specular highlights correctly elongate and soften to match the light's physical shape, eliminating the pinpoint specular artifacts of point lights on smooth surfaces.
