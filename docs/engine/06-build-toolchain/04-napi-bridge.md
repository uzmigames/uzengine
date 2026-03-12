# Build Toolchain — N-API Bridge & Three-Layer Architecture

> **Status**: Planned (Phase 2)
> **Source**: `napi/CMakeLists.txt`, `napi/src/addon.cpp`
> **Depends on**: Runtime static library, Electron/VSCode

## Purpose

UzEngine's final deployment target is a VSCode extension. The runtime (C++) runs **in-process** as a native N-API addon (`.node` file) loaded directly by the VSCode extension (TypeScript). This is the "three-layer" architecture:

```
┌────────────────────────────────────────────────────────────┐
│  Layer 3: extension/ (TypeScript)                          │
│  VSCode extension, editor UI, command palette              │
│  require('./native/uzengine.node')                         │
│          ↕ N-API function calls (in-process, zero-copy)    │
├────────────────────────────────────────────────────────────┤
│  Layer 2: napi/ (C++, MSVC)                                │
│  Thin C++ bridge wrapping runtime's extern "C" API         │
│  Links uzengine_runtime.lib + node.lib + delay-load hook   │
│          ↕ Direct function calls (same process)            │
├────────────────────────────────────────────────────────────┤
│  Layer 1: runtime/ (C++, Zig)                              │
│  Engine core: RHI, renderer, ECS, physics, audio           │
│  Compiles as static library (uzengine_runtime.lib)         │
└────────────────────────────────────────────────────────────┘
```

### Why In-Process (Not Separate Process)?

POC 0.1 used a separate process architecture with WebSocket IPC. Problems:
- **Frame delivery bottleneck** — had to encode frames (JPEG/H.264) and stream them to webview
- **Latency** — round-trip for every input event and API call
- **Process management** — cold start times, pool management, build lock issues

POC 0.2 proved that N-API in-process is viable: DX12 renderer runs at 6069 FPS inside Electron. The engine creates its own HWND with a direct DX12 swap chain — no webview, no encoding, no intermediate copies for the primary viewport.

**H.264 streaming** (120 FPS via Media Foundation + WebCodecs) is still used for secondary webview panels (preview, remote/headless), but the main viewport is native.

## Build Pipeline

The full build sequence orchestrated by the root `package.json`:

```
npm run build
  │
  ├─ 1. build:runtime     zig build -Doptimize=ReleaseFast
  │                        → runtime/build/lib/uzengine_runtime.lib
  │
  ├─ 2. build:napi        cmake-js compile (MSVC, Electron ABI)
  │                        Links: uzengine_runtime.lib + node.lib
  │                        → napi/build/Release/uzengine.node
  │
  ├─ 3. copy:node         cp → extension/native/uzengine.node
  │
  └─ 4. build:extension   tsc -p .
                           → extension/out/extension.js
```

### Why MSVC for N-API?

The N-API bridge **must** use MSVC because:

1. **Delay-load hook** — Electron's executable is `Code.exe`, not `node.exe`. The addon must delay-load `node.dll` symbols via a hook provided by `cmake-js`. This hook is MSVC-specific (`__pfnDliNotifyHook2`).
2. **ABI compatibility** — Electron is built with MSVC. Mixing Clang-compiled C++ with MSVC-compiled Node internals causes ABI mismatches (vtable layout, exception handling, RTTI).
3. **cmake-js** — The standard tool for building Node native addons. It handles Electron header downloads, ABI matching, and delay-load configuration automatically.

The runtime library itself is compiled with Zig/Clang, but it exposes only a C API (`extern "C"`) to the N-API bridge — C ABI is compatible across all compilers.

## N-API Bridge Requirements (POC 0.2 Findings)

### Critical Build Configuration

| Requirement | Why | Consequence if Missing |
|-------------|-----|----------------------|
| Include `${CMAKE_JS_SRC}` in CMakeLists | Provides delay-load hook for Electron | `require()` segfaults — addon tries to resolve symbols from `node.exe` but exe is `Code.exe` |
| `NAPI_DISABLE_CPP_EXCEPTIONS` | C++ exceptions crash Electron (electron#22799) | Random crashes on any N-API error path |
| `--runtime=electron --runtime-version=<ver>` | Match Electron's Node ABI | `MODULE_NOT_FOUND` or silent ABI corruption |

### Version Matrix (VSCode 1.111.0)

| Component | Version |
|-----------|---------|
| Electron | 39.6.0 |
| Node | 22.22.0 |
| ABI | 140 |
| VSCode path | `f:/IDEs/Microsoft VS Code/` |

To verify Electron version:
```bash
ELECTRON_RUN_AS_NODE=1 "f:/IDEs/Microsoft VS Code/Code.exe" \
  -e "console.log(process.versions)"
```

## Viewport Architecture

```
┌─────────────────────────────────────────────────┐
│ VSCode Window                                   │
│  ┌───────────────────────────────────────────┐  │
│  │ Primary Viewport (native HWND)            │  │
│  │ ┌─────────────────────────────────────┐   │  │
│  │ │ DX12 SwapChain renders directly     │   │  │
│  │ │ No webview, no encoding, no copies  │   │  │
│  │ │ Full GPU performance (6069 FPS)     │   │  │
│  │ └─────────────────────────────────────┘   │  │
│  └───────────────────────────────────────────┘  │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │Inspector │ │Hierarchy │ │ Preview (webview)│ │
│  │(webview) │ │(webview) │ │ H.264 stream     │ │
│  │ HTML/CSS │ │ HTML/CSS │ │ 120 FPS          │ │
│  └──────────┘ └──────────┘ └──────────────────┘ │
└─────────────────────────────────────────────────┘
```

- **Primary viewport**: Runtime creates a child HWND inside the VSCode editor area. DX12 swap chain renders directly to this window. Zero overhead.
- **Editor panels**: Standard VSCode webviews using HTML/CSS. Updated via N-API data binding, not frame streaming.
- **Preview panels**: For secondary viewports (material preview, remote debugging), H.264 streaming via Media Foundation encode + WebCodecs decode.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Editor embedding | Standalone app (Slate UI) | Standalone app (custom widgets) | **VSCode extension** (N-API addon) |
| Native ↔ Script bridge | UObject reflection + Blueprints | GDNative/GDExtension C ABI | **N-API** (C → Node bindings) |
| Editor rendering | Editor renders in own process | Editor renders in own process | Engine renders in **VSCode's process** |
| Frame delivery | Direct (same process) | Direct (same process) | Direct (HWND) for viewport, H.264 for webviews |

### Why Not Electron as the Editor?

Many tools (VS Code itself, Figma) use Electron for cross-platform desktop apps. UzEngine embeds **inside** VS Code rather than building a separate Electron app because:

1. **VS Code is already the dominant code editor** — developers don't want another IDE to learn
2. **Extension ecosystem** — Git, linting, debugging, AI assistants all available
3. **AI-first** — Claude Code, Copilot, and other AI tools already integrate with VS Code
4. **No reinvented editor chrome** — VS Code handles panels, tabs, keybindings, themes, search

The cost is the N-API bridge complexity and Electron-specific build requirements. The payoff is that UzEngine feels like a natural extension of the developer's existing workflow.

## Rules

1. **ALWAYS expose a C API (`extern "C"`) from the runtime** — C ABI is the only safe bridge between Zig/Clang and MSVC
2. **NEVER use C++ exceptions in N-API code** — always `NAPI_DISABLE_CPP_EXCEPTIONS`
3. **ALWAYS include `${CMAKE_JS_SRC}` in CMakeLists** — the delay-load hook is not optional
4. **ALWAYS verify Electron/Node ABI version before building** — ABI mismatch causes silent corruption
5. **NEVER stream frames for the primary viewport** — use a native HWND with direct DX12 swap chain
