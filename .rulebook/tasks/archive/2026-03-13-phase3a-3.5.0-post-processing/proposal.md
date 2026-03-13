# Proposal: 3.5.0 — Basic Post-Processing

## Why
Minimum post-processing for visual quality. Tone mapping, bloom, and anti-aliasing transform raw HDR output into presentable images. Advanced effects (SSAO, SSR, DOF) deferred to Phase 7.

## What Changes
- ACES filmic tone mapping (HDR → LDR)
- Bloom: downsample mip chain + Gaussian blur + composite
- FXAA (non-temporal fast approximate AA)
- Post-process stack: ordered pipeline via render graph with per-effect toggles

## Impact
- Affected specs: `docs/engine/12-rendering/`
- Affected code: `runtime/core/renderer/postprocess/`, shaders
- Breaking change: NO
- User benefit: Visually polished output, bloom glow, reduced aliasing
