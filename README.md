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

## Quick Start

### Prerequisites

- [Zig 0.15+](https://ziglang.org/download/)
- [Node.js 22+](https://nodejs.org/)
- [Visual Studio Build Tools 2022](https://visualstudio.microsoft.com/downloads/) (C++ workload)
- [CMake 3.15+](https://cmake.org/)

### Build

```bash
# Install dependencies
cd napi && npm install && cd ..
cd extension && npm install && cd ..

# Full build pipeline: runtime → napi → copy .node → extension
npm run build
```

### Run

Press **F5** in VSCode to launch the Extension Development Host, or:

```bash
npm run build:debug
```

## Build Pipeline

```
npm run build
  │
  ├─ 1. build:runtime    zig build → runtime/zig-out/lib/uzengine_runtime.lib
  ├─ 2. build:napi       cmake-js  → napi/build/Release/uzengine.node
  ├─ 3. copy:node                  → extension/native/uzengine.node
  └─ 4. build:extension  tsc       → extension/out/extension.js
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md) — system design, layers, execution model
- [Roadmap](docs/ROADMAP.md) — development phases and milestones
- [DAG](docs/DAG.md) — milestone dependency graph
- [PDR](docs/PDR.md) — project design record
- [Specs](docs/specs/) — per-milestone specifications

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
