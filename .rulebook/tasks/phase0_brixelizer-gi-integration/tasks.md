## 1. FFX SDK Build Integration

- [x] 1.1 Add FFX SDK 1.1.4 include paths to `build.zig`. Added 8 include paths for host, gpu, backends, components. Conditional on header presence.
- [x] 1.2 FFX SDK CMake build completed — `cmake -B build -G "Visual Studio 17 2022" -A x64 -DFFX_API_BACKEND=DX12_X64 -DFFX_ALL=ON && cmake --build build --config Release`. Produces 22 .lib files in `sdk/bin/ffx_sdk/`.
- [x] 1.3 All FFX component .lib files built: `ffx_backend_dx12_x64.lib` (18MB), `ffx_brixelizer_x64.lib`, `ffx_brixelizergi_x64.lib`.
- [x] 1.4 50+ Brixelizer/BrixelizerGI HLSL shaders pre-compiled into shader blob headers by CMake/DXC.
- **BLOCKER:** FFX SDK .lib files depend on MSVC CRT (`libmsvcprt`, `libuuid`, `liboleaut32`) — incompatible with Zig's mingw libc. Linking requires MSVC CMake preset or DLL wrapper.
- [x] 1.5 Build + verify: `zig build` compiles with FFX SDK sources, no errors. Stub path compiles cleanly. Enabled path compiles but needs .lib for linking.

## 2. BrixelizerGIPass — Context Lifecycle

- [x] 2.1 Create `brixelizer_gi_pass.h/.cpp` in `engine/renderer/passes/gi/` — PIMPL pattern, full Init/Shutdown/Execute/OnResize/RegisterGeometry API.
- [x] 2.2 Implement `Init()` — dual FfxInterface backends (Brixelizer + GI), FfxBrixelizerContext with configurable cascades (CVar-driven voxel sizes), FfxBrixelizerGIContext with reversed-Z depth flag.
- [x] 2.3 Implement `Shutdown()` — destroy both contexts, WaitIdle, free scratch memory, destroy GPU resources.
- [x] 2.4 Add CVars: `r.BrixelizerGI` (bool), `r.BrixelizerGI.Cascades` (2-8), `r.BrixelizerGI.Intensity`, `r.BrixelizerGI.Debug`, `r.BrixelizerGI.VoxelSize`, `r.BrixelizerGI.RayPushoff`, `r.BrixelizerGI.SdfSolveEps`, `r.BrixelizerGI.MaxReferences`, `r.BrixelizerGI.MaxBricksPerBake`, `r.BrixelizerGI.TriangleSwapSize`.
- [x] 2.5 Wire `BrixelizerGIPass` into `SceneRendererImpl` — create in Init, destroy in Shutdown, resize in OnResize.
- [ ] 2.6 Build + verify: BrixelizerGIPass creates/destroys contexts without GPU errors when `r.BrixelizerGI=true`. Needs .lib linked.

## 3. Geometry Registration

- [ ] 3.1 Create geometry registration bridge — requires GPUMesh buffer registration with `ffxBrixelizerRegisterBuffers()`. DrawCommand lacks raw vertex/index buffer handles; need to bridge from GPUMesh pool.
- [ ] 3.2 Convert UzEngine mesh data to `FfxBrixelizerInstanceDescription` — map vertex stride, index format, transform matrix (row-major), instance flags (static/dynamic).
- [ ] 3.3 Call `ffxBrixelizerCreateInstances()` for new meshes and `ffxBrixelizerDeleteInstances()` for removed meshes. Track instance IDs per mesh. Scaffolding in place (dynamic delete + re-register pattern).
- [ ] 3.4 Handle mesh LOD changes — when LOD switches, delete old instance and create new one with updated vertex/index buffers.
- [ ] 3.5 Build + verify: all Sponza meshes register as Brixelizer instances (log instance count).

## 4. Per-Frame SDF Bake + GI Dispatch

- [x] 4.1 Implement `Execute()` — BrixelizerBakeUpdate + BrixelizerUpdate + BrixelizerGIContextDispatch pipeline. All CVar-driven parameters wired.
- [x] 4.2 Create output textures: `diffuseGI` (RGBA16F) and `specularGI` (RGBA16F) at display size. SRV + UAV indices allocated via GetTextureSRVIndex/GetTextureUAVIndex.
- [x] 4.3 Populate `FfxBrixelizerGIDispatchDescription` — matrices, camera, depth/normal/roughness/velocity/envmap/prevLit inputs via WrapTexture, normals unpack 2/-1, reversed-Z.
- [x] 4.4 Call `ffxBrixelizerGIContextDispatch()`. Gated by r.BrixelizerGI CVar.
- [ ] 4.5 Populate Brixelizer internal resources (sdfAtlas, brickAABBs, cascadeAABBTrees, cascadeBrickMaps) — requires ffx_backend_dx12.lib linked.
- [ ] 4.6 Build + verify: `--pass-dump` shows non-black diffuseGI and specularGI targets. Needs .lib linked.

## 5. GI Composite in Deferred Lighting

- [x] 5.1 Add `brixelizerGIDiffuseSrv` field to PerFrameCB and PerFrameParams. Specular SRV = diffuse+1 by allocation convention (saves CB space).
- [x] 5.2 Populate SRV index in `frame_constants.cpp` from BrixelizerGIPass::GetDiffuseGISrv().
- [x] 5.3 Add `brixGIDiffuseSrv` CB field to `deferred_lighting_ps.hlsl` PerFrameCB at offset 764.
- [x] 5.4 Sample Brixelizer diffuse GI in IBL section — adds to `diffuseAmbient` when SRV valid, gated by bindless.
- [x] 5.5 Sample Brixelizer specular GI — reads diffuseSrv+1, blends with metallicF0/roughness.
- [ ] 5.6 Build + verify: Sponza with `r.BrixelizerGI=true` shows spatially-varying indirect lighting. Needs .lib linked + geometry registration.

## 6. Resource Management + History

- [x] 6.1 History resources implemented — prevDepth (R32F) + prevNormals (RG16F) created in Init, destroyed in Shutdown.
- [x] 6.2 Blue noise texture — 64x64 R8 procedural R2 low-discrepancy sequence generated at Init.
- [x] 6.3 Resize handling — OnResize destroys/recreates GI context, output textures, history textures.
- [x] 6.4 `ffxBrixelizerGetRawContext()` call in Execute passes raw context pointer to GI dispatch.

## 7. Debug Visualization

- [x] 7.1 Registered `brixelizer.diffuse` and `brixelizer.specular` visualization targets with PassInspector.
- [ ] 7.2 Implement Brixelizer debug AABB visualization via `ffxBrixelizerGetDebugCounters()`. Needs .lib linked.
- [x] 7.3 Added `r.BrixelizerGI.Debug` CVar (0=off, 1=radiance cache, 2=irradiance cache).

## 8. Testing

- [ ] 8.1 Unit test: BrixelizerGIPass lifecycle — create context, register 1 mesh instance, bake update, destroy. No leaks or GPU errors.
- [ ] 8.2 Unit test: geometry registration bridge — verify FfxBrixelizerInstanceDescription fields match DrawList mesh data (transform, AABB, vertex stride).
- [ ] 8.3 Integration test: Sponza scene with `r.BrixelizerGI=true` — render 200 frames, verify diffuseGI output is non-zero via `--diag` target inspection.
- [ ] 8.4 Visual test: Sponza A/B comparison — screenshot with/without BrixelizerGI, verify PSNR difference > 5 dB (GI adds visible indirect light).
- [ ] 8.5 Performance test: BrixelizerGI total frame overhead < 4ms at 1080p on RTX 4090.
- [ ] 8.6 Verify 95%+ coverage on all new code.

## 9. Documentation

- [x] 9.1 Updated `docs/specs/rendering/gi-system.md` with Tier 4 Brixelizer GI section (architecture, CVars, build prereqs, relationship to Tiers 1-3).
- [x] 9.2 Updated `docs/specs/rendering/ibl-pipeline.md` with Brixelizer GI composite integration description.
- [x] 9.3 Created `docs/guides/brixelizer-gi-guide.md` — setup guide (CVars, cascade config, performance tuning, debug visualization, troubleshooting).
- [x] 9.4 Updated `runtime/CHANGELOG.md` with v0.9.0 Brixelizer GI feature addition.
