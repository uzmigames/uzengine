## 1. Compute Shader Infrastructure (RHI)

- [x] 1.1 Implement `compute_pipeline_create` in `rhi_dx12.c` — dedicated compute root signature (b0 root constants, t0+t1 SRV, u0+u1 UAV, s0 static sampler), D3DCompile cs_5_0, `D3D12_COMPUTE_PIPELINE_STATE_DESC`
- [x] 1.2 Add `cmd_set_uav(cmd, slot, tex, mip_level)` to RHI vtable — per-mip UAV binding
- [x] 1.3 Per-mip UAV descriptor allocation via bump allocator in CBV/SRV/UAV heap (region 4096+), stored as `uav_heap_base` + `uav_mip_count` in `Dx12TextureSlot`
- [x] 1.4 Compute vs graphics routing — `is_compute_bound` flag routes `SetComputeRoot*` vs `SetGraphicsRoot*` for root constants, textures, and UAVs
- [x] 1.5 `DX12_CBV_SRV_UAV_HEAP_SIZE` expanded from 4096 to 8192 (SRV 0..4095, UAV 4096+)

## 2. IBL Compute Shaders

- [x] 2.1 `ibl_brdf_lut.cs.hlsl` — 256x256 RG16F BRDF integration LUT, 1024 Hammersley samples, GGX importance sampling, `[numthreads(8,8,1)]`
- [x] 2.2 `ibl_irradiance.cs.hlsl` — cosine-weighted irradiance cubemap convolution, 1024 hemisphere samples, `RWTexture2DArray` output (6 faces), `TextureCube` input
- [x] 2.3 `ibl_prefilter.cs.hlsl` — GGX importance-sampled prefiltered env map, 256 samples/texel, per-mip dispatch with roughness via root constants, mip-level LOD selection (Karis 2013)

## 3. IblBaker Pipeline

- [x] 3.1 `IblBaker` class (`ibl_baker.h/cpp`) — init() creates 3 compute PSOs from embedded shader headers, shutdown() destroys them
- [x] 3.2 `bake_brdf_lut()` — dispatches BRDF LUT compute, returns 256x256 RG16F texture (environment-independent, call once)
- [x] 3.3 `bake(env_cubemap, env_size)` — dispatches irradiance (32x32x6) + prefiltered (128x128x6, 5 mips) from source env cubemap, returns `IblBakeResult`
- [x] 3.4 Full per-mip prefilter dispatch loop — roughness linearly interpolated from 0.0 (mip 0) to 1.0 (mip 4), per-mip UAV binding

## 4. Renderer Integration

- [x] 4.1 `IblBaker` member added to `Renderer`, initialized in `init()`
- [x] 4.2 BRDF LUT generated via GPU compute (`iblBaker_.bake_brdf_lut()`) with CPU fallback
- [x] 4.3 Init path: CPU generates env cubemap, GPU compute convolves irradiance + prefiltered, CPU-convolved results destroyed
- [x] 4.4 Sky-dirty update path: regenerates env cubemap + GPU compute convolution when SkyAtmosphere params change
- [x] 4.5 Scene-load path: same GPU compute + CPU fallback pattern
- [x] 4.6 `envCubemap_` stored for future rebakes, properly cleaned up in `shutdown()`

## 5. Documentation

- [x] 5.1 `renderer.md` IBL section updated — documents hybrid pipeline (CPU env cubemap + GPU compute convolution), compute root signature layout, per-mip UAV strategy

## Deferred

- [ ] D1. GPU-side procedural sky cubemap generation — currently env cubemap is CPU-generated in `sky_cubemap.cpp`, then GPU convolves. Full GPU path would generate the procedural sky gradient in a compute shader too.
- [ ] D2. `.uzenv` asset format for offline IBL baking — serialize baked cubemaps to disk (DDS BC6H) for reuse without runtime bake
- [ ] D3. Equirectangular HDR → cubemap conversion compute shader — for loading `.hdr`/`.exr` environment maps from disk
- [ ] D4. Scene-driven IBL source selection — `[environment]` section in `.uzscene` with `ibl = "environments/studio.uzenv"` path
