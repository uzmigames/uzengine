## 1. Implementation

### 1.1 Froxel Grid Setup
- [x] 1.1.1 Create `VolumetricFogPass` class in `passes/atmosphere/`. Manage 3D textures: inscattering (RGBA16F, 160x90x64) and density (R16F, same size). _(Session 1: the extinction is packed in the alpha of the RGBA16F injected volume instead of a separate R16F; plus the RGBA16F integrated volume and two temporal-history volumes allocated on the first temporal frame.)_
- [x] 1.1.2 Implement exponential depth slicing: `sliceDepth = nearZ * pow(farZ/nearZ, (slice+0.5)/numSlices)`. This gives more resolution near camera.
- [x] 1.1.3 Add `VolumetricFogComponent` to ECS: `density`, `albedo` (float3), `phaseG` (Henyey-Greenstein asymmetry, default 0.5), `heightFalloff`, `heightOffset`, `globalDensity`. _(JSON key `volumetric_fog`; also `extinction_scale`, `emissive`, `distance`, `start_distance`, `near_fade_in_distance`, `ambient_intensity`, `local_light_scattering`. A HeightFogComponent with `enable_volumetric_fog` stays as the fallback source.)_

### 1.2 Light Injection (Compute)
- [x] 1.2.1 Create `volumetric_fog_inject_cs.hlsl` — [numthreads(8,8,1)] dispatch over XY froxel tiles, loop Z slices. For each froxel: compute world position, evaluate density (exponential height + noise), inject light contribution from directional + point + spot lights. _(Session 1: [numthreads(8,8,4)], one thread per froxel; no noise term — the density is the exponential height field; the point/spot lights come from the frame's LightingCB bound at b1.)_
- [x] 1.2.2 Apply shadow map sampling per froxel (CSM for directional, atlas for point/spot) to create volumetric shadows.
- [x] 1.2.3 Apply Henyey-Greenstein phase function for anisotropic scattering: `phase(cosTheta) = (1-g^2) / (4*PI*(1+g^2-2*g*cosTheta)^1.5)`.
- [x] 1.2.4 Add `@ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"`.

### 1.3 Scattering Integration (Compute)
- [x] 1.3.1 Create `volumetric_fog_scatter_cs.hlsl` — [numthreads(8,8,1)] dispatch over XY tiles. For each column, march front-to-back integrating inscattering and transmittance: `accum.rgb += slice.rgb * transmittance; transmittance *= exp(-slice.a * sliceThickness)`.
- [x] 1.3.2 Add temporal reprojection: blend current frame with previous frame (EMA alpha ~0.05) using motion vectors to reduce noise. _(Session 1: the froxel centre is reprojected with the previous frame's unjittered view-projection — the same matrix the velocity pass encodes, applied to mid-air points — with a frustum/grid validity guard, a 3x3x3 neighbourhood clamp and a per-frame sample jitter; `r.VolumetricFog.Temporal*`.)_

### 1.4 Composite (Pixel Shader)
- [x] 1.4.1 Create `volumetric_fog_composite_ps.hlsl` — fullscreen pass that samples integrated froxel volume at scene depth, applies inscattering: `finalColor = sceneColor * transmittance + inscattering`.
- [x] 1.4.2 Handle sky pixels (depth=0 in reversed-Z): use maximum fog distance for sky blending.

### 1.5 Render Graph Integration
- [x] 1.5.1 Register VolumetricFogPass in `scene_renderer_init.cpp`: inject → scatter → composite, after shadow pass, before final composite. _(The pass object is created in scene_renderer_init.cpp; the per-frame registration lives in frame_atmosphere_passes.cpp after the height fog pass, which now starts at the grid's far edge so the two do not overlap.)_
- [x] 1.5.2 Bind shadow atlas SRV, light CB, and camera CB to fog compute shaders.
- [x] 1.5.3 Add CVar `r.VolumetricFog` (0=off, 1=on, default=1). _(Landed with default 0 (OFF) for the visual review; OFF is byte-identical to 00e337b3 — see the evidence report.)_

## 2. Testing

### 2.1 Incremental Visual Verification
- [x] 2.1.1 Step 1: Solid color fog output — verify froxel grid renders anything to screen. _(`r.VolumetricFog.Stage=1`, montage_steps.png)_
- [x] 2.1.2 Step 2: Density-only fog (no lighting) — verify exponential height falloff looks correct. _(Stage 2)_
- [x] 2.1.3 Step 3: Add directional light injection — verify light shafts through fog. _(Stage 3: the unshadowed sun/local-light haze; the shafts appear with the shadows of step 4.)_
- [x] 2.1.4 Step 4: Add shadow sampling — verify volumetric shadows (dark regions behind occluders). _(Stage 4: the god ray through the arch, Sponza view E.)_
- [x] 2.1.5 Step 5: Add temporal reprojection — verify noise reduction without ghosting. _(Stage 5 / full: jittered-sample temporal std 0.66 % → 0.28 % (= the unjittered floor); strafe/pan ON-vs-OFF deviation 0.6-1.0 % in motion, decaying to the static 0.6 % after the stop; the emerging moving lantern lags by a 3 % mean / 15 % peak luminance deficit at 3.6 m/s (alpha 0.05) and converges to 0.2 % after it stops.)_

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Capture foggy outdoor scene at multiple densities. Compare against UE5 exponential height fog + volumetric fog reference.
- [ ] 2.2.2 Verify no banding artifacts in fog gradient (16-bit float precision).
- [ ] 2.2.3 Verify froxel fog + height fog combination looks correct.

### 2.3 Performance
- [ ] 2.3.1 Benchmark fog ON vs OFF: target < 1.5ms GPU at 1080p on RTX 3060. _(RTX 5080 only so far: 0.15 ms for the three passes at 1920x1080; no RTX 3060 measurement.)_
- [x] 2.3.2 Profile compute dispatch times (inject, scatter, composite separately). _(RTX 5080, 1080p: inject 0.040 ms, scatter 0.088 ms (0.025 without the temporal filter), composite 0.024 ms.)_

### 2.4 Coverage
- [ ] 2.4.1 Verify 95%+ coverage on all new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/renderer.md` with volumetric fog pass pipeline description.
- [ ] 3.2 Update `docs/specs/rendering/shader-library.md` with froxel fog shader functions.
- [x] 3.3 Add `@ref Hillaire 2015` citations to all shader headers.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
