# Proposal: phase1_falcor-nrd-denoiser

## Why
Path-traced output is inherently noisy at low sample counts — at 1 sample per pixel (real-time budget), the noise is visually unacceptable without denoising. NVIDIA NRD (Real-time Denoisers) is a free-to-use SDK providing ReLAX (specular) and ReBLUR (diffuse) denoisers specifically optimized for low-SPP ray-traced input, using spatiotemporal filtering that preserves lighting frequency. Falcor uses NRD as the primary denoiser for all RT output. Without NRD, path-traced output is too noisy for real-time use even at 4 samples per pixel. Requires: DXR RT infrastructure + path tracer or ReSTIR DI.

## What Changes
- NRD SDK added to thirdparty via `setup.ps1`
- `NrdDenoiser` class: `Init(device, width, height, method)`, `Denoise(cmd, diffuse, specular, emission, hitDist, normalRoughness, viewZ, motionVectors) → DenoisedBuffers { diffuse, specular }`, `Destroy()`
- Illumination decomposition: path tracer output split into separate `diffuse`, `specular`, and `emission` channels (NRD requires separated input for optimal filtering). `ModulateIllumination` pass recombines after denoising.
- NRD method configuration: `nrd::Method::RELAX_SPECULAR` for specular channel, `nrd::Method::REBLUR_DIFFUSE` for diffuse channel. Both run in parallel.
- Per-frame denoise: `nrd::SetMethodSettings` with camera data, `nrd::GetComputeDispatches` → execute each dispatch as a compute pass via RHI
- Auxiliary buffers: normal+roughness (from GBuffer), view-space Z (from depth), motion vectors (screen-space) — all required inputs for temporal tracking
- `ModulateIllumination` compute pass: `uResult = diffuseDenoised * albedo + specularDenoised + emission`
- Affected files: `setup.ps1`, new `nrd_denoiser.h/.cpp`, new `modulate_illumination_cs.hlsl`, `path_tracer_pass.cpp`

## Impact
- Affected specs: new `docs/specs/rendering/nrd-denoiser.md`
- Affected code: new `nrd_denoiser.h/.cpp`, `modulate_illumination_cs.hlsl`, `path_tracer_pass.cpp`, `setup.ps1`
- Breaking change: NO — NRD is opt-in via `--denoiser nrd`; rasterized path and self-implemented SVGF are unchanged
- User benefit: Path-traced output becomes viewable in real time at 1 SPP; specular/diffuse denoising preserves frequency better than SVGF; enables hybrid rasterize+denoise workflows for production use
