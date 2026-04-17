<!-- doc-meta
title: Indoor Lighting Documentation Index
date: 2026-04-06
category: rendering
tags: indoor, lighting, three.js, godot, uzengine
-->

# Indoor Lighting Documentation Index

Complete research and recommendations for implementing indoor lighting in game engines, with focus on Three.js, Godot 4, and UzEngine.

---

## Quick Navigation

### 🚀 **START HERE: For Sponza Atrium Fix**

→ **Read**: `/docs/guides/sponza-interior-lighting-fix.md` (10 min read)

This is the **action plan** for fixing the Sponza atrium in UzEngine. It includes:
- Step-by-step instructions for placing point lights
- SkyLight parameter tweaks
- Diagnostic commands to verify the fix
- Troubleshooting guide

**Output**: Interior walls will be visible with proper lighting variation.

---

## Documentation Structure

### 1. **Main Analysis Document** (for understanding the problem)

**File**: `/docs/analysis/indoor-lighting-analysis.md` (814 lines, ~30 KB)

**Covers**:
- **Part 1**: Three.js approach (HemisphereLight, AmbientLight, PointLight, LightProbe, SSAO)
- **Part 2**: Godot 4 approach (VoxelGI with interior mode, SDFGI with ReadSkyLight)
- **Part 3**: Industry-standard approach (three-light hierarchy, practical guidelines)
- **Part 4**: UzEngine current state analysis (what exists, critical gaps)
- **Part 5**: Recommended implementation path (Phase 1 quick win, Phase 2 light probes, Phase 3 real-time GI)
- **Part 6**: Specific recommendations for Sponza
- **Part 7**: Technical deep dive (deferred shading + interior lighting)
- **Part 8**: Comparison table
- **Part 9**: Key parameters reference
- **Part 10**: Visual verification methodology

**Read if**: You want to understand WHY the current system fails for interiors, and what the proper solutions are.

---

### 2. **Quick Start Guide** (for implementing Phase 1)

**File**: `/docs/guides/sponza-interior-lighting-fix.md` (305 lines, ~9 KB)

**Covers**:
- What's wrong with Sponza now
- How to modify SkyLight component (4 parameters)
- How to place 4 point lights in the atrium (exact code + positions)
- How to verify with diagnostics (`--pass-dump`, `--pixel-debug`, `--view-mode`)
- Troubleshooting (too dark, too bright, artifacts, flickering)
- Expected result
- Next steps for Phase 2+

**Read if**: You want to implement the fix TODAY. This is copy-paste-friendly code.

---

### 3. **Parameter Reference** (for side-by-side lookup)

**File**: `/docs/analysis/indoor-lighting-parameter-reference.md` (463 lines, ~16 KB)

**Covers**:
- Ambient fill light parameters (Three.js AmbientLight/HemisphereLight, Godot Environment, UzEngine SkyLight)
- Local fill light parameters (Three.js PointLight/SpotLight, Godot OmniLight3D/SpotLight3D, UzEngine Point/SpotLightComponent)
- Real-time GI parameters (Godot VoxelGI, Godot SDFGI)
- Common scenarios (atrium, cavern, house interior)
- Quick copy-paste templates for each engine

**Read if**: You need to look up a specific parameter value or compare engines side-by-side.

---

## Key Findings Summary

### The Problem
**Directional light only** NEVER works for indoor scenes. Walls/ceiling block the light, leaving interiors pitch black or requiring unrealistic ambient boost.

### The Solution (All Engines Use This)
**Layered illumination**:
1. **Ambient fill** (lowest priority) — SkyLight or HemisphereLight (affects everywhere uniformly)
2. **Multiple point/spot lights** (medium priority) — artist-placed inside buildings for local fill
3. **Real-time GI** (optional, high cost) — indirect light propagation (Godot VoxelGI/SDFGI)
4. **Screen-space AO/shadows** (postprocessing) — depth perception (GTAO, SSAO)
5. **Baked lightmaps** (static geometry) — pre-computed direct + indirect light

### For Sponza Atrium (Immediate Fix)
1. Add 4 point lights (ceiling, 2x pillars, floor level)
2. Boost `SkyLight.indirectLightingIntensity` from 1.0 → 2.5
3. Enable lower hemisphere bounce color
4. Result: Interior visible with proper lighting variation

---

## How Three.js Does It

```javascript
// The canonical Three.js indoor pattern
const hemi = new THREE.HemisphereLight(0xb1e1ff, 0xb97316, 0.8);    // Sky/ground fill
const ambient = new THREE.AmbientLight(0x404040, 0.4);               // Minimum brightness
const lamp = new THREE.PointLight(0xffffff, 1200.0, 4000.0, 2.0);   // Ceiling fixture
lamp.position.set(100, 1200, -50);
scene.add(hemi, ambient, lamp);
```

**Key insight**: HemisphereLight is directional (depends on surface normal), so it feels natural. AmbientLight is global and uniform. PointLight provides local detail.

---

## How Godot 4 Does It

**Option A: SDFGI (recommended for simplicity)**
```gdscript
env.sdfgi_enabled = true
env.sdfgi_read_sky_light = false    # CRITICAL: disable sky leaking in interior-only scenes
env.sdfgi_y_scale = 0.5             # 50% = 2x detail on Y axis
```

**Option B: VoxelGI (more control)**
```gdscript
voxel_gi.interior = true            # CRITICAL: prevent sky leaking
voxel_gi.subdiv = VoxelGI.Subdiv.SUBDIV_64
voxel_gi.bias = 1.5                 # Reduce light bleeding
```

**Key insight**: The `interior` flag (VoxelGI) or `read_sky_light=false` (SDFGI) is THE critical parameter that prevents light from unrealistically leaking into enclosed spaces from above.

---

## How UzEngine Should Do It (Phase 1)

```cpp
// Update SkyLight
auto sky = world.GetSingletonComponent<SkyLightComponent>();
sky.intensity = 2.0f;                              // Boost overall
sky.indirectLightingIntensity = 2.5f;              // Boost diffuse ONLY
sky.lowerHemisphereIsBlack = false;                // Allow floor bounce
sky.lowerHemisphereColor = {0.15f, 0.2f, 0.25f};  // Warm bounce light

// Add ceiling light
auto light1 = world.CreateEntity();
auto& ptLight1 = world.AddComponent<PointLightComponent>(light1);
ptLight1.position = {-50.0f, 1200.0f, -50.0f};
ptLight1.color = {1.0f, 1.0f, 1.0f};
ptLight1.intensity = 1200.0f;
ptLight1.range = 4000.0f;
```

---

## Phase Timeline

| Phase | Effort | Deliverable | Status |
|-------|--------|-------------|--------|
| **Phase 1** | 1–2 hours | Point lights + SkyLight boost | Ready to implement (see quick start guide) |
| **Phase 2** | 4–8 hours | Light probe system (simple CPU-side) | Design complete, no implementation yet |
| **Phase 3a** | 1 week | Ambient probe grid (lightweight GI) | Design complete, not started |
| **Phase 3b** | 2–3 weeks | VoxelGI (full real-time GI) | Complex, deferred to future |

---

## Critical Parameters for Interior Lighting

### Three.js
- `HemisphereLight.intensity` — directional fill strength (0.6–1.0)
- `PointLight.decay` — falloff curve (2.0 = physically correct)
- `PointLight.distance` — attenuation range (2000–4000 cm for 30m interior)

### Godot 4
- `SDFGI.read_sky_light` — **SET TO FALSE FOR INTERIOR-ONLY SCENES** (critical!)
- `VoxelGI.interior` — **SET TO TRUE FOR INTERIOR SCENES** (critical!)
- `VoxelGI.bias` — light bleeding reduction (1.5–2.0)

### UzEngine
- `SkyLight.indirectLightingIntensity` — diffuse fill strength (boost to 2.0–3.0 for indoor)
- `SkyLight.lowerHemisphereIsBlack` — allow floor bounce (set to false for interior)
- `PointLight.range` — attenuation radius (2500–4000 cm for interior fill)
- `PointLight.intensity` — brightness (400–1200 for fill lights)

---

## Decision Tree: Which Approach Should UzEngine Use?

```
Is interior dark?
├─ YES: Add point lights + boost SkyLight (Phase 1, 2 hours) ✓ START HERE
└─ NO: Interior visible but no lighting detail?
   ├─ YES: Add light probes (Phase 2, 4–8 hours)
   └─ NO: Interior needs dynamic bounces?
      ├─ YES: Implement real-time GI (Phase 3a/3b, 1–3 weeks)
      └─ NO: Done! (Use current setup with GTAO for depth cues)
```

---

## Testing Methodology

### Quick Visual Check
```bash
uzengine.exe --screenshot-stdout --frames 8 --project sponza.uzproject --editor
```

### Detailed Diagnosis
```bash
uzengine.exe --pass-dump --frames 120 --project sponza.uzproject --editor
# Open .screenshots/pass-dump-fN/INDEX.html
# Check: lighting.hdr should be non-zero everywhere (no black interior)
```

### Per-Pixel Validation
```bash
uzengine.exe --pixel-debug --pixel-debug-x 960 --pixel-debug-y 540 --project ... --editor
# Check PIXEL_DEBUG_JSON: id=5,6,7 (HDR color) should be > 0.1 in all channels
```

---

## References

### Research Sources

**Three.js documentation**:
- [AmbientLight](https://threejs.org/docs/pages/AmbientLight.html)
- [HemisphereLight](https://discoverthreejs.com/book/first-steps/ambient-lighting/)
- [PointLight](https://threejs.org/docs/pages/PointLight.html)
- [LightProbe](https://threejs.org/docs/pages/LightProbe.html)

**Godot 4 documentation**:
- [Introduction to Global Illumination](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/introduction_to_global_illumination.html)
- [Using VoxelGI](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_voxel_gi.html)
- [Using SDFGI](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html)

**Game development references**:
- [The Level Design Book — Lighting](https://book.leveldesignbook.com/process/lighting)
- [Three-Point Lighting](https://en.wikipedia.org/wiki/Three-point_lighting)
- [Unreal Engine Lighting Quick Start](https://docs.unrealengine.com/4.26/en-US/BuildingWorlds/LightingAndShadows/QuickStart)
- [pmndrs/postprocessing](https://github.com/pmndrs/postprocessing)

---

## Next Actions

1. **TODAY**: Implement Phase 1 (2 hours)
   - Follow `/docs/guides/sponza-interior-lighting-fix.md`
   - Add 4 point lights to atrium
   - Boost SkyLight parameters
   - Capture screenshot to verify
   - Commit changes

2. **THIS WEEK**: Code review findings
   - Verify interior lighting looks natural
   - Check outdoor areas aren't washed out
   - Adjust light positions/intensities if needed

3. **NEXT WEEK**: Consider Phase 2
   - Light probe system (4–8 hours)
   - Or skip if Phase 1 is "good enough"

4. **FUTURE**: If GI is priority
   - Decide between ambient probe grid (1 week) vs VoxelGI (2–3 weeks)
   - Ambient probe grid is faster and art-friendly
   - VoxelGI is more dynamic but complex

---

**Documentation generated**: 2026-04-06  
**Status**: Research + Analysis + Quick-Start Guide  
**Next step**: Read `/docs/guides/sponza-interior-lighting-fix.md` and implement Phase 1
