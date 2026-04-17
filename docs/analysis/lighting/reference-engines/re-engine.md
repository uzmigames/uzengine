# RE Engine (Capcom) — Lighting Analysis

**Shipped titles**: Resident Evil 7 (2017), RE2 remake, RE3 remake, RE Village, RE4 remake, Devil May Cry 5, Street Fighter 6, **Monster Hunter Wilds (2024)**.

**Distinctive identity**: **Stable, predictable, artist-friendly lighting**. RE Engine emphasizes lighting that looks the same across scenes, where materials behave consistently, and the image "doesn't turn into mush" in busy moments. More conservative than UE5/BlackSpace but extremely polished and reliable.

---

## 1 · Identity: built for real play, not marketing frames

A common theme in Capcom's public statements and Creative Bloq's
interview: RE Engine is designed for **stability during action**, not
for screenshot-perfect static moments. Monster Hunter Wilds's visual
quality holds up while dozens of creatures fight on-screen; that's the
success metric.

Translating to lighting: the pipeline avoids expensive dynamic techniques
that would flicker or degrade under motion. Bakes are heavy, real-time
elements are carefully chosen, and nothing creates temporal artifacts
under rapid camera changes.

---

## 2 · Lighting pipeline overview

Reverse-engineered from public mods (MHW graphics enhancers), Digital
Foundry reviews, and Capcom's own engine overviews:

```text
G-Buffer (standard deferred)
   ▼
Shadow maps:
   - CSM for sun
   - Local shadow atlas for spot/point
   - Cached static shadow contribution
   ▼
Deferred lighting:
   - Analytic direct with PBR BRDF
   - Baked irradiance (lightmaps for static)
   - DDGI-style dynamic probe grid (newer games)
   - IBL from scene reflection capture
   ▼
Post-effects:
   - SSAO / SSGI (blend with baked GI)
   - Surfel-based RTGI (Monster Hunter Wilds + newer)
   - RT reflections (where enabled)
   - RT AO (where enabled)
   ▼
Volumetric lighting (custom — RE Engine's distinctive look)
   ▼
TAA (Denoiser2022 / Denoiser2022v2 — their named denoiser algorithm)
   ▼
Post: bloom, tonemap, grading
```

---

## 3 · Distinctive techniques

### 3.1 Surfel-based RTGI (Monster Hunter Wilds onward)

Surfels (surface elements) are small discs sampled from the scene's
geometry. RE Engine distributes them across visible surfaces, samples
incoming radiance at each surfel by ray tracing into the scene, and
aggregates them to produce GI.

Similar to probe-based GI but:
- Surfels are **surface-aligned** (not 3D grid).
- Surfel density is **adaptive** (denser near complex geometry).
- Each surfel is a tiny light sample contributing to its vicinity.

Confirmed in graphics mods discussion threads and engine references.
Performance: comparable to DDGI but with better handling of thin
geometry (which is Monster Hunter's gameplay domain: scaly monster
skin, claws, armor details).

### 3.2 Stable lighting philosophy

No visible cascade boundaries. No flickering GI under movement. No
surprise disocclusions. RE Engine's denoiser (Denoiser2022v2) is
notably conservative:
- Longer temporal history windows.
- Tighter variance clamping.
- More aggressive spatial filtering.

Trade-off: some high-frequency detail is lost in motion. For Monster
Hunter's gameplay (fast monster fights) this is the right trade.

### 3.3 Custom volumetric fog

RE Engine's fog has a recognizable "look" distinct from Frostbite-
style froxel fog. Key elements:
- Height-attenuated exponential baseline.
- **Per-area density painters** — artists author 3D fog volumes.
- Strong forward scattering for god rays.
- Heavy use of **fog-as-atmosphere** in RE Village (Castle Dimitrescu
  interior, village square).

### 3.4 Character lighting (hero pass)

Capcom is known for cinematic character lighting in Street Fighter 6
and RE remakes. Implementation includes:
- Separable SSS (Jimenez 2015) for skin.
- Dedicated eye shading (cornea caustic, iris depth).
- **Hair shaded with Marschner approximation** — simpler than UE5's
  strand-based but faster.
- Rim-light calculations in a character-specific pass.

### 3.5 Material system stability

RE Engine's materials are designed so the same material looks right
under many lighting conditions without per-shot tuning. Parameters like
"specular level", "clearcoat", "anisotropy" are tuned for perceptual
consistency across lighting configurations.

Art direction helped: most scenes have carefully constrained lighting
ranges (no extreme HDR), so material responses are bounded.

---

## 4 · Comparison matrix

| Axis | Lumen | BlackSpace | **RE Engine** |
|---|---|---|---|
| Direct lighting | Analytic + VSM | Analytic + RT shadow | Analytic + CSM + RT shadow optional |
| Diffuse indirect | Lumen dynamic | Per-pixel RT | **Baked + surfel RTGI + SSGI** |
| Surfel technique | Not really | N/A | **Signature surfel GI** |
| Philosophy | Dynamic | Physical | **Stable, art-directed** |
| Motion handling | Good w/ TSR | Neural denoise | **Exceptional (wins under action)** |

---

## 5 · UzEngine takeaways

1. **Prioritize temporal stability over peak quality**. Our current SSGI
   can look great in a still frame but flickers under camera motion.
   RE Engine's lesson: tune for motion, not for screenshots. Widen
   temporal windows, clamp more, accept slightly softer details.

2. **Consider surfels as an alternative to DDGI probes**. Surfels
   match geometry better (no probes inside walls), adapt density to
   surface complexity, and don't waste samples on empty space. Research
   step; not a near-term priority.

3. **Hero-pass for characters**. Main-character shading can use a more
   expensive code path (SSS, eye, hair) without affecting the world
   lighting budget. Common in RE Engine / Decima / Naughty Dog engines.

4. **Material consistency discipline**. Our materials should produce
   visually similar results across lighting conditions. Spot-test with
   the same material under 4 lighting presets (noon sun, sunset,
   overcast, nighttime torch). If the material "breaks" in one, tune
   it or update the BRDF code.

### 5.1 Concrete action: Denoiser temporal window widening

Our current SSGI denoiser has a short temporal history (likely 4–8
frames). Widening to 16 frames + tighter variance clamp will
significantly reduce motion flicker at the cost of some high-frequency
detail in motion. Test on Sponza camera sweep to confirm improvement.

### 5.2 Concrete action: Hero-character lighting path

Add a `CCharacterShading` ECS component flag. When a shaded pixel
belongs to a hero character:
- Run separable SSS pass.
- Use Marschner-inspired hair shading (if the hair has a dedicated
  material).
- Add eye caustic.

Keeps world lighting budget unchanged; hero visual quality goes up.

---

## 6 · References

- Wikipedia: RE Engine — en.wikipedia.org/wiki/RE_Engine
- Prolific Studio, "Capcom's Proprietary Engine Made Monster Hunter Wilds Standout" — prolificstudio.co/blog/monster-hunter-wilds/
- Creative Bloq, "How Monster Hunter Wilds was made possible by Capcom's secretive RE Engine" — creativebloq.com/3d/video-game-design/how-monster-hunter-wilds-was-made-possible-by-capcoms-secretive-re-engine
- Nexus Mods, "Wilds Rehydrated ReShade" and "MHWilds Graphics Enhancement" — reveal internal CVar names for surfel GI, denoiser selection.
- Capcom GDC talks: "GPU Work Distribution in RE Engine" (2018, 2020).
- Digital Foundry Monster Hunter Wilds PC review (2024).
