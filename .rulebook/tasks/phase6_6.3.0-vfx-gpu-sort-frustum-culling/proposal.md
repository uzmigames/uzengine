# Proposal: phase6_6.3.0-vfx-gpu-sort-frustum-culling

## Why
Alpha-blended particles (Smoke, Whirlpool, LitSmoke) render with incorrect ordering
because the GPU bitonic sort pass is disabled (`if (false)` in frame_particles.cpp).
Additionally, all emitters dispatch unconditionally even when off-screen, wasting GPU
cycles. The standalone has both features working.

## What Changes
- Re-enable bitonic merge sort compute pass for alpha-blended emitters
- Wire sorted indices SRV to billboard vertex shader
- Add CPU sphere-frustum culling before emitter dispatch
- Skip spawn+update+billboard for culled emitters

## Impact
- Affected code: frame_particles.cpp, frame_vfx_inline.cpp, scene_renderer.h
- Breaking change: NO
- User benefit: Correct alpha-blend ordering + GPU savings from culling
