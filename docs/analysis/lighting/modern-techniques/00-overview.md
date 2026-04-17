# Modern Lighting Techniques — Selection Matrix

A catalog of the lighting & shadow techniques that matter in 2026, with
explicit guidance on **when to use each** and **when to skip each**. Used
as the master reference for the UzEngine pipeline redesign.

---

## 1 · Technique matrix

| Technique | Type | Year | HW floor | Dynamic? | UzEngine fit |
|-----------|------|------|----------|----------|--------------|
| **Analytic lights + CSM shadow maps** | Direct | 1990s | SM4 | Yes | Baseline — already have |
| **IBL (image-based lighting)** | Indirect | 2000s | SM4 | Semi | Baseline — already have |
| **PCSS soft shadows** | Direct | 2005 | SM4 | Yes | Easy upgrade for directional & spot lights |
| **Contact shadows** | Direct | ~2010 | SM5 | Yes | Easy screen-space improvement |
| **SSGI (Screen Space GI)** | Indirect | 2017 | SM5 | Yes | Already have a simple version |
| **SSR (Screen Space Reflection)** | Indirect | 2010 | SM5 | Yes | Already have; upgrade to stochastic |
| **SVOGI (Voxel Cone Tracing)** | Indirect | 2011 | SM5 | Yes | Skip — bad fit for open worlds |
| **DDGI (Dynamic Diffuse GI)** | Indirect | 2019 | SM6 / RTX | Yes | Strong candidate for mid-tier HW |
| **SDFGI (Godot)** | Indirect | 2020 | SM6 | Yes | Similar to DDGI but SDF-traced |
| **Lumen SW (mesh SDF)** | Indirect | 2021 | SM6 | Yes | Can borrow architecture; big effort |
| **Lumen HWRT** | Indirect | 2022 | RTX / RDNA2 | Yes | Phase 2 after SW path |
| **Virtual Shadow Maps (VSM)** | Direct | 2022 | SM6 | Yes | **Replaces CSM** — big quality win |
| **ReSTIR DI (reservoir DI)** | Direct | 2020 | SM6 / RTX | Yes | Solves many-lights problem |
| **ReSTIR GI (path reuse)** | Indirect | 2021 | RTX / RDNA2 | Yes | For RT tier |
| **Radiance Cascades (Sannikov)** | Indirect | 2023 | SM6 | Yes | Research; promising |
| **Neural Radiance Cache (NRC)** | Indirect | 2021 | RTX (Tensor) | Yes | Research; NVIDIA specific |
| **Ray Reconstruction (DLSS 3.5)** | Denoiser | 2023 | RTX | — | Plug-in via NGX SDK |
| **FSR Ray Regeneration** | Denoiser | 2024 | RDNA | — | Plug-in via FSR SDK |
| **NRD (NVIDIA Real-time Denoise)** | Denoiser | 2022 | SM6 | — | Drop-in for classical denoise |
| **SVGF (Spatiotemporal Var-Guided Filter)** | Denoiser | 2017 | SM5 | — | Classic; implement before neural |
| **Hybrid SSR + RT fallback** | Indirect | 2020 | SM6 | Yes | **Default for AAA today** |

---

## 2 · What UzEngine should adopt (tl;dr)

Three tiers mirroring how AAA engines tier their pipelines:

### Tier 1 — SM6 (no RT HW required)

- **Direct**: Analytic lights + **VSM** (replace CSM) + PCSS + **contact shadows**.
- **Indirect diffuse**: SSGI (current) + **DDGI-style probe grid** + sky LUT fallback.
- **Indirect specular**: SSR (stochastic-VNDF, current) + probe fallback + sky.
- **Shadows for local lights**: local shadow atlas (we have).
- **Denoise**: SVGF + TAA / TSR.
- **AO**: GTAO (current).

### Tier 2 — RT-capable HW (RTX 20xx+ / RDNA2+)

Everything in Tier 1, plus:

- **Hybrid**: SSR → RT fallback; SSGI → RT fallback.
- **Ray-traced shadows** optional (replaces VSM for sun when enabled).
- **ReSTIR DI** for many-lights scenes.
- **Denoise**: NRD (NVIDIA Real-time Denoise library) for specular+diffuse+shadow channels.

### Tier 3 — RT-high-end

Everything in Tier 2, plus:

- **Full path-traced GI** (inspired by BlackSpace): per-pixel indirect ray + ReSTIR GI.
- **NGX Ray Reconstruction** / FSR RR as the denoiser (replaces NRD+TSR).

Each tier is gated by a single CVar bundle, not ten CVars. See
[../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md).

---

## 3 · What to skip

**SVOGI (voxel cone tracing)** — Crytek's SVOGI is beautiful at small scale
but falls apart at open-world scale. Voxel octree memory becomes a
nightmare. Lumen's voxel lighting is *only* a multi-bounce layer, not the
primary gather. Skip.

**Light propagation volumes** — deprecated; replaced by probe-based
techniques (DDGI/SDFGI).

**Baked irradiance volumes with runtime lerp** — we said dynamic. Skip unless
we add a bake mode.

**Reflective Shadow Maps (RSMs)** — very GPU-inefficient for more than
one directional light. Skip.

**Photon mapping** — not real-time at any HW tier. Offline only.

---

## 4 · Per-feature deep-dives

Each of the following files is a standalone study of one technique. Read
the ones relevant to the tier you're working on.

- [restir.md](./restir.md) — ReSTIR DI & ReSTIR GI.
- [virtual-shadow-maps.md](./virtual-shadow-maps.md) — VSM page-table, HZB, Nanite interop.
- [ddgi-sdfgi.md](./ddgi-sdfgi.md) — Probe-based dynamic GI (NVIDIA DDGI + Godot SDFGI).
- [radiance-cascades.md](./radiance-cascades.md) — Sannikov's radiance cascades.
- [ray-reconstruction.md](./ray-reconstruction.md) — DLSS 3.5 RR, FSR RR, NRD, SVGF comparison.
- [pcss-contact-shadows.md](./pcss-contact-shadows.md) — PCSS + screen-space contact shadows.
- [neural-radiance-cache.md](./neural-radiance-cache.md) — NRC (research).

---

## 5 · References

- Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH 2013 course) — PBR baseline.
- Bitterli et al., "Spatiotemporal reservoir resampling" (2020).
- Ouyang et al., "ReSTIR GI: Path Resampling for Real-Time Path Tracing" (2021).
- Majercik et al., "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes" (JCGT 2019).
- Sannikov, "Radiance Cascades: A Novel Approach to Calculating Global Illumination" (preprint 2023).
- Müller et al., "Real-time Neural Radiance Caching for Path Tracing" (SIGGRAPH 2021).
- NVIDIA DLSS 3.5 Ray Reconstruction — nvidia.com/geforce/news/nvidia-dlss-3-5-ray-reconstruction/.
- Schied et al., "Spatiotemporal Variance-Guided Filtering" (HPG 2017).
- Epic Virtual Shadow Maps docs.
- Stephano, "Sparse Virtual Shadow Maps" (ktstephano.github.io/rendering/stratusgfx/svsm).
