# Separate Compute Root Signature for GPU Bake Passes

**Category**: architecture
**Tags**: rendering, compute, root-signature, ibl, dx12, descriptors

## Description

GPU compute passes (IBL baking, LUT generation) use a separate root signature from the graphics pipeline. This avoids consuming graphics root signature DWORDs and allows compute-specific layouts (root constants + SRV/UAV tables). Per-mip UAV descriptors use a bump allocator at heap offset 4096+ to avoid polluting the main descriptor heap region.

## Example

// Compute root sig: 5 params\n// b0: 16 root constants (inline, no CBV upload)\n// t0: SRV table (input textures)\n// t1: SRV table (secondary inputs)\n// u0: UAV table (output textures)\n// u1: UAV table (per-mip UAVs at heap[4096+])\n// s0: static sampler (linear clamp)

## When to Use

When adding new compute shaders (SSAO, SSR, bloom, particle sim). When the graphics root signature is full (currently 64/64 DWORDs).

## When NOT to Use

For simple fullscreen post-process effects that can use the existing graphics pipeline.
