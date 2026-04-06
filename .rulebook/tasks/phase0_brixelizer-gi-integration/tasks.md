## 1. FFX SDK Build Integration

- [ ] 1.1 Add FFX SDK 1.1.4 include paths to `build.zig` — `sdk/include/`, `sdk/include/FidelityFX/host/`, `sdk/include/FidelityFX/gpu/`.
- [ ] 1.2 Compile `ffx_dx12.cpp` (DX12 backend) into the runtime static library.
- [ ] 1.3 Compile `ffx_brixelizer.cpp`, `ffx_brixelizer_raw.cpp`, `ffx_brixelizergi.cpp` into the runtime static library.
- [ ] 1.4 Pre-compile 50 Brixelizer + BrixelizerGI HLSL shaders via DXC into shader blob headers (or runtime compile via ShaderManager).
- [ ] 1.5 Build + verify: `zig build` compiles with FFX SDK sources, no errors.

## 2. BrixelizerGIPass — Context Lifecycle

- [ ] 2.1 Create `brixelizer_gi_pass.h/.cpp` in `engine/renderer/gi/` — `BrixelizerGIPass` class with Init/Shutdown/Execute.
- [ ] 2.2 Implement `Init()` — create `FfxInterface` via `ffxGetInterfaceDX12()`, create `FfxBrixelizerContext` with 4 cascades (static+dynamic, voxel sizes 2/4/8/16 cm), create `FfxBrixelizerGIContext` with display size and `FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED` (reversed-Z).
- [ ] 2.3 Implement `Shutdown()` — destroy both contexts, free scratch memory.
- [ ] 2.4 Add `r.BrixelizerGI` CVar (default false) and `r.BrixelizerGI.Cascades` (default 4) to gate the feature.
- [ ] 2.5 Wire `BrixelizerGIPass` into `SceneRendererImpl` — create in `InitPasses()`, destroy in `ShutdownPasses()`.
- [ ] 2.6 Build + verify: BrixelizerGIPass creates/destroys contexts without GPU errors when `r.BrixelizerGI=true`.

## 3. Geometry Registration

- [ ] 3.1 Create geometry registration bridge — iterate `DrawList` commands, extract vertex buffer SRV, index buffer SRV, world transform, AABB per mesh instance.
- [ ] 3.2 Convert UzEngine mesh data to `FfxBrixelizerInstanceDescription` — map vertex stride, index format, transform matrix (row-major), instance flags (static/dynamic).
- [ ] 3.3 Call `ffxBrixelizerCreateInstances()` for new meshes and `ffxBrixelizerDeleteInstances()` for removed meshes. Track instance IDs per mesh.
- [ ] 3.4 Handle mesh LOD changes — when LOD switches, delete old instance and create new one with updated vertex/index buffers.
- [ ] 3.5 Build + verify: all Sponza meshes register as Brixelizer instances (log instance count).

## 4. Per-Frame SDF Bake + GI Dispatch

- [ ] 4.1 Implement `Execute()` — per-frame: (1) update geometry instances, (2) `ffxBrixelizerBakeUpdate()` to prepare SDF update, (3) `ffxBrixelizerUpdate()` to execute SDF build on command list.
- [ ] 4.2 Create output textures: `diffuseGI` (RGBA16F, display size) and `specularGI` (RGBA16F, display size). Register SRVs.
- [ ] 4.3 Populate `FfxBrixelizerGIDispatchDescription` — view/projection matrices, camera position, depth SRV, normal SRV, roughness SRV, motion vectors SRV, environment map SRV, previous frame lit output SRV, output diffuseGI/specularGI UAVs.
- [ ] 4.4 Call `ffxBrixelizerGIContextDispatch()` to execute GI compute passes.
- [ ] 4.5 Add resource barriers: transition GI outputs from UAV → SRV before deferred lighting.
- [ ] 4.6 Build + verify: `--pass-dump` shows non-black diffuseGI and specularGI targets.

## 5. GI Composite in Deferred Lighting

- [ ] 5.1 Add `brixelizerDiffuseGISrv` and `brixelizerSpecularGISrv` fields to `PerFrameParams`.
- [ ] 5.2 Populate SRV indices in `frame_constants.cpp` from `BrixelizerGIPass` output.
- [ ] 5.3 Add CB fields `_brixGIDiffuseSrv` and `_brixGISpecularSrv` to `deferred_lighting_ps.hlsl` PerFrameCB.
- [ ] 5.4 Sample Brixelizer diffuse GI in the IBL section — add to `diffuseAmbient` when SRV is valid. Scale by `r.BrixelizerGI.Intensity` CVar.
- [ ] 5.5 Sample Brixelizer specular GI — blend with IBL specular based on roughness. Gate by `FFX_BRIXELIZER_GI_FLAG_DISABLE_SPECULAR`.
- [ ] 5.6 Build + verify: Sponza with `r.BrixelizerGI=true` shows spatially-varying indirect lighting (windows bright, deep corridors dark). Screenshot comparison.

## 6. Resource Management + History

- [ ] 6.1 Manage history resources — previous frame depth, normals, lit output needed by Brixelizer GI temporal accumulation. Double-buffer or copy-on-use.
- [ ] 6.2 Create blue noise texture (64x64 R8) for Brixelizer GI stochastic sampling. Load from `runtime/content/textures/blue_noise_64.png` or generate procedurally.
- [ ] 6.3 Handle resize — destroy and recreate GI output textures and Brixelizer GI context on window resize.
- [ ] 6.4 Add `ffxBrixelizerGetRawContext()` bridge to pass raw context pointer from Brixelizer to BrixelizerGI (required by dispatch description).

## 7. Debug Visualization

- [ ] 7.1 Add `--vis brixelizer.diffuse` and `--vis brixelizer.specular` visualization targets.
- [ ] 7.2 Implement Brixelizer debug AABB visualization via `ffxBrixelizerGetDebugCounters()` — show SDF brick coverage as wireframe AABBs.
- [ ] 7.3 Add `r.BrixelizerGI.Debug` CVar (0=off, 1=radiance cache, 2=irradiance cache) using `FfxBrixelizerGIDebugMode`.

## 8. Testing

- [ ] 8.1 Unit test: BrixelizerGIPass lifecycle — create context, register 1 mesh instance, bake update, destroy. No leaks or GPU errors.
- [ ] 8.2 Unit test: geometry registration bridge — verify FfxBrixelizerInstanceDescription fields match DrawList mesh data (transform, AABB, vertex stride).
- [ ] 8.3 Integration test: Sponza scene with `r.BrixelizerGI=true` — render 200 frames, verify diffuseGI output is non-zero via `--diag` target inspection.
- [ ] 8.4 Visual test: Sponza A/B comparison — screenshot with/without BrixelizerGI, verify PSNR difference > 5 dB (GI adds visible indirect light).
- [ ] 8.5 Performance test: BrixelizerGI total frame overhead < 4ms at 1080p on RTX 4090.
- [ ] 8.6 Verify 95%+ coverage on all new code.

## 9. Documentation

- [ ] 9.1 Update `docs/specs/rendering/renderer.md` with Brixelizer GI pipeline stage (after shadow pass, before deferred lighting).
- [ ] 9.2 Update `docs/specs/rendering/ibl-pipeline.md` with Brixelizer GI composite description.
- [ ] 9.3 Create `docs/guides/brixelizer-gi-guide.md` — setup guide (CVars, cascade config, performance tuning, debug visualization).
- [ ] 9.4 Update `runtime/CHANGELOG.md` with Brixelizer GI feature addition.
