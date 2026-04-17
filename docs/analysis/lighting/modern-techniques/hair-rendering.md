# Hair Rendering — Marschner Model & Dual Scattering

Hair is one of the last "visibly fake" elements in most real-time
games. Lambert + anisotropic specular produces bad hair; proper hair
shading requires modeling the actual light-fiber interaction.

---

## 1 · Why hair is special

A hair fiber is a tiny translucent cylinder with a cuticle (outer
layer), cortex (middle), and (for some hair) medulla (inner). Light can:

- **Reflect** off the cuticle (primary specular "R").
- **Transmit** through twice (TT): enter → exit → enter the next fiber.
- **Transmit-Reflect-Transmit** (TRT): one internal reflection.
- **Transmit-R-T** and higher orders (diminishing).

Traditional Lambert + anisotropic specular produces only the first
(R), missing the defining hair look of colored backlighting (TT) and
the secondary highlight from TRT.

---

## 2 · The Marschner model (2003)

Steve Marschner et al. published the canonical model:

### 2.1 Three specular lobes

**R (Primary reflection)**:
- Cuticle surface reflection.
- Gives the "highlight you see in shampoo ads" — high on the surface.
- Cool-toned (whitish).

**TT (Transmit-transmit)**:
- Light enters and exits the fiber (effectively bending around it).
- Warm color (absorbed through the melanin-containing cortex).
- Visible as backlight glow.

**TRT (Transmit-reflect-transmit)**:
- One internal reflection.
- Secondary highlight offset from R by a few degrees in the lobe direction.
- Dark/colored.

### 2.2 Azimuthal and longitudinal angles

Each lobe is a function of:
- **Longitudinal angle θh** (along the hair strand axis) — Gaussian.
- **Azimuthal angle φh** (around the strand) — lobe-dependent function.

Typical implementation uses precomputed LUTs for the azimuthal term
(since it's not closed-form).

---

## 3 · Dual Scattering (Zinke 2006)

Marschner gives single-fiber scattering. Real hair has thousands of
fibers close together — light bouncing between them produces
**multiple scattering** which dominates the "softness" of hair (the
reason hair appears fluffy, not sharp).

Zinke et al.'s dual scattering approximation splits multiple
scattering into:

- **Global scattering**: light bouncing many times between fibers.
  Approximated as a Gaussian sphere around the hair bulk.
- **Local scattering**: lower-order bounces between nearby fibers.
  Approximated via a constant local term.

This is the technique that makes hair look like hair, not like
plastic tentacles.

---

## 4 · Real-time implementations

### 4.1 UE5 hair strand

Full Marschner + dual scattering approximation on strand-based hair.
Performance:
- ~2 ms for a fully-haired character at 1440p on RTX 3080.
- Strand sorting (depth-ordered) required for correct blending.
- Custom geometry (hair is its own pipeline, not mesh-based).

### 4.2 Frostbite hair

Simpler card-based approach (hair is painted onto planes). Uses a
Marschner-inspired 2-lobe shader:
- R lobe (primary specular).
- TT lobe (backlight transmission).

No TRT. Loses the secondary highlight but runs 3× faster. Trade-off for
MMO/crowd scenarios.

### 4.3 Dell'Olio / NVIDIA RTX Kit (2025)

NVIDIA's Disjoint Orthogonal Triangle Strips (DOTS) + Marschner-inspired
BSDF for real-time ray-traced hair. Tier-3 quality.

---

## 5 · Simplified shader (UE5-style card-based)

```hlsl
float3 HairShade(float3 V, float3 L, float3 T,  // T = strand tangent
                 float3 baseColor, float roughness, float absorption)
{
    // Project V, L onto the plane perpendicular to T
    float3 Vp = V - T * dot(V, T);
    float3 Lp = L - T * dot(L, T);
    Vp = normalize(Vp);
    Lp = normalize(Lp);

    float cosΘh_V = dot(V, T);  // longitudinal
    float cosΘh_L = dot(L, T);

    // R lobe — primary highlight
    float3 R = /* Gaussian around Θh_V = -Θh_L, tinted cool */
    // TT lobe — backlight transmission
    float3 TT = /* Gaussian around Θh_V = Θh_L, tinted by absorption (warm) */
    // TRT lobe — secondary highlight
    float3 TRT = /* Gaussian offset by few degrees from R */

    return R + TT + TRT;
}
```

Details vary by implementation. The actual R/TT/TRT evaluation
involves precomputed LUTs for the azimuthal factors.

---

## 6 · Adoption

Engines shipping strand-based hair + Marschner:
- **UE5** — full strand hair with Marschner + dual scattering (since 4.26).
- **Dogma Engine** (SE Dawn Dogma) — similar strand-based approach.
- **Frostbite** — card-based with 2-lobe approximation.
- **Decima (Death Stranding 2)** — hybrid: strand for heroes, card for NPCs.
- **Crimson Desert (BlackSpace)** — card-based currently, strand-based plans per dev updates; hair is one of CD's weaker rendering aspects per user reports.

Engines using Kajiya-Kay or simpler models (ubiquitous until ~2020):
everyone else.

---

## 7 · UzEngine current state

We do not have hair. Our characters are placeholder meshes. Planning
hair is appropriate as the character pipeline matures.

When we do implement it, the right tier for our scope is:
- **Card-based hair** (painted cards, not strands) — simpler geometry.
- **2-lobe Marschner approximation** (R + TT, skip TRT).
- **No dual scattering** initially (just single scattering).

This gets ~80% of the perceived hair quality at ~20% of the
implementation cost of full strand-based hair.

---

## 8 · Implementation plan (when we're ready)

### Phase 1 — Card-based 2-lobe Marschner (3 weeks)

1. Card-hair asset pipeline (baked strand-density, tangent maps).
2. New material model: `MaterialID::Hair`.
3. Hair-specific G-Buffer layout (tangent in place of normal variation).
4. R + TT evaluation shader.
5. Strand-aware depth sorting (easy for cards).

### Phase 2 — TRT + dual scattering (1 month)

6. TRT evaluation.
7. Dual scattering approximation for multi-fiber.

### Phase 3 — Strand-based hair (future)

When we need hero-character quality. Major investment.

---

## 9 · References

- Stephen R. Marschner, Henrik Wann Jensen, Mike Cammarano, Steve Worley, Pat Hanrahan, "Light Scattering from Human Hair Fibers" (SIGGRAPH 2003).
- Matt Pharr, "The Implementation of a Hair Scattering Model" (pbrt reference). pbrt.org/hair.pdf
- Zinke, Yuksel, Weber, Keyser, "Dual Scattering Approximation for Fast Multiple Scattering in Hair" (SIGGRAPH 2008).
- Pixar, "A Data-Driven Light Scattering Model for Hair" (SIGGRAPH 2010). graphics.pixar.com/library/DataDrivenHairScattering/paper.pdf
- Zhong Ren, Kun Zhou, Toshiya Hachisuka, "Interactive Hair Rendering Under Environment Lighting" — kunzhou.net/2010/hair-rendering.pdf
- "Study Notes On The Marschner Hair Shading Model" — gaukler.github.io/2023/10/21/study-notes-on-the-marschner-hair-shading-model.html (accessible intuition).
- NVIDIA RTX Kit documentation for DOTS hair: developer.nvidia.com/rtx-kit/
- Real-time hair rendering blog series: hairrendering.wordpress.com/tag/marschner/
