# Architecture Overview — Project Structure

> **Status**: Reference (active — evolves as new modules are added)
> **Source**: ARCHITECTURE.md §Project Structure, §Build Pipeline, §Why Three Layers, §Directory Structure

## Three-Layer Architecture

UzEngine is organized into three layers, built and deployed independently:

```
┌─ Extension (TypeScript) ───── Phase 2 ──────────────────┐
│  Editor UI, webview panels, project management           │
│  Loads uzengine.node via require()                       │
├─ N-API Bridge (C++/MSVC) ──── Phase 2 ──────────────────┤
│  Thin wrapper: runtime C API → Node.js bindings          │
│  cmake-js + Electron headers, produces .node addon       │
├─ Runtime (C++/Zig) ────────── Phase 1 (CURRENT) ────────┤
│  Pure C++ engine: RHI, renderer, ECS, physics, audio     │
│  Zig build system, compiles as .lib + standalone .exe    │
└──────────────────────────────────────────────────────────┘
```

### Why Three Layers

- **Runtime** is pure C++ with zero Node/N-API dependency. Compiles as a static library consumed by CLI tools, test harnesses, or standalone game executables.
- **N-API bridge** wraps the runtime's C API for Node/Electron. cmake-js compiles it with MSVC for proper delay-load support. Links `uzengine.lib` and produces `uzengine.node`.
- **Extension** is pure TypeScript. It `require()`s the `.node` addon. No native compilation happens here.

## Repository Layout

```
UzEngine/0.2/
├── docs/                                 # Architecture documentation
│   └── engine/                           # This documentation tree
├── runtime/                              # Native engine (C++)
│   ├── build.zig                         # Zig build system
│   ├── build.zig.zon                     # Zig package manifest
│   ├── core/                             # Engine foundation
│   │   ├── assertions/                   # UZ_CHECK, UZ_ENSURE, UZ_VERIFY
│   │   ├── asset/                        # Asset registry, importers, cache
│   │   ├── config/                       # .uzproject loading, cascading config
│   │   ├── containers/                   # HandleMap, FreeList, RingBuffer, FrameArray
│   │   ├── logging/                      # UZ_LOG, categories, sinks
│   │   ├── math/                         # Vec2/3/4, Mat4, Quat, Transform, Color
│   │   ├── memory/                       # Own<T>, Ref<T>, allocators
│   │   ├── paths/                        # GetExeDir, ResolveEngineResource
│   │   ├── profiling/                    # UZ_PROFILE_SCOPE, stat groups
│   │   ├── random/                       # PCG random generator
│   │   ├── renderer/                     # Mesh, camera, upload context, constant buffers
│   │   ├── rhi/                          # RHI abstraction + DX12 backend
│   │   │   ├── rhi.h                     # Core RHI types and interface
│   │   │   ├── rhi_backend.h             # Backend selection
│   │   │   ├── rhi_config.h/.cpp         # RHI configuration resolution
│   │   │   ├── rhi_types.h               # GPU resource types
│   │   │   └── dx12/                     # DX12 implementation
│   │   └── shader/                       # Shader pipeline (VFS, compiler, cache, hot-reload)
│   ├── demos/                            # Standalone demo executables
│   ├── editor/                           # Editor-only runtime features
│   ├── platform/                         # OS abstraction (window, input)
│   ├── schemas/                          # JSON Schemas for .uz* formats
│   ├── shaders/                          # HLSL shaders (SM6.6 + SM5)
│   ├── tests/                            # Test harness
│   ├── thirdparty/                       # Third-party libraries
│   │   ├── agility/                      # D3D12 Agility SDK
│   │   ├── dxc/                          # DirectX Shader Compiler
│   │   └── vcpkg/                        # vcpkg-managed dependencies
│   └── tools/                            # Build tools (uzcook, etc.)
├── napi/                                 # N-API bridge (Phase 2)
├── extension/                            # VSCode extension (Phase 2)
├── mcp/                                  # MCP server (Phase 3)
├── .rulebook/                            # Task management
└── package.json                          # Root orchestrator
```

## Build Pipeline

```
npm run build (future — currently standalone zig build)
  │
  ├─ 1. build:runtime    zig build -Doptimize=ReleaseFast
  │                       → runtime/zig-out/lib/uzengine.lib
  │                       → runtime/zig-out/bin/uzengine.exe
  │
  ├─ 2. build:napi       cmake-js compile (MSVC, Electron 39.6.0 headers)
  │                       links uzengine.lib + node.lib + delay-load hook
  │                       → napi/build/Release/uzengine.node
  │
  ├─ 3. copy:node        → extension/native/uzengine.node
  │
  └─ 4. build:extension  tsc -p .
                          → extension/out/extension.js
```

### Current State (Phase 1)

Only step 1 is active. The runtime builds as both a static library (for linking) and standalone executables (for demos and tests). Steps 2-4 are Phase 2.

## Module Dependencies

```
demos/        → runtime (all modules)
editor/       → core, scene (stripped in shipping)
runtime/src/  → core
tests/        → core, runtime
```

Within `core/`:
```
renderer/   → rhi, shader, math, memory
shader/     → rhi, paths, asset, logging
rhi/        → logging, assertions, paths, config
asset/      → logging, paths, config, containers
everything  → logging, assertions (leaf dependencies)
```

**Strict invariant:** No circular dependencies. Modules at the bottom of the stack (logging, assertions, paths) depend on nothing engine-specific.
