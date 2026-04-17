# id Tech 8 (id Software) — Lighting Analysis

**Shipped titles**: Doom: The Dark Ages (2025), with the June 2025 path-tracing update that turned id Tech 8 into a fully path-traced engine on capable HW.

**Distinctive identity**: **Mandatory ray-traced lighting** from day one (even at low settings), path tracing at top tier, and one of the most performant RT implementations shipped. id Software's Billy Khan and Tiago Sousa consistently push the efficiency envelope.

---

## 1 · Lighting pipeline overview

id Tech 8 ships with two lighting modes:

### Tier 1 (default "ray-traced" mode — always on)

```text
G-Buffer lay-down
   ▼
Ray-traced shadows (primary lights)
   ▼
Analytic direct lighting (with ray-traced shadow term)
   ▼
Hybrid GI (SSGI + probe fallback + ray-traced fallback for misses)
   ▼
Ray-traced reflections (all glossy surfaces)
   ▼
Volumetric fog (froxel)
   ▼
Denoise + TSR/DLSS
   ▼
Post + tonemap
```

### Tier 2 (path tracing update, June 2025)

```text
G-Buffer lay-down (primary visibility)
   ▼
┌─ PATH-TRACED LIGHTING (per pixel) ──────────────────────┐
│ 1 indirect diffuse ray                                  │
│ 1 reflection ray (gated by roughness)                   │
│ 1 shadow ray per light                                  │
│ Multi-bounce via SHaRC radiance cache                   │
└─────────────────────────────────────────────────────────┘
   ▼
NVIDIA SHaRC (Spatial Hash Radiance Cache)
   ▼
Shader Execution Reordering (SER) for coherent exec
   ▼
DLSS 4 / DLSS Ray Reconstruction
   ▼
Post + tonemap + Multi Frame Generation
```

---

## 2 · Distinctive techniques

### 2.1 Ray tracing as baseline (not a toggle)

id Tech 8 was designed from day one with ray tracing as the minimum
quality tier, not an optional extra. This is architecturally different
from UE5 (where Lumen can do SW RT and fall back to SSGI+IBL for
non-RT) and most other modern engines.

Reasons stated by Billy Khan (director of engine tech):
- Simplifies the lighting pipeline: one code path, not two.
- Hardware ubiquity: RTX 20xx+ / RDNA2+ / Xbox Series X/S / PS5 all support HWRT.
- Future-proofing: every platform still being released in 2025+ has RT HW.

Trade-off: Doom: The Dark Ages does not run on GPUs without RT HW. That
excludes GTX 1060, etc. Accepted for a 2025 title.

### 2.2 SHaRC (Spatial Hash Radiance Cache)

The signature technology of id Tech 8's path tracing mode. NVIDIA's
open-source SHaRC (nvidia-rtx/SHaRC on GitHub) caches radiance in a
world-space hash grid.

How it works:
1. Path tracer queries "what's the estimated radiance at this world
   position in this direction?"
2. Hash the (position, direction) into a hash grid.
3. If cached → return the cached value.
4. Else → compute via path tracing, accumulate into cache slot.

Advantages over Lumen's probe+voxel cache:
- **Sparse**: only cells that are queried are allocated.
- **Adaptive**: cells dynamically resize based on variance.
- **Hash**: O(1) lookup without explicit probe placement.

id Tech 8 uses SHaRC as the **distant-gather layer**, analogous to
Lumen's radiance cache but simpler and more adaptive.

### 2.3 Shader Execution Reordering (SER)

NVIDIA-specific (RTX 40xx+), but heavily used by id Tech 8. After a ray
returns a hit, SER sorts subsequent work so that pixels hitting similar
materials execute together. Improves GPU throughput on divergent shading
by ~30%.

id Tech 8 leverages SER for the path tracer's shade-after-hit step,
where otherwise the GPU would wait for slow lanes.

### 2.4 Opacity Micro Maps (OMM)

For alpha-tested foliage, NVIDIA's Opacity Micro Maps accelerate ray
tracing through dense vegetation. id Tech 8 uses OMM to keep RT fast in
Hell's organic foliage.

### 2.5 Tiago Sousa's "Fast as Hell" GI approach

From the Reddit thread and partial coverage of the presentation:

- Direct lighting is **fully ray-traced** (not screen-space).
- Indirect lighting is **hybrid**: SSGI + SHaRC (cache) + RT fallback.
- The three stages are carefully blended using confidence masks.
- Denoising is minimized because SHaRC output is already stable.

---

## 3 · Path tracing update details

Released June 18, 2025. Key numbers:

- **Multi-bounce GI**: 2–3 bounces (not just 1).
- **Sun + moon + hell sky** all contribute to GI via path tracing.
- **DLSS Ray Reconstruction mandatory** at max settings (like BlackSpace).
- **Multi Frame Generation** (DLSS 4) gives 6.8× at 4K on RTX 5080/5090.
- **230 fps at 4K** on RTX 5090 with all features.

Tight integration with NVIDIA: SHaRC, SER, OMM, DLSS 4, RR are all NVIDIA
tech. AMD equivalents (FSR RR, FSR 4) reach most of the way but trail in
final quality.

---

## 4 · Comparison matrix

| Axis | Lumen | BlackSpace | **id Tech 8** |
|---|---|---|---|
| RT mandatory | No | Only at max | **Yes (baseline)** |
| Baseline GI | Lumen (SW or HWRT) | Path-traced | Ray-traced (baseline), path-traced (top) |
| Radiance cache | Surface + voxel + probes | None (per-pixel RT) | **SHaRC (spatial hash)** |
| Denoiser | SVGF + TSR | RR (neural) | Custom + RR (path tracing only) |
| HW floor | SM6 | RT-capable | **RT-capable (mandatory)** |
| Philosophy | Flexible | Full physical | **RT-first** |

---

## 5 · UzEngine takeaways

id Tech 8 is aspirational but not attainable in the next 12 months for
UzEngine. Key lessons:

1. **SHaRC is really interesting** — simpler to implement than Lumen's
   cache hierarchy, production-quality, open source. If we go RT, this
   is the right cache to build. GitHub: nvidia-rtx/SHaRC.

2. **RT-mandatory simplifies the codebase**. Two-tier pipelines (SW+HWRT)
   have twice the bug surface. If our target audience is RT-capable HW
   by Phase 3, consider going RT-only at that tier.

3. **Leverage vendor-specific tech when available**. id Software's tight
   NVIDIA integration (SHaRC, SER, OMM, DLSS 4) gives them 2× the
   effective performance of a vanilla implementation. UzEngine should
   integrate NGX SDK and FSR SDK as first-class, not afterthought.

4. **Design for multi-bounce from day one**. id Tech 8 does 2–3 bounces.
   Our pipeline today does 1. Adding bounces via temporal accumulation
   (ReSTIR GI path reuse or SHaRC-style caching) is the key next step.

### 5.1 Concrete action: SHaRC evaluation

We should build a small SHaRC prototype alongside our existing DDGI.
Criteria:
- Does SHaRC produce better GI than DDGI on Sponza?
- What is the memory overhead of the hash grid?
- Does it integrate cleanly with our SDF backend?

If SHaRC outperforms DDGI at similar frame time, pivot to SHaRC as the
distant-gather layer.

---

## 6 · References

- NVIDIA, "How id Software Used Neural Rendering and Path Tracing in DOOM: The Dark Ages" — developer.nvidia.com/blog/how-id-software-used-neural-rendering-and-path-tracing-in-doom-the-dark-ages/
- NVIDIA Geforce News, "DOOM: The Dark Ages Update Out Now, Adding Path Tracing, DLSS 4 With Multi Frame Generation & DLSS Ray Reconstruction" — nvidia.com/en-us/geforce/news/doom-the-dark-ages-path-tracing-dlss-ray-reconstruction-update/
- Digital Foundry, "Creating Doom: The Dark Ages - ray tracing, load times and optimisation in id Tech 8" — digitalfoundry.net/articles/digitalfoundry-2025-creating-doom-the-dark-ages-how-id-tech-8-took-shape
- Reddit r/Games: "Fast as Hell id tech 8 global illumination by Tiago Sousa" (GDC talk reference).
- WCCFTech, "DOOM: The Dark Ages Id Tech 8 Engine Shows Its Muscles".
- NVIDIA SHaRC open source: github.com/NVIDIA-RTX/SHARC.
- NVIDIA RTX Kit: developer.nvidia.com/rtx-kit/
