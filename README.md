# UzEngine

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
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

## Rendering Features (v0.2.49)

| System | Features |
|--------|----------|
| **PBR** | GGX + Burley diffuse + SmithJoint + multiscatter energy compensation |
| **Shadows** | 4-cascade CSM, per-cascade PCF, distance field shadows, contact shadows |
| **Lighting** | Directional + point (64) + spot (16) lights, IBL (SH L2 + prefiltered specular) |
| **Atmosphere** | Bruneton sky, multi-scatter LUT, ozone, aerial perspective, height fog (2-layer + sky injection) |
| **Post-Process** | TAA, SSR, GTAO, bloom (6-mip Karis), FilmToneMap ACES, auto-exposure, DOF, motion blur |
| **Anti-Aliasing** | TAA, SMAA (1x/T2x/S2x/4x), FXAA |
| **Upscaling** | FSR 1.0 (spatial), FSR 2.x (temporal with motion vectors) |
| **Film Effects** | Film grain, vignette, chromatic aberration, color grading LUT (32x32x32) |
| **Backends** | DX12 SM6.8 (bindless), DX12 SM5, DX11, Vulkan |

## Monorepo Structure

| Package | Description | Build |
|---------|------------|-------|
| [`runtime/`](runtime/) | Engine core (C++) | Zig / CMake (MSVC, Clang) |
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

```bash
# Default (Zig)
scripts/build.ps1

# MSVC
scripts/build.ps1 --compiler=msvc

# Clang
scripts/build.ps1 --compiler=clang

# CMD equivalent
scripts/build.bat
scripts/build.bat --compiler=msvc
```

> The build scripts check for locked files (e.g. `.node` loaded by VSCode) before building.
> If a lock is detected, close the Extension Host or run **Developer: Reload Window** first.

### Test

```bash
scripts/test.ps1                         # Zig (default)
scripts/test.ps1 --compiler=msvc         # MSVC
scripts/test.ps1 --compiler=clang        # Clang
scripts/test.ps1 --coverage              # With coverage (TS modules)
```

### Run

Press **F5** in VSCode to launch the Extension Development Host, or:

```bash
npm run build:debug
```

## Build Pipeline

```
scripts/build.ps1 [--compiler=zig|msvc|clang]
  │
  ├─ 0. Pre-flight        check for locked .node / .lib files
  ├─ 1. build:runtime     zig build / cmake → runtime/build/
  ├─ 2. build:napi        cmake-js  → napi/build/Release/uzengine.node
  │     └─ copy:node                → extension/native/uzengine.node
  ├─ 3. build:mcp         tsc       → mcp/dist/
  └─ 4. build:extension   tsc       → extension/out/extension.js
```

## Quality

| Tool | Runtime | N-API | Extension | MCP |
|------|---------|-------|-----------|-----|
| **Compiler** | Zig / MSVC / Clang | MSVC | tsc | tsc |
| **Lint** | clang-tidy | — | ESLint 9 | ESLint 9 |
| **Format** | clang-format | clang-format | Prettier 3.8 | Prettier 3.8 |
| **Test** | GoogleTest 1.15 | — | Vitest 4 | Vitest 4 |

```bash
scripts/lint.ps1                   # clang-tidy + eslint
scripts/format.ps1                 # clang-format + prettier
scripts/format.ps1 --check         # CI mode (check only)
scripts/test.ps1                   # zig test + vitest
scripts/test.ps1 --compiler=msvc   # MSVC tests
```

- **Zero warnings** — all warnings promoted to errors across every module
- **95% coverage** — enforced via Vitest thresholds on branches, functions, lines, and statements
- **Structured logging** — `LogOutputChannel` (extension), `pino` (mcp), `spdlog` (runtime)

## Thirdparty Dependencies

All thirdparty libraries are **committed to the repo** under `runtime/thirdparty/`. See [`runtime/thirdparty/README.md`](runtime/thirdparty/README.md) for the full list with versions, licenses, and extracted files.

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — system design, layers, execution model
- [Roadmap](docs/ROADMAP.md) — development phases and milestones
- [DAG](docs/DAG.md) — milestone dependency graph
- [PDR](docs/PDR.md) — project design record
- [Specs](docs/specs/) — per-milestone specifications

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
