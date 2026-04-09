<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — Upscaling, Frame Generation & Temporal Reconstruction

## #3 — GFFE: G-buffer Free Frame Extrapolation for Low-latency Real-time Rendering
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/gffe-g-buffer-free-frame-extrapolation.html
- **Date**: 12/05/2024
- **Authors**: Songyin Wu (UC), Deepak Vembar (Intel), Anton Sochenov (Intel), Selvakumar Panneer (Intel), Sungye Kim (Intel), Anton Kaplanyan (Intel), Ling-Qi Yan (UC)
- **Paper**: https://dl.acm.org/doi/pdf/10.1145/3687923
- **Summary**: Frame extrapolation method that generates future frames from previous frames *without* requiring G-buffers as input and *without* introducing additional latency (unlike DLSS 3 / FSR 3 frame interpolation). Uses a heuristic framework analyzing dynamic fragment motion and disocclusion types, followed by a lightweight shading correction neural network. Achieves comparable or better results than G-buffer-dependent extrapolation methods with easier engine integration.
- **Key insight**: Extrapolation (predict future frame) vs interpolation (generate between two known frames). Extrapolation adds zero latency but must handle disocclusion and motion prediction. GFFE solves this without engine-side G-buffer export.
- **Relevance**: **HIGH** — UzEngine has no frame generation. GFFE's G-buffer-free design means minimal engine integration cost compared to DLSS3/FSR3 which need motion vector + depth export. Could provide 2x effective framerate boost.
- **UzEngine integration point**: Post-present pass, reads only final color + motion vectors. No render graph changes needed.
- **Difficulty**: 2-3 weeks.

## #10 — ExtraSS: A Framework for Joint Spatial Super Sampling and Frame Extrapolation
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/extrass-a-framework-for-joint-spatial.html
- **Date**: 12/11/2023
- **Authors**: Intel
- **Summary**: Novel framework combining spatial super-sampling and frame extrapolation to enhance real-time rendering performance. Unifies upscaling + frame gen in a single pass rather than chaining two separate systems (e.g., XeSS + GFFE separately).
- **Relevance**: **HIGH** — represents the next evolution: one neural pass handles both upscaling and frame gen. More efficient than chaining XeSS + GFFE.
- **UzEngine integration point**: Replace separate upscaler + frame gen with unified ExtraSS pipeline.
- **Difficulty**: 2-3 weeks.

## #23 — Temporally Stable Real-Time Joint Neural Denoising and Supersampling
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/temporally-stable-denoising-and-supersampling.html
- **Date**: 07/27/2022
- **Authors**: Intel
- **Summary**: Given noisy, low-resolution input, the network performs spatiotemporal filtering to produce denoised and antialiased output at 2x resolution. Targets path-traced content where input is inherently noisy (1-4 spp).
- **Relevance**: **HIGH** — direct application for our `phase0_path-tracer-runtime-mode`. Combines denoising + upscaling in one pass, reducing total post-process cost vs separate OIDN + TAA.
- **UzEngine integration point**: Path tracer → this network → final output. Replaces OIDN + separate upscaler.
- **Difficulty**: 3-4 weeks (neural network inference integration required).

## #30 — Coarse Pixel Shading with Temporal Supersampling
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/coarse-pixel-shading-with-temporal-supersampling.html
- **Date**: 05/15/2018
- **Authors**: Intel
- **Summary**: Extends Variable Rate Shading (VRS / coarse pixel shading) with a temporal supersampling scheme. Achieves notably better image quality than checkerboard rendering while providing substantial reduction in shading cost. Key: the temporal component recovers the sub-pixel detail lost by coarse shading.
- **Relevance**: **HIGH** — UzEngine has no VRS support. This paper provides a production-ready VRS + temporal resolve approach. DX12 Tier 2 VRS is available on all modern GPUs. The temporal supersampling component is directly compatible with our TAA pipeline.
- **UzEngine integration point**: GBuffer pass with VRS Tier 2 → temporal resolve in TAA → final output. Requires DX12 VRS API integration in RHI.
- **Difficulty**: 1-2 weeks.

## Intel XeSS (not on library page — separate SDK)
- **SDK**: https://github.com/intel/xess
- **Summary**: Neural temporal upsampler (XMX on Arc, DP4a fallback). XeSS 2.0 adds frame generation.
- **Relevance**: **HIGH** — third upscaler alongside DLSS/FSR, already planned in UzEngine tasks.
- **Note**: Not listed on the research library page but closely related. Tracked separately.

## Adoption priority for UzEngine

1. **Coarse Pixel Shading + Temporal SS** (#30) — cheapest win, VRS is a DX12 feature we should support
2. **GFFE** (#3) — frame gen with zero latency and minimal integration
3. **ExtraSS** (#10) — unified upscaling+frame gen for the long term
4. **Neural denoising+supersampling** (#23) — for path tracer mode
