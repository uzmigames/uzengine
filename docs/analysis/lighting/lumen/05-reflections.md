# Lumen · Reflections

Lumen Reflections is a distinct subsystem from Lumen GI but shares the
tracing infrastructure. It handles **glossy and rough reflections** on all
surfaces, replacing UE4's hybrid SSR + planar + probe system.

---

## 1 · Goals

- One unified reflection technique for all roughnesses, no visible transition.
- Accurate at grazing angles (where SSR falls apart).
- Accurate for objects off-screen (where SSR misses).
- Cheap enough to run full-screen at 1080p on a 2070S.

---

## 2 · Pipeline

```text
Per pixel:
  roughness > 0.6 → sample radiance cache (world probes) only, skip ray tracing
  roughness < 0.6 → trace reflection ray

For ray-traced pixels:
  1. Compute reflection direction using GGX VNDF stochastic sampling
  2. Screen-space ray march first (~8 steps, Hi-Z accelerated)
  3. If screen hit found → sample current-frame scene color at the hit
  4. If screen miss or off-screen → world-space trace (SDF or HWRT) → sample surface cache at hit
  5. If still miss → sample radiance cache (world probes) in the ray direction
  6. Else → sky LUT sample
  Output: noisy reflection at 1 spp

Denoise:
  Temporal filter with motion vectors (reproject prev reflection via parallax-corrected reprojection)
  Spatial bilateral with roughness-weighted kernel
```

---

## 3 · Stochastic GGX sampling

The reflection direction is **not** `reflect(V, N)` — that's the mirror
direction. For glossy surfaces, the BRDF lobe is a cone around the mirror
direction. Lumen samples one direction from this cone per pixel using
**GGX VNDF** (Visible Normal Distribution Function, Heitz 2018):

```hlsl
// Get a random H vector from the GGX VNDF lobe
float2 xi = Hammersley(frameIdx, pixelIdx);
float3 H  = SampleGGXVNDF(xi, viewDir, roughness);
float3 L  = reflect(-viewDir, H);
```

One sample per pixel is noisy; multiple frames of temporal accumulation
plus spatial denoise clean it up.

### 3.1 Per-pixel blue noise

Lumen uses a 128×128 blue noise texture (Wolfram / Heitz) for `xi`.
Temporally rotated every frame. This gives the signal its noise-floor
spectrum matching neighborhood-clamp TAA expectations.

---

## 4 · Hybrid trace (screen-space first)

Screen-space SSR is **free** (all data is on screen already). Lumen uses
it opportunistically:

1. Compute the reflection ray in screen space.
2. March through the Hi-Z depth buffer for ~8 steps.
3. If hit, sample the current frame's HDR buffer color at the hit UV.
4. Clamp by neighborhood or reject if hit looks wrong (normal mismatch).

If SSR misses (off-screen, behind geometry), fall back to **world-space
tracing**. This is the "SSR first, RT fallback" pattern used by almost
every modern AAA engine.

---

## 5 · Surface cache at RT hit

When the ray hits world-space geometry (via SDF or HWRT), the shader
samples the **surface cache** at the hit location. This means reflections
see **correctly lit geometry**, including dynamic lights and GI.

Compared to SSR (which samples the already-tonemapped current-frame
color), surface-cache sampling is physically correct — reflections reflect
the same scene the rest of the lighting sees, not the post-tonemap picture.

---

## 6 · Radiance cache for rough reflections

For rough surfaces (r > 0.6), the reflection lobe is wide. Instead of
tracing a ray (which would be noisy in such a wide lobe), Lumen samples
the **radiance cache's octahedral** in the reflection direction. Fast,
noise-free.

The transition from ray-traced to radiance-cache-sampled is smooth: at
r ≈ 0.6, Lumen blends between the two.

---

## 7 · Denoiser

Reflections denoiser is separate from GI denoiser. It is **stronger** on
reflections because:

- Reflections have a narrower hemisphere (lobe) than diffuse GI.
- Depth-of-field-like blur matches what a wider roughness implies.
- Hit distance is used to size the spatial kernel.

Pipeline:

1. **Temporal**: reproject previous-frame reflection via **parallax-corrected
   motion vectors** (the reflection moves differently from the surface
   underneath it).
2. **Spatial**: bilateral kernel in the GGX lobe direction, size
   proportional to roughness.

Parallax correction is non-trivial — see the Lumen docs §"Reflection
Temporal Filter" and Guillaume Boissé (Ubisoft) SSSR blog posts.

---

## 8 · Comparison with classical SSR

| Attribute | Classical SSR | Lumen Reflections |
|---|---|---|
| Source | Screen color | Surface cache (lit world) |
| Off-screen handling | Black / skybox | Radiance cache fallback |
| Grazing angles | Fails | Works (RT fallback) |
| Transparency | Single layer | Single layer (same limit) |
| Temporal stability | Poor (TAA-dependent) | Strong (parallax reproj) |
| Cost at 1440p | 1.0 ms | 2.5 ms (HWRT) / 3.0 ms (SW) |

Lumen is ~3× more expensive but considerably more correct.

---

## 9 · 5.6 improvements

From the web search results:

- Radiance cache reworked to **sample sky directly** for reflections when
  the ray hits sky, giving higher quality sky reflections (vs a blurry
  radiance-cache sky sample).
- Better controls for **where** radiance cache is applied (allowing artists
  to disable it on specific materials where they want pure RT).

---

## 10 · UzEngine relevance

Our current SSR is in `reflections/ssr/` (analysis docs). Implementation is
in `runtime/engine/renderer/postprocess/` or similar. It is classical
screen-space marching — no world-space fallback.

Migration path to Lumen-style:

1. Keep classical SSR as the first-trace pass.
2. Add a second pass that runs where SSR missed, tracing via our SDF.
3. Sample a radiance cache (once we have one) at the hit.
4. Add roughness-gated switch to radiance-cache-only for r > 0.6.
5. Upgrade denoiser to parallax-corrected temporal + spatial.

Each step independently improves quality. Full rewrite not needed.

See [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md) §7 and existing analysis in `docs/analysis/reflections/`.

---

## 11 · References

- Epic docs §"Lumen Reflections".
- Narkowicz GDC 2022 — §"Lumen Reflections".
- Heitz, "Sampling the GGX Distribution of Visible Normals" (JCGT 2018).
- Wolfram blue-noise texture set (GitHub).
- Guillaume Boissé, "Stochastic Screen-Space Reflections" (SIGGRAPH 2015 + 2019 updates).
- `reflections/ssr/` (this repo) for UzEngine's current SSR analysis.
