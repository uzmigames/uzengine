# Phase 3 -- SSR (Screen-Space Reflections)

## Context

The PBR renderer (Phase 2) provides Image-Based Lighting (IBL) for specular reflections, but IBL captures only distant environment lighting. It cannot reflect nearby geometry -- a metallic floor will not show the object sitting on it, a mirror will not show the room. This is a major gap in visual fidelity for any PBR engine. Screen-Space Reflections solve this by tracing reflection rays against the depth buffer to find on-screen geometry that should be reflected.

## Why

- **IBL alone is not convincing for nearby geometry** -- IBL cubemaps capture the distant environment, so metallic/glossy surfaces look "disconnected" from their surroundings without SSR
- **Critical for PBR material showcase** -- roughness and metallic properties only look correct when reflections respond to nearby objects
- **Hi-Z ray marching is efficient** -- by traversing a mip-chain of the depth buffer, the algorithm skips empty space in large steps and converges quickly
- **Temporal accumulation (via TAA)** dramatically reduces noise without additional ray cost
- **Graceful fallback** -- SSR blends with IBL reflections based on confidence; where SSR misses (off-screen geometry, back-facing surfaces), IBL takes over seamlessly

## What

Implement **Hierarchical Z-Buffer (Hi-Z) Screen-Space Reflections** as a compute-shader post-process:

1. **Hi-Z mip chain generation** -- compute shader that builds a min-depth pyramid from the depth buffer (each mip stores the minimum of its 2x2 parent)
2. **SSR ray march** -- for each pixel with roughness below the cutoff, reflect the view direction around the normal and march through the Hi-Z pyramid to find intersections
3. **Temporal reprojection** -- reuse the previous frame's SSR result via motion vectors to reduce noise and fill in misses
4. **Resolve pass** -- blend SSR result (where confident) with existing IBL reflections based on hit confidence and roughness
5. **IPC handlers** -- `renderer.setSSR` for runtime parameter control

## Spec Reference

[Screen-Space Effects Spec](../../../docs/specs/screen-space-effects.md) -- Section B (SSR)

## Dependencies

- **phase2-pbr-renderer** (G-Buffer: depth, normals, roughness, metallic; IBL specular for fallback)
- **phase3-taa** (velocity buffer for temporal reprojection; TAA history for temporal stability)
- **phase2-shader-library** (shared include files)

## Performance Budget

| Pass | Cost at 1080p | Notes |
|------|--------------|-------|
| Hi-Z generation | ~0.2ms | 10-11 dispatches, each smaller than the last |
| Ray march | ~1.2ms | Full-res, 64 steps max, roughness early-out helps |
| Resolve | ~0.1ms | Simple blend SSR + IBL |
| Temporal blend | ~0.1ms | Reprojection + history accumulation |
| **Total** | **~1.6ms** | Well within 2ms budget |

The roughness cutoff (default 0.4) is critical for performance: rough surfaces skip SSR entirely and fall back to IBL, dramatically reducing the number of pixels that need ray marching.

## Limitations

SSR inherently cannot reflect:
- Geometry not visible on screen (behind the camera, occluded)
- Back-facing surfaces
- Objects outside the viewport

These are fundamental to any screen-space technique. IBL provides the fallback, and the confidence-based blending makes transitions smooth.

## Success Criteria

- Metallic/glossy floor reflects nearby objects with correct position and color
- Roughness gradient on a surface: smooth = sharp SSR reflections, rough = IBL-only
- No visible hard edges where SSR confidence transitions to IBL fallback
- Temporal reprojection provides stable reflections during slow camera movement
- Objects near screen edges fade gracefully (screen-edge fade, no hard cutoff)
- Performance stays within 2ms budget at 1080p with default settings
