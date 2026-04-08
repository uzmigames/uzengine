<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — AO & Shadows

Note: XeGTAO and shadow-specific papers are NOT on the Intel GPU Research Library page
(which lists 33 publications). XeGTAO is maintained as an open-source project at
GameTechDev/XeGTAO. The library page focuses on research publications, not SDK/tool releases.

## XeGTAO — open-source, not on library page
- **Repository**: https://github.com/GameTechDev/XeGTAO
- **Authors**: Filip Strugar et al. (Intel)
- **Summary**: Horizon-based ground-truth AO with temporal + spatial denoising. Latest versions add bent-normal output for specular occlusion.
- **Relevance**: **Already shipping in UzEngine**. Bent-normal variant can fix the over-occlusion bug (2026-03-30 memory).
- **Difficulty**: 3-5 days for bent-normal pass upgrade.

## Shadow-relevant techniques from library publications

The following library publications contain shadow-relevant components:

- **#16 — Micro-Poly RT with Hierarchical LOD** (08/02/23): Shadow ray optimization for virtualized geometry. See [ray-tracing.md](./ray-tracing.md).
- **#30 — Coarse Pixel Shading** (05/15/18): Temporal supersampling recovers shadow detail lost in VRS. See [upscaling.md](./upscaling.md).
- **#33 — Masked Occlusion Culling** (06/20/16): While primarily a culling technique, the hierarchical depth buffer can inform shadow culling. See [geometry-perf.md](./geometry-perf.md).

No standalone shadow-specific paper exists on the library page.
