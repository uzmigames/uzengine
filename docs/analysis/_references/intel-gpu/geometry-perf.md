<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — Geometry, LOD & Culling

## #5 — Concurrent Binary Trees for Large-Scale Game Components
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/cbt-for-large-scale-game-components.html
- **Date**: 07/02/2024
- **Authors**: Intel
- **Venue**: HPG 2024
- **Summary**: GPU-driven Concurrent Binary Tree (CBT) representation enables processing large sceneries at planetary scales at high speeds. The CBT provides fine-grained per-triangle LOD selection while making the system easier to use for developers and artists. Applicable to terrain heightmaps, large tiled meshes, and any content requiring continuous LOD.
- **Key contribution**: CBTs encode a complete binary tree in a flat array with O(1) split/merge operations. GPU compute shader walks the tree each frame, selecting LOD per triangle based on screen-space error. No CPU readback needed.
- **Relevance**: **HIGH** — Directly applicable to UzEngine's planned terrain system (`.uzterrain` spec exists). CBT replaces traditional quadtree clipmaps with a more flexible per-triangle LOD. Also applicable to water surface tessellation.
- **UzEngine integration point**: `src/engine/renderer/terrain/` — CBT compute pass → indirect draw of tessellated patches.
- **Difficulty**: 3-4 weeks.

## #33 — Masked Software Occlusion Culling
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/masked-software-occlusion-culling.html
- **Date**: 06/20/2016
- **Authors**: Intel
- **Venue**: HPG 2016
- **Open source**: https://github.com/GameTechDev/MaskedOcclusionCulling
- **Summary**: Efficient CPU occlusion culling algorithm targeting SIMD architectures (SSE4.1/AVX2/AVX-512). Supports interleaving occluder rendering and occlusion queries in the same pass. Outperforms previous CPU occlusion culling work by 3x. Uses a hierarchical depth buffer with masked rasterization for fast occluder coverage.
- **Key contribution**: The "masked" approach uses bit masks to track coverage within tiles, enabling efficient SIMD parallel rasterization of occluders and queries without full per-pixel depth testing.
- **Relevance**: **HIGH** — UzEngine currently uses GPU-only culling (`gpu_cull_cs.hlsl`). Adding CPU occlusion culling provides:
  - Earlier rejection before GPU submission (reduces draw call count)
  - Works on previous frame's depth (no GPU round-trip latency)
  - Open-source, well-maintained, SIMD-optimized (SSE/AVX)
  - Used in production by multiple AAA engines
- **UzEngine integration point**: CPU-side culling in `instance_batcher.cpp` before GPU indirect draw. Rasterize major occluders (terrain, large buildings) into a low-res SW depth buffer, then test all meshes against it.
- **Difficulty**: 1-2 weeks (library is drop-in, integration into batcher is the main work).

## #8 — Transform Rasterizers with Stochastic Gradient Estimation
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/non-differentiable-to-differentiable-rasterizers.html
- **Date**: 05/13/2024
- **Authors**: Intel (Thomas Deliot, Eric Heitz, Laurent Belcour)
- **Venue**: I3D 2024 (Best Presentation + Best Paper runner-up)
- **Summary**: Transforms a non-differentiable rasterizer into a differentiable one using stochastic gradient estimation. Enables inverse rendering (optimizing scene parameters from target images) with standard GPU rasterization pipelines. No need for custom differentiable renderers.
- **Relevance**: **LOW** — Inverse rendering research. Not applicable to forward production rendering. Potentially interesting if UzEngine ever adds artist tools for automatic material fitting from photographs.
- **Difficulty**: Research-grade.

## #2 — Image-GS: Content-Adaptive Image Representation via 2D Gaussians
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/image-gs-content-adaptive-image.html
- **Date**: 06/17/2025
- **Authors**: Intel
- **Summary**: Neural image representations using 2D Gaussians for encoding and rendering visual data. Content-adaptive — allocates more Gaussians to complex regions.
- **Relevance**: **LOW** — Image compression/representation research. Not applicable to real-time rendering pipeline.
- **Difficulty**: N/A.

## Adoption priority for UzEngine

1. **Masked Software Occlusion Culling** (#33) — immediate perf win, open-source, 1-2 weeks
2. **Concurrent Binary Trees** (#5) — when terrain system is implemented
3. **Differentiable Rasterizer** (#8) — future artist tools only
