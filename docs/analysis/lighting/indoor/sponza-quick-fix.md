<!-- doc-meta
title: Quick Start — Sponza Interior Lighting Fix (Phase 1)
date: 2026-04-06
category: rendering
tags: sponza, interior-lighting, point-lights, quick-fix
-->

# Sponza Interior Lighting Fix — Phase 1 Quick Start

**Goal**: Make the Sponza atrium interior visible by adding point lights + boosting SkyLight.

**Time estimate**: 1–2 hours (placement + testing)

---

## What's Wrong Now

The Sponza atrium ceiling is illuminated by the directional sun (outside the building), but the interior floor and walls are in shadow. Currently, the only fill light is `SkyLight.indirectLightingIntensity`, which affects the entire scene uniformly. Result: interior is either too dark or, if you boost SkyLight, the entire scene gets washed out.

**Solution**: Add point lights inside the atrium that provide local fill without affecting outdoor areas.

---

## Step 1: Understand Current SkyLight Settings

Check `sample/projects/sponza.uzproject` (or wherever your Sponza scene is):

```cpp
// Expected current state:
SkyLightComponent skyLight;
skyLight.intensity = 1.0f;
skyLight.indirectLightingIntensity = 1.0f;
skyLight.lightColor = {1.0f, 1.0f, 1.0f};
skyLight.lowerHemisphereIsBlack = true;  // Floor doesn't bounce light
```

**This is correct for outdoor scenes.** For interior, we need to change it.

---

## Step 2: Modify SkyLight Component (for Indoor Test)

Update the Sponza scene to have:

```cpp
SkyLightComponent skyLight;
skyLight.intensity = 2.0f;                    // ← Boost overall
skyLight.indirectLightingIntensity = 2.5f;   // ← Boost diffuse fill
skyLight.lightColor = {1.0f, 1.0f, 1.0f};
skyLight.lowerHemisphereIsBlack = false;      // ← Allow floor bounce
skyLight.lowerHemisphereColor = {0.15f, 0.2f, 0.25f};  // ← Warm bounce light
```

**Why these values**:
- `intensity = 2.0` makes the environment map brighter overall
- `indirectLightingIntensity = 2.5` boosts ONLY the diffuse contribution (fills shadows)
- `lowerHemisphereColor` simulates warm light bouncing off the floor (instead of pure black)

---

## Step 3: Place Point Lights in the Atrium

Add 4 point lights to the Sponza scene at these approximate positions. Adjust to match your level geometry.

### Light 1: Ceiling Center (Top)

```cpp
auto light1 = world.CreateEntity();
auto& comp1 = world.AddComponent<PointLightComponent>(light1);
auto& transform1 = world.AddComponent<TransformComponent>(light1);

transform1.SetPosition({-50.0f, 1200.0f, -50.0f});  // Centered, high up

comp1.color = {1.0f, 1.0f, 1.0f};                   // White
comp1.intensity = 1200.0f;                          // Strong
comp1.range = 4000.0f;                              // ~40 meter radius
comp1.useInverseSquaredFalloff = true;
comp1.castShadows = false;                          // Not needed for fill
```

### Light 2: Upper Left Pillar

```cpp
auto light2 = world.CreateEntity();
auto& comp2 = world.AddComponent<PointLightComponent>(light2);
auto& transform2 = world.AddComponent<TransformComponent>(light2);

transform2.SetPosition({-800.0f, 800.0f, -200.0f});  // Left side, upper

comp2.color = {1.0f, 0.95f, 0.9f};                  // Warm white
comp2.intensity = 600.0f;
comp2.range = 3000.0f;
comp2.castShadows = false;
```

### Light 3: Upper Right Pillar

```cpp
auto light3 = world.CreateEntity();
auto& comp3 = world.AddComponent<PointLightComponent>(light3);
auto& transform3 = world.AddComponent<TransformComponent>(light3);

transform3.SetPosition({800.0f, 800.0f, -200.0f});  // Right side, upper

comp3.color = {1.0f, 0.95f, 0.9f};                  // Warm white
comp3.intensity = 600.0f;
comp3.range = 3000.0f;
comp3.castShadows = false;
```

### Light 4: Floor Level (Accent)

```cpp
auto light4 = world.CreateEntity();
auto& comp4 = world.AddComponent<PointLightComponent>(light4);
auto& transform4 = world.AddComponent<TransformComponent>(light4);

transform4.SetPosition({0.0f, 200.0f, 300.0f});  // Near statue, low

comp4.color = {0.9f, 1.0f, 1.0f};                 // Slight cool tint
comp4.intensity = 400.0f;
comp4.range = 2500.0f;
comp4.castShadows = false;
```

---

## Step 4: Verify With Diagnostics

### Option A: Visual Screenshot

```bash
cd runtime && zig build
cd demos/sponza && zig build

# Capture screenshot
./build/bin/sponza \
    --screenshot-stdout \
    --screenshot-frame 5 \
    --frames 8 \
    --project ../sample/projects/sponza.uzproject \
    --editor > sponza_phase1.png

# View the image (use Read tool to inspect)
```

**What to look for**:
- Interior walls should be visibly lit (not dark gray/black)
- Outdoor areas should still show sun shadows
- There should be lighting variation (not uniform)
- No blown-out whites (interior shouldn't be too bright)

### Option B: Pass Dump (Detailed Diagnosis)

```bash
./build/bin/sponza \
    --pass-dump \
    --frames 120 \
    --project ../sample/projects/sponza.uzproject \
    --editor
```

**Output**: `.screenshots/pass-dump-fN/` with PNGs:
- `gbuffer.albedo` — base colors (should be bright everywhere)
- `gbuffer.normal` — surface normals (should look smooth)
- `lighting.hdr` — computed lighting before tonemap (should be non-zero indoors)
- `ao.result` — ambient occlusion (should darken corners)
- `final.ldr` — final tone-mapped image (your result)

**Check**: Open `INDEX.html` in the folder. Compare `lighting.hdr` with and without point lights. Interior pixels should have higher brightness with lights.

### Option C: Pixel-by-Pixel Inspection

```bash
# Sample a pixel deep inside the atrium (should not be black)
./build/bin/sponza \
    --pixel-debug \
    --pixel-debug-x 960 \
    --pixel-debug-y 540 \
    --project ../sample/projects/sponza.uzproject \
    --editor
```

**Output**: `PIXEL_DEBUG_JSON:{...}` on stdout

Look for the `records` array. Record indices:
- `id=0`: depth
- `id=1`: AO
- `id=2,3,4`: albedo RGB
- `id=5,6,7`: HDR color RGB (lighting result)
- `id=8`: roughness
- `id=9`: metallic
- `id=10,11,12`: emissive RGB

**Good result**: `id=5,6,7` (HDR color) should be > 0.1 in all channels (not black).

---

## Step 5: Fine-Tune Light Positions & Intensity

If interior is still too dark:
- Increase `comp1.intensity` from 1200 to 1500
- Move `light1` position up (increase Y coordinate)
- Increase `skyLight.indirectLightingIntensity` to 3.0

If interior is too bright / washed out:
- Decrease `comp1.intensity`
- Decrease `skyLight.indirectLightingIntensity` back to 2.0
- Check if outdoor sun is still visible (should be)

**Golden rule**: Interior should look well-lit but not uniformly. There should be shadows from the point lights and variation in brightness.

---

## Step 6: Commit Changes

```bash
# After verification:
git add -A
git commit -m "fix: sponza interior lighting — add point lights + boost skylight

- Added 4 point lights inside atrium (ceiling center, 2x pillars, floor level)
- Increased SkyLight.intensity to 2.0 and indirectLightingIntensity to 2.5
- Enabled lowerHemisphereColor for warm floor bounce
- Result: Interior walls now visible with proper lighting variation
- Tested: --pass-dump confirms interior lighting > 0.1 (previously ~0.05)

Visual inspection: atrium ceiling, walls, and floor have realistic depth cues.
No blown-out whites, shadows still visible from directional sun."
```

---

## Troubleshooting

### Interior Still Too Dark

**Likely cause**: Point lights are too far away or too weak.

**Fix**:
1. Increase `light1.intensity` by 200 (from 1200 → 1400)
2. Move `light1` position closer to interior (lower Y value)
3. Decrease `range` slightly (3500 instead of 4000) to concentrate light

### Outdoor Areas Too Bright

**Likely cause**: `skyLight.indirectLightingIntensity` boost is too aggressive.

**Fix**:
1. Reduce to 2.0 (from 2.5)
2. Rely more on point lights for interior fill
3. Verify `DirectionalLight.shadowCascades > 0` so outdoor sun still casts shadows

### Lighting Looks Uniform / No Shadows Visible

**Likely cause**: GTAO or shadows might be disabled.

**Fix**:
1. Check `--pass-dump` → `ao.result` should show AO darkening corners
2. Check `--pass-dump` → final image should show shadow variation
3. Verify `DirectionalLight.shadowCascades >= 2`
4. Run `--view-mode lit` to see raw lighting before tonemap

### Interior Has Artifacts (Banding, Flickering)

**Likely cause**: Temporal anti-aliasing fighting with point lights.

**Fix**:
1. Disable TAA temporarily: `r.TAA=0` CVar
2. If flickering stops, increase `pointShadowBias` on the light
3. Re-enable TAA after tuning

---

## Expected Result

After Phase 1, the Sponza atrium should look like this:

| Area | Before | After |
|------|--------|-------|
| **Ceiling** | Bright (lit by sun) | Bright (lit by sun + point light) |
| **Upper walls** | Medium (some skylight) | Brighter (skylight + point light fill) |
| **Floor** | Dark (in shadow) | Medium (skylight indirect + floor bounce) |
| **Corners** | Black | Dark gray (AO + point light spillover) |
| **Outdoor areas** | Medium (skylight) | Unchanged (point lights have limited range) |

---

## Next Steps (After Phase 1 Works)

### Phase 2: Light Probes

Implement a simple grid of light probes for even better fill lighting (without full GI).

### Phase 3: Real-Time GI (If Priority)

Implement a basic voxel GI system for full dynamic indirect lighting.

See main analysis document: `/docs/analysis/indoor-lighting-analysis.md`

---

**Created**: 2026-04-06  
**Category**: Rendering / Interior Lighting  
**Difficulty**: Easy (no new systems, just component tweaking)
