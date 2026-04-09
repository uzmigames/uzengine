<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — Denoising

## #23 — Temporally Stable Real-Time Joint Neural Denoising and Supersampling
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/temporally-stable-denoising-and-supersampling.html
- **Date**: 07/27/2022
- **Authors**: Intel
- **Summary**: Given noisy, low-resolution input (e.g., 1-4 spp path-traced), the network performs spatiotemporal filtering to produce denoised and antialiased output at 2x the input resolution. Combines denoising + upscaling in a single neural pass.
- **Key contribution**: Joint optimization avoids the quality loss of chaining separate denoiser → upscaler. The temporal stability comes from explicit motion-compensated history accumulation within the network.
- **Relevance**: **HIGH** — Foundation for UzEngine's `phase0_path-tracer-runtime-mode`. A single pass replaces OIDN + TAA/upscaler, reducing total post-process cost.
- **UzEngine integration point**: Path tracer output → this network → final display. Needs inference runtime (DirectML or custom compute).
- **Difficulty**: 3-4 weeks.

## #1 — CGVQM+D: Computer Graphics Video Quality Metric and Dataset
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/cgvqm-d-computer-graphics-video-quality.html
- **Date**: 06/30/2025
- **Authors**: Intel
- **Summary**: Full-reference video quality metric that significantly outperforms existing metrics (SSIM, LPIPS, etc.) while generating both per-pixel error maps and global quality scores. Designed specifically for computer graphics content (not natural video).
- **Key contribution**: CG-specific quality metric — existing metrics (PSNR, SSIM) were designed for natural images and miss CG-specific artifacts like temporal aliasing, shader precision errors, and LOD transitions.
- **Relevance**: **MEDIUM** — Excellent for automated visual regression testing. Could replace or augment our current PSNR-based Falcor comparison pipeline. Per-pixel error maps can pinpoint exactly which pixels diverge.
- **UzEngine integration point**: CI/CD visual regression pipeline. Compare path-tracer output vs Falcor reference using CGVQM instead of raw PSNR.
- **Difficulty**: 1 week.

## Open Image Denoise (OIDN) — not on library page but Intel-maintained
- **Authors**: Attila T. Afra et al. (Intel)
- **Summary**: Production neural denoiser (U-Net) with albedo/normal aux buffers. v2.x ships SYCL + CUDA + HIP + Metal backends.
- **Relevance**: **HIGH** for path tracer reference mode. Well-documented API, trivial integration.
- **Website**: https://www.openimagedenoise.org/

## Adoption priority for UzEngine

1. **Neural denoising+supersampling** (#23) — when path tracer mode ships
2. **CGVQM+D** (#1) — for visual regression CI
3. **OIDN** — as offline reference denoiser
