# Proposal: phase1_falcor-dlss-integration

## Why
UzEngine has `UpscalerType::DLSS` in its enum but zero implementation — the stub returns a blank texture. Falcor has native DLSS integration. DLSS (Deep Learning Super Sampling) is NVIDIA's AI-powered temporal upscaling that consistently outperforms TAA and FSR in perceptual quality, especially on RTX GPUs targeting 4K output at 1080p render resolution. For the large NVIDIA GPU install base this is the highest-quality upscaler available. Without a real implementation, players on RTX hardware are silently falling back to a broken path. DLSS SDK is free for commercial use with NVIDIA developer registration.

## What Changes
- DLSS SDK added to thirdparty via `setup.ps1` (`Download-Header`/`Clone-Repo` for `nvsdk_ngx_defs.h`, DLL, and import lib)
- `DlssUpscaler` class implementing `IUpscaler` interface: `Init(device, outputWidth, outputHeight, quality)`, `Evaluate(cmd, color, depth, motionVectors, exposure) → upscaledTexture`, `SetQualityMode(DlssQuality)`, `Destroy()`
- SDK initialization: `NVSDK_NGX_D3D12_Init(appId, path, device)`, `NVSDK_NGX_D3D12_CreateFeature` with `NVSDK_NGX_Feature_SuperSampling`
- Quality modes: `Performance` (0.5× res), `Balanced` (0.58×), `Quality` (0.67×), `UltraPerformance` (0.33×), `DLAA` (1× native, anti-aliasing only) — all 5 modes supported
- Per-frame evaluate: `NVSDK_NGX_D3D12_EvaluateFeature` with color, depth, motion vectors, exposure, jitter offset inputs
- Graceful fallback: if `NVSDK_NGX_Init` fails (non-NVIDIA GPU, driver too old), log warning and fall back to `FsrUpscaler` automatically
- CLI: `--upscaler dlss`, `--dlss-quality performance/balanced/quality/ultra/dlaa`
- Affected files: `setup.ps1`, new `dlss_upscaler.h/.cpp`, `upscaler_factory.cpp`, `application_args.cpp`

## Impact
- Affected specs: `docs/specs/screen-space-effects.md`
- Affected code: new `dlss_upscaler.h/.cpp`, `upscaler_factory.cpp`, `setup.ps1` (SDK fetch)
- Breaking change: NO — `DlssUpscaler` is a new implementation of the existing `IUpscaler` interface; existing TAA/FSR paths unchanged
- User benefit: RTX GPU users get the highest-quality upscaling available; resolves the silent stub fallback; DLAA mode provides best-in-class anti-aliasing at native resolution
