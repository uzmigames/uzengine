# PCSS & Contact Shadows

Two cheap, high-impact techniques that close a visible quality gap in any
classical-shadow pipeline. Both are already partially in UzEngine; this
document describes what they should do and what to tune.

---

## 1 · PCSS — Percentage-Closer Soft Shadows

Fernando 2005. The standard "soft shadows from a shadow map" technique.

### 1.1 What it does

Shadows get softer with distance from the occluder. A pole standing 5 m
above the ground casts a soft shadow on the ground (penumbra scales
with distance × light-source size).

### 1.2 Algorithm

Per pixel being shadowed:

1. **Blocker search**: sample the shadow map in a small disk (~16 taps)
   around the projected pixel position. Compute the *average depth* of
   taps that are blockers (closer to the light than the pixel).

2. **Penumbra size**: `penumbra = (pixelDepth - avgBlockerDepth) * lightSize / avgBlockerDepth`

3. **PCF filter**: sample the shadow map with a disk of radius `penumbra`.
   Average the results. That's the shadow value.

### 1.3 Performance

- Blocker search: ~16 taps
- PCF filter: ~32 taps
- Total: ~48 shadow map samples per pixel

At 1080p: ~2 ms on a GTX 1080. At 1440p: ~3 ms on a 3080. Not free, but
cheap for the visual improvement.

### 1.4 Variants

- **Variance Shadow Maps (VSM)** — precomputes moments; cheaper PCF but
  light bleeding.
- **Moment Shadow Maps (MSM)** — higher-order moments, better than VSM.
- **Hardware PCF** — `SAMPLER_STATE_PCF` reduces tap count by 4× via
  fixed-function 2×2 compare.

### 1.5 Tuning for Sponza

Sponza is an excellent test because it has:

- A single strong directional light (the sun).
- Tall pillars casting long shadows.
- A ceiling with many small occluders.

If Sponza's shadows look hard-edged everywhere, penumbra size is too
small (`lightSize` too small). If they look mushy everywhere, penumbra is
too large.

Production values for PCSS:
- `lightSize = 5e-3` (in world units of the shadow map)
- `blockerSearchRadius = 1e-2` (typical)
- Min penumbra = 1.0 pixel, max penumbra = 16 pixels

UE5 uses "Light Source Angle" as the user-facing control (0.5 degrees for
real sun). That converts internally to a PCSS light size.

---

## 2 · Contact shadows (screen-space)

Also called "micro-shadows" or "screen-space shadows".

### 2.1 What they do

Fill the visual gap between where the shadow map says a shadow stops and
where the object actually is. In particular:

- Objects slightly above a surface (fingers near a table, a dropped coin
  on concrete).
- Small occluders that shadow maps miss due to resolution.
- Detail the shadow map could not resolve at its resolution.

### 2.2 Algorithm

Per pixel:

1. Compute ray direction toward the light.
2. March along that ray in screen space (reading the depth buffer).
3. If a sample's depth is in front of the pixel's depth + a small
   threshold, we're occluded.
4. Output 0 (shadow) or 1 (lit) accordingly.

Few samples suffice (~16). Fixed max distance (~0.5 m in world space).

### 2.3 Why it's cheap

- No shadow map access (samples the already-available depth buffer).
- No vertex transforms.
- Exploits spatial coherence for cache hits.

Cost: ~0.2 ms at 1080p.

### 2.4 Combining with PCSS

Contact shadows shadow in the range the shadow map couldn't resolve.
Combine:

```
finalShadow = min(shadowMapPCSS, contactShadow)
```

Or multiplicatively: `finalShadow = shadowMapPCSS * contactShadow`.
Multiplicative is usually subtly wrong physically, but looks fine.

### 2.5 Failure modes

- **Self-shadow acne** — fix with a small depth bias.
- **Missing shadows at screen edges** — ray marches off-screen, no samples.
  Usually OK since the shadow map covers that region.
- **Transparency** — screen-space shadows don't know about alpha-tested
  objects. OK most of the time.

---

## 3 · UzEngine current state

We have:
- `runtime/shaders/runtime/lighting/shadow_pcss.hlsli` — has PCSS code.
- `runtime/shaders/runtime/lighting/contact_shadows.hlsli` — has contact
  shadow code.

Both are wired into the deferred lighting pass (`deferred_lighting_ps.hlsl`).

Likely issues (Sponza symptoms suggest):
- PCSS light size hardcoded or mis-calibrated.
- Contact shadow max distance wrong for the scene scale.
- Combination with local lights not correctly handled (only directional
  uses PCSS today).
- No PCSS for local lights at all (they use plain PCF, which looks
  uniform).

---

## 4 · Recommendations

1. **Expose PCSS parameters as CVars**: `r.Shadow.PCSS.LightSize`,
   `r.Shadow.PCSS.BlockerSearch`, `r.Shadow.PCSS.PenumbraMax`.

2. **Extend PCSS to all lights**, not just directional. Spot and point
   lights should soft-shadow proportional to their "source radius" (we
   already have this in `CLight`).

3. **Tune for Sponza**: set `r.Shadow.PCSS.LightSize = 0.01` (sun angle
   ~1.1 degrees) and verify penumbra is visible but not mushy.

4. **Contact shadows on by default**: with max distance = 0.5 m in world
   units; max steps = 16.

5. **Verify contact-shadow depth bias** for our reversed-Z — there's a
   prior doc on this at `shadows/shadow-depth-z-convention.md`.

---

## 5 · References

- Fernando, "Percentage-Closer Soft Shadows" (ShaderX 3, 2005).
- UE Docs: "Contact Shadows" — dev.epicgames.com/documentation/en-us/unreal-engine/contact-shadows.
- Peters, "Moment Shadow Mapping" (I3D 2015) — VSM alternative.
- Bavoil, "Variance Shadow Maps" (I3D 2006).
- NVIDIA: "Percentage-Closer Soft Shadows" tech report.
- `lighting/shadows/shadow-depth-z-convention.md` (UzEngine) — reversed-Z audit.
