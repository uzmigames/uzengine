# Phase 3 -- TAA (Temporal Anti-Aliasing)

## Context

Deferred rendering produces hard geometric edges because each pixel maps to exactly one surface sample. Traditional MSAA does not work with deferred shading (it would require multi-sampled G-Buffer reads, which is prohibitively expensive). TAA solves this by accumulating sub-pixel samples across multiple frames using a jittered projection matrix and motion-vector reprojection.

Beyond anti-aliasing, TAA serves as a general-purpose temporal denoiser. Both SSAO and SSR produce noisy output (limited samples per pixel per frame) that TAA can smooth out over time. This makes TAA a foundational infrastructure piece for all temporal effects.

## Why

- **Aliasing is highly visible in deferred rendering** -- without MSAA, geometric edges, specular highlights, and texture detail show staircase artifacts
- **TAA is the industry-standard solution** -- used by UE4/5, Unity HDRP, Frostbite, and virtually every modern deferred renderer
- **Temporal denoiser for SSAO and SSR** -- these effects produce per-frame noise that TAA accumulates into a clean result, allowing fewer samples per frame (cheaper) with better visual quality
- **Sub-pixel detail recovery** -- TAA effectively supersamples the scene over time, revealing detail finer than the pixel grid (useful for thin geometry, text, wire fences)
- **Velocity buffer enables future features** -- motion blur, temporal upscaling (DLSS/FSR), and other temporal effects all require motion vectors

## What

Implement **standard TAA** with Halton jitter, variance clamping, and Catmull-Rom sharpening:

1. **Velocity G-Buffer channel** -- add GBuffer3 (RG16F) to the G-Buffer layout, storing per-pixel screen-space motion vectors computed from current and previous frame transforms
2. **Halton jitter** -- apply an 8-sample Halton(2,3) sequence as a sub-pixel offset to the projection matrix each frame
3. **TAA resolve compute shader** -- blend the current (jittered) frame with the reprojected history buffer using variance clamping for ghost rejection
4. **Catmull-Rom sharpening** -- counter TAA-induced blur with a post-resolve sharpening pass (built into the resolve shader)
5. **Per-frame data management** -- store previous-frame view-projection matrix and per-object model matrices for velocity computation
6. **IPC handlers** -- `renderer.setTAA` for runtime parameter control

## Spec Reference

[Screen-Space Effects Spec](../../../docs/specs/screen-space-effects.md) -- Section C (TAA) and G-Buffer Layout Update

## Dependencies

- **phase2-pbr-renderer** (G-Buffer pass, deferred lighting, post-process infrastructure)
- **phase1-scene-system** (per-node transforms for previous-frame model matrix storage)
- **phase2-shader-library** (shared include files)

## Performance Budget

| Component | Cost at 1080p | Notes |
|-----------|--------------|-------|
| Velocity buffer write | ~0 (embedded in G-Buffer pass) | Adds one extra RT output to existing G-Buffer pass |
| TAA resolve | ~0.4ms | Single compute dispatch: 3x3 gather + history sample + blend + sharpen |
| History copy | ~0 (ping-pong, no copy needed) | Swap buffer indices each frame |
| **Total** | **~0.4ms** | Well within 0.5ms budget |

## Architectural Impact

TAA touches multiple systems because it requires:
- **G-Buffer layout change**: adding a 4th render target (GBuffer3: RG16F velocity)
- **Per-object CBV change**: adding `prev_model` matrix (16 floats, 64 bytes per object)
- **Per-frame CBV change**: adding `prev_view_proj` matrix and `jitter_offset` vector
- **Projection matrix modification**: jitter must be applied before all main camera geometry passes
- **Scene node system**: must store and track previous-frame transforms for all visible nodes

These are cross-cutting changes that affect the G-Buffer pass, vertex shader, constant buffer layout, and frame sequencing. This is why TAA is scheduled in Phase 3 -- it requires the rendering infrastructure from Phase 2 to be stable before modifying it.

## Success Criteria

- Geometric edges (cube, sphere silhouettes) appear smooth with TAA enabled
- No visible ghosting on slowly moving objects (variance clamping works)
- Fast-moving objects do not ghost (velocity-based blend factor increase works)
- Camera rotation does not produce trailing artifacts
- Specular highlights are temporally stable (no flickering)
- Disocclusion (newly revealed geometry) resolves within 2-3 frames
- TAA-induced blur is compensated by sharpening (text and fine detail remain legible)
- Performance stays within 0.5ms at 1080p
