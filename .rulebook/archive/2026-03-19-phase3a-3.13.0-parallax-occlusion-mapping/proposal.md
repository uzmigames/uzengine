# Parallax Occlusion Mapping (POM)

## Summary

Implement Parallax Occlusion Mapping (POM) as a material feature for the UzEngine renderer. POM simulates surface depth displacement by ray marching through a heightmap in the pixel shader, creating the illusion of 3D surface detail without additional geometry. This is a standard rendering technique found in UE5 and Godot 4.

## Motivation

Current materials are flat — normal maps provide shading variation but no depth parallax. POM adds convincing depth to surfaces like brick walls, cobblestones, rocky terrain, and tiled floors at minimal geometry cost. It's essential for AAA-quality material rendering.

## Reference Engine Analysis

### Unreal Engine 5
- **Material node**: `ParallaxOcclusionMapping` custom expression in the material graph
- **Algorithm**: Steep parallax ray march (tangent space) + binary search refinement (3 iterations)
- **Adaptive steps**: 8–128 steps based on view angle (more at grazing angles via `lerp(minSteps, maxSteps, dot(N, V))`)
- **Self-shadowing**: Secondary ray march along light direction through heightmap → contact shadows in crevices
- **Silhouette**: Optional geometry expansion via tessellation (not required for basic POM)
- **Distance fade**: Smooth transition to flat UV at distance via `saturate((fadeEnd - dist) / (fadeEnd - fadeStart))`
- **Output**: UV offset applied to all texture lookups (albedo, normal, roughness, etc.)

### Godot 4
- **Material property**: `StandardMaterial3D::heightmap_enabled` + `heightmap_deep_parallax`
- **Algorithm**: Steep parallax with configurable step count (min_layers=5, max_layers=20)
- **Parameters**: `heightmap_scale` (depth), `heightmap_flip_tangent`, `heightmap_flip_binormal`
- **Integration**: Directly in `scene.glsl` — UV offset computed early, applied to all texture samples
- **No self-shadowing**: Godot does not implement POM self-shadows

### UzEngine Approach
Take the best of both: UE5's quality (adaptive steps, binary refinement, self-shadowing) with Godot's simplicity (material params, shader include). Implement as an HLSL include (`pom.hlsli`) that any material shader can `#include`.

## Impact

- **Shaders**: New `pom.hlsli` include + modifications to `mesh_ps.hlsl` (POM variant)
- **Material system**: New per-material params (`displacementScale`, `pomMinLayers`, `pomMaxLayers`, `pomSelfShadow`, `pomFadeStart`, `pomFadeEnd`)
- **PerObjectCB**: Add displacement params to constant buffer (or use separate material CB)
- **Demo**: New or updated demo showing POM on textured cubes with different heightmaps
- **Performance**: ~0.5ms per fullscreen at 1080p with 32 steps (GPU-bound, ALU-heavy)

## Breaking Changes

None — POM is additive. Existing materials without heightmaps are unaffected.

## Acceptance Criteria

1. POM shader produces correct depth parallax visible in screenshot comparison (flat vs POM)
2. Self-shadowing creates contact shadows in surface crevices when light is at grazing angles
3. Adaptive step count varies with view angle (fewer steps at head-on, more at grazing)
4. Distance fade smoothly transitions to flat UV beyond configurable distance
5. Demo renders POM surface with clear depth effect, visually verified via screenshot
6. All existing demos/tests continue to work (no regression)
