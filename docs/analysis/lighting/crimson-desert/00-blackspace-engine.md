# Crimson Desert · BlackSpace Engine — Lighting Architecture

**Status**: Reverse-engineered from public materials (GDC 2025, Digital Foundry, CES 2026, Pearl Abyss dev notices, gamegpu.com technical breakdown, Steam community forum posts, game config reverse engineering).
**Confidence**: High on direction (per-pixel RTGI, neural denoising), medium on exact implementation details (Pearl Abyss has not published papers).
**Why we care**: Digital Foundry stated at CES 2026 that BlackSpace's per-pixel RTGI **"rivals or edges out Lumen" while demanding "noticeably less GPU overhead"**. The user wants UzEngine to match this quality.

> **TL;DR**: BlackSpace is a fully ray-traced renderer. No baked lighting, no lightmaps, no reflection probes. Every pixel in Crimson Desert is lit by at least one primary GI ray, then aggressively denoised through a neural reconstruction net (DLSS 3.5 RR or AMD's equivalent "Ray Regeneration"). The trick that gets them AAA quality at 4K/60 on a 7900 XTX is **not** cheap rays — it is very high-quality temporal+neural reconstruction operating on a small number of rays, combined with path-tracing-style unification of all lighting into one mathematical model instead of 6 separate techniques glued together.

---

## 1 · Engine identity

- **Name**: BlackSpace Engine (proprietary; successor to the original Black Desert Online engine from 2015).
- **Developer**: Pearl Abyss (Korea).
- **Showcase**: GDC 2025 (closed-door), CES 2026 (Digital Foundry demo), Crimson Desert launch demo (March 2026).
- **Platforms**: PC (full path tracing), PS5 Pro (hybrid), PS5/XSX (reduced), XSS (gated), Switch 2 (TBD).
- **Distinct from Lumen in two ways**:
  1. **No probe-based distant gather**. Lumen offloads distant rays to a world-probe atlas; BlackSpace traces per-pixel all the way.
  2. **No surface cache**. Lumen caches lit material attributes on mesh cards; BlackSpace evaluates materials inline during tracing.

Pearl Abyss describes their rewrite as going from "artistic imitation" (the original BDO engine's screen-space approximations) to "physically accurate simulation" (full path tracing).

---

## 2 · Pipeline overview

```text
Geometry submit
    ├── Nanite-equivalent virtual geometry (cluster LOD)
    └── BVH build / refit (TLAS refit every frame, BLAS refit on skeletal)

G-Buffer lay-down (raster prime)         ← primary visibility + motion vectors
    │
    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   PATH-TRACED LIGHTING (per-pixel)                   │
│   1 primary ray already given by raster G-Buffer (depth/normal/mat) │
│   +1 shadow ray per analytic light                                  │
│   +1 indirect ray (GI diffuse) bounced into importance-sampled env  │
│   +1 reflection ray for glossy surfaces (roughness < 0.5)           │
│   +N volumetric samples along ray (fog/god-rays)                    │
└─────────────────────────────────────────────────────────────────────┘
    │
    ▼
ReSTIR DI (reservoir resampling) ─── many-light direct shadowing
    │
    ▼
ReSTIR GI (path reuse) ─── multi-bounce indirect reuse across pixels & frames
    │
    ▼
Noisy HDR buffer (1 spp GI, 1 spp refl, 1 spp shadow per light)
    │
    ▼
┌──────── NEURAL RECONSTRUCTION (mandatory for max quality) ────────┐
│  DLSS 3.5 Ray Reconstruction  (NVIDIA RTX)                        │
│  FSR + Ray Regeneration       (AMD)                               │
│  XeSS + AI Denoiser           (Intel)                             │
│  Transformer-based AI denoiser replaces hand-tuned SVGF/NRD       │
└───────────────────────────────────────────────────────────────────┘
    │
    ▼
Clean HDR buffer at output resolution (4K)
    │
    ▼
Volumetric integration (fog, clouds, god rays — injected in scene)
    │
    ▼
Tonemap / bloom / post
    │
    ▼
Present
```

Compared to a traditional deferred renderer:

- The **lighting pass is a single path-traced dispatch**, not a screen-space quad shader reading the G-Buffer.
- **All shadows** (including CSM replacements, local-light shadows, contact shadows) are one ray per light per pixel.
- **All GI** (skylight, bounce, emissive, area light) is one indirect ray.
- **Reflections** are one reflection ray (cone-traced at high roughness).
- The **denoiser is the long pole**, not the ray tracing. Without neural reconstruction the image is unwatchably noisy.

---

## 3 · Why "per-pixel" matters vs Lumen

Lumen's headline limitation is that distant lighting is computed on a coarse probe atlas and then **composited** into screen space. This is fast and stable but produces:

- **Light leaks** at probe resolution boundaries.
- **Disocclusion darkening** when new geometry appears and no probe has sampled it yet.
- **Low-frequency GI** (SH level 2 ≈ 9 coeffs per probe) in the distance.

BlackSpace avoids all three by tracing a real ray from every pixel — even distant ones — into the full scene. This costs more GPU, but the output is fundamentally higher frequency:

| Feature | Lumen (Software RT, default UE5) | Lumen (HWRT) | BlackSpace (PC max) |
|---|---|---|---|
| Indirect diffuse source | Screen probe → radiance cache → surface cache | Screen probe → surface cache via HWRT | Per-pixel ray → material eval inline |
| Distant lighting | World probe atlas (8³ per clipmap) | World probe + HWRT fallback | Native ray to scene |
| Light leaks | Yes (probe interp) | Reduced | None (by construction) |
| Ray budget / pixel / frame | ~0.25 (one probe per 16×16) | ~0.5 | ~3–4 (GI + refl + 1–2 shadow) |
| Minimum HW | SM6 + SDFs | RTX / RDNA2 | RTX 20xx+ / RDNA3 (RT-tier required) |
| Denoiser | SVGF + TAA | SVGF + TAA | DLSS-RR / FSR-RR (neural) |
| 4K 60 fps | Yes with DLSS Q | Borderline | Native 4K on 7900 XTX (per VideoCardz) |

The trade-off is stark: BlackSpace requires a capable RT GPU and a neural denoiser, but when both are present the image is noticeably cleaner than Lumen.

---

## 4 · Ray budget & what every pixel costs

Estimated per-pixel ray budget at max settings, interpolated from frame time analyses in public technical reviews:

| Ray | Purpose | Count | Notes |
|---|---|---|---|
| Primary | Visibility (G-Buffer) | 0 | Substituted by raster G-Buffer — saves ~2.5 ms. |
| Shadow | Per analytic light | N lights (filtered) | ReSTIR DI reduces to ~1 ray per pixel via reservoir selection. |
| GI diffuse | Indirect bounce | 1 | Bounced along cosine-weighted lobe aligned to G-Buffer normal. |
| GI secondary | Second bounce | 0 (screen reuse) | Reused via ReSTIR GI temporal/spatial reservoirs. |
| Reflection | Glossy | 0–1 | Skipped when roughness > 0.5; otherwise one ray per lobe. |
| Transmission | Glass / skin | 0–1 | Gated by material flags. |
| Volumetric | Fog march | 8–32 ray-march samples | In-scattering along primary direction. |

Total per-pixel "real" ray work at 4K = ~3–5 rays / pixel / frame in average content. At 3840×2160 that's ~25–40 M rays/frame. RTX 4070 Ti does ~500 Grays/s for primary rays, giving plenty of headroom for the denoiser to consume the rest of the 16.6 ms budget.

---

## 5 · Direct lighting (analytic + emissive)

### 5.1 Problem statement

Crimson Desert scenes frequently contain 100+ visible analytic lights: torches, fires, windows, enemy attack VFX, skill FX, lanterns, etc. Evaluating shadow rays for every light per pixel is infeasible.

### 5.2 Solution: ReSTIR DI

Pearl Abyss almost certainly uses **ReSTIR DI** (Bitterli et al. 2020), the canonical solution for many-lights direct illumination. See [../modern-techniques/restir.md](../modern-techniques/restir.md) for the full treatment.

Key properties used here:

- **Reservoir per pixel** stores a running estimate of the "best" light to shadow-test.
- **Spatial resampling** (1 pass, ~5 neighbors): rewrites the pixel's reservoir if a neighbor saw a brighter light.
- **Temporal resampling** (previous frame reprojection): reuses the reservoir from previous frame.
- **One shadow ray per pixel** is traced after the final reservoir selection.

Net effect: O(1) shadow rays per pixel for arbitrarily many lights, with correct visibility.

### 5.3 Emissive surfaces

Emissive geometry (torch flame, forge, windows at night) is handled by the same path as analytic lights via **light-sampling the triangle mesh of emissive primitives** (LSTM-style light BVH). Every emissive triangle becomes a light entry in the reservoir.

---

## 6 · Indirect GI

### 6.1 One bounce, per pixel

At every shaded pixel, one ray is fired along a cosine-weighted hemisphere aligned to the G-Buffer normal. The ray hits the scene, samples the material at the hit (albedo × BRDF), evaluates direct lighting there (reusing ReSTIR DI from a coarser probe grid), and returns.

### 6.2 Multi-bounce via temporal & spatial reuse

Instead of recursively tracing bounce 2, 3, 4… Pearl Abyss reuses indirect-ray results from *previous frames and neighboring pixels* via **ReSTIR GI** (Ouyang et al. 2021).

- Each pixel keeps a reservoir containing the best "indirect path" seen in a spatiotemporal neighborhood.
- Paths found last frame are motion-vector-reprojected and resampled into this frame's reservoirs.
- Paths found on neighboring pixels (same surface, similar normal) are spatially resampled.

This gives an effective bounce count of 3–8 for the frame time of 1 bounce. Sannikov-style radiance cascades or DDGI probes may be used as a fallback for "missed" reservoirs (no evidence either way), but ReSTIR GI by itself is sufficient for the quality Crimson Desert shows.

---

## 7 · Reflections

- **All surfaces** with roughness < 0.5 get a real reflection ray.
- **Hit shading** reuses the same ReSTIR DI + GI pipeline (reflections see correctly lit scenes, not just colored roughness approximations).
- **Glossy roughness** uses **stochastic lobe sampling** with GGX VNDF (Heitz 2018) — one sample per pixel, denoised.
- **Mirror surfaces** (water, wet roads) are effectively "free" from the denoiser's perspective because the signal is high-contrast and temporal reprojection is trivial.

Trade-off with Lumen: Lumen reflections on rough surfaces reuse the **same radiance cache** as diffuse GI, which causes noticeable mismatch between reflection and underlying shading. BlackSpace avoids this by shooting real reflection rays.

---

## 8 · Volumetric fog & god rays

From public demos: Crimson Desert implements physically accurate heterogeneous volumetric fog with light scattering, god rays, and dynamic density that reacts to wind and weather.

Likely implementation (based on Hillaire 2018 Frostbite approach + ray-tracing extension):

1. **Froxel grid** at camera frustum (160×90×64 typical).
2. Per-froxel: extinction σ_t from density field + in-scattering from all visible lights.
3. Analytic lights: shadow-ray into scene from every froxel (possibly stochastic).
4. **Skylight inscattering**: one ray from every froxel into sky LUT.
5. **Temporal accumulation** 8 frames deep, reprojected with camera+wind motion.
6. Final slice-by-slice integration along view ray, composited over opaque scene.

Heterogeneous medium (fog density varies spatially) is achieved via **Woodcock tracking** / **delta tracking** (Novák et al. 2018 course notes) — the standard approach for heterogeneous volumetric path tracing.

---

## 9 · Neural reconstruction (Ray Reconstruction)

This is the keystone. Without it the rendering is unusable at 1 spp/frame.

### 9.1 What it does

Ray Reconstruction (RR, NVIDIA DLSS 3.5 terminology) replaces the conventional denoiser stack (bilateral + SVGF + TAA) with a single transformer-based neural network trained on offline path-traced ground truth. Inputs:

- Noisy HDR from ray tracer (1 spp)
- Motion vectors
- Depth
- Normal
- Albedo
- Roughness / metallic
- Specular & diffuse demodulation buffers
- Previous frame's **clean** output (history)

Output: clean, temporally stable, upsampled HDR at output resolution.

### 9.2 Why a transformer

DLSS 3.5 introduced a transformer architecture (replacing DLSS 3's CNN). Transformers attend across space more coherently than conv kernels, which matters because neighboring noisy pixels correlate over *irregular* receptive fields (depending on geometry). Cross-pixel attention handles disocclusion + moving lights far better than fixed-kernel convs.

### 9.3 Why it unlocks more than "denoising"

Steam community forum testing (Crimson Desert launch) shows that enabling RR does **more** than clean up noise — it visibly unlocks GI frequency detail that's present in the raw ray tracer but lost to SVGF's aggressive spatial filter. SVGF sacrifices high-frequency content for noise reduction; RR's neural net preserves high-frequency information because it's trained to match path-traced GT.

### 9.4 Integration detail

BlackSpace makes RR (or FSR Ray Regen / XeSS equivalent) **effectively mandatory** at max quality. The in-engine hand-tuned denoiser used at Medium preset noticeably degrades GI quality. See [02-ray-reconstruction-integration.md](./02-ray-reconstruction-integration.md) for the integration layer.

---

## 10 · Shadows

All shadows are ray-traced — no CSM, no shadow maps, no VSM. This includes:

- Sun shadows (one ray per pixel, reuses ReSTIR DI reservoir).
- Local analytic light shadows (same).
- Contact shadows (emerge naturally from the same ray — no separate contact-shadow screen-space pass).
- Transparency shadows / stochastic order-independent (via ray payloads).

Why no VSM? Because per-pixel ray tracing already gives what VSM was trying to approximate at orders of magnitude better quality: arbitrary-resolution point-sampled visibility at no penumbra artifacts.

---

## 11 · Comparison with Lumen (one-line verdict)

| Axis | Lumen | BlackSpace |
|---|---|---|
| Fidelity | High | Higher |
| HW floor | SM6 + compute | RT-capable GPU mandatory |
| Denoiser | SVGF + TSR | DLSS-RR / FSR-RR / XeSS-AI |
| Distant GI | Probe atlas | Per-pixel |
| Reflection | Shared radiance cache | Dedicated ray |
| Best environment | Open world + stylized | Photoreal open world |

---

## 12 · What this means for UzEngine

BlackSpace's lesson is not "do full path tracing" — very few studios can ship that. The lessons that **do** transfer:

1. **Unify lighting under one mathematical model** (path-integral) instead of gluing 6 unrelated techniques together. Our current shader evaluates direct lights + IBL + SSGI + probes as separate add-ops; this produces mismatch between each term and is impossible to tune globally.

2. **Invest in the denoiser, not the ray tracer.** Pearl Abyss's signal is noisy at 1 spp/frame; the neural net does the heavy lifting. Our shadows & SSGI denoiser is currently primitive.

3. **ReSTIR is non-optional** for many-lights worlds. We currently evaluate all lights per pixel without shadowing — that's how Sponza ends up looking flat.

4. **Real reflection rays win** once you have RT. Our SSR has classic edge-fallback issues; a hybrid "screen-trace first, RT fallback" is the right architecture.

5. **Reversed-Z + motion vectors everywhere** are pre-requisites that BlackSpace gets right. (We already use reversed-Z — good.)

See [../uzengine/gap-analysis.md](../uzengine/gap-analysis.md) and [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md) for the concrete plan.

---

## 13 · Sources

- Pearl Abyss Dev Notice: "A Look into Pearl Abyss' BlackSpace Engine" (crimsondesert.pearlabyss.com, 2025).
- GDC 2025 closed-door showcase (coverage: Terminal Gamer, Fix Gaming, Gamer Matters, TechPorn — March 2025).
- Digital Foundry CES 2026 coverage (via wccftech, ResetEra thread, VGTimes).
- gamegpu.com "Tracing the path in Crimson Desert · BlackSpace Engine Technical Analysis" (2026).
- Krensh.com: "BlackSpace Engine: Ray Tracing Without the FPS Hit".
- Tech Sportskeeda: engine/tech details.
- NVIDIA DLSS 3.5 Ray Reconstruction material (nvidia.com/geforce/news, blogs.nvidia.com).
- Benedikt Bitterli et al., "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting" (SIGGRAPH 2020).
- Ouyang, Liu, Kettunen, Pharr, Pantaleoni, "ReSTIR GI: Path Resampling for Real-Time Path Tracing" (EGSR 2021).
- Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (Frostbite, 2020).
- Novák, Georgiev, Hanika, Jarosz, "Monte Carlo methods for volumetric light transport" (SIGGRAPH 2018 course).
- Steam community forum threads: "DLSS / RR / Visual bugs", "Please don't make RR mandatory" (empirical observations of RR's effect).
