<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — Global Illumination

Note: The Intel GPU Research Library page does not have standalone GI papers.
GI-relevant content is distributed across the ray tracing, denoising, and neural
graphics publications. This file consolidates GI-relevant insights from across the library.

## GI-relevant publications on the library page

### #23 — Joint Neural Denoising and Supersampling (07/27/22)
- The primary use case for this paper's denoiser is path-traced GI content (1-4 spp).
- See [denoising.md](./denoising.md) for full analysis.

### #21 — Stochastic Subsets for BVH Construction (05/23/23)
- Better BVH quality → faster GI ray traversal.
- See [ray-tracing.md](./ray-tracing.md) for full analysis.

### #7 — N-dimensional Gaussians (05/30/24)
- Potential for neural radiance cache representation using N-D Gaussian mixtures.
- See [neural-graphics.md](./neural-graphics.md) for full analysis.

### #16 — Micro-Poly RT with Hierarchical LOD (08/02/23)
- Enables path-traced GI on massive geometry via hierarchical RT.
- See [ray-tracing.md](./ray-tracing.md) for full analysis.

## Intel GI work NOT on the library page (related projects)

### Path Tracing a Trillion Triangles (SIGGRAPH 2023 course)
- **Authors**: Kaplanyan, Kuznetsov et al. (Intel)
- **Summary**: Demonstrates practical path tracing on integrated GPUs with tiling, material LOD, and sampling strategies. The course is not listed as a library publication but is heavily referenced.
- **Relevance**: **HIGH** — Directly relevant to `phase0_path-tracer-runtime-mode`.

### Practical path guiding (Intel + Karlsruhe)
- **Summary**: Online-learned directional distributions for importance sampling in complex lighting.
- **Relevance**: HIGH for path tracer convergence. Not a library page entry.

### ReSTIR GI reservoir reuse
- **Summary**: Screen-space and world-space reservoir resampling for GI variance reduction.
- **Relevance**: HIGH for any 1-spp GI pass. Not an Intel-authored library page entry but referenced in Intel work.

## Adoption priority for UzEngine

1. **Joint denoising+supersampling** (#23) — for path tracer output
2. **Path guiding** (course material) — for path tracer convergence
3. **N-D Gaussians** (#7) — future neural radiance cache (experimental)
