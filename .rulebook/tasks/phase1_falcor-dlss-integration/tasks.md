## Session 1: DLSS SDK Integration + DlssUpscaler Class

- [x] 1.1 Add DLSS SDK to `scripts/setup.ps1` — Clone-Repo for NVIDIA/DLSS from GitHub, stored under `runtime/thirdparty/dlss/`. Added to `.gitignore` to avoid committing proprietary SDK. Do NOT run the script — user must run manually.
- [x] 1.2 Create `dlss_upscaler.h` — `DlssUpscaler` class implementing `ITemporalUpscaler` with PIMPL pattern. `DlssQuality` enum: `UltraPerformance, Performance, Balanced, Quality, DLAA`. `IsAvailable()` query for graceful fallback detection.
- [x] 1.3 Implement `dlss_upscaler.cpp` — `Init`: calls `NVSDK_NGX_D3D12_Init` with application ID and `GetExeDirW()` for NGX path, queries optimal settings via `NGX_DLSS_GET_OPTIMAL_SETTINGS`, creates feature via `NGX_D3D12_CREATE_DLSS_EXT`. `UZ_DLSS_ENABLED` guard compiles as stub when SDK not present.
- [x] 1.4 Graceful fallback: all NGX calls wrapped in `NVSDK_NGX_FAILED()` result checking. If NGX init fails or DLSS not supported, logs warning via `UZ_LOG(Renderer, Warning, ...)` and returns false from `InitNgx()`. Factory returns DlssUpscaler which Init() will fail gracefully, falling back handled by caller.
- [x] 1.5 Add `DlssUpscaler` to `temporal_upscaler.cpp` factory — `CreateUpscaler(UpscalerType::DLSS)` instantiates `DlssUpscaler`. XeSS still falls back to TSR.
- [x] 1.6 Build + verify (without SDK): code compiles as stub (UZ_DLSS_ENABLED=0). build.zig auto-detects SDK presence via openDirAbsolute on thirdparty/dlss/include/. Added `--upscaler dlss` and `--dlss-quality` CLI args to application_args.cpp.

## Session 2: Per-Frame Evaluate + Quality Modes + Demo

- [x] 2.1 Implement `DlssUpscaler::Evaluate` — sets color (HDR), depth (reversed-Z), motion vectors (pixel units), exposure, jitter via NVSDK_NGX_Parameter::Set() virtual methods. Calls pfnEvaluateFeature(). Dynamic loading via LoadLibrary avoids MSVC CRT incompatibility.
- [x] 2.2 Motion vector format: verified velocity_ps.hlsl outputs RG16F in pixel units (not NDC). MV_Scale set to (1,1) — correct for DLSS.
- [x] 2.3 Quality mode reinitialization: Execute() detects quality change via cv_DLSSQuality CVar, calls DestroyFeature() + CreateFeature() with new settings. Render resolution updated from NGX optimal settings query.
- [x] 2.4 CLI args: `--upscaler dlss` maps to r.AntiAliasingMethod=4; `--dlss-quality <mode>` maps to r.DLSS.Quality (0=ultra, 1=perf, 2=balanced, 3=quality, 4=dlaa).
- [ ] 2.5 Build + verify on RTX hardware: `uzengine.exe --upscaler dlss --dlss-quality quality --screenshot-stdout --frames 30` — requires NVIDIA RTX GPU for runtime testing. Code compiles and runs as stub on non-NVIDIA hardware.
