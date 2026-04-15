# Proposal: phase6_6.3.0-vfx-gpu-sort-frustum-culling

## Why
GPU bitonic sort + frustum-cull integration so depth-correct billboard rendering plays nicely with view-volume culling.

## What Changes
See the corresponding tasks.md for the resolution path.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/
- Breaking change: NO
- User benefit: Niagara-parity feature delivered as part of phases 3-5.
