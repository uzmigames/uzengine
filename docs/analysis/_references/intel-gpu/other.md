<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — SciVis, Perception & Other

Publications with no direct applicability to UzEngine's real-time game rendering pipeline.
Listed for completeness — all 33 library entries are accounted for.

## #12 — Raycasting for Memory Constrained Isosurface Visualization
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/raycasting-for-isosurface-visualization.html
- **Date**: 12/06/2023
- **Summary**: Isosurface-rendering algorithm for volume visualization via ray-data traversal for ray-isosurface intersections. Targets scientific/medical visualization with memory constraints.
- **Relevance**: **NONE** — SciVis domain, not applicable to game rendering.

## #13 — VESTEC
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/vestec-project-and-framework.html
- **Date**: 08/02/2023
- **Summary**: Framework helping public authorities plan for and respond to natural disasters and epidemics. Visualization + simulation infrastructure.
- **Relevance**: **NONE** — Emergency response framework, not rendering research.

## #22 — SynPeDS: Synthetic Dataset for Pedestrian Detection
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/synpeds-detection-in-urban-traffic-scenes.html
- **Date**: 12/09/2022
- **Summary**: Synthetic dataset for pedestrian detection in urban traffic scenes. Uses rendering to generate training data for computer vision models.
- **Relevance**: **NONE** — Computer vision / autonomous driving dataset. Not applicable.

## #25 — Scalable Ray Tracing for In Situ Visualization Using Intel OSPRay
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/scalable-cpu-ray-tracing-for-in-situ-visualization.html
- **Date**: 05/05/2022
- **Summary**: MPI-parallel CPU ray tracing for scientific visualization using Intel OSPRay. In situ visualization renders images for post hoc exploration.
- **Relevance**: **NONE** — HPC/SciVis domain. OSPRay is CPU-based and MPI-parallel, not applicable to real-time GPU rendering.

## #27 — ANARI: A 3D Rendering API Standard
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/anari-a-3-d-rendering-api-standard.html
- **Date**: 03/30/2022
- **Summary**: A 3D rendering API standard enabling state-of-the-art rendering techniques across hardware platforms and rendering engines. An abstraction layer for scientific/professional rendering backends.
- **Relevance**: **LOW** — API design reference. UzEngine has its own RHI abstraction layer. ANARI targets scientific rendering APIs, not game engine backends.
- **Note**: The API design philosophy (backend-agnostic vtable) is conceptually similar to UzEngine's RHI but targets a different domain.
