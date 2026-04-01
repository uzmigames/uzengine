## Session 1: NRD SDK Integration + NrdDenoiser Class

- [ ] 1.1 Add NRD SDK to `scripts/setup.ps1` — `Clone-Repo` or `Download-Header` for NRD headers (`NRD.h`, `NRDDescs.h`, `NRDSettings.h`), static library, and HLSL include files. Store under `runtime/thirdparty/nrd/`. Do NOT run the script — user runs manually.
- [ ] 1.2 Create `nrd_denoiser.h` — `NrdDenoiser` class: `bool Init(device, width, height)`, `DenoisedBuffers Denoise(cmd, DenoiserInputs inputs)`, `void SetSettings(NrdSettings)`, `void Destroy()`. `DenoisedBuffers { RhiTextureHandle diffuse; RhiTextureHandle specular; }`. `DenoiserInputs { diffuseSRV, specularSRV, emissionSRV, hitDistSRV, normalRoughnessSRV, viewZSRV, motionVectorsSRV, viewMatrix, projMatrix, frameIndex }`.
- [ ] 1.3 Implement `NrdDenoiser::Init` — call `nrd::CreateDenoiser` with `nrd::DenoiserDesc` configuring `RELAX_SPECULAR` and `REBLUR_DIFFUSE` methods at target resolution. Allocate NRD-managed textures via `nrd::GetTextureDescs` (internal history buffers).
- [ ] 1.4 NRD resource translation: map `nrd::TextureDesc` (format, usage) to `RhiTextureHandle` via `rhi_create_texture`. Store in `std::vector<RhiTextureHandle> nrdTextures`.
- [ ] 1.5 Build + verify: NRD SDK links cleanly. `NrdDenoiser::Init` runs without crash. Log internal texture count via `UZ_LOG`.

## Session 2: Per-Frame Denoise + Illumination Decomposition + Demo

- [ ] 2.1 Illumination decomposition in path tracer: split `PathPayload.radiance` at each hit into `diffuseContrib = kd * diffuse_lighting` and `specularContrib = ks * specular_lighting`. Write to separate `uDiffuse` and `uSpecular` UAVs. Emission stored separately in `uEmission`.
- [ ] 2.2 Implement `NrdDenoiser::Denoise` — call `nrd::SetMethodSettings` with camera matrices (view, proj, prev-view, prev-proj) and frame index. Call `nrd::GetComputeDispatches` to obtain list of compute dispatches. For each dispatch: bind NRD's shader, bind input/output textures via RHI, set push constants, call `rhi_dispatch`.
- [ ] 2.3 NRD shader binding: NRD provides pre-compiled DXIL blobs; load them via `rhi_create_compute_pipeline_from_bytecode`. Bind textures by the indices specified in `nrd::DispatchDesc::resourceRanges`.
- [ ] 2.4 Create `modulate_illumination_cs.hlsl` — `[numthreads(8,8,1)]` `main`: `float3 result = tDiffuseDenoised * tAlbedo + tSpecularDenoised + tEmission`. Write to `uHdr`. Wire into render graph after NRD passes, before tone mapping.
- [ ] 2.5 CLI: `--denoiser nrd` activates `NrdDenoiser`. Build + verify on RTX hardware: `demo.exe --path-trace --denoiser nrd --screenshot-stdout --frames 5` produces a clearly denoised image at 1 SPP. Compare first frame (noisy) vs denoised via `--flip`; expect FLIP score improvement > 50%.
