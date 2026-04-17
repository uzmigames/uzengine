# Subsurface Scattering (SSS) — Real-Time

Skin, wax, marble, leaves, paper. Any material where light enters and
exits the surface at different points. The defining "cheap real-time
win" for character realism.

---

## 1 · The problem

Lambert diffuse assumes light scatters back at the entry point. Real
translucent materials scatter light **through** the surface, exit from a
neighboring point, and carry a color shift (Rayleigh-like) based on the
depth traveled.

Result: a photograph of a hand with the sun behind it reveals that light
leaking through finger webbing is **bright red**, not diffuse pink. This
is the hallmark of subsurface scattering.

Without SSS, characters look like plastic. With it, they look alive.

---

## 2 · Techniques (chronological)

### 2.1 Texture-space diffusion (Oat 2007, d'Eon 2007)

- Render scene into a diffuse-only texture in UV space.
- Apply several Gaussian blurs with different radii and weights
  (matching a diffusion profile).
- Sample the blurred texture in the final shading.

Issues:
- Expensive (UV-space rendering is fill-rate heavy).
- Per-object cost scales with UV map resolution.
- Seam artifacts at UV boundaries.

### 2.2 Screen-Space Subsurface Scattering (Jimenez 2009)

Key insight: convert the diffusion from UV-space to **screen space**.
Instead of blurring the UV-space diffuse, blur the **screen-space
result**. Runs only on visible skin pixels.

- Fast (screen-space, fixed-fill).
- Runs in a single pass per blur direction.
- Works for any skin shape.

Still expensive (~1.5 ms at 1080p in 2009 hardware).

### 2.3 Separable Subsurface Scattering (Jimenez 2015)

**The modern standard.** Approximates the 2D diffusion kernel as a
**separable** 1D kernel. Two 1D blurs (horizontal + vertical) instead of
one 2D.

- Shader-wise: trivial. Just two 1D blur passes on a "diffuse sss" output.
- Cost: 0.5 ms at 1080p on mid-range GPU.
- Quality: indistinguishable from full 2D in double-blind testing
  (Jimenez et al. verified this in the 2015 paper).

This is what every shipping game uses for skin in 2020+.

### 2.4 Burley normalized diffusion (2015)

Disney/Burley's alternative profile. More physically derived. Same
computational structure (can be made separable).

### 2.5 Path-traced SSS (offline, 2020+)

For hero characters in path-traced titles (Crimson Desert at max).
Sample 4–8 sub-scattering positions inside the surface, integrate
radiance along each sub-surface path. Expensive but correct.

Real-time feasibility: 1 subsurface sample per pixel with ReSTIR-style
resampling. Pre-production in Crimson Desert / DLSS-RR integrated
titles.

---

## 3 · Separable SSS — the workhorse

### 3.1 Data flow

```text
G-Buffer lay-down
   ▼
Deferred lighting
   ├── For non-SSS pixels: standard diffuse
   └── For SSS pixels: skin diffuse goes into an HDR buffer (separate channel
                       or 4th G-Buffer color layer)
   ▼
Horizontal 1D blur on the SSS-only channel (weights from SSS profile)
   ▼
Vertical 1D blur on that buffer
   ▼
Composite: final_diffuse = blurred_SSS + nonSSS_diffuse
```

### 3.2 The kernel

Jimenez's human-skin kernel (11-tap, separable):

```
weights = [0.560, 0.203, 0.203, 0.065, 0.065, ... (radii up to ~10 cm equivalent)]
```

Actually the kernel is **RGB-colored**: different blur radii for different
channels (red blurs more, because red light penetrates deeper). This is
what gives skin its characteristic warm shadow hue.

### 3.3 Depth-aware blur

Blur must **not** cross depth discontinuities (a finger against the sky
should not bleed pink into the sky). Use a bilateral filter:
- Weight by depth similarity (reject far-away pixels).
- Weight by normal similarity (reject edges).

### 3.4 Cost

- Horizontal blur: 11 taps × 1920×1080 (only SSS pixels) ≈ 0.2 ms on GTX 1080.
- Vertical blur: same. 0.2 ms.
- Total: 0.5 ms for typical scene with ~5% skin pixels.

---

## 4 · Translucency (thin-sheet)

SSS's cousin for thin materials: leaves, paper, ears, waxed masks.
Light enters one side and exits the other.

### 4.1 Simple model

```hlsl
// Given light direction L and view direction V
float  transmitAmount = saturate(-dot(L, V) + translucencyPower);
float3 transmitColor  = baseColor * translucencyTint;
float3 transmitContribution = lightColor * pow(transmitAmount, translucencySpread);
```

Not physically correct but extremely cheap and perceptually believable.

### 4.2 Thickness

Most implementations include a **thickness map** — a precomputed texture
storing "how thick is the object at this point" (baked from inverted
SDF or ambient-occlusion-style). Used to modulate the transmission
intensity.

### 4.3 Uses in UzEngine's future

- Leaves in foliage (Snowdrop's Pandora tech).
- Character skin (ears, fingers).
- Curtains, tent fabric, fabric characters.
- Wax candles, marble statues.

---

## 5 · Scene-wide integration

In a deferred pipeline, SSS materials need:

1. **Material ID bit** in G-Buffer indicating "this pixel has SSS."
2. **Separate SSS diffuse output** (4th channel or separate MRT target).
3. **Post-lighting screen-space blur pass** on that channel.
4. **Composite into final color**.

For forward or forward+ pipelines, SSS is evaluated per pixel — the
separable blur must be done carefully to avoid seams.

---

## 6 · BlackSpace / Decima / UE5 comparison

| Engine | SSS technique | Where |
|--------|---------------|-------|
| Lumen / UE5 | **Separable SSS** (Jimenez 2015) | Character shader |
| BlackSpace (Crimson Desert) | **Path-traced SSS** at max, Separable fallback | Per-character flag |
| Decima (Death Stranding 2) | **Separable SSS** + hero-only extension | Hero faces |
| Frostbite | **Separable SSS** | Skin / waxy materials |
| RE Engine | **Separable SSS** (Denoiser2022 aware) | Hero characters |

Separable is the ubiquitous choice. Path-traced is emerging for top-tier.

---

## 7 · Implementation plan for UzEngine

### Phase 1 — Separable SSS (1 week)

1. Add material flag `Material::HasSSS`.
2. Extend G-Buffer with a 4th target for SSS diffuse (or repurpose alpha).
3. Horizontal + vertical 1D blur passes with Jimenez's human-skin kernel (pre-tabulated).
4. Depth/normal bilateral stop.
5. Composite into final color.

Test scene: human character under 3-point lighting. Expected: warm color
bleed in shadow transition, softness matching photographic reference.

### Phase 2 — Translucency (thin-sheet) (3 days)

6. Thickness map support (baked during asset import or runtime-AO-derived).
7. Thin-sheet transmission shader block.
8. Wire into deferred lighting.

Test: a leaf or curtain under backlight. Expected: backlit glow with color tint.

### Phase 3 — Path-traced SSS (future, requires RT)

9. Sub-surface sample dispatch in the path tracer.
10. Integration with Ray Reconstruction denoise.

Not a near-term priority; Separable is 95% as good for typical content.

---

## 8 · References

- Jorge Jimenez, "Screen-Space Subsurface Scattering" (2009). iryoku.com/screen-space-subsurface-scattering/
- Jimenez, "Separable Subsurface Scattering" (SIGGRAPH Asia 2015 / I3D 2015 journal). iryoku.com/separable-sss/
- Jimenez et al., "Screen-Space Perceptual Rendering of Human Skin" (ACM TAP 2009). dl.acm.org/doi/10.1145/1609967.1609970
- d'Eon, "Advanced Techniques for Realistic Real-Time Skin Rendering" (GPU Gems 3, Ch. 14). developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-14-advanced-techniques-realistic-real-time-skin
- Burley, "Extending the Disney BRDF to a BSDF with Integrated Subsurface Scattering" (2015).
- therealmjp.github.io/posts/sss-intro/ — MJP's intro.
- Penner & Jimenez, "Pre-Integrated Skin Shading" (GPU Pro 2).
- Jimenez et al., "Real-Time Realistic Skin Translucency" (2010).
