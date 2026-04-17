<!-- doc-meta
date: 2026-04-08
source: Intel Graphics Research Library (Playwright verified)
-->

# Intel Research — PBR Sampling & Materials

## #15 — Sampling Visible GGX Normals with Spherical Caps
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/sampling-visible-ggx-normals.html
- **Date**: 08/02/2023
- **Authors**: J. Dupuy, A. Benyoub (Intel Corporation)
- **Venue**: HPG 2023 (Delft, Netherlands)
- **Paper**: https://arxiv.org/abs/2306.05044
- **PDF**: https://cdrdv2.intel.com/v1/dl/getContent/782052
- **Summary**: Novel VNDF (Visible Normal Distribution Function) sampling algorithm for GGX microfacets. Key insight: any GGX material can be reduced to a hemispherical mirror, making the sampling geometry trivial. Simpler than the state-of-the-art (Heitz 2018) and leads to faster performance. Drop-in replacement for existing VNDF samplers.
- **Key contribution**: Replaces the complex hemisphere-to-disk projection in Heitz 2018 with a spherical cap sampling approach. Fewer instructions, no numerical edge cases at grazing angles, same or better quality.
- **Relevance**: **HIGH** — Direct upgrade for UzEngine's `pbr_common.hlsli` GGX sampler. Affects:
  - IBL prefilter convolution (importance sampling)
  - SSR ray direction sampling
  - Any future path-traced reflection/GI sampling
  - Fewer fireflies on rough surfaces = less temporal noise for TAA
- **UzEngine integration point**: Replace `sampleGGXVNDF()` in `pbr_common.hlsli` with spherical cap algorithm.
- **Difficulty**: 2-3 days. Drop-in replacement — same inputs (view dir, roughness, random pair), same output (sampled normal).

## #14 — Real-Time Rendering of Glinty Appearances using Distributed Binomial Law
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/real-time-rendering-of-glinty-appearances.html
- **Date**: 08/02/2023
- **Authors**: Intel
- **Venue**: SIGGRAPH 2023
- **Summary**: Renders glittery/sparkle materials (car paint, snow, metallic flakes) in real-time by counting the number of discrete flakes reflecting light toward the camera within every texel covered. Uses a distributed binomial law for efficient statistical counting rather than brute-force per-flake evaluation.
- **Key contribution**: Enables physically-based glitter/sparkle without per-flake geometry. The binomial approximation is cheap enough for real-time while capturing the characteristic glinty appearance.
- **Relevance**: **MEDIUM** — Not immediately needed but valuable for automotive/material showcase demos. Would extend UzEngine's material system beyond standard PBR roughness/metallic.
- **UzEngine integration point**: New material type or material layer in `mesh_ps.hlsl`. Needs flake density + size parameters in material buffer.
- **Difficulty**: 1-2 weeks.

## #6 — Nonorthogonal Reduction for Rendering Fluorescent Materials
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/nonorthogonal-reduction-for-rendering.html
- **Date**: 06/25/2024
- **Authors**: Intel
- **Venue**: EGSR 2024
- **Summary**: Nonorthogonal function basis to accurately render fluorescent materials (wavelength re-emission) in nonspectral (RGB) engines. Normally fluorescence requires spectral rendering; this method approximates it within standard RGB pipelines.
- **Relevance**: **LOW** — UzEngine is an RGB-only engine with no fluorescence content planned. Interesting academically but no practical use case currently.
- **Difficulty**: 1 week if ever needed.

## #11 — The Effect of Display Capabilities on Gloss Consistency
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/effect-display-capabilities-on-gloss-consistency.html
- **Date**: 12/11/2023
- **Authors**: Intel
- **Summary**: Studies how display dynamic range, peak luminance, and 3D capabilities affect gloss reproduction fidelity. Faithful gloss reproduction is inherently difficult due to display limitations.
- **Relevance**: **LOW** — Perceptual study. Informative for HDR output tuning but not actionable code.
- **Difficulty**: N/A (reference material).

## #9 — Physically Based Lighting for 3D Generative Models of Cars
- **URL**: https://www.intel.com/content/www/us/en/developer/articles/technical/physically-based-lighting-for-3d-generative-models.html
- **Date**: 04/30/2024
- **Authors**: Intel
- **Summary**: Computing PBR lighting for car models from 3D-aware generative models. Creates realistic imagery with accurate reflections by applying standard PBR pipeline to AI-generated geometry.
- **Relevance**: **LOW** — GenAI + rendering intersection. Not applicable to production game rendering.
- **Difficulty**: N/A.

## Additional Intel-affiliated PBR work (not on library page but related)

### Bounded VNDF sampling (Eto & Tokuyoshi)
- Not directly on Intel's library page but cited in Intel work. The Spherical Cap method (#15) is the Intel-native solution and supersedes this for our purposes.

### XeGTAO bent normals
- Open-source at GameTechDev/XeGTAO. Latest variants add bent-normal output for specular occlusion. Addresses UzEngine's GTAO over-occlusion bug (2026-03-30 memory). Not a paper on the library page but maintained by Intel.

## Adoption priority for UzEngine

1. **Spherical Cap VNDF** (#15) — highest ROI, 2-3 day drop-in upgrade
2. **Glinty Appearances** (#14) — when material showcase is prioritized
3. **Fluorescent Materials** (#6) — only if spectral rendering ever added
