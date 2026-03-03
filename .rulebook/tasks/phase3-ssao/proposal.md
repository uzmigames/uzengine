# Phase 2 -- SSAO (Screen-Space Ambient Occlusion)

## Context

The deferred renderer (phase2-pbr-renderer) produces lit scenes with direct lighting and IBL, but ambient lighting is uniform. Without ambient occlusion, interior corners, crevices, and contact shadows between objects appear flat and unconvincing. SSAO is the single highest-impact post-process effect for visual quality relative to its GPU cost.

## Why

- **Highest visual quality-to-cost ratio** of any post-process effect in a deferred renderer
- Ambient occlusion provides the "contact shadow" and "depth cue" that makes scenes look grounded and three-dimensional
- Works entirely from the G-Buffer (depth + normals) -- no additional scene data required
- Can be implemented standalone without TAA or velocity buffer (those come in Phase 3)
- Essential for any PBR renderer to look convincing -- without AO, ambient lighting is flat and unrealistic

## What

Implement **HBAO+ (Horizon-Based Ambient Occlusion Plus)** as a compute-shader post-process pass:

1. **HBAO compute shader** -- reads G-Buffer depth + normals, outputs a half-resolution occlusion buffer (R8_UNORM)
2. **Bilateral blur** (2-pass: horizontal + vertical) -- denoises the AO while preserving depth edges to prevent bleeding across object boundaries
3. **Integration into deferred lighting** -- the blurred AO buffer is sampled and multiplied with the ambient/IBL term during the lighting composite
4. **Three quality levels** (Low/Medium/High) -- controls the number of sampling directions (4/8/16), allowing quality/performance trade-off
5. **IPC handlers** -- `renderer.setSSAO` for runtime parameter control from the editor

## Spec Reference

[Screen-Space Effects Spec](../../../docs/specs/screen-space-effects.md) -- Section A (SSAO)

## Dependencies

- **phase2-pbr-renderer** (G-Buffer depth + normals must exist)
- **phase2-shader-library** (shared include files: `common.hlsli`, `noise.hlsli`, `global_cbuffer.hlsli`)

## Performance Budget

| Quality | Cost at 1080p | Notes |
|---------|--------------|-------|
| Low | ~0.3ms | 4 directions, 16 total samples |
| Medium | ~0.6ms | 8 directions, 32 total samples |
| High | ~1.0ms | 16 directions, 64 total samples |
| Blur (2-pass) | ~0.2ms | 4-tap bilateral kernel |
| **Total (Medium)** | **~0.8ms** | Well within 1ms budget |

Half-resolution rendering (640x360 at 1080p) reduces the compute cost by 4x with minimal visible quality loss thanks to the bilateral blur upsampling.

## Success Criteria

- Concave surfaces (e.g., box corners, sphere-plane contact) show visible darkening
- No self-occlusion artifacts on flat surfaces (bias parameter works correctly)
- No AO bleeding across depth discontinuities (bilateral blur preserves edges)
- Quality levels produce visibly different results (Low is grainy, High is smooth)
- Performance stays within budget at all quality levels
- IPC handler allows real-time parameter adjustment from the VSCode editor
