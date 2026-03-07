# Proposal: phase2-pbr-quality-improvements

## Why

The UzEngine forward PBR renderer has a working Cook-Torrance BRDF pipeline with GGX/Schlick/Burley,
but three critical quality gaps prevent it from reaching professional reference quality:

1. **Scene lighting data is never passed to the shader** — `LightFrameCB` in `static_mesh_renderer.cpp`
   is populated with hardcoded values (direction `(0.3,-0.8,0.5)`, intensity `1.0`) instead of using
   the `cachedLight_` collected from the scene. The `Sun` node in `.uzscene` with
   `light_color = 1.0 0.95 0.88` and `light_intensity = 3.0` has zero effect on the render.
   Camera world position is also hardcoded to `(0,0,0)`, which makes the specular highlight
   computed from the wrong viewpoint on every frame.

2. **No surface displacement** — stone brick materials look flat despite having a 4K displacement
   map available (`StoneBricksSplitface001_DISP_4K.png`). Parallax Occlusion Mapping (POM) is the
   technique used in Unreal Engine to give the illusion of depth on flat geometry without geometry
   subdivision. It is a pixel-shader-only change that dramatically improves surface realism.

3. **`.uzmat` has no displacement slot** — the material format does not expose a `displacement_texture`
   or `parallax_scale` field, so even if POM were implemented in the shader there is no way to
   author it from a scene file.

Fixing these three gaps will close the visual gap between the current render and a Unreal Engine
reference render for the stone brick test scene.

## What Changes

### 1. Scene Light Data Pipeline Fix

- `StaticMeshRenderer::draw()` receives a `LightState` argument (already computed by
  `light_collect_primary()` in `renderer.cpp`) and uses it to populate `LightFrameCB` slots 0-11
  (direction, intensity, color from the `Sun` directional light node).
- Camera world position is extracted from `camState` and forwarded to `StaticMeshRenderer::draw()`
  so `LightFrameCB` slot 97-99 is correct.
- `static_mesh_renderer.h` draw signature updated:
  `draw(RhiCmdList, const Mat4& viewProj, const LightState& light, const uz::Vec3& cameraPos)`.

### 2. Parallax Occlusion Mapping in pbr.ps.hlsl

- Add `Texture2D displacementTex : register(t5)` to `pbr.ps.hlsl`.
- Implement `ParallaxOcclusionMapping(UV, V_tangent, scale, minLayers, maxLayers)` — standard
  steep POM loop (8-32 iterations based on NdotV), binary refinement (4 steps), UV offset output.
- All downstream texture samples (albedo, normal, roughness, AO) use the offset UV.
- POM is gated on `parallaxScale > 0.0` to remain a no-op for materials without a displacement map.
- `MaterialCB` gains a `parallaxScale` float (replaces existing `_matPad.x`).

### 3. Material Format Extension

- `stonebricks.uzmat` gains `displacement_texture` and `parallax_scale` fields.
- `material.cpp` parser reads both fields; `MaterialData` struct gains `displacement_path[260]`
  and `parallax_scale` float.
- `MeshInstance` gains `displacement_tex RhiTexture` and `parallax_scale float`.
- Root signature slot `t5` wired up in `static_mesh_renderer.cpp`.

## Impact

- Affected specs: `docs/specs/renderer.md`, `docs/architecture/render-pipeline.md`
- Affected code:
  - `src/engine/renderer/components/static_mesh_renderer.h/.cpp`
  - `src/engine/renderer/shaders/pbr.ps.hlsl`
  - `src/engine/renderer/resources/material.h/.cpp`
  - `assets/materials/stonebricks.uzmat`
  - `src/engine/renderer/rhi/rhi_dx12.c` (root signature slot t5)
- Breaking change: YES — `StaticMeshRenderer::draw()` signature changes; all call sites in
  `renderer.cpp` must pass `LightState` and `cameraPos`. Currently only one call site.
- User benefit: Scene light authoring (color, intensity, direction via Sun node rotation) works
  as expected; stone brick surface shows convincing depth/relief matching Unreal reference quality.
