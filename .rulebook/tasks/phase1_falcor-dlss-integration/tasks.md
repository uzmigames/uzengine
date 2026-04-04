## Session 1: DLSS SDK Integration + DlssUpscaler Class

- [x] 1.1 Add DLSS SDK to `scripts/setup.ps1` — Clone-Repo for NVIDIA/DLSS from GitHub, stored under `runtime/thirdparty/dlss/`. Added to `.gitignore` to avoid committing proprietary SDK. Do NOT run the script — user must run manually.
- [x] 1.2 Create `dlss_upscaler.h` — `DlssUpscaler` class implementing `ITemporalUpscaler` with PIMPL pattern. `DlssQuality` enum: `UltraPerformance, Performance, Balanced, Quality, DLAA`. `IsAvailable()` query for graceful fallback detection.
- [x] 1.3 Implement `dlss_upscaler.cpp` — `Init`: calls `NVSDK_NGX_D3D12_Init` with application ID and `GetExeDirW()` for NGX path, queries optimal settings via `NGX_DLSS_GET_OPTIMAL_SETTINGS`, creates feature via `NGX_D3D12_CREATE_DLSS_EXT`. `UZ_DLSS_ENABLED` guard compiles as stub when SDK not present.
- [x] 1.4 Graceful fallback: all NGX calls wrapped in `NVSDK_NGX_FAILED()` result checking. If NGX init fails or DLSS not supported, logs warning via `UZ_LOG(Renderer, Warning, ...)` and returns false from `InitNgx()`. Factory returns DlssUpscaler which Init() will fail gracefully, falling back handled by caller.
- [x] 1.5 Add `DlssUpscaler` to `temporal_upscaler.cpp` factory — `CreateUpscaler(UpscalerType::DLSS)` instantiates `DlssUpscaler`. XeSS still falls back to TSR.
- [x] 1.6 Build + verify (without SDK): code compiles as stub (UZ_DLSS_ENABLED=0). build.zig auto-detects SDK presence via openDirAbsolute on thirdparty/dlss/include/. Added `--upscaler dlss` and `--dlss-quality` CLI args to application_args.cpp.

## Session 2: Per-Frame Evaluate + Quality Modes + Demo

- [ ] 2.1 Implement `DlssUpscaler::Evaluate` — fill `NVSDK_NGX_D3D12_DLSS_Eval_Params`: color (HDR), depth (reversed-Z), motion vectors (screen-space, pixel units), exposure (1.0 for SDR), jitter offsets (from Halton sequence). Call `NVSDK_NGX_D3D12_EvaluateFeature`. Return output texture SRV.
- [ ] 2.2 Motion vector format: DLSS expects full-resolution motion vectors in pixel units (not normalized). Verify existing motion vector pass produces correct format; add conversion if needed.
- [ ] 2.3 `SetQualityMode` reinitialization: if quality mode changes, call `NVSDK_NGX_D3D12_ReleaseFeature` then recreate feature with new settings. Update render resolution accordingly.
- [ ] 2.4 CLI args in `application_args.cpp`: `--upscaler dlss` sets `UpscalerType::DLSS`; `--dlss-quality <mode>` sets `DlssQuality` (performance/balanced/quality/ultra/dlaa, case-insensitive)
- [ ] 2.5 Build + verify on RTX hardware: `demo.exe --upscaler dlss --dlss-quality quality --screenshot-stdout --frames 30` produces a correctly upscaled output. Run `--flip` comparing DLSS output vs native resolution reference — FLIP score should be under 0.05. Confirm `--stats` shows render resolution at 0.67× of output.
