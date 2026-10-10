# Proposal: phase8_8.16.0-vfx-weighted-oit-rendering

## Why
Depth-sorted billboard rendering has two failure modes: sort artefacts on interpenetrating transparents, and sort cost at high particle counts. Weighted Blended OIT (McGuire/Bavoil 2013) eliminates both at the cost of one accumulation + one resolve pass.

## What Changes
Add VfxOITRenderer as a toggleable alternative to the sort path. Accumulation pass writes (color*weight, alpha) to two MRTs; resolve pass divides and composites over the framebuffer. Per-emitter opt-in via blend-mode enum.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/modules/ + new OIT render pass
- Breaking change: NO
- User benefit: No more sort artefacts on overlapping transparents.
- Estimated effort: L

## Source
McGuire and Bavoil 2013 Weighted Blended Order-Independent Transparency.
