## Session 1: DLSS SDK Integration + DlssUpscaler Class

- [ ] 1.1 Add DLSS SDK to `scripts/setup.ps1` — `Download-Header` for `nvsdk_ngx_defs.h`, `nvsdk_ngx.h`, `nvsdk_ngx_helpers_dx12.h`; download `nvngx_dlss.dll` and import library. Store under `runtime/thirdparty/dlss/`. Do NOT run the script — user must run manually.
- [ ] 1.2 Create `dlss_upscaler.h` — `DlssUpscaler` class implementing `IUpscaler`: `bool Init(ID3D12Device*, uint outputW, uint outputH, DlssQuality)`, `RhiTextureHandle Evaluate(cmd, colorSRV, depthSRV, motionVectorsSRV, exposure, jitterX, jitterY)`, `void SetQualityMode(DlssQuality)`, `void Destroy()`; `DlssQuality` enum: `Performance, Balanced, Quality, UltraPerformance, DLAA`
- [ ] 1.3 Implement `dlss_upscaler.cpp` — `Init`: call `NVSDK_NGX_D3D12_Init` with application ID and `GetExeDir()` for NGX path, query optimal settings via `NVSDK_NGX_D3D12_GetOptimalSettingsCallback`, call `NVSDK_NGX_D3D12_CreateFeature` with `NVSDK_NGX_Feature_SuperSampling` and quality parameters
- [ ] 1.4 Graceful fallback: wrap all NGX calls in `UZ_CHECK`-style result checking; if `NVSDK_NGX_Init` returns `NVSDK_NGX_Result_Fail_FeatureNotSupported`, log warning via `UZ_LOG(Renderer, Warning, "DLSS not supported, falling back to FSR")` and return false from `Init`
- [ ] 1.5 Add `DlssUpscaler` to `upscaler_factory.cpp` — `CreateUpscaler(UpscalerType::DLSS)` instantiates `DlssUpscaler`, calls `Init`, returns it or falls back to `FsrUpscaler` on failure
- [ ] 1.6 Build + verify (without running setup.ps1): code compiles with SDK headers present. On non-NVIDIA machine: fallback path triggered, FSR activates, no crash.

## Session 2: Per-Frame Evaluate + Quality Modes + Demo

- [ ] 2.1 Implement `DlssUpscaler::Evaluate` — fill `NVSDK_NGX_D3D12_DLSS_Eval_Params`: color (HDR), depth (reversed-Z), motion vectors (screen-space, pixel units), exposure (1.0 for SDR), jitter offsets (from Halton sequence). Call `NVSDK_NGX_D3D12_EvaluateFeature`. Return output texture SRV.
- [ ] 2.2 Motion vector format: DLSS expects full-resolution motion vectors in pixel units (not normalized). Verify existing motion vector pass produces correct format; add conversion if needed.
- [ ] 2.3 `SetQualityMode` reinitialization: if quality mode changes, call `NVSDK_NGX_D3D12_ReleaseFeature` then recreate feature with new settings. Update render resolution accordingly.
- [ ] 2.4 CLI args in `application_args.cpp`: `--upscaler dlss` sets `UpscalerType::DLSS`; `--dlss-quality <mode>` sets `DlssQuality` (performance/balanced/quality/ultra/dlaa, case-insensitive)
- [ ] 2.5 Build + verify on RTX hardware: `demo.exe --upscaler dlss --dlss-quality quality --screenshot-stdout --frames 30` produces a correctly upscaled output. Run `--flip` comparing DLSS output vs native resolution reference — FLIP score should be under 0.05. Confirm `--stats` shows render resolution at 0.67× of output.
