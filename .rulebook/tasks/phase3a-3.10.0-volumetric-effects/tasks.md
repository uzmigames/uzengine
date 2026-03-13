## 1. Implementation

- [ ] 1.1 Implement 3D shape noise compute shader (Perlin-Worley, 128x128x128)
- [ ] 1.2 Implement 3D detail noise compute shader (Worley, 32x32x32)
- [ ] 1.3 Implement 2D weather map compute shader (coverage/type, 512x512)
- [ ] 1.4 Implement cloud ray march pixel shader (Beer-Lambert, density sampling, multi-scatter ambient)
- [ ] 1.5 Implement cloud temporal filtering shader (reprojection + accumulation)
- [ ] 1.6 Add VolumetricCloudsData struct (coverage, density, altitude band, wind, absorption)
- [ ] 1.7 Implement cloud-atmosphere compositing (premultiplied alpha blend over sky)
- [ ] 1.8 Implement god rays shader (radial blur from sun position, intensity/decay/density)
- [ ] 1.9 Register noise bake, cloud march, cloud temporal, god rays passes in render graph
- [ ] 1.10 Create volumetric effects demo (clouds + god rays + sky atmosphere)

## 2. Testing

- [ ] 2.1 Test 3D noise texture dimensions and format (128^3, RGBA8)
- [ ] 2.2 Test weather map output (512x512, valid coverage range 0-1)
- [ ] 2.3 Test cloud shader compilation (SM6.6 and SM5)
- [ ] 2.4 Test god rays shader compilation
- [ ] 2.5 Test cloud density at zero coverage (fully transparent)
- [ ] 2.6 Test VolumetricCloudsData defaults (reasonable Earth-like cloud layer)
- [ ] 2.7 Test render graph ordering (noise bake before march, temporal after march)
- [ ] 2.8 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Add `docs/engine/12-rendering/11-volumetric-effects.md` with cloud model and noise pipeline
- [ ] 3.2 Add cloud parameter tuning guide (coverage, density, wind, altitude)
