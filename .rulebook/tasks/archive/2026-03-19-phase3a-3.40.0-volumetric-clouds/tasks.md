## 1. Implementation

- [x] Done in phase3a-3.10.0-volumetric-effects. 1.1 Implement 3D noise generator compute shader — Perlin-Worley 128x128x128 RGBA8 (R=Perlin-Worley, GBA=Worley octaves). Bake once at startup, cache to disk.
- [x] Done in phase3a-3.10.0-volumetric-effects. 1.2 Implement detail noise — 32x32x32 RGBA8 Worley noise for cloud edge erosion.
- [x] 1.3 Define CloudComponent — VolumetricCloudsComponent in runtime/engine/ecs/environment/volumetric_clouds_component.h. Pure data struct with layer geometry (altitude/height km), density (coverage/density/cloudType), wind (Vec3 velocity + detail scale), scattering (extinction/albedo/dual-lobe HG phase), multi-scatter ambient, ray march params (50km max, 64 primary + 6 light steps), enabled flag. UE5 VolumetricCloudComponent defaults. Also created cloud_weather_cs.hlsl (512x512 RGBA8 procedural weather map: R=coverage fbm4, G=cloud type gradient noise, B=precipitation, A=1).
- [x] Done in phase3a-3.10.0-volumetric-effects. 1.4 Implement volumetric_cloud_cs.hlsl — ray march from camera through spherical shell [innerRadius, outerRadius]. 64 primary steps, 6 light steps. Sample density = shape noise * coverage remapping - detail erosion. Accumulate transmittance via Beer-Lambert.
- [x] Done in phase3a-3.10.0-volumetric-effects. 1.5 Implement single-scattering — at each ray step, march toward sun (6 steps), accumulate optical depth. Apply Henyey-Greenstein phase function for forward/back scatter. Silver lining via powder term.
- [x] Done in phase3a-3.10.0-volumetric-effects. 1.6 Implement temporal reprojection — render clouds at quarter resolution, reproject previous frame result. Only update 1/16th of pixels per frame (checkerboard). Reduces cost by 16x.
- [x] Done in phase3a-3.10.0-volumetric-effects. 1.7 Implement depth-aware compositing — blend cloud result behind scene geometry using depth buffer comparison. Clouds behind mountains are occluded.
- [x] Done in phase3a-3.10.0-volumetric-effects. 1.8 Add VolumetricCloudPass to render graph — after sky atmosphere, before post-process. Read: depth, atmosphere LUT. Write: cloud color + transmittance texture.
- [x] Done in phase3a-3.10.0-volumetric-effects. 1.9 Create volumetric_clouds demo — outdoor scene with terrain and cloud layer, time-of-day slider showing cloud lighting from dawn to dusk.

## 2. Visual Verification

- [x] Done in phase3a-3.10.0-volumetric-effects. 2.1 Screenshot: midday clouds — white cumulus shapes with soft edges, silver lining near sun
- [x] Done in phase3a-3.10.0-volumetric-effects. 2.2 Screenshot: sunset clouds — orange/pink lighting on cloud undersides
- [x] Done in phase3a-3.10.0-volumetric-effects. 2.3 Screenshot: clouds behind mountains — correct depth occlusion, no bleed-through
- [x] Done in phase3a-3.10.0-volumetric-effects. 2.4 Screenshot: temporal stability — no visible checkerboard or ghosting during camera movement
- [x] Done in phase3a-3.10.0-volumetric-effects. 2.5 Performance: quarter-res + temporal reprojection must run under 2ms GPU at 1080p

## 3. Documentation

- [x] Done in phase3a-3.10.0-volumetric-effects. 3.1 Update `docs/engine/12-rendering/07-post-processing.md` with volumetric cloud pass
- [x] Done in phase3a-3.10.0-volumetric-effects. 3.2 Document cloud parameter tuning guide (coverage, density, phase function)
