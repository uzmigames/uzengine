## Session 1: Volume Asset + CVolumeComponent

- [ ] 1.1 Create `volume_asset.h/.cpp` — `VolumeAsset` class: `Load(path, format)` reads raw binary 3D grid (header: uint32 width, height, depth; data: R8_UNORM or R16F per voxel). `GetTexture3D() → RhiTextureHandle`. `GetDimensions() → uint3`. `GetMaxDensity() → float` (precomputed maximum value for Woodcock tracking majorant).
- [ ] 1.2 Binary format: magic `UZVOL`, version 1, width/height/depth as uint32, format enum (R8=0, R16F=1), then raw data. Include a CPU utility `GenerateSphereVolume(radius, density)` for test volumes.
- [ ] 1.3 Create `volume_component.h` — `CVolumeComponent` ECS component: `VolumeAsset* asset`, `float sigmaA = 0.1f` (absorption coefficient, cm⁻¹), `float sigmaS = 0.9f` (scattering coefficient), `float g = 0.0f` (Henyey-Greenstein anisotropy, [-1,1]), `float3 halfExtents` (OBB size). Default sigma values from Novak 2018 Table 1 smoke example.
- [ ] 1.4 Add `CVolumeComponent` collection to `scene_gather.cpp`: gather up to 4 volume components per frame, upload as `StructuredBuffer<VolumeDesc>` (transform matrix, sigma values, anisotropy, Texture3D index in bindless heap).
- [ ] 1.5 Build + verify: volume asset loads a test sphere binary. `CVolumeComponent` attaches to an entity. Scene gather populates volume buffer. No crashes.

## Session 2: Volume Ray March Shader

- [ ] 2.1 Create `volume_raymarch_cs.hlsl` — `[numthreads(8,8,1)]` entry `main`. For each pixel: reconstruct world-space ray from depth buffer. Ray/OBB intersection test (transform ray to volume local space, axis-aligned slab test). Clamp ray to [tMin, tMax] from intersection.
- [ ] 2.2 Woodcock delta tracking for transmittance: set majorant `mu_t_max = (sigmaA + sigmaS) * volume.maxDensity`. Advance by free path `t += -log(rand) / mu_t_max`. At each sample: read `Texture3D` density, compute `mu_t_real = (sigmaA + sigmaS) * density`. Accept with probability `mu_t_real / mu_t_max` (rejection sampling). Accepted samples contribute in-scattering. @ref Novak 2018 Eq. 13
- [ ] 2.3 In-scattering at accepted sample: cast shadow ray toward directional light. Evaluate Henyey-Greenstein phase function `p(cosTheta, g) = (1 - g²) / (4π(1 + g² - 2g*cosTheta)^1.5)`. @ref Henyey & Greenstein 1941. Accumulate: `radiance += transmittance * sigmaS * phaseFunction * lightColor * (1 - shadowOcclusion)`
- [ ] 2.4 Beer-Lambert extinction for shadow ray: integrate density along shadow ray using fixed-step march (8 steps), compute transmittance as `exp(-sigmaT * integratedDensity)`. Use this as shadow term in step 2.3.
- [ ] 2.5 Output: `RWTexture2D<float4> uVolume` stores `float4(scatteredRadiance, transmittance)` per pixel. Build + verify: constant-density sphere volume produces correct forward-scattering cone (brighter in forward direction with g>0).

## Session 3: VolumePass Integration + Temporal Reprojection + Demo

- [ ] 3.1 Create `volume_pass.h/.cpp` — `VolumePass` render graph pass: dispatch `volume_raymarch_cs` at half resolution (for performance). Inputs: scene depth SRV, volume buffer SRV, Texture3D array SRV. Outputs: volume radiance + transmittance at half-res.
- [ ] 3.2 Upscale + composite: bilinear upsample volume output to full resolution. Composite over scene color: `finalColor = sceneColor * volumeTransmittance + volumeRadiance`. Insert in engine loop after deferred lighting, before bloom/tone map.
- [ ] 3.3 Temporal reprojection: ping-pong `uPrevVolume` buffer. Reproject previous frame via camera delta: `prevUV = projectWorldPos(worldPos, prevViewProj)`. Blend: `accumulated = lerp(current, prevSample, 0.9)` if reprojection UV is in [0,1] and depth matches. Reset on disocclusion.
- [ ] 3.4 Demo: create a smoke volume using `GenerateSphereVolume` (radius=200cm, density=0.5). Place directional light on one side. Run `demo.exe --screenshot-stdout --frames 30`. Verify: visible volumetric shadow cone, forward-scattering halo, transmittance fade at volume edges.
- [ ] 3.5 Build + verify: volume renders without device removal. GPU timestamp < 3ms for single 128³ volume at 1080p half-res. Temporal reprojection reduces frame-to-frame noise (compare frame 1 vs frame 30 via `--flip`, expect significant noise reduction).

## 1. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 1.1 Update or create documentation covering the implementation
- [ ] 1.2 Write tests covering the new behavior
- [ ] 1.3 Run tests and confirm they pass
