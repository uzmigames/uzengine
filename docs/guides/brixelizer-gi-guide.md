<!-- doc-meta
type: guide
category: rendering/gi
title: Brixelizer GI Setup Guide
created: 2026-04-06
updated: 2026-10-08
status: deprecated
authors: UzEngine Core Team
references: AMD 2024 FidelityFX Brixelizer GI
-->

# Brixelizer GI Setup Guide (dormant)

> **Status (2026-10-08): dormant. This guide does not work as written.** Brixelizer GI has not run
> in UzEngine since 2026-04-07, and `phase0_brixelizer-gi-integration` was closed as superseded on
> 2026-10-08. The GI that runs is the SDF radiance cache with the SDF final gather and SDF
> reflections, and DDGI as the DXR option (`docs/specs/rendering/gi-system.md`,
> `docs/engine/12-rendering/33-sdf-gi-final-gather.md`).
>
> **How the build actually detects it.** `runtime/build.zig` defines `UZ_BRIXELIZER_ENABLED`
> (`:929-931`) only when both the SDK header
> `runtime/thirdparty/amd-fidelityfx-1.1/sdk/include/FidelityFX/host/ffx_brixelizer.h` (`:879-888`)
> and the MSVC-built wrapper `runtime/thirdparty/amd-fidelityfx-1.1/wrapper/build/bin/uz_brixelizer.dll`
> (`:903-911`) exist. It does not look for `ffx_backend_dx12_x64.lib` (section 1 below is wrong on
> this point, and the SDK writes its libraries to `sdk/bin/ffx_sdk/`, not `sdk/build/lib/Release/`),
> and it never links the FFX libraries: the engine loads the wrapper at run time and resolves its 18
> `uz_brix_*` exports (`runtime/engine/renderer/passes/gi/brixelizer_loader.cpp:93-126`). Nothing in
> the repo builds the wrapper (`scripts/setup.ps1:543-549` only clones SDK v1.1.4), so every build
> compiles the no-op stub, and none of the CVars below exists.
>
> **Why `r.BrixelizerGI 1` does nothing.** Besides the CVar not being registered, the pass is never
> constructed (`runtime/engine/renderer/scene/scene_renderer_init.cpp:666-674`, commented out since
> `c84a8bce` on 2026-04-10, because `ffxBrixelizerContextCreate` failed with
> `FFX_ERROR_BACKEND_API_ERROR` on non-AMD GPUs) and never dispatched
> (`runtime/engine/renderer/frame/frame_gi_passes.cpp:387` returns first since `29c83ba5`,
> 2026-04-07). Even in a build with the DLL, `r.BrixelizerGI.Intensity` and `r.BrixelizerGI.Debug`
> have no reader, and the `brixelizer.*` inspector targets register only for an initialised pass.
>
> **What reviving it would take** (a new task, not this guide): build the SDK 1.1.4 static libraries
> with MSVC/CMake (they land in `sdk/bin/ffx_sdk/`, `sdk/CMakeLists.txt:122-124`, where
> `wrapper/CMakeLists.txt:34-43` expects them); build the wrapper (`wrapper/CMakeLists.txt:9-11`),
> which writes `wrapper/build/bin/uz_brixelizer.dll`, and copy it next to `uzengine.exe` (no build
> step does); restore the construction and the pass registration, then rebuild; diagnose the
> context-creation failure with the D3D12 debug layer; fix the wiring defects listed in
> `.rulebook/tasks/phase0_brixelizer-gi-integration/tasks.md` (vertex/index buffer pairing,
> world-space AABBs, camera matrices, G-buffer normal and roughness inputs, cube-map type, history
> and noise inputs, render-graph declarations, specular composite); and gate the result with region
> dumps and per-pass `gpu_ms` against the current GI.
>
> The rest of this page is the original 2026-04-06 text, kept for reference; dated markers flag the
> statements found wrong.

AMD FidelityFX Brixelizer GI provides production-quality real-time global illumination
via sparse SDF voxelization and screen-space probes. It outputs both diffuse and specular
indirect lighting with a built-in denoiser. Works on any DX12 GPU without DXR hardware.

## Prerequisites

### 1. Build the FFX SDK

The FFX SDK 1.1.4 must be pre-built with CMake/MSVC to produce the backend library.
This is a one-time step.

```bash
cd runtime/thirdparty/amd-fidelityfx-1.1/sdk
cmake -B build -G "Visual Studio 17 2022" -A x64 -DFFX_API_BACKEND=DX12_X64 -DFFX_ALL=ON
cmake --build build --config Release
```

This produces `sdk/build/lib/Release/ffx_backend_dx12_x64.lib`. The Zig build system
auto-detects this lib and defines `UZ_BRIXELIZER_ENABLED=1`.

> *2026-10-08: wrong. The Zig build checks for the wrapper DLL, not this lib; see the status banner.*

### 2. Rebuild UzEngine

```bash
cd runtime && zig build
```

If the FFX SDK .lib is found, you'll see no stub warning in the log. If the .lib is
missing, BrixelizerGIPass logs a warning and runs as a no-op.

## Enabling Brixelizer GI

> *2026-10-08: this does not work in any current build; see the status banner.*

Brixelizer GI is disabled by default. Enable it at runtime:

```
r.BrixelizerGI 1
```

Or via command line:

```bash
uzengine.exe --cvar r.BrixelizerGI=1 --project ./sample/projects/sponza.uzproject --editor
```

## CVar Reference

| CVar | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| `r.BrixelizerGI` | bool | false | — | Master toggle |
| `r.BrixelizerGI.Cascades` | int | 4 | 2-8 | Number of SDF voxel cascades |
| `r.BrixelizerGI.Intensity` | float | 1.0 | 0-10 | GI intensity multiplier |
| `r.BrixelizerGI.VoxelSize` | float | 2.0 | 0.5-32 | Finest cascade voxel size (cm) |
| `r.BrixelizerGI.RayPushoff` | float | 0.5 | 0.01-10 | Surface offset for diffuse rays (cm) |
| `r.BrixelizerGI.SdfSolveEps` | float | 0.5 | 0.01-10 | SDF ray march epsilon (cm) |
| `r.BrixelizerGI.MaxReferences` | int | 32768 | 1K-256K | Max triangle references per cascade update |
| `r.BrixelizerGI.MaxBricksPerBake` | int | 16384 | 1K-64K | Max SDF bricks per update |
| `r.BrixelizerGI.TriangleSwapSize` | int | 64MB | 1MB-256MB | Triangle swap space (bytes) |
| `r.BrixelizerGI.Debug` | int | 0 | 0-2 | Debug viz: 0=off, 1=radiance, 2=irradiance |

## Cascade Configuration

Cascades use doubling voxel sizes starting from `r.BrixelizerGI.VoxelSize`:

| Cascade | Voxel Size (default) | Coverage Radius |
|---------|---------------------|-----------------|
| 0 | 2 cm | ~2.5 m |
| 1 | 4 cm | ~5 m |
| 2 | 8 cm | ~10 m |
| 3 | 16 cm | ~20 m |

> *2026-10-08: wrong. A cascade is 64 voxels per axis (`FFX_BRIXELIZER_CASCADE_RESOLUTION`,
> `sdk/include/FidelityFX/gpu/brixelizer/ffx_brixelizer_host_gpu_shared.h:30`), so the defaults span
> 1.28, 2.56, 5.12 and 10.24 m per axis (radius 0.64-5.12 m).*

More cascades = larger coverage but higher VRAM and compute cost. 4 cascades is
recommended for indoor scenes. Outdoor scenes may benefit from 6-8 cascades.

## Debug Visualization

```bash
# View diffuse GI output
uzengine.exe --vis brixelizer.diffuse --screenshot-stdout --frames 8 --cvar r.BrixelizerGI=1

# View specular GI output
uzengine.exe --vis brixelizer.specular --screenshot-stdout --frames 8 --cvar r.BrixelizerGI=1

# Radiance cache debug
uzengine.exe --cvar r.BrixelizerGI.Debug=1

# Irradiance cache debug
uzengine.exe --cvar r.BrixelizerGI.Debug=2
```

## Performance Tuning

**Target:** < 4ms total frame overhead at 1080p.

| Parameter | Effect on Performance |
|-----------|----------------------|
| Cascades ↓ | Fewer SDF builds per frame |
| VoxelSize ↑ | Coarser voxels, faster build |
| MaxReferences ↓ | Less voxelization work per frame |
| MaxBricksPerBake ↓ | Fewer SDF bricks updated per frame |

For 4K resolution, consider using `FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_50_PERCENT`
(half-resolution GI with built-in upsampling).

## Architecture

```
engine/renderer/passes/gi/
├── brixelizer_gi_pass.h    — Public API (PIMPL pattern)
└── brixelizer_gi_pass.cpp  — Implementation (#if UZ_BRIXELIZER_ENABLED) + no-op stub
```

### Pipeline Position

```
GBuffer → Shadows → GTAO → SSGI → SDF GI → Brixelizer GI → SSR → Deferred Lighting
```

Brixelizer GI runs as a render graph compute pass after the SDF-based radiance cache
and before screen-space reflections. Both GI systems can be active simultaneously.

### CB Integration

`brixGIDiffuseSrv` at offset 764 in PerFrameCB. Specular SRV = diffuse + 1 by
allocation convention. The deferred lighting shader samples both via bindless
`ResourceDescriptorHeap[]` access.

> *2026-10-08: wrong. The shader reads no specular (the read was removed in `29c83ba5`), the "+1"
> convention does not hold (SRVs and UAVs come from one allocator,
> `runtime/engine/rhi/dx12/dx12_texture_creation.cpp:195`, `:232`), and the diffuse composite would
> replace the diffuse ambient rather than add to it
> (`runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:1321`).*

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| "UZ_BRIXELIZER_ENABLED=0" in log | FFX SDK .lib not found | Build the FFX SDK (see Prerequisites) |
| Black GI output | No geometry registered | Geometry registration bridge not yet complete |
| GI flickers | Low temporal blend | Increase frame count or reduce ray pushoff |
| High GPU cost | Too many cascades/references | Reduce cascades, MaxReferences, MaxBricksPerBake |
