# UzEngine

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Zig](https://img.shields.io/badge/Zig-0.15-orange.svg)]()
[![VSCode](https://img.shields.io/badge/VSCode-1.85+-purple.svg)]()
[![Electron](https://img.shields.io/badge/Electron-39.6.0-teal.svg)]()

AAA-quality game engine embedded in VSCode, with open file formats designed for LLM manipulation. **The first engine built for autonomous AI-driven development.**

## Key Features

- **AAA Visual Quality** — rendering math and techniques based on proven engine architectures (clean-room reimplementation)
- **Embedded in VSCode** — native N-API addon loaded in-process, no separate process or IPC
- **Open Formats** — all scene/asset files are JSON, designed for LLM manipulation
- **Multi-Backend RHI** — DX12 SM6.6 primary, DX12 SM5 / DX11 / Vulkan / Metal fallbacks
- **No GC** — deterministic memory via Rust-inspired ownership and borrow checking in C++
- **AI-First** — MCP server integration for autonomous development workflows

## Monorepo Structure

| Package | Description | Build |
|---------|------------|-------|
| [`runtime/`](runtime/) | Engine core (C++) | Zig |
| [`napi/`](napi/) | N-API bridge to Electron | cmake-js / MSVC |
| [`extension/`](extension/) | VSCode extension | TypeScript |
| [`mcp/`](mcp/) | MCP server for AI workflows | TypeScript |

## Quick Start

### Prerequisites

- [Zig 0.15+](https://ziglang.org/download/)
- [Node.js 22+](https://nodejs.org/)
- [Visual Studio Build Tools 2022](https://visualstudio.microsoft.com/downloads/) (C++ workload)
- [CMake 3.15+](https://cmake.org/)

### Setup

```bash
# Run the setup script (downloads thirdparty deps + npm install)
scripts/setup.ps1    # PowerShell
scripts/setup.bat    # CMD
```

### Build

```bash
# Full build (with pre-flight lock check)
scripts/build.ps1    # PowerShell
scripts/build.bat    # CMD

# Or via npm (no lock check)
npm run build
```

> The build scripts check for locked files (e.g. `.node` loaded by VSCode) before building.
> If a lock is detected, close the Extension Host or run **Developer: Reload Window** first.

### Run

Press **F5** in VSCode to launch the Extension Development Host, or:

```bash
npm run build:debug
```

## Build Pipeline

```
scripts/build.ps1
  │
  ├─ 0. Pre-flight        check for locked .node / .lib files
  ├─ 1. build:runtime     zig build → runtime/zig-out/lib/uzengine_runtime.lib
  ├─ 2. build:napi        cmake-js  → napi/build/Release/uzengine.node
  │     └─ copy:node                → extension/native/uzengine.node
  ├─ 3. build:mcp         tsc       → mcp/dist/
  └─ 4. build:extension   tsc       → extension/out/extension.js
```

## Quality

| Tool | Runtime | N-API | Extension | MCP |
|------|---------|-------|-----------|-----|
| **Lint** | clang-tidy | — | ESLint 9 | ESLint 9 |
| **Format** | clang-format | clang-format | Prettier 3.8 | Prettier 3.8 |
| **Test** | GoogleTest 1.15 | — | Vitest 4 | Vitest 4 |

```bash
# Run all quality checks
scripts/lint.ps1       # clang-tidy + eslint
scripts/format.ps1     # clang-format + prettier (--check for CI)
scripts/test.ps1       # zig test + vitest (--coverage for thresholds)

# Or per-module via npm
npm run lint           # extension + mcp
npm run test           # extension + mcp
npm run format         # extension + mcp
```

- **Zero warnings** — all warnings promoted to errors across every module
- **95% coverage** — enforced via Vitest thresholds on branches, functions, lines, and statements
- **Structured logging** — `LogOutputChannel` (extension), `pino` (mcp), `spdlog` (runtime)

## Thirdparty Dependencies

All thirdparty libraries are **committed to the repo** under `runtime/thirdparty/`. The setup script (`scripts/setup.ps1`) clones them with `--depth 1`, strips `.git`, docs, tests, and CI files to keep the footprint minimal. This ensures:

- **Reproducible builds** — no external download required after clone
- **Offline development** — no network dependency during build
- **Pinned versions** — exact versions tracked in git

| Library | Version | Purpose |
|---------|---------|---------|
| spdlog | 1.15.3 | Structured logging |
| Tracy | 0.11.1 | CPU profiling |
| mimalloc | 2.1.7 | Global allocator |
| D3D12MA | 2.0.1 | GPU memory allocator |
| DirectX Headers | 1.614.1 | DX12 API headers |
| DirectXMath | oct2024 | SIMD math |
| Agility SDK | 1.614.1 | DX12 runtime |
| DXC | 1.8.2407 | Shader compiler |
| glm | 1.0.1 | Math (header-only) |
| nlohmann/json | 3.11.3 | JSON parsing (single header) |
| atomic_queue | latest | Lock-free queue |
| GoogleTest | 1.15.2 | Testing framework |
| vcpkg | latest | zlib, zstd |

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — system design, layers, execution model
- [Roadmap](docs/ROADMAP.md) — development phases and milestones
- [DAG](docs/DAG.md) — milestone dependency graph
- [PDR](docs/PDR.md) — project design record
- [Specs](docs/specs/) — per-milestone specifications

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
