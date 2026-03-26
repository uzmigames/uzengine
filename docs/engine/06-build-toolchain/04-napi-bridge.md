# Build Toolchain — N-API Bridge & Three-Layer Architecture

> **Status**: **DONE (Phase 2.0)**
> **Source**: `napi/CMakeLists.txt`, `napi/src/addon.cpp`, `runtime/build.zig`
> **Depends on**: Runtime DLL + import lib, Electron/VSCode

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
│  Compiles as DLL (uzengine_runtime.dll) + import lib       │
└────────────────────────────────────────────────────────────┘
```

### Architecture: Why DLL, Not Static Lib?

Zig produces libraries in **LLVM archive format** (via `ar`), which MSVC's linker cannot read (LNK1143 COMDAT error). Solution: build runtime as DLL instead:

```
zig build -Ddll=true
  ├─ uzengine_runtime.dll (41MB, runtime code)
  └─ uzengine_runtime.lib (6KB, import lib, MSVC-native format)
       ↓ (MSVC linker understands this)
  napi/src/*.cpp → uzengine.node
```

**Benefits**:
- N-API addon built with MSVC (zero ABI surprises)
- Full C ABI compatibility between Zig runtime and MSVC bridge
- Multi-compiler pipeline: Zig (runtime) + MSVC (addon) + TypeScript (extension)

### Why In-Process (Not Separate Process)?

POC 0.1 used a separate process architecture with WebSocket IPC. Problems:
- **Frame delivery bottleneck** — had to encode frames (JPEG/H.264) and stream them to webview
- **Latency** — round-trip for every input event and API call
- **Process management** — cold start times, pool management, build lock issues

POC 0.2 proved that N-API in-process is viable: DX12 renderer runs at 6069 FPS inside Electron. The engine creates its own HWND with a direct DX12 swap chain — no webview, no encoding, no intermediate copies for the primary viewport.

**H.264 streaming** (120 FPS via Media Foundation + WebCodecs) is still used for secondary webview panels (preview, remote/headless), but the main viewport is native.

## Build Pipeline

The full build sequence orchestrated by `scripts/build.ps1`:

```
scripts/build.ps1
  │
  ├─ 1. build:runtime     zig build -Ddll=true
  │                        → uzengine_runtime.dll (41MB)
  │                        → uzengine_runtime.lib (MSVC-compatible)
  │                        → uzengine.exe (standalone)
  │
  ├─ 2. build:napi        cmake-js compile --runtime=electron
  │                        Links: uzengine_runtime.lib + node.lib
  │                        Includes: ${CMAKE_JS_SRC} (delay-load hook)
  │                        → napi/build/Release/uzengine.node
  │
  ├─ 3. deploy:artifacts  cp napi/build/Release/*.dll → extension/native/
  │                        cp napi/build/Release/*.node → extension/native/
  │
  ├─ 4. build:mcp         tsc -p . → mcp/dist/
  │
  └─ 5. build:extension   tsc -p . → extension/out/extension.js
```

### Why MSVC for N-API, Zig for Runtime?

**N-API (MSVC)**:
1. **Delay-load hook** — Electron's executable is `Code.exe`, not `node.exe`. The addon must delay-load `node.dll` symbols via a hook provided by `cmake-js`. This hook is MSVC-specific (`__pfnDliNotifyHook2`).
2. **ABI compatibility** — Electron is built with MSVC. The addon's C++ calling conventions, exception handling, and RTTI must match MSVC exactly.
3. **cmake-js standard** — The standard tool for building Node native addons. Handles Electron headers, ABI matching, and delay-load automatically.

**Runtime (Zig)**:
1. **Fast compilation** — Zig's build system is ~3× faster than CMake for large C++ codebases
2. **Multi-compiler** — Zig can produce the same binary via MSVC, Clang, or GCC backends
3. **C ABI bridge** — Runtime exposes only `extern "C"` functions. C ABI is compatible across all compilers.

**Separation ensures**:
- Zig's internal libraries (libc++, lto, sanitizers) don't leak into MSVC-linked code
- MSVC's C++ exception semantics (NAPI-specific) are isolated from runtime
- Each layer compiles independently and can be updated without cross-contamination

## N-API Bridge Requirements

### Critical Build Configuration

| Requirement | Why | Consequence if Missing |
|-------------|-----|----------------------|
| `zig build -Ddll=true` | Produces MSVC-compatible import lib | `LNK1143 COMDAT error` — static lib format incompatible |
| Include `${CMAKE_JS_SRC}` in CMakeLists | Provides delay-load hook for Electron | `require()` segfaults — addon tries to resolve symbols from `node.exe` but exe is `Code.exe` |
| `NAPI_DISABLE_CPP_EXCEPTIONS` | C++ exceptions crash Electron (electron#22799) | Random crashes on any N-API error path |
| `--runtime=electron --runtime-version=39.6.0` | Match Electron/VSCode 1.111.0 | `MODULE_NOT_FOUND` or silent ABI corruption |
| Link `uzengine_runtime.lib` | Import lib from DLL | Missing symbols, linker error |
| Copy DLLs to `extension/native/` | Runtime DLL must load at runtime | `ERROR_MOD_NOT_FOUND` at `require()` time |

### Version Matrix (VSCode 1.111.0)

| Component | Version | Notes |
|-----------|---------|-------|
| VSCode | 1.111.0 | Latest stable (2026-03-25) |
| Electron | 39.6.0 | Bundled with VSCode |
| Node | 22.22.0 | LTS, ABI 140 |
| ABI | 140 | Must match exactly for `.node` to load |

To verify your VSCode Electron version:
```bash
ELECTRON_RUN_AS_NODE=1 "C:\Program Files\Microsoft VS Code\Code.exe" \
  -e "console.log(process.versions.electron, process.versions.node)"
```

### N-API Addon Structure

The addon is split into 6 source files for maintainability:

```cpp
// addon.cpp — Module entry point
NAPI_MODULE(NODE_GYP_MODULE_NAME, Initialize)

static napi_value Initialize(napi_env env, napi_value exports) {
    RegisterLifecycleBindings(env, exports);
    RegisterViewportBindings(env, exports);
    RegisterSceneBindings(env, exports);
    RegisterInputBindings(env, exports);
    RegisterDiagnosticsBindings(env, exports);
    return exports;
}

// napi_lifecycle.cpp — init, shutdown, getVersion, getBackend, tick
// napi_viewport.cpp — createViewport, renderFrame, captureScreenshot, resizeViewport
// napi_scene.cpp — loadScene, saveScene, createEntity, getComponentJson, setComponentProperty
// napi_input.cpp — onMouseMove, onMouseButton, onKeyDown, onKeyUp, onScroll
// napi_diagnostics.cpp — setLogCallback (ThreadSafeFunction), setEditorMode, setContentRoot
```

All callbacks use `ThreadSafeFunction` for thread-safe posting to the JavaScript event loop.

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

## Deployment Checklist

Before releasing a new version:

1. **Build clean** — `zig build --clean && zig build -Ddll=true`
2. **Verify import lib** — Check `runtime/build/lib/uzengine_runtime.lib` exists (6KB+)
3. **Verify DLL** — Check `runtime/build/bin/uzengine_runtime.dll` exists (40MB+)
4. **Build N-API** — `npx cmake-js compile --runtime=electron --runtime-version=39.6.0`
5. **Deploy artifacts** — Copy `.node` + all `.dll` to `extension/native/`
6. **Verify extension** — `npm run build` succeeds, `extension/out/extension.js` generated
7. **Test in VSCode** — Press F5, verify commands work, no console errors

## Rules

1. **ALWAYS build runtime as DLL (`-Ddll=true`)** — static lib format incompatible with MSVC linker
2. **ALWAYS expose a C API (`extern "C"`) from the runtime** — C ABI is the only safe bridge between Zig/Clang and MSVC
3. **NEVER use C++ exceptions in N-API code** — always `NAPI_DISABLE_CPP_EXCEPTIONS`
4. **ALWAYS include `${CMAKE_JS_SRC}` in CMakeLists** — the delay-load hook is not optional
5. **ALWAYS verify Electron/Node ABI version before building** — ABI mismatch causes silent corruption
6. **NEVER stream frames for the primary viewport** — use a native HWND with direct DX12 swap chain
7. **ALWAYS copy DLLs alongside `.node`** — `extension/native/` must have uzengine.node + uzengine_runtime.dll
