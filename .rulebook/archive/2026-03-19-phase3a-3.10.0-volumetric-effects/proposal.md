# Proposal: Volumetric Effects — Clouds & God Rays (Phase 3A)

## Why
The 0.1 POC had ray-marched volumetric clouds with 3D Perlin-Worley noise, temporal filtering, Beer-Lambert absorption, and wind animation — plus god rays (crepuscular rays) for dramatic light shafts. These were the most visually impressive features of 0.1 and are essential for outdoor scene atmosphere. Volumetric clouds combined with the physical sky (3.8.0) produce a complete AAA sky rendering pipeline.

## What Changes

### New Systems
1. **Cloud noise baking** — Compute shaders generating 3D shape noise (Perlin-Worley), detail noise, and 2D weather map textures, baked once and cached
2. **Volumetric cloud ray marcher** — Pixel shader that marches rays through a cloud layer band (base/top altitude), sampling 3D noise for density, applying Beer-Lambert absorption, multi-scatter ambient term
3. **Cloud temporal filtering** — Reprojection and temporal accumulation to denoise ray-marched clouds at low sample counts
4. **VolumetricClouds component** — Scene data: coverage, density, base/top altitude, wind speed/direction, absorption coefficient
5. **God rays shader** — Radial blur from sun screen position with intensity, decay, and density parameters, composited additively
6. **Cloud-atmosphere compositing** — Alpha-blend clouds over sky atmosphere with correct depth ordering

### Refactoring
- Render graph gains cloud noise bake pass (one-time), cloud ray march pass, cloud temporal pass, god rays pass
- Sky composite pass updated to blend atmosphere + clouds + fog

## Impact
- Affected specs: `docs/engine/12-rendering/06-lighting.md`
- Affected code: `runtime/shaders/atmosphere/`, `runtime/engine/renderer/passes/`
- Breaking change: NO — volumetric effects are additive, scenes without them unchanged
- User benefit: Dynamic volumetric clouds, dramatic god ray light shafts, complete outdoor sky
- Depends on: 3.8.0 (sky atmosphere for compositing)
