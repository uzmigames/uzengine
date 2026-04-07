## 1. Implementation

### 1.1 Froxel Grid Setup
- [ ] 1.1.1 Create `VolumetricFogPass` class in `passes/atmosphere/`. Manage 3D textures: inscattering (RGBA16F, 160x90x64) and density (R16F, same size).
- [ ] 1.1.2 Implement exponential depth slicing: `sliceDepth = nearZ * pow(farZ/nearZ, (slice+0.5)/numSlices)`. This gives more resolution near camera.
- [ ] 1.1.3 Add `VolumetricFogComponent` to ECS: `density`, `albedo` (float3), `phaseG` (Henyey-Greenstein asymmetry, default 0.5), `heightFalloff`, `heightOffset`, `globalDensity`.

### 1.2 Light Injection (Compute)
- [ ] 1.2.1 Create `volumetric_fog_inject_cs.hlsl` — [numthreads(8,8,1)] dispatch over XY froxel tiles, loop Z slices. For each froxel: compute world position, evaluate density (exponential height + noise), inject light contribution from directional + point + spot lights.
- [ ] 1.2.2 Apply shadow map sampling per froxel (CSM for directional, atlas for point/spot) to create volumetric shadows.
- [ ] 1.2.3 Apply Henyey-Greenstein phase function for anisotropic scattering: `phase(cosTheta) = (1-g^2) / (4*PI*(1+g^2-2*g*cosTheta)^1.5)`.
- [ ] 1.2.4 Add `@ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"`.

### 1.3 Scattering Integration (Compute)
- [ ] 1.3.1 Create `volumetric_fog_scatter_cs.hlsl` — [numthreads(8,8,1)] dispatch over XY tiles. For each column, march front-to-back integrating inscattering and transmittance: `accum.rgb += slice.rgb * transmittance; transmittance *= exp(-slice.a * sliceThickness)`.
- [ ] 1.3.2 Add temporal reprojection: blend current frame with previous frame (EMA alpha ~0.05) using motion vectors to reduce noise.

### 1.4 Composite (Pixel Shader)
- [ ] 1.4.1 Create `volumetric_fog_composite_ps.hlsl` — fullscreen pass that samples integrated froxel volume at scene depth, applies inscattering: `finalColor = sceneColor * transmittance + inscattering`.
- [ ] 1.4.2 Handle sky pixels (depth=0 in reversed-Z): use maximum fog distance for sky blending.

### 1.5 Render Graph Integration
- [ ] 1.5.1 Register VolumetricFogPass in `scene_renderer_init.cpp`: inject → scatter → composite, after shadow pass, before final composite.
- [ ] 1.5.2 Bind shadow atlas SRV, light CB, and camera CB to fog compute shaders.
- [ ] 1.5.3 Add CVar `r.VolumetricFog` (0=off, 1=on, default=1).

## 2. Testing

### 2.1 Incremental Visual Verification
- [ ] 2.1.1 Step 1: Solid color fog output — verify froxel grid renders anything to screen.
- [ ] 2.1.2 Step 2: Density-only fog (no lighting) — verify exponential height falloff looks correct.
- [ ] 2.1.3 Step 3: Add directional light injection — verify light shafts through fog.
- [ ] 2.1.4 Step 4: Add shadow sampling — verify volumetric shadows (dark regions behind occluders).
- [ ] 2.1.5 Step 5: Add temporal reprojection — verify noise reduction without ghosting.

### 2.2 Visual Regression (Tier 2)
- [ ] 2.2.1 Capture foggy outdoor scene at multiple densities. Compare against UE5 exponential height fog + volumetric fog reference.
- [ ] 2.2.2 Verify no banding artifacts in fog gradient (16-bit float precision).
- [ ] 2.2.3 Verify froxel fog + height fog combination looks correct.

### 2.3 Performance
- [ ] 2.3.1 Benchmark fog ON vs OFF: target < 1.5ms GPU at 1080p on RTX 3060.
- [ ] 2.3.2 Profile compute dispatch times (inject, scatter, composite separately).

### 2.4 Coverage
- [ ] 2.4.1 Verify 95%+ coverage on all new C++ code.

## 3. Documentation
- [ ] 3.1 Update `docs/specs/rendering/renderer.md` with volumetric fog pass pipeline description.
- [ ] 3.2 Update `docs/specs/rendering/shader-library.md` with froxel fog shader functions.
- [ ] 3.3 Add `@ref Hillaire 2015` citations to all shader headers.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
