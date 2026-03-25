# @uzmigames/extension

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![TypeScript](https://img.shields.io/badge/TypeScript-5.3-blue.svg)]()
[![VSCode](https://img.shields.io/badge/VSCode-1.85+-purple.svg)]()
[![Electron](https://img.shields.io/badge/Electron-39.6.0-teal.svg)]()

UzEngine VSCode extension — the editor interface. Loads the native `.node` N-API addon and provides the full game engine editor experience inside VSCode.

**This module is pure TypeScript.** All native code lives in [`runtime/`](../runtime/) and [`napi/`](../napi/).

## Architecture

The extension is a thin **UI consumer** of the N-API addon:

```
extension.ts (entry point)
  ├─ loads uzengine.node from native/
  ├─ subscribes to commands (uzengine.openViewport, uzengine.openScene, etc.)
  └─ manages webview panels (scene tree, inspector, console, viewport)

Webview panels
  ├─ panels/viewport-panel.ts     (native HWND viewport rendering)
  ├─ panels/scene-tree-panel.ts   (entity hierarchy + components)
  ├─ panels/console-panel.ts      (log streaming via ThreadSafeFunction)
  └─ engine-mode.ts               (layout toggle: Code / Engine)
```

## Build

```bash
# Install dependencies
npm install

# Build TypeScript (compiles extension, copies .node)
npm run build

# Watch mode (auto-recompile on changes)
npm run watch
```

The `.node` binary and all dependent DLLs are copied to `native/` during the build process.

## Run

Press **F5** in VSCode (launch config in `.vscode/launch.json`) to start the Extension Development Host with the extension loaded.

## Commands

| Command | Description |
|---------|------------|
| `uzengine.openViewport` | Create a native viewport panel (HWND with DX12 swapchain) |
| `uzengine.openScene` | Load a `.uzscene` file into the viewport |
| `uzengine.toggleEngineMode` | Toggle between Code layout and Engine layout (Ctrl+Shift+E) |
| `uzengine.openSceneTree` | Open the scene entity hierarchy panel |
| `uzengine.openConsole` | Open the log console panel |
| `uzengine.openInspector` | Open the entity inspector panel |

## Syntax Highlighting

| Language | Extensions | Grammar |
|----------|-----------|---------|
| HLSL | `.hlsl`, `.hlsli`, `.fx`, `.fxh` | Full TextMate grammar |
| Zig | `.zig` | Full TextMate grammar |
| UzScene / UzMaterial / UzMesh / UzPrefab / UzProject | `.uz*` | JSON-based grammar |
| **C/C++ (injection)** | `.c`, `.cpp`, `.h` | UzEngine types, macros, RHI interfaces |

The C++ injection highlights engine-specific symbols (e.g. `UZ_LOG`, `Vec3`, `IRHIDevice`, `ShaderPipeline`) with semantic scopes so your color theme can distinguish them from standard C++ code.

## Directory Structure

```
extension/
├── package.json                      # @uzmigames/extension manifest + commands
├── tsconfig.json
├── native/                           # .node + DLLs (copied by build, gitignored)
│   ├── uzengine.node               # N-API addon (compiled by cmake-js)
│   ├── uzengine_runtime.dll        # Runtime executable (Zig-built)
│   ├── d3dcompiler_47.dll          # DirectX (optional, fallback)
│   ├── dxcompiler.dll              # DXC shader compiler
│   ├── dxil.dll                    # DXIL validator
│   ├── lua51.dll                   # LuaJIT runtime
│   └── D3D12/
│       └── D3D12Core.dll           # Direct3D 12 core library
├── src/
│   ├── extension.ts                # Extension entry point, command registration
│   ├── uzengine.d.ts               # TypeScript type declarations (from N-API)
│   ├── engine-mode.ts              # Layout toggle: Code / Engine
│   └── panels/
│       ├── viewport-panel.ts       # Native HWND viewport
│       ├── scene-tree-panel.ts     # Entity hierarchy webview
│       ├── console-panel.ts        # Log stream webview
│       └── inspector-panel.ts      # Entity properties webview
├── syntaxes/                         # TextMate grammars
│   ├── hlsl.tmLanguage.json
│   ├── zig.tmLanguage.json
│   ├── uzengine-json.tmLanguage.json
│   ├── uzengine-cpp-injection.tmLanguage.json   # C++ UzEngine highlighting
│   ├── hlsl-markdown-injection.tmLanguage.json
│   └── zig-markdown-injection.tmLanguage.json
└── .vscode/
    └── launch.json                  # F5 launch config (Extension Development Host)
```

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
