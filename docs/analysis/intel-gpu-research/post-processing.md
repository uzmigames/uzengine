<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — Post-Processing

Note: The Intel GPU Research Library has limited standalone post-processing publications.
Most post-processing-relevant work is integrated into the upscaling/denoising papers.

## #1 — CGVQM+D: Computer Graphics Video Quality Metric and Dataset (06/30/25)
- Full-reference video quality metric optimized for CG content (not natural video).
- Generates per-pixel error maps + global quality scores.
- See [denoising.md](./denoising.md) for full analysis.
- **Relevance**: **MEDIUM** — Useful for visual regression testing pipeline.

## #30 — Coarse Pixel Shading with Temporal Supersampling (05/15/18)
- VRS + temporal resolve. The temporal supersampling component is a post-process pass.
- See [upscaling.md](./upscaling.md) for full analysis.
- **Relevance**: **HIGH** — The temporal resolve component improves quality of any reduced-rate shading.

## Post-processing-adjacent work NOT on the library page

### XeGTAO spatial/temporal denoising
- The GTAO denoiser is effectively a post-process blur with variance-guided weights.
- Open-source at GameTechDev/XeGTAO.

### Tone mapping / HDR display
- Intel has GDC/SIGGRAPH talks on HDR display handling but no standalone library publication.
- **Relevance**: MEDIUM — revisit when HDR output is added to UzEngine.
