<!-- doc-meta
type: guide
category: rendering/gi
title: Brixelizer GI Setup Guide
created: 2026-04-06
updated: 2026-04-06
status: active
authors: UzEngine Core Team
references: AMD 2024 FidelityFX Brixelizer GI
-->

# Brixelizer GI Setup Guide

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

### 2. Rebuild UzEngine

```bash
cd runtime && zig build
```

If the FFX SDK .lib is found, you'll see no stub warning in the log. If the .lib is
missing, BrixelizerGIPass logs a warning and runs as a no-op.

## Enabling Brixelizer GI

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

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| "UZ_BRIXELIZER_ENABLED=0" in log | FFX SDK .lib not found | Build the FFX SDK (see Prerequisites) |
| Black GI output | No geometry registered | Geometry registration bridge not yet complete |
| GI flickers | Low temporal blend | Increase frame count or reduce ray pushoff |
| High GPU cost | Too many cascades/references | Reduce cascades, MaxReferences, MaxBricksPerBake |
