<!-- doc-meta
date: 2026-04-08
source: https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/library.html
verified: true (Playwright browser scrape, 33 publications enumerated)
related:
  - https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/overview.html
  - https://www.intel.com/content/www/us/en/developer/articles/news/visual-compute-and-graphics-lab-2024-update.html
  - https://www.intel.com/content/www/us/en/developer/articles/news/gpu-research-generative-ai-update.html
-->

# Intel Graphics Research Library — Complete Analysis for UzEngine

Comprehensive review of **all 33 publications** from Intel's GPU Research (Visual
Compute & Graphics / VCG Lab) library page, with relevance rankings for UzEngine's
C++23/HLSL deferred PBR renderer.

## Data source

Scraped via Playwright MCP on 2026-04-08 from the live Intel library page.
All 33 entries verified — titles, dates, URLs, and descriptions match the page exactly.

## Complete publication list (33 entries, newest first)

| # | Title | Date | Category | UzEngine relevance |
|---|-------|------|----------|-------------------|
| 1 | CGVQM+D: Computer Graphics Video Quality Metric and Dataset | 06/30/25 | Quality metrics | MEDIUM |
| 2 | Image-GS: Content-Adaptive Image Representation via 2D Gaussians | 06/17/25 | Neural graphics | LOW |
| 3 | GFFE: G-buffer Free Frame Extrapolation | 12/05/24 | Frame generation | **HIGH** |
| 4 | New Publications and Tools for GenAI (news) | 07/25/24 | News | REF |
| 5 | Concurrent Binary Trees for Large-Scale Game Components | 07/02/24 | Geometry/LOD | **HIGH** |
| 6 | Nonorthogonal Reduction for Rendering Fluorescent Materials | 06/25/24 | Materials | LOW |
| 7 | N-dimensional Gaussians for Fitting High-Dimensional Functions | 05/30/24 | Neural graphics | LOW-MEDIUM |
| 8 | Transform Rasterizers with Stochastic Gradient Estimation | 05/13/24 | Differentiable | LOW |
| 9 | Physically Based Lighting for 3D Generative Models of Cars | 04/30/24 | PBR/GenAI | LOW |
| 10 | ExtraSS: Joint Spatial Super Sampling and Frame Extrapolation | 12/11/23 | Upscaling | **HIGH** |
| 11 | Effect of display capabilities on gloss consistency | 12/11/23 | Perception | LOW |
| 12 | Raycasting for Memory Constrained Isosurface Visualization | 12/06/23 | SciVis | NONE |
| 13 | VESTEC | 08/02/23 | Emergency planning | NONE |
| 14 | Real-Time Rendering of Glinty Appearances (Distributed Binomial) | 08/02/23 | Materials | MEDIUM |
| 15 | Sampling Visible GGX Normals with Spherical Caps | 08/02/23 | PBR sampling | **HIGH** |
| 16 | Real-Time RT of Micro-Poly Geometry with Hierarchical LOD | 08/02/23 | Geometry/RT | MEDIUM |
| 17 | Neural Prefiltering for Correlation-Aware Levels of Detail | 07/26/23 | Neural LOD | LOW-MEDIUM |
| 18 | Iterative alpha-(de)Blending: Minimalist Deterministic Diffusion | 07/23/23 | ML/Diffusion | LOW |
| 19 | Instant Neural Representation for Interactive Volume Rendering | 07/07/23 | Neural volume | LOW |
| 20 | Intel's Latest Research for Graphics and Generative AI (news) | 06/23/23 | News | REF |
| 21 | Stochastic Subsets for BVH Construction | 05/23/23 | RT/BVH | MEDIUM |
| 22 | SynPeDS: Synthetic Dataset for Pedestrian Detection | 12/09/22 | CV/Datasets | NONE |
| 23 | Temporally Stable Real-Time Joint Neural Denoising and Supersampling | 07/27/22 | Denoising+upscaling | **HIGH** |
| 24 | PLOC++: Parallel Locally-Ordered Clustering for BVH | 07/27/22 | RT/BVH | MEDIUM |
| 25 | Scalable Ray Tracing for In Situ Visualization (OSPRay) | 05/05/22 | SciVis | NONE |
| 26 | Wide BVH Traversal with a Short Stack | 05/02/22 | RT/BVH | MEDIUM |
| 27 | ANARI: A 3D Rendering API Standard | 03/30/22 | API design | LOW |
| 28 | Lazy Build of Acceleration Structures with Traversal Shaders | 11/17/20 | RT/BVH | MEDIUM |
| 29 | Flexible Ray Traversal with an Extended Programming Model | 11/17/19 | RT/BVH | LOW-MEDIUM |
| 30 | Coarse Pixel Shading with Temporal Supersampling | 05/15/18 | VRS/shading | **HIGH** |
| 31 | Bandwidth-Efficient BVH Layout for Incremental HW Traversal | 06/20/16 | RT/BVH | LOW |
| 32 | Watertight Ray Traversal with Reduced Precision | 06/20/16 | RT/BVH | LOW |
| 33 | Masked Software Occlusion Culling | 06/20/16 | Culling | **HIGH** |

## Category breakdown

| Category | Count | Detail file |
|----------|-------|-------------|
| Upscaling, frame gen & temporal | 5 | [upscaling.md](./upscaling.md) |
| PBR sampling & materials | 6 | [materials.md](./materials.md) |
| Denoising | 3 | [denoising.md](./denoising.md) |
| Ray tracing & BVH | 8 | [ray-tracing.md](./ray-tracing.md) |
| Geometry, LOD & culling | 4 | [geometry-perf.md](./geometry-perf.md) |
| Neural graphics & GenAI | 6 | [neural-graphics.md](./neural-graphics.md) |
| SciVis, perception & other | 5 | [other.md](./other.md) |
| News roundups | 2 | (referenced inline) |

## Top 10 "should adopt" recommendations (ranked)

Ranking weights: (a) quality win vs current UzEngine state, (b) implementation cost,
(c) open-source availability, (d) alignment with deferred+forward hybrid pipeline.

| # | Technique | Publication | Why adopt | Cost |
|---|-----------|-------------|-----------|------|
| 1 | **Spherical Cap VNDF sampling** | #15 — Dupuy & Benyoub, HPG 2023 | Drop-in GGX sampler upgrade. Simpler, faster, fewer fireflies on rough surfaces. Paper: [arxiv 2306.05044](https://arxiv.org/abs/2306.05044). | 2-3 days |
| 2 | **GFFE frame extrapolation** | #3 — Wu, Vembar, Kaplanyan et al., SIGGRAPH Asia 2024 | G-buffer-free frame gen with zero latency (unlike DLSS3/FSR3 interpolation). Lightweight neural net. Paper: [ACM 10.1145/3687923](https://dl.acm.org/doi/pdf/10.1145/3687923). | 2-3 weeks |
| 3 | **ExtraSS joint upscaling+extrapolation** | #10 — Intel, 2023 | Combines spatial super sampling and frame extrapolation in one framework. | 2-3 weeks |
| 4 | **Masked Software Occlusion Culling** | #33 — Intel, HPG 2016 | SIMD-optimized CPU occlusion culling, 3x faster than prior art. Interleaved occluder rendering + queries. Open source at GameTechDev. | 1-2 weeks |
| 5 | **Coarse Pixel Shading + Temporal SS** | #30 — Intel, I3D 2018 | VRS with temporal supersampling beats checkerboard rendering. Reduces shading cost with better quality. | 1-2 weeks |
| 6 | **Concurrent Binary Trees** | #5 — Intel, HPG 2024 | GPU-driven LOD for planetary-scale terrain. Directly applicable to `.uzterrain` spec. | 3-4 weeks |
| 7 | **Neural denoising + supersampling** | #23 — Intel, HPG 2022 | Joint denoising + 2x upscaling from noisy low-res input. Foundation for path-tracer mode. | 3-4 weeks |
| 8 | **Glinty appearances (distributed binomial)** | #14 — Intel, SIGGRAPH 2023 | Real-time flake/glitter materials. Counts reflecting flakes per texel with binomial distribution. | 1-2 weeks |
| 9 | **PLOC++ BVH construction** | #24 — Intel, HPG 2022 | GPU-parallel BVH construction for DXR acceleration structures. | 2-3 weeks |
| 10 | **CGVQM+D quality metric** | #1 — Intel, 2025 | Per-pixel error maps + global quality scores. Useful for automated visual regression testing. | 1 week |

## Gaps vs UzEngine current stack

| UzEngine subsystem | Intel technique to adopt | Publication # |
|--------------------|--------------------------|---------------|
| GGX BRDF sampling | Spherical Cap VNDF | #15 |
| Frame generation (none) | GFFE | #3 |
| Upscaler (planned) | ExtraSS / XeSS integration | #10 |
| CPU culling (GPU-only) | Masked Software Occlusion Culling | #33 |
| VRS (none) | Coarse Pixel Shading + Temporal SS | #30 |
| Terrain (spec only) | Concurrent Binary Trees | #5 |
| Path tracer denoiser (none) | Neural denoising + supersampling | #23 |
| Visual regression testing | CGVQM+D | #1 |
| BVH for DXR | PLOC++ / Stochastic Subsets | #24, #21 |
| Material sparkle/flake | Glinty Appearances | #14 |

## References

- Intel GPU Research library: https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/library.html
- Intel GPU Research overview: https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-research/overview.html
- 2023 update: https://www.intel.com/content/www/us/en/developer/articles/news/gpu-research-generative-ai-update.html
- 2024 update: https://www.intel.com/content/www/us/en/developer/articles/news/visual-compute-and-graphics-lab-2024-update.html
- XeSS SDK: https://github.com/intel/xess
- XeGTAO: https://github.com/GameTechDev/XeGTAO
- Masked Occlusion Culling: https://github.com/GameTechDev/MaskedOcclusionCulling
- Open Image Denoise: https://www.openimagedenoise.org/
- Embree: https://www.embree.org/
