# Crimson Desert · Character Lighting

Hero characters in Crimson Desert's cutscenes and close gameplay
sequences are among the most impressive in any shipped 2025–2026 title.
This document analyzes the character-specific lighting pipeline
deduced from public material, community reverse-engineering, and the
known composition of the BlackSpace engine.

---

## 1 · What's impressive

From trailers, CES 2026 demo, and gameplay footage:

- **Skin looks alive** — warm subsurface color bleeding visibly on shadowed edges of faces and hands.
- **Eyes sparkle** — correct corneal caustic, iris depth, wetness.
- **Hair follows light** — backlit scenes produce correct warm-tint hair backlight (characteristic of Marschner's TT lobe).
- **Cloth fabric responds to area lights** — robes and capes under interior lighting show the stretched highlight of a rectangle source, not a uniform cone.
- **Night scenes are not flat** — torchlight produces skin subsurface glow on faces held close.

---

## 2 · Pipeline additions for characters

On top of the standard per-pixel RTGI pipeline (see [00-blackspace-engine.md](./00-blackspace-engine.md)), characters add:

```text
Standard pixel shading:
  path-traced GI + ReSTIR DI + ReSTIR GI + RR denoise
   ▼
If CMaterial::HasSSS:
   ▼
Separable SSS (Jimenez 2015, likely path-traced at max tier)
   - Horizontal 1D blur (depth-aware)
   - Vertical 1D blur
   - Warm-biased per-channel radius
   ▼
If CMaterial::HasTranslucency (ears, thin cloth):
   ▼
Thin-sheet transmission (backlight evaluation)
   ▼
If CMaterial::IsEye:
   ▼
Eye-specific caustic pass
   - Cornea refraction
   - Iris depth parallax
   - Sclera SSS (slight)
   ▼
If CMaterial::IsHair (cards):
   ▼
Hair BSDF (Marschner R + TT + TRT)
   - Card-sorted depth blend
   - Anisotropic GGX-like specular aligned to strand tangent
   - Possible dual-scattering approximation for multi-bounce
```

---

## 3 · Skin

### 3.1 Separable SSS with RGB-colored radius

Production skin shaders use a separable 1D blur kernel with per-channel
radius:
- Red: ~5 cm effective radius (penetrates deep — why shadow transitions go red).
- Green: ~2 cm.
- Blue: ~0.5 cm.

Jimenez's published profile is approximately:
- R: σ = 0.55 mm (diffuse profile 1/e)
- G: σ = 0.27 mm
- B: σ = 0.13 mm

But used at a larger "effective" scale (screen-space radius ~5 pixels)
once projected.

### 3.2 Hero-pass path tracing

At max tier, skin shading becomes a **proper volumetric integration**:

1. Sample a sub-surface position by perturbing the entry point
   along a Gaussian profile.
2. Evaluate incoming light at that sub-surface point via an RT shadow
   ray.
3. Integrate along the sub-surface path using Henyey-Greenstein phase
   function (g ≈ 0.9 for skin).
4. Reconstruct outgoing radiance at the visible pixel.

One sub-surface sample per pixel per frame, denoised via RR. Only
enabled for hero characters in cutscenes and close-up gameplay to keep
budget manageable.

### 3.3 Thickness-map fallback

For non-hero characters or low-fidelity modes, a **thickness map**
(baked per mesh as inverse AO) modulates a simple thin-sheet
transmission formula:
```
thin_sheet = baseColor × max(0, -dot(L, V)) × pow(thickness, fallOff) × intensity
```
Cheap, convincing, used ubiquitously.

---

## 4 · Eyes

Crimson Desert's eyes benefit from these pieces:

### 4.1 Cornea shell

The eye has a thin **cornea shell** modeled with parallax offset:
- Incoming light refracts through cornea (n = 1.376).
- Refraction traces into the iris below.
- Iris is drawn with parallax depth offset matching the refraction
  displacement.

### 4.2 Sclera SSS

The white of the eye (sclera) has subtle SSS (reuses the skin pipe
with a different profile — less red, more white).

### 4.3 Caustic highlight

A small circular caustic appears on the iris when a light source is at
the right angle. Implemented as a precomputed caustic map, applied
billboard-aligned to light direction.

### 4.4 Wetness

A slight specular roughness reduction along the lower eyelid. Art-
directed, not derived from physics.

---

## 5 · Hair

From trailers: Crimson Desert hair is **card-based**, not strand-based.
User reports (Reddit, Steam community) suggest hair is one of the weaker
rendering aspects — jagged edges at distance, shimmer in motion. This
matches card-based hair with insufficient AA.

Likely BSDF: 2-lobe Marschner (R + TT). See
[../modern-techniques/hair-rendering.md](../modern-techniques/hair-rendering.md)
§4.2 for the approach.

Areas where Crimson Desert's hair excels:
- Physics (real collision, reacts to wind and horse movement).
- Backlight TT lobe visible when sun is behind character.

Areas where it falls short:
- Aliasing/shimmer (card-based needs careful TAA or supersampling).
- Strand-level detail lost at distance.

---

## 6 · Cloth & fabric

Hero cloth (robes, capes) uses:

1. **Anisotropic specular** along weave direction (via LTC).
2. **Sheen/fuzz lobe** (silk, velvet) — ASHIKHMIN-inspired.
3. **SSS for translucent fabric** (curtains, backlit cloth).

Pre-computed weave normal maps drive the anisotropy. The result is that
a silk cape under a rectangle window light shows a long stretched
highlight that slides as the wearer moves — the signature of area light
shading on anisotropic fabric.

---

## 7 · Lighting rig for hero scenes

Cutscene lighting in Crimson Desert appears to use:

- **Key light**: area light (rectangle or disc) for main illumination.
- **Fill light**: sky + bounce dominant, ~30% of key intensity.
- **Rim light**: dedicated rim-light component (via screen-space
  direction-based eval).
- **Practical lights**: torches, campfires as emissive point / area sources.

This is standard cinematic lighting translated to real-time via area
lights and dedicated rim shaders. UE5's "Light Card" component is similar.

---

## 8 · UzEngine takeaways

1. **Separable SSS is the minimum** for any character rendering. It's
   cheap, established, and delivers skin-that-looks-alive.

2. **Dedicated hero-path for main characters**. Full SSS + eye shading
   + better hair + anisotropic cloth. Gate by a character-level flag.

3. **Area lights are mandatory** for interior cinematic lighting. Point
   lights simply cannot produce the stretched highlights and correct
   soft shadows. See [../modern-techniques/area-lights-ltc.md](../modern-techniques/area-lights-ltc.md).

4. **Plan for card-based hair first**. Strand-based is aspirational.
   Cards + 2-lobe Marschner is industry-standard.

5. **Cloth anisotropy via anisotropic-LTC**. Adds visible quality to
   any scene with fabric. Requires the extended LTC tables (Heitz/Hill 2022).

### 8.1 Concrete sequence

Phase order:
1. Area lights (LTC) → any interior scene improves.
2. Separable SSS → any character visible up close.
3. Anisotropic materials → cloth & brushed metal.
4. Card hair → hero character quality.
5. Eye shader → close-up cutscene quality.
6. Path-traced SSS → top-tier only.

Each phase is 1–3 weeks. Total character-lighting upgrade: ~2–3 months.

---

## 9 · References

- See [../modern-techniques/subsurface-scattering.md](../modern-techniques/subsurface-scattering.md) and [../modern-techniques/hair-rendering.md](../modern-techniques/hair-rendering.md) for the foundational technique references.
- NVIDIA GPU Gems 3, Ch. 14, "Advanced Techniques for Realistic Real-Time Skin Rendering".
- Nadir Saghar, "Real-Time Eye Rendering with Parallax and SSS" (2019).
- Naughty Dog, "Lighting Hero Characters in The Last of Us Part II" (SIGGRAPH 2020).
- Crimson Desert Pearl Abyss dev notices — character animation + physics focus.
- Nexus Mods "Ultra Realistic Hair Physics at Crimson Desert" — community evidence of hair system configurability.
