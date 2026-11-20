# NVIDIA NRD SDK adapter for the DXR path tracer

Status: implemented, opt-in, default off. Measured on an RTX 5080 (2026-11-10).
Baseline: `feat/phase1-lighting`, `03f6eaf6`. Rulebook task: `phase1_falcor-nrd-denoiser`.
Scope: the path-traced image only (`--path-trace`). The real-time raster signals keep the
engine-native backend decided in `unified-realtime-denoiser.md`; this adapter adds no
build-time dependency (the engine builds and runs identically without the SDK).

## 1. What it is

`NrdSdkDenoiser` (`runtime/engine/renderer/passes/pathtracing/nrd_sdk_denoiser.{h,cpp}`)
runs NVIDIA NRD v4.17.3 (`REBLUR_DIFFUSE` + `RELAX_SPECULAR`) on the stage-2 signal
decomposition that the native `NrdDenoiser` already produces (demodulated primary diffuse,
demodulated primary specular, untouched residual = emission + background + hair). The native
stages (`r.NRD=1`) stay the default denoiser; the SDK replaces them only with `r.NRD.SDK=1`.

```
pathtracer (nrdEnabled)  ->  raw, diffuse, albedo, specular, F0 modulation, virtual position, guide
NRD_SDK_Pack      (engine compute, bindless)   -> IN_MV, IN_NORMAL_ROUGHNESS, IN_VIEWZ,
                                                  IN_DIFF_RADIANCE_HITDIST, IN_SPEC_RADIANCE_HITDIST
NRD_SDK_Denoise   (nrd::GetComputeDispatches on the native DX12 command list)
                                               -> OUT_DIFF_RADIANCE_HITDIST, OUT_SPEC_RADIANCE_HITDIST
NRD_SDK_Modulate  (engine compute, bindless)   -> diffuse*albedo + specular*F0 + residual
```

## 2. Provisioning and build

- `scripts/setup.ps1` clones `https://github.com/NVIDIA-RTX/NRD.git` at tag `v4.17.3`
  (commit `792eff196afdd350fd9c3f862119017ccb438a0e`) into `runtime/thirdparty/nrd/`
  (gitignored) and runs `scripts/build_nrd.ps1`.
- `scripts/build_nrd.ps1` builds `NRD.dll` + `NRD.lib` with CMake and MSVC 2022:
  DXIL only, shaders compiled by the engine's pinned DXC (`runtime/thirdparty/dxc`),
  `NRD_NORMAL_ENCODING=4` (RGBA16_SNORM; NRD accepts RGBA16_SFLOAT storage),
  `NRD_ROUGHNESS_ENCODING=1` (linear), static CRT (the DLL imports only kernel32),
  `SHADERMAKE_TOOL=OFF` (ShaderMake builds in-solution; the nested ExternalProject build
  dies silently under a parent MSBuild). 159 DXIL blobs are embedded in the DLL.
  NRD's CMake downloads two pinned archives (ShaderMake commit, MathLib v11) while configuring.
- `runtime/build.zig`: `has_nrd` (headers + DLL present, Windows, no locked `native-deps`
  profile) adds `thirdparty/nrd/Include`, defines `UZ_NRD_SDK_ENABLED=1` and deploys
  `NRD.dll` next to the executables. Nothing is linked: the Zig/mingw build cannot link
  MSVC static libraries, so, like DLSS and Brixelizer, `nrd_sdk_denoiser.cpp` loads the DLL
  with `LoadLibrary` and resolves its nine `extern "C"` exports. Without the SDK tree the
  adapter compiles to a stub whose `Init` logs and returns false.

## 3. Runtime contract

- `Init(device, width, height)`: loads NRD.dll, verifies the library version (4.17) and
  encodings against `nrd::GetLibraryDesc()`, creates the instance, allocates the permanent
  and transient pools as committed D3D12 resources (NRD asks for `R8_UINT`/`R16_UINT`
  formats `RHIFormat` does not expose), creates the user-side RHI textures
  (motion `RG16Float`, normal+roughness `RGBA16Float`, viewZ `R32Float`, four
  `RGBA16Float` signal textures), one root signature + PSO per `nrd::PipelineDesc`
  from the embedded DXIL (root CBV, SRV table, UAV table, two static samplers in NRD's
  register spaces), a 3-frame shader-visible descriptor ring and a 3-frame 256-byte-aligned
  constant ring. Logs the pool counts: 15 permanent + 7 transient textures, 27 pipelines,
  51 dispatch sets, 864-byte constants at 640x360.
- `Denoise(builder, inputs)`: three render-graph passes. The pack pass reconstructs the
  primary hit from the DXR guide depth (`inverseViewProj`), writes `viewZ = (worldToView *
  P).z`, `mv = -velocity` (NRD: `uvPrev = uv + mv * 1/extent`), NRD's RGBA16 normal/roughness
  packing, YCoCg diffuse radiance with `REBLUR_FrontEnd_GetNormHitDist` (hit distance =
  distance from the primary hit to the unfolded virtual point, 10000 units on an environment
  miss) and linear specular radiance with the raw hit distance. The denoise pass fills
  `nrd::CommonSettings` (the engine's row-vector `Mat4` is byte-compatible with NRD's
  column-major `float[16]`), `ReblurSettings`/`RelaxSettings`, then executes every
  `nrd::DispatchDesc`: per-resource state tracking with transition/UAV barriers exactly like
  `NRDIntegration.hpp`, descriptors written into the ring, constants uploaded when
  `constantBufferDataMatchesPreviousDispatch` is false. User textures are restored to the
  render-graph states afterwards and `DX12CommandList::RebindBindlessHeaps()` restores the
  engine heaps. The path tracer's own history reset drives `AccumulationMode::RESTART`
  (`CLEAR_AND_RESTART` on the first frame).
- Units: the engine uses centimeters; `r.NRD.SDK.UnitsPerMeter` (100) scales NRD's metric
  hit-distance normalization (`A = 3 m`). `denoisingRange` = camera far plane; sky pixels get
  `viewZ = 4 * farPlane` so NRD skips them and the modulate pass keeps the raw background.

## 4. Controls (all default off / NRD defaults unless stated)

| Control | Default | Meaning |
|---|---|---|
| `--denoiser nrd-sdk` | - | sets `r.NRD=1` and `r.NRD.SDK=1` |
| `r.NRD.SDK` | 0 | use the SDK instead of the native stages when `r.NRD=1` |
| `r.NRD.SDK.DiffuseFrames` / `SpecularFrames` | 30 / 30 | REBLUR / RELAX max accumulated frames |
| `r.NRD.SDK.FastFrames` | 6 | fast-history length |
| `r.NRD.SDK.ClampSigma` | 2 | fast-history clamping sigma |
| `r.NRD.SDK.AntiFirefly` | 0 (NRD: 1) | REBLUR anti-firefly once history exists; measured -4% MSE off |
| `r.NRD.SDK.AntiFireflyFrames` | 8 | frames after a reset with anti-firefly forced on (the 1-spp reset frame is firefly-dominated: MSE 106 / +68% without it, 21 / +7% with it) |
| `r.NRD.SDK.DiffusePrepassRadius` | 0 (NRD: 30) | REBLUR pre-pass radius |
| `r.NRD.SDK.SpecularPrepassRadius` | 0 (NRD: 50) | RELAX pre-pass radius; any non-zero radius loses ~14% specular energy on sparse emitter reflections |
| `r.NRD.SDK.SpecularPhiLuminance` / `SpecularMinLuminanceWeight` | 1 / 0 | RELAX A-trous luminance edge stopping |
| `r.NRD.SDK.UnitsPerMeter` | 100 | metric scaling |
| `r.NRD.SDK.SplitScreen` | 0 | NRD's noisy/denoised split |
| `r.NRD.SDK.DebugSignal` | 0 | 1 raw, 2 residual, 3 specular, 4 diffuse, 5 denoised diffuse, 6 denoised specular |

NRD's pre-passes exist for probabilistic lobe selection; the stage-2 attribution feeds both
lobes every sample, so they are off by default (measured, see below).

## 5. Evidence

`python scripts/rendering/test_nrd_sdk.py --out <dir inside the repo>` (every capture has a
180-second bound; the output must live inside the repository because the VFS rejects scene
paths outside the content root). PNG display-code metrics at 640x360 against a 1024-frame
converged capture of the emitter-lit stage-2 fixture, frame 32:

| Image | full-frame MSE | mean bias | glossy-floor ROI MSE |
|---|---|---|---|
| raw 1 spp | 206.94 | -2.7% | 184.45 |
| native stage 2 | 5.63 | -3.7% | 0.55 |
| NRD SDK (defaults above) | 6.40 | -4.2% | 0.44 |
| NRD SDK with NRD's own defaults | 7.48 | -8.0% | 2.03 |

MSE falls 96.9% versus the raw input; the reset frame (f4) is 21.1 (stage 2: 18.0) and f8 is
7.3. Frame-to-frame change f28 to f32: MAE 0.20 (stage 2: 0.32). After the scripted
100-unit camera step the frame matches the new pose (full MSE 21.9) and not the old one
(275), i.e. no ghosting. The raw path-traced frame and the native-NRD frame rendered by this
build are byte-identical to the pristine `03f6eaf6` build (same PNG SHA-256); the raster
default frame is not deterministic run to run (same-exe deltas of 13 to 674 pixels), and the
patched build sits inside that envelope (9 to 63 pixels against HEAD) once a fresh shader
cache is used.

## 6. Known limits

- DX12 only (native D3D12 dispatch like the DLSS/Brixelizer integrations).
- Hit distance is the engine's stage-2 secondary segment length; paths that terminate at the
  primary vertex report 0 ("no data" for NRD).
- `IN_DIFF_CONFIDENCE`, `IN_DISOCCLUSION_THRESHOLD_MIX` and `OUT_VALIDATION` are not wired.
