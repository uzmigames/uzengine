# Snowdrop (Massive Entertainment / Ubisoft) — Lighting Analysis

**Shipped titles**: The Division 1 & 2, Avatar: Frontiers of Pandora (2023), Star Wars Outlaws (2024), upcoming The Division Heartland, Splinter Cell: Remake.

**Distinctive identity**: **Photoreal outdoor open-world lighting with dense vegetation and weather**. Snowdrop shipped some of the best-looking outdoor foliage scenes of the past decade (Avatar) and pioneered early production-quality RTGI on open-world (SSRTGI in The Division).

---

## 1 · Lighting pipeline overview

Snowdrop's pipeline has evolved substantially across generations. Recent (Avatar-era, 2023+):

```text
G-Buffer with packed normal + material details
   ▼
Virtual shadow mapping (VSM-like, predates UE5's by years)
   ▼
Deferred lighting:
   - Analytic direct + VSM
   - Dynamic probe grid (custom, cascaded)
   - IBL cubemap (sky-capture)
   ▼
SSR (stochastic) + radiance cache fallback
   ▼
Volumetric clouds + fog (Hillaire-style froxel + stratiform clouds)
   ▼
Custom foliage shading (Tech Division's biggest strength)
   ▼
Sky atmosphere (Bruneton-based with custom Pandora-specific tweaks)
   ▼
TAA + TSR-equivalent
   ▼
Post-process
```

---

## 2 · Distinctive techniques

### 2.1 Early production RTGI (Division 2)

The Division 2 (2019) shipped with one of the first production screen-
space RTGI systems called "SSRTGI" — a hybrid screen-space trace with
probe fallback. Predated Lumen's public release by 2 years.

Architecture:
- Screen-space cone tracing for short-range GI.
- Probe lookup for off-screen fallback.
- Custom denoiser for low-sample signal.

The technique's published by Tomasz Stachowiak at SIGGRAPH 2017 remains
a reference for hybrid GI pipelines.

### 2.2 Vegetation-first material model

Avatar: Frontiers of Pandora has **incredibly dense foliage** with
translucent leaves, volumetric grass, fluorescent plant species. Snowdrop's
material model handles:

- **Translucency per-fragment** (dedicated material channel).
- **Thin-sheet SSS** for leaves.
- **Emissive plants** with contribution to GI.
- **Wind displacement** coupled to shading (shadowing from wind-displaced
  branches is actually correct).

Per the Pandora presentations:
- Grass blades use forward shading with TAA-resilient lighting.
- Tree leaves use modified PBR with translucency tap.
- Bioluminescent plants contribute as emissive lights (via light BVH).

### 2.3 Stratiform + cumulus cloud system

Pandora's cloud system renders multiple cloud layers with different
densities and phases. Implementation:
- **Cumulus (lower)**: 3D ray marched noise, Schneider-style.
- **Stratiform (upper)**: 2D planar warped texture.
- **Light-scattering integration**: each layer contributes to sun
  transmittance separately.

The output is some of the most believable cloud rendering in games.

### 2.4 Virtual-texture everything

Snowdrop has used virtual textures for many years. By 2024 they virtual-
texture:
- Diffuse / normal / roughness (standard).
- Shadow maps (virtual shadows).
- Reflection captures (virtual reflection probes).

Enables vast open worlds with consistent high-frequency detail.

---

## 3 · Comparison matrix

| Axis | Lumen | BlackSpace | **Snowdrop (Avatar-era)** |
|---|---|---|---|
| Shadows | VSM | RT | **VSM-like (shipped earlier)** |
| Diffuse indirect | Lumen dynamic | Per-pixel RT | Hybrid screen-space + probe cascade |
| Foliage | Weak | Good | **Exceptional** |
| Volumetric clouds | Good | Stochastic | **Multi-layer stratiform + cumulus** |
| Open world scale | Good | Good | **Industry leading** |
| HW floor | SM6 | RT-capable | SM6 (RT optional) |

---

## 4 · UzEngine takeaways

1. **Virtual shadows predate UE5**. Our VSM plan (from the AAA default
   pipeline) is well-precedented. Snowdrop, Lumen, Stratus all ship
   similar systems. Confidence is high that this is the right choice.

2. **Foliage is a distinct problem**. Our material system treats foliage
   as generic PBR. For the trees & vegetation use case, we should:
   - Add a translucency channel to the G-Buffer.
   - Use thin-sheet SSS for leaves.
   - Support emissive plants in the light BVH (once we have one).

3. **Hybrid RTGI precedent**. The Division 2's SSRTGI is a reference
   for our own "SSGI + DDGI + RT fallback" approach. The architecture
   has shipped; it works; we can do the same.

4. **Wind-coupled shading**. Trees swaying should also swing their
   shadows. Currently our foliage just rotates geometry; shadow
   contribution stays static. Snowdrop gets this right.

### 4.1 Concrete action: Foliage translucency channel

Add a single material flag `IsFoliage`. When set:
- Allocate 1 bit of G-Buffer material ID.
- In deferred lighting, run a thin-sheet SSS approximation:
  `translucent_contrib = baseColor * saturate(-dot(L, V)) * translucency_scalar`
- Add emissive and translucent contributions before integration.

Low memory cost, big quality win for scenes with any vegetation.

---

## 5 · References

- Ubisoft Massive GDC talks (2018–2024, various).
- Tomasz Stachowiak, "Stochastic Screen Space Reflections" (SIGGRAPH 2015).
- Snowdrop technical blog posts on ubisoft.com engineering/graphics.
- Digital Foundry reviews of Avatar: Frontiers of Pandora (2023) and Star Wars Outlaws (2024).
- The Division 2 SIGGRAPH 2017 talk on SSRTGI.
