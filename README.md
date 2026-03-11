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

All thirdparty libraries are **committed to the repo** under `runtime/thirdparty/`. The setup script (`scripts/setup.ps1`) clones them with `--depth 1`, strips `.git`, docs, tests, and CI files to keep the footprint minimal. This ensures:

- **Reproducible builds** — no external download required after clone
- **Offline development** — no network dependency during build
- **Pinned versions** — exact versions tracked in git

| Library | Version | License | Purpose |
|---------|---------|---------|---------|
| spdlog | 1.15.3 | MIT | Structured logging (12 categories with compile-time filtering) |
| Tracy | 0.11.1 | BSD-3-Clause | CPU profiling with Tracy integration |
| mimalloc | 2.1.7 | MIT | Global allocator (high performance, low fragmentation) |
| D3D12MA | 2.0.1 | MIT | GPU memory allocator (named tracking, budget enforcement) |
| DirectX Headers | 1.614.1 | MIT | DX12 API headers (SM6.6 support) |
| DirectXMath | oct2024 | MIT | SIMD math library (Vec2/Vec3/Vec4/Mat4 operations) |
| Agility SDK | 1.614.1 | MIT | DX12 runtime (variable shading rate, mesh shaders) |
| DXC | 1.8.2407 | NCSA | Shader compiler (HLSL to DXIL) |
| glm | 1.0.1 | MIT | Math (header-only, reference implementation) |
| nlohmann/json | 3.11.3 | MIT | JSON parsing (single header, .uzproject files) |
| atomic_queue | latest | MIT | Lock-free MPMC queue (EventBus, work queues) |
| GoogleTest | 1.15.2 | BSD-3-Clause | Testing framework (444 tests, 64 test suites) |
| SPIRV-Reflect | 1.3.268 | Apache 2.0 | Shader reflection & root signature generation |
| xxhash | 0.8.2 | BSD-2-Clause | Fast hashing (content fingerprints, asset caching) |
| SSE Math Fun | RJVB fork | MIT | SIMD math functions (sin, cos, log, sqrt optimized) |
| MikkTSpace | latest | MIT | Tangent space generation for normal maps |
| dlmalloc | 2.8.6 | Public Domain | Lightweight allocator (alternative to mimalloc) |
| jemalloc | 5.3.0 | BSD-2-Clause | Alternative allocator (multi-threaded workloads) |
| vcpkg | latest | MIT | Dependency management (zlib, zstd) |

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — system design, layers, execution model
- [Roadmap](docs/ROADMAP.md) — development phases and milestones
- [DAG](docs/DAG.md) — milestone dependency graph
- [PDR](docs/PDR.md) — project design record
- [Specs](docs/specs/) — per-milestone specifications

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
