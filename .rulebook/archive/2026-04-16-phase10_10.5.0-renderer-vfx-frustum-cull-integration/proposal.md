# Proposal: phase10_10.5.0-renderer-vfx-frustum-cull-integration

## Why
The phase 3.89 frustum-cull helpers (`ExtractFrustumPlanes`, `IsSphereInsideFrustum`, `EmitterCullState`) exist but have zero call-sites in the scene VFX path. Emitters outside the frustum still sim and render. Phase 9.14 (HZB cull gate) layers on top of this, so it needs 3.89 wired first.

## What Changes
Hook the frustum cull into `frame_particles.cpp` / `SceneRenderer::RegisterParticlePasses`. Every VFX entity contributes a bounding sphere (via `ComputeEmitterBoundingSphereRadius`); per-frame the scene renderer extracts frustum planes from the current view-projection, tests each emitter, feeds the vote through the per-emitter `EmitterCullState` for 2-frame hysteresis, and skips sim + render when committed-culled.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/renderer/frame/frame_particles.cpp + VfxEmitterComponent cull state
- Breaking change: NO
- User benefit: Off-screen emitters stop paying GPU cost.
- Estimated effort: M

## Source
Phase 3.89 helpers already in `engine/vfx/lod/vfx_frustum_cull.h`.
