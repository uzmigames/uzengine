# Ray Reconstruction & Modern Denoisers

Comparison of the denoiser options for a real-time ray-traced pipeline in
2026. Covers:

- **SVGF** — the classical reference (2017)
- **NVIDIA NRD** — production-ready NRD library (2022)
- **NVIDIA DLSS 3.5 Ray Reconstruction (RR)** — neural denoiser+upscaler (2023)
- **AMD FSR Ray Regeneration** — AMD equivalent (2024)
- **Intel XeSS AI Denoiser** — Intel equivalent (2024)

---

## 1 · Why denoising matters

At 1 spp (sample per pixel) per frame — which is what real-time path
tracing can afford — the noise variance is approximately `σ ≈ 1/√1 = 1.0`
(relative to signal). The image is unwatchable.

Goal: reduce σ to ~0.05 (20 dB noise reduction) while preserving:

- Sharp material boundaries
- High-frequency texture detail
- Moving objects (no ghosting)
- Disoccluded regions (no smearing)

Neural denoisers do this substantially better than classical at the cost
of HW specialization (Tensor cores, XMX, compute-intensive matmul).

---

## 2 · SVGF — the classical reference

Schied et al. 2017, "Spatiotemporal Variance-Guided Filtering."

### 2.1 Architecture

```
Per frame:
  1. Temporal integration:    blend current noisy with history, compute variance
  2. Variance estimation:     7×7 box filter on luminance moments
  3. À-trous wavelet filter:  5 iterations of increasing-stride bilateral edge-stopping
  4. History update:          write back to temporal buffer
```

Edge-stopping weights use:
- Depth gradient
- Normal gradient  
- Luminance variance (the guide signal)

### 2.2 Strengths

- Cheap (~1.5 ms at 1080p on GTX 1080)
- Well-understood, open-source implementations everywhere
- Works on any SM5+ GPU

### 2.3 Weaknesses

- Over-blurs sharp shadow edges in open areas (large variance).
- Struggles with fast-moving lights (temporal lag).
- Disocclusion flicker on camera cuts.

---

## 3 · NRD — NVIDIA Real-time Denoisers

github.com/NVIDIA-RTX/NRD. Open-source library.

### 3.1 What it is

A collection of production-quality denoisers for specific RT signals:

- **REBLUR** — diffuse / specular denoiser (reblur means "reconstruct + blur").
- **RELAX** — variant of REBLUR optimized for weaker spatial filtering.
- **SIGMA** — shadow denoiser.
- **Reference** — a slower, higher-quality ground-truth denoiser.

Each is a tuned mini-SVGF + custom heuristics.

### 3.2 Architecture (REBLUR)

```
Input:   noisy HDR + motion + depth + normal + hit distance
Output:  clean HDR

Stages:
  1. Pre-blur (bilateral, small kernel)
  2. Temporal accumulation (reprojected, with occlusion test via motion vector)
  3. History correction (anti-ghosting)
  4. Variance estimation
  5. Spatial blur (à-trous, wider kernel)
  6. Post-filtering
```

### 3.3 Why it's better than SVGF

- Hit-distance-aware kernel radius (narrow for sharp reflections, wide for rough).
- Per-pixel demodulation (divides out albedo/fresnel for clean temporal).
- Sub-pixel jitter handling for DLSS compatibility.
- Separate diffuse and specular pipes (they have different noise characteristics).
- Well-tested on shipping titles (Cyberpunk 2077, Alan Wake 2, Portal RTX).

### 3.4 Integration cost

NRD is a drop-in library. You:

1. Allocate NRD buffers (~100 MB at 1440p).
2. Feed it your noisy HDR + G-Buffer every frame.
3. Consume the clean output.

~1 week of integration. Works on any HW (SM6 compute).

---

## 4 · DLSS 3.5 Ray Reconstruction (NVIDIA)

See [../crimson-desert/02-ray-reconstruction-integration.md](../crimson-desert/02-ray-reconstruction-integration.md)
for the full deep-dive. Summary here:

- Replaces **both NRD AND TAA/DLSS upscaling** with one transformer net.
- Trained on 16K+ spp offline path-traced ground truth.
- Takes demodulated diffuse+specular separately.
- Runs on Tensor cores (RTX 20xx+).
- Output is clean HDR at upscaled resolution, ready for tonemap.

### 4.1 When to pick RR over NRD + DLSS

- **RR wins**: extreme quality on RTX HW; GI detail preservation; disocclusion handling.
- **NRD+DLSS wins**: non-RTX HW (NRD runs on any compute); more artist control over denoise strength; simpler debugging.

Production games (Cyberpunk with Overdrive, Alan Wake 2, Portal RTX,
Crimson Desert at max) all ship RR by default on supported HW and fall
back to NRD+DLSS or NRD+DLAA on other HW.

---

## 5 · FSR Ray Regeneration (AMD, 2024)

Architecturally similar to RR. Differences:

- Implemented as compute shaders (no Tensor cores needed).
- Slightly less quality than RR (smaller net, simpler training).
- Runs on RDNA2+ and even older GPUs (any GPU supporting FSR).
- Open-source SDK from AMD.

Integration is parallel to RR — same buffer contract (demodulated diffuse
+ specular + G-Buffer).

---

## 6 · Intel XeSS 2 AI Denoiser

For Arc GPUs (XMX acceleration) and others (DP4a compute fallback).
Architecturally similar. Integration: parallel to RR/FSR RR.

---

## 7 · Decision matrix for UzEngine

| Target user | Denoiser |
|-------------|----------|
| No RT, classical pipeline only | None — SSGI / SSR have their own temporal filters |
| SW RT enabled | **NRD-style** (SVGF clone or NRD integration) |
| HWRT on RTX 20xx+ | NRD + optional RR plug-in |
| HWRT on RDNA2+ | NRD + optional FSR RR |
| Engine extensibility (modder-facing) | NRD (open, documented) |

**Recommendation**: build our own **NRD-inspired denoiser** as the baseline
(we control it, it's portable, it works on any HW). Add an **NGX plug-in
slot** for RR on supported GPUs in a Phase 2.

---

## 8 · Hit distance: the unsung input

Regardless of which denoiser you pick, **hit distance** is the killer
feature all of them share.

### 8.1 Why

A reflection ray that hits at 1 cm should denoise with a tiny kernel (it's
a near-mirror surface). A reflection ray that hits at 20 m should denoise
with a large kernel (it's far-field glossy). Classical SVGF has a fixed
kernel per iteration — wastes detail up close, under-filters far away.

Hit-distance-aware kernels scale with `tan(coneAngle) * hitDistance`,
giving correct kernel widths at every distance.

### 8.2 What to write

Every RT pass must write:

- Noisy color (HDR fp16)
- Hit distance (R16F) — or compacted into alpha of color

That's one extra channel. Trivial to add, big quality win downstream.

---

## 9 · Temporal demodulation

All production denoisers **demodulate**:

- Diffuse: divide out `albedo × (1 - metallic)`.
- Specular: divide out `F × (1 - roughness)`. Or simpler, divide by fresnel.

Denoise the lighting signal, then remodulate at the end. This keeps
material detail sharp.

Our current SSGI/SSR don't demodulate. First thing to add.

---

## 10 · References

- Schied et al., "Spatiotemporal Variance-Guided Filtering" (HPG 2017). cg.ivd.kit.edu/svgf.php
- Dmitry Zhdan, "NRD: NVIDIA Real-time Denoiser" — github.com/NVIDIA-RTX/NRD documentation and source.
- NVIDIA DLSS 3.5 Ray Reconstruction: nvidia.com/geforce/news/nvidia-dlss-3-5-ray-reconstruction/, blogs.nvidia.com/blog/ai-decoded-ray-reconstruction/
- AMD FSR SDK — gpuopen.com/fsr/
- Intel XeSS 2 — intel.com/content/www/us/en/developer/articles/guide/get-started-xess-v2.html
- Bako et al., "Kernel-Predicting Convolutional Networks for Denoising Monte Carlo Renderings" (SIGGRAPH 2017) — neural denoising foundation.
