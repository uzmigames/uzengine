# Reflection Probe Placement Guide

<!-- doc-meta
title: Reflection Probe Placement Guide
category: guide
theme: rendering
created: 2026-03-24
updated: 2026-03-24
status: active
author: docs-operator
-->

## Overview

Reflection probes capture local cubemap reflections at scene positions and blend with global IBL. This guide covers practical placement strategies, parameter tuning, and performance optimization for artists and level designers.

## Quick Start

1. **Add probe to scene** — Attach `ReflectionProbeComponent` to a node at a representative position (center of a room, above a courtyard, near a reflective object)
2. **Set `influenceRadius`** — Typically 300–2000 cm depending on area size; defines where reflections fade out
3. **Set `blendDistance`** — Use ~25% of `influenceRadius` for smooth transitions (75–500 cm)
4. **Set `resolution`** — Default 128 (good quality/cost); use 64 for distant/cheap areas, 256 for hero spaces
5. **Mark `needsCapture = true`** — System captures automatically (one probe per frame amortized)
6. **Test in-game** — Verify reflections blend smoothly at probe boundaries

## Placement Principles

### Spacing & Coverage

**Goal:** Probes should overlap (blendDistance > 0) for seamless transitions between regions.

**Rule of thumb:**
- Place one probe per **distinct environment** — indoor room, outdoor courtyard, cave chamber, etc.
- Probes should be spaced approximately `1.5 × influenceRadius` apart for smooth blending
- Position probe at **centroid of the region** it represents (e.g., center of a room, not in a corner)

**Example — 3-Room Layout:**
```
Room A (10m × 8m)  Room B (12m × 10m)  Room C (8m × 8m)
   Probe A              Probe B              Probe C
   influenceRadius:     influenceRadius:     influenceRadius:
   400 cm               500 cm               350 cm
   (covers room + 1m)   (covers room + 1m)   (covers room + 0.5m)
```

In overlapping doorway regions, both probes contribute (blended by distance). Player moves from A→B: Probe A fades, Probe B brightens smoothly over `blendDistance`.

### influenceRadius — Size of Reflection Area

Controls how far reflections extend from the probe position. **Unit: centimeters (cm).**

| Value | Use Case | Example |
|-------|----------|---------|
| **300–500** | Small rooms, tight indoor spaces | 4×4m closet, small office |
| **800–1200** | Medium rooms, standard indoor areas | 10×8m bedroom, 12×10m conference room |
| **1500–2000** | Large open areas, courtyards | 30m×30m outdoor plaza, grand hall |
| **2500+** | Huge open worlds, distant background | 50m×50m mountain plateau, vast desert |

**Distance-based fade:**
- At distance < `influenceRadius - blendDistance`: Full influence (alpha = 1.0)
- At distance `influenceRadius - blendDistance` to `influenceRadius`: Smooth fade (Hermite interpolation)
- At distance > `influenceRadius`: Zero influence (alpha = 0.0, use global IBL only)

**How to set:**
1. Estimate the longest distance from probe center to geometry you want to reflect
2. Add ~10% margin
3. Set `influenceRadius` to that value

### blendDistance — Transition Softness

Controls how smoothly probes blend at boundaries. **Unit: centimeters (cm).**

| Value | Effect | Cost |
|---|---|---|
| **50–100** | Abrupt transition, visible seams | Cheaper (sharp falloff) |
| **200–500** | Smooth, invisible transition | Slightly more expensive (longer fade zone) |
| **> 500** | Very soft, gradual blending | More expensive (wider overlap = more probes sampled) |

**Rule:** Set `blendDistance ≈ 0.2–0.25 × influenceRadius` for invisible transitions.

**Examples:**
- `influenceRadius = 400, blendDistance = 100` — Tight transition in cramped space
- `influenceRadius = 800, blendDistance = 200` — Balanced for medium rooms
- `influenceRadius = 1500, blendDistance = 500` — Soft transition for large open areas

## Resolution & Quality Tradeoffs

**`resolution` parameter — Cubemap face width/height.**

| Resolution | Quality | Capture Cost | Storage/Probe | Use Case |
|---|---|---|---|---|
| **64** | Low detail, blurry reflections | ~1ms per probe | ~130 KB | Distant areas, fog-heavy scenes, performance-critical |
| **128** | **Standard quality** | ~4ms per probe | ~520 KB | **Most scenes — default** |
| **256** | High detail, sharp reflections | ~16ms per probe | ~2 MB | Hero shots, showcase areas, close-up reflections |

**GPU Cost Per Frame (amortized over 8 probes):**
- 64px: ~0.125ms (negligible)
- 128px: ~0.5ms (small)
- 256px: ~2ms (noticeable)

**VRAM Cost (per probe):**
- 64px: ~130 KB
- 128px: ~520 KB
- 256px: ~2 MB

With 8 active probes at 128px: ~4.2 MB total (negligible vs. scene textures). At 256px: ~16 MB (acceptable for mid-range hardware).

**How to choose:**
- **Default (128):** Use everywhere unless performance is a concern
- **Reduce to 64 if:**
  - Scene has 10+ probes competing for capture budget
  - Target platform is low-end (mobile, older GPUs)
  - Area is outdoors/foggy (details get obscured anyway)
- **Increase to 256 if:**
  - Area has close-range polished reflections (mirror, water, chrome)
  - Hero/showcase space (intro cutscene, portfolio shot)
  - GPU time is available (< 60% utilization)

## Priority & Overlap Handling

**`priority` parameter — Resolution tiebreaker in overlapping regions.**

When probes overlap at a pixel, the **highest priority is blended first**: it takes its fade weight, and lower-priority probes (then the sky IBL) fill only what it leaves (the under operator). Inside a higher-priority probe's full-weight zone, lower-priority probes do not show.

| Priority | Purpose |
|---|---|
| **0 (default)** | Standard probe (no special treatment) |
| **1–9** | Elevate a specific probe (e.g., important room, landmark) |

**Use case — Multi-Zone Level:**
```
Outdoor courtyard      (priority = 0)
├─ Church interior     (priority = 5) ← overrides courtyard in sanctuary
└─ Crypt below church  (priority = 3) ← softer override

Result:
- Inside church: Church probe dominates (5 > 0)
- Inside crypt: Crypt probe dominates (3 > 0)
- Outer courtyard: Courtyard probe (0)
- Church vestibule (overlap): Smooth blend toward church (higher priority)
```

**How to set:**
1. Identify "important" or "iconic" spaces (player spends time here, reflections are visible)
2. Give those probes `priority = 5–9`
3. Give "background" or "distant" probes `priority = 0–2`
4. In overlaps, the higher priority gradually dominates as distance decreases

## Scene Setup Best Practices

### Probe Placement Checklist

- [ ] **One probe per distinct environment** — Room, courtyard, cave, etc.
- [ ] **Positioned at centroid** of the region (not corners, not near walls)
- [ ] **influenceRadius covers the region + margins** — Check overlap with neighboring probes
- [ ] **blendDistance set to 20–25% of radius** — Smooth transitions without overdoing cost
- [ ] **resolution appropriate for quality needs** — 128 default, 64 if budget-tight, 256 for hero areas
- [ ] **priority assigned to important spaces** — Ensure iconic areas dominate their overlaps
- [ ] **enabled = true** — Activate probes you're using (can toggle for debugging)
- [ ] **needsCapture = true** — Set initially; system clears after capture
- [ ] **All probes captured before gameplay** — Load asynchronously or during menu transitions

### Static vs. Dynamic Scenes

#### Static Geometry (Most Scenes)

Probes are captured **once at load time** and never updated:

```cpp
// At scene load
for (auto& probe : probes) {
    probe.needsCapture = true;  // Mark all for capture
}
// System amortizes: 1 probe captured per frame until all complete

// During gameplay
// probes never re-capture (unless artist explicitly marks needsCapture)
// → Zero per-frame cost beyond deferred lighting blend
```

**Performance:** Deferred blend only (measured: +0.01–0.02 ms on the deferred lighting pass for 2 probes at 1280×720 on an RTX 5080). No capture cost.

#### Dynamic Geometry (Moving Objects, Destructible Structures)

If geometry moves (destruction, doors opening, etc.), affected probes need re-capture:

```cpp
// When destruction event happens
probes[nearbyProbeId].needsCapture = true;

// System re-captures next frame (or next available slot if backlogged)
// Other probes continue normally; one-per-frame amortization continues
```

**Performance:** Spike during re-capture (~4–16ms depending on resolution), then back to baseline.

**Recommendation:** Avoid frequent dynamic updates. Re-capture only on **major geometry changes** (destruction, doors slamming). Minor dynamic objects (NPCs, small props) don't need probe updates — use global IBL + direct lighting instead.

### Level Designer Workflow

1. **Block out level** with geometry (walls, terrain, major props)
2. **Place one probe per major space** — Estimate room size, set `influenceRadius` to 1.5× longest wall
3. **Test with default parameters** — 128px resolution, blendDistance = 0.25 × influenceRadius
4. **Walk the level** — Check for:
   - Abrupt reflections at probe boundaries (increase `blendDistance`)
   - "Dark spots" (probes not covering that area — add new probe)
   - "Washed out" reflections (reduce `priority` of competing probes, or increase resolution)
5. **Fine-tune** — Adjust radius/distance based on visual feedback
6. **Bake offline** — Before final export, have system pre-capture all probes
7. **Document** — List probe positions + settings in level design doc (for future artists maintaining the level)

## Optimization Strategies

### Frame Budget Management

**System amortizes probe captures:** 1 probe per frame, up to 8 active probes.

| Scenario | Probe Count | Capture Interval | Frame Impact |
|---|---|---|---|
| **Static level** | 8 | Load time (8 frames total) | None (post-load) |
| **Frequent updates** | 8 | Every 10 frames (one flagged/frame) | ~4–16ms every 10 frames (~0.4–1.6ms average) |
| **Many probes** | 20 | Every 20 frames (one flagged/frame) | ~4–16ms every 20 frames (~0.2–0.8ms average) |

If capture cost is excessive:
- Reduce `resolution` to 64 (4× faster capture)
- Capture during loading screen / menu (not in-game)
- Reduce frequency of re-capture (only on major events)

### VRAM Budget Management

8 probes at 128px ≈ 4.2 MB (negligible).
8 probes at 256px ≈ 16.8 MB (noticeable on memory-constrained platforms).

**Strategy:**
- **Mid/High-end (PC, console):** Use 128–256px freely
- **Low-end (mobile, old GPU):** Limit to 64px or reduce active probe count to 4

### Fallback: Global IBL Only

If reflection probes are disabled or unavailable:
- Deferred lighting falls back to **global environment map** (global IBL)
- Reflections are less localized but still plausible
- **Zero per-frame cost** (global IBL sampled anyway)

Use this for very low-end platforms or as a fallback quality setting.

## Troubleshooting

### Problem: Sharp seams between probe regions

**Cause:** `blendDistance` too small or probes poorly positioned.

**Fix:**
1. Increase `blendDistance` (target: 20–25% of `influenceRadius`)
2. Reposition probes closer to centroid of region
3. Verify overlapping regions have similar `influenceRadius` values

### Problem: "Dark spot" — probe not reflecting anything

**Cause:** `influenceRadius` too small; some geometry is outside probe's influence zone.

**Fix:**
1. Increase `influenceRadius` (check geometry distance from probe center)
2. Add additional probe closer to that geometry
3. Verify probe has `enabled = true` and `needsCapture = true`

### Problem: Reflections change suddenly when camera moves

**Cause:** Only one probe active in a region; no blending.

**Fix:**
1. Add secondary probe to create overlap
2. Increase `blendDistance` for softer transitions
3. Check that both probes have `enabled = true`

### Problem: Reflections don't update when geometry moves

**Cause:** `needsCapture` not set; probe using stale baked cubemap.

**Fix:**
1. Manually set `needsCapture = true` after geometry change
2. Verify system is capturing (check GPU utilization; expect 4–16ms spike for one frame)
3. If capture doesn't happen, check that probe is within an active region and `enabled = true`

### Problem: Performance drop when moving through level

**Cause:** Cascading probe captures (multiple probes flagged `needsCapture` simultaneously).

**Fix:**
1. Stagger probe capture — don't flag multiple probes in same frame
2. Pre-capture all probes during loading screen (set `needsCapture = true` at load time; system auto-captures over 8 frames before gameplay starts)
3. Reduce `resolution` for non-critical probes (outdoor = 64, indoor = 128)

### Problem: No probe reflections at all

**Cause:** reflection probes are opt-in. `r.ReflectionProbes` defaults to 0, under which no probe is captured, baked or sampled, whatever the scene authors.

**Fix:** set `r.ReflectionProbes` to 1 in the project's `cvars` (the shipped `reflection-probes.uzproject` does) or at the console.

### Problem: Reflections inside a room are far too bright, or black

**Cause (fixed 2026-11-19):** the capture used to light the scene with the sun's N·L only (no shadows, no ambient). It now renders a sun shadow map of its own and lights the geometry as the main pass does (sun + shadows, sky light, emissive); the reflected walls measure 1.02–1.07 × the frame's lighting of the same points. What a capture still does not carry: local lights, GI, SSR, contact and cloud shadows. See [17-lighting.md — Limitations](../engine/12-rendering/17-lighting.md#limitations).

**Fix:** if a room looks too bright or dark in a reflection, check it is not lit mainly by local lights or GI, which the capture does not include; and re-capture after the sky light changes (`needsCapture`).

### Problem: A smooth metal ignores the probe

**Cause:** the screen-space and ray-traced reflections layer over the probes on smooth surfaces (on DXR hardware with `r.SSR` off the ray-traced reflections are on by default), and `r.EnvMIS` replaces the split-sum specular on smooth metals with a sky-only estimate. The probe shows where those layers do not cover.

**Fix:** expected; to inspect the probe layer alone, set `r.RayTracing.Reflections 0`, `r.SSR 0`, `r.EnvMIS 0` (as `scripts/rendering/reflection-probes.uzproject` does).

## Parallax Correction (`boxExtents`)

Without a box a probe's cubemap is an infinitely distant environment: a surface near a wall reflects the wall as the probe's centre sees it. With `boxExtents` (JSON `box_extents: [x, y, z]`, half-extents in cm, all three > 0) the reflection ray is intersected with that box and the cubemap is read towards the hit point, so the wall appears where it is (box projection, Lagarde 2012).

- The box is **centred on the probe**: put the probe at the room's centre and set the half-extents to the room's interior (e.g. an 8 × 8 × 3 m room: probe at mid-height, `box_extents: [400, 400, 150]`).
- The influence is still the **sphere**: to keep the corners at full weight, use `influenceRadius >= half-diagonal + blendDistance` (8 × 8 × 3 m: half-diagonal 585 cm).
- A receiver outside the box but inside the sphere (a doorway) is projected from the closest point of the box, so the reflection stays continuous through the opening while the probe fades out.
- Leave `box_extents` out (or any component 0) for outdoor probes and open areas.

## References

- [17-lighting.md — Reflection Probes System](../engine/12-rendering/17-lighting.md#reflection-probes-cubemap-parallax-corrected--phase-2)
- [23-ibl-lighting.md — IBL Pipeline](../engine/12-rendering/23-ibl-lighting.md)
- UE5 ReflectionCapture documentation (reference architecture)
- Godot ReflectionProbe documentation (alternative approach)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-24 | Initial creation — placement strategies, parameter tuning, optimization, troubleshooting |
| 2026-11-16 | Box parallax (`box_extents`), the under-operator priority blend, measured blend cost, the capture-lighting and reflection-layering limitations (phase3a 3.37.0) |
| 2026-11-19 | `r.ReflectionProbes` opt-in (default 0); the capture lit like the frame with its own sun shadow map (A1) |
