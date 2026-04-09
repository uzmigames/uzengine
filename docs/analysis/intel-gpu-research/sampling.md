<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — Sampling & Monte Carlo

## #15 — Sampling Visible GGX Normals with Spherical Caps
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/sampling-visible-ggx-normals.html
- **Date**: 08/02/2023
- **Authors**: J. Dupuy, A. Benyoub (Intel Corporation)
- **Venue**: HPG 2023, Delft, Netherlands
- **Paper**: https://arxiv.org/abs/2306.05044
- **PDF**: https://cdrdv2.intel.com/v1/dl/getContent/782052
- **Summary**: Novel VNDF sampling algorithm for GGX microfacets. Reduces any GGX material to a hemispherical mirror, making sampling trivial. Simpler and faster than Heitz 2018 (the current SOTA used in most engines).
- **Relevance**: **HIGH** — See [materials.md](./materials.md) for full analysis. Drop-in upgrade for `pbr_common.hlsli`.
- **Difficulty**: 2-3 days.

## Blue-noise dithered sampling (Intel-adjacent, widely adopted)
- **Authors**: Heitz & Belcour (Unity; method adopted across Intel work and cited in Intel publications)
- **Summary**: Precomputed blue-noise masks shift per-pixel sample sequences to produce temporally stable, spatially decorrelated noise. Standard technique referenced in multiple Intel papers.
- **Relevance**: **HIGH** — Cheap quality upgrade for TAA jitter, SSR, GTAO, SSS. Not a paper on the Intel library page but frequently cited in their work.
- **Difficulty**: 2 days.

## Low-discrepancy sequences (Owen-scrambled Sobol)
- **Summary**: Intel talks have championed Owen-scrambled Sobol for real-time path tracing. Referenced in the Path Tracing a Trillion Triangles course material.
- **Relevance**: HIGH for path tracer mode. Not a standalone paper on the library page.
- **Difficulty**: 2-3 days.

Note: The Intel library page focuses more on specific rendering techniques than on
general sampling theory. The sampling-relevant entries are primarily the VNDF paper (#15)
and the sampling strategies embedded within the BVH/RT papers. Broader sampling work
(blue-noise, Sobol, CMJ) appears in Intel talks and courses rather than as standalone
library entries.
