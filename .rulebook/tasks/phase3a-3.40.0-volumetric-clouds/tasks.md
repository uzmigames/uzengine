## 1. Implementation

- [ ] 1.1 Implement 3D noise generator compute shader — Perlin-Worley 128x128x128 RGBA8 (R=Perlin-Worley, GBA=Worley octaves). Bake once at startup, cache to disk.
- [ ] 1.2 Implement detail noise — 32x32x32 RGBA8 Worley noise for cloud edge erosion.
- [ ] 1.3 Define CloudComponent — cloudLayerAltitude (2000m default), thickness (1500m), coverage (0.5), density (0.3), windSpeed (10 m/s), windDirection (Vec2), silverLiningIntensity (0.8), HenyeyGreensteinG (0.3).
- [ ] 1.4 Implement volumetric_cloud_cs.hlsl — ray march from camera through spherical shell [innerRadius, outerRadius]. 64 primary steps, 6 light steps. Sample density = shape noise * coverage remapping - detail erosion. Accumulate transmittance via Beer-Lambert.
- [ ] 1.5 Implement single-scattering — at each ray step, march toward sun (6 steps), accumulate optical depth. Apply Henyey-Greenstein phase function for forward/back scatter. Silver lining via powder term.
- [ ] 1.6 Implement temporal reprojection — render clouds at quarter resolution, reproject previous frame result. Only update 1/16th of pixels per frame (checkerboard). Reduces cost by 16x.
- [ ] 1.7 Implement depth-aware compositing — blend cloud result behind scene geometry using depth buffer comparison. Clouds behind mountains are occluded.
- [ ] 1.8 Add VolumetricCloudPass to render graph — after sky atmosphere, before post-process. Read: depth, atmosphere LUT. Write: cloud color + transmittance texture.
- [ ] 1.9 Create volumetric_clouds demo — outdoor scene with terrain and cloud layer, time-of-day slider showing cloud lighting from dawn to dusk.

## 2. Visual Verification

- [ ] 2.1 Screenshot: midday clouds — white cumulus shapes with soft edges, silver lining near sun
- [ ] 2.2 Screenshot: sunset clouds — orange/pink lighting on cloud undersides
- [ ] 2.3 Screenshot: clouds behind mountains — correct depth occlusion, no bleed-through
- [ ] 2.4 Screenshot: temporal stability — no visible checkerboard or ghosting during camera movement
- [ ] 2.5 Performance: quarter-res + temporal reprojection must run under 2ms GPU at 1080p

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/07-post-processing.md` with volumetric cloud pass
- [ ] 3.2 Document cloud parameter tuning guide (coverage, density, phase function)
