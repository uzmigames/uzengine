# Proposal: phase8_8.20.0-vfx-hzb-occlusion-culling

## Why
The existing frustum cull (3.89) skips emitters outside the view volume. HZB occlusion queries go further: an emitter whose bounds are fully behind opaque geometry also gets skipped. Builds on the existing HZB pyramid (3.36).

## What Changes
Per-emitter bounds query against the screen-space HZB mip chain: project the emitter AABB, read the max depth at the covering mip, compare to the emitter's near-plane depth. Skip sim + render when occluded.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/lod/ + integration with 3.89 frustum cull
- Breaking change: NO
- User benefit: Additional ~30-50% cost reduction for VFX-heavy scenes with lots of occlusion.
- Estimated effort: M

## Source
Niemeyer 2007 Hierarchical Z-Buffer Occlusion Culling.
