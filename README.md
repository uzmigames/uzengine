# UzEngine

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)]()
[![Zig](https://img.shields.io/badge/Zig-0.15-orange.svg)]()
[![MSVC](https://img.shields.io/badge/MSVC-17.14-blue.svg)]()
[![Clang](https://img.shields.io/badge/Clang-21-green.svg)]()
[![VSCode](https://img.shields.io/badge/VSCode-1.85+-purple.svg)]()
[![Electron](https://img.shields.io/badge/Electron-39.6.0-teal.svg)]()

AAA-quality game engine embedded in VSCode, with open file formats designed for LLM manipulation. **The first engine built for autonomous AI-driven development.**

## Key Features

- **AAA Visual Quality** — rendering math and techniques based on proven engine architectures (clean-room reimplementation)
- **Embedded in VSCode** — native N-API addon loaded in-process, no separate process or IPC
- **Open Formats** — all scene/asset files are JSON, designed for LLM manipulation
- **Multi-Backend RHI** — DX12 SM6.6 primary, DX12 SM5 / DX11 / Vulkan / Metal fallbacks
- **Multi-Compiler** — Zig, MSVC, and Clang all verified on Windows
- **No GC** — deterministic memory via Rust-inspired ownership and borrow checking in C++
- **AI-First** — MCP server integration for autonomous development workflows

## Rendering Features (v0.6.0)

| System | Features |
|--------|----------|
| **PBR** | GGX + Burley diffuse + SmithJoint + multiscatter energy compensation, anisotropic GGX, sheen (Estevez & Kulla 2017), 5 metal IOR presets |
| **Shadows** | 4-cascade CSM, PCSS blocker search, ESM, contact shadows, distance field shadows, static shadow cache |
| **Lighting** | Directional + point (64) + spot (16) lights, IBL (SH L2 + env map importance sampling + prefiltered specular + VNDF), clustered shading |
| **Atmosphere** | Bruneton sky, multi-scatter LUT, ozone, aerial perspective, height fog (2-layer + sky injection), volumetric clouds (UE5 parity), god rays |
| **GI** | SSGI (5 compute passes: trace, spatial denoise, temporal accum, bilateral upsample, prev color reduce), SVGF denoiser (3 passes) |
| **Post-Process** | TAA (variance clipping), SSR (stochastic multi-ray), GTAO (multi-bounce + bent normals), bloom (6-mip Karis smooth knee), FilmToneMap ACES, auto-exposure, DOF (near/far/sharp layers), motion blur |
| **Anti-Aliasing** | TAA, SMAA (1x/T2x/S2x/4x), FXAA |
| **Upscaling** | FSR 1.0 (spatial), FSR 2.x (temporal with motion vectors) |
| **Film Effects** | Film grain, vignette, chromatic aberration, color grading LUT (32x32x32), white balance (Bradford adaptation) |
| **Quality** | 5-tier scalability (Low/Medium/High/Ultra/Cinematic) with per-group controls (shadows, textures, effects, GI, screen, view distance, post-process) |
| **VFX** | GPU compute particle system (65K/emitter), collision, flipbook SubUV, trails, decals, attractor forces, vector fields, sub-emitters |
| **Raytracing** | DXR infrastructure (BLAS/TLAS managers, shader table builder, RT pipeline) |
| **Camera** | CameraManager with push/pop stack, editor free-cam (F5), per-camera render targets, multi-camera switching |
| **Diagnostics** | FLIP perceptual metric, pass dump, pixel debug, per-pass GPU profiling, GPU validation, DRED breadcrumbs |
| **Backends** | DX12 SM6.8 (bindless), DX12 SM5, DX11, Vulkan |

## Monorepo Structure

| Package | Description | Build |
|---------|------------|-------|
| [`runtime/`](runtime/) | Engine core (C++23, 145 shaders) | Zig / CMake (MSVC, Clang) |
| [`sample/`](sample/) | 22 demo scenes with Lua scripts | `.uzproject` / `.uzscene` |
| [`napi/`](napi/) | N-API bridge to Electron | cmake-js / MSVC |
| [`extension/`](extension/) | VSCode extension | TypeScript |
| [`mcp/`](mcp/) | MCP server for AI workflows | TypeScript |

## Quick Start

### Prerequisites

- [Zig 0.15+](https://ziglang.org/download/)
- [Node.js 22+](https://nodejs.org/)
- [Visual Studio Build Tools 2022](https://visualstudio.microsoft.com/downloads/) (C++ workload)
- [CMake 3.21+](https://cmake.org/) (for MSVC/Clang builds)

### Setup

```bash
scripts/setup.ps1    # PowerShell
scripts/setup.bat    # CMD
```

### Build

All operations go through `scripts/`. The `--compiler` flag selects the toolchain.

**Complete pipeline** (all steps handled automatically):

```bash
# Default (Zig + cmake-js + TypeScript)
scripts/build.ps1

# MSVC (static lib for all components)
scripts/build.ps1 --compiler=msvc

# Clang
scripts/build.ps1 --compiler=clang

# CMD equivalent
scripts/build.bat
scripts/build.bat --compiler=msvc
```

**Manual step-by-step** (for development):

```bash
# 1. Build runtime as DLL + static lib
cd runtime && zig build -Ddll=true

# 2. Compile N-API addon (MSVC links DLL import lib)
cd napi && npx cmake-js compile --runtime=electron --runtime-version=39.6.0

# 3. Deploy addon + DLLs to extension
cp napi/build/Release/uzengine.node extension/native/
cp napi/build/Release/*.dll extension/native/
mkdir -p extension/native/D3D12
cp napi/build/Release/D3D12/*.dll extension/native/D3D12/

# 4. Build extension TypeScript
cd extension && npx tsc
```

> The build scripts check for locked files (e.g. `.node` loaded by VSCode) before building.
> If a lock is detected, close the Extension Host or run **Developer: Reload Window** first.

### Test

```bash
scripts/tools/test.ps1                         # Zig (default)
scripts/tools/test.ps1 --compiler=msvc         # MSVC
scripts/tools/test.ps1 --compiler=clang        # Clang
scripts/tools/test.ps1 --coverage              # With coverage (TS modules)
```

### Run

Press **F5** in VSCode to launch the Extension Development Host, or:

```bash
npm run build:debug
```

## Architecture

### DLL-Based N-API Bridge

UzEngine uses a **DLL-based architecture** for ABI compatibility with MSVC:

```
Runtime (Zig)  ──→  uzengine_runtime.dll (41MB)
                     + uzengine_runtime.lib (6KB, MSVC-compatible import lib)
                         │
                         └──→  napi/src/*.cpp (MSVC)
                              + uzengine.node (N-API addon, 162KB)
                                  │
                                  └──→  extension/src/extension.ts (VSCode extension)
```

**Why DLL?** Zig's static library format (LLVM archive) is incompatible with MSVC's `link.exe` (LNK1143 COMDAT error). By building the runtime as a DLL, we produce an MSVC-compatible import lib that cmake-js can link against.

**Benefits**:
- N-API addon compiled with MSVC (zero ABI surprises)
- Zig runtime + MSVC N-API bridge + TypeScript extension all work together
- Multi-compiler support (Zig produces the DLL, MSVC compiles the bridge, tsc compiles the UI)

### Build Pipeline

```
scripts/build.ps1 [--compiler=zig|msvc|clang]
  │
  ├─ 0. Pre-flight        check for locked .node / .lib files
  ├─ 1. build:runtime     zig build -Ddll=true
  │                       → uzengine_runtime.dll + import lib
  ├─ 2. build:napi        cmake-js compile
  │                       → napi/build/Release/uzengine.node
  │                       → napi/build/Release/*.dll (copies)
  │     └─ copy:artifacts → extension/native/
  ├─ 3. build:mcp         tsc → mcp/dist/
  └─ 4. build:extension   tsc → extension/out/extension.js
```

## Quality

| Tool | Runtime | N-API | Extension | MCP |
|------|---------|-------|-----------|-----|
| **Compiler** | Zig / MSVC / Clang | MSVC | tsc | tsc |
| **Lint** | clang-tidy | — | ESLint 9 | ESLint 9 |
| **Format** | clang-format | clang-format | Prettier 3.8 | Prettier 3.8 |
| **Test** | GoogleTest 1.15 | — | Vitest 4 | Vitest 4 |

```bash
scripts/tools/lint.ps1             # clang-tidy + eslint
scripts/tools/format.ps1           # clang-format + prettier
scripts/tools/format.ps1 --check   # CI mode (check only)
scripts/tools/test.ps1                   # zig test + vitest
scripts/tools/test.ps1 --compiler=msvc   # MSVC tests
```

- **Zero warnings** — all warnings promoted to errors across every module
- **95% coverage** — enforced via Vitest thresholds on branches, functions, lines, and statements
- **Structured logging** — `LogOutputChannel` (extension), `pino` (mcp), `spdlog` (runtime)

## Thirdparty Dependencies

All thirdparty libraries are **committed to the repo** under `runtime/thirdparty/`. See [`runtime/thirdparty/README.md`](runtime/thirdparty/README.md) for the full list with versions, licenses, and extracted files.

## Documentation

- [Architecture](docs/architecture.md) — system design, layers, execution model
- [Roadmap](docs/roadmap.md) — development phases and milestones
- [DAG](docs/dag.md) — milestone dependency graph
- [PDR](docs/pdr.md) — project design record
- [Specs](docs/specs/) — per-milestone specifications
- [Guides](docs/guides/) — implementation guides (build, shaders, materials, VFX, Lua, etc.)
- [Changelog](runtime/CHANGELOG.md) — detailed per-version changelog

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
