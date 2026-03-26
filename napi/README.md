# @uzmigames/napi

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)]()
[![N-API](https://img.shields.io/badge/N--API-10-green.svg)]()
[![Electron](https://img.shields.io/badge/Electron-39.6.0-teal.svg)]()

Thin N-API bridge between the UzEngine runtime and VSCode/Electron. Wraps the runtime's C API into a `.node` addon that the extension can `require()`.

**This module does NOT contain engine logic.** It only marshals calls between JavaScript and the runtime's DLL via the C API.

## Architecture

The N-API addon is a **DLL-based bridge**:

```
Runtime (Zig)
  ↓
uzengine_runtime.dll (41MB, Zig-built)
  + uzengine_runtime.lib (MSVC-compatible import lib)
  ↓
napi/src/*.cpp (compiled with MSVC)
  ↓
uzengine.node (162KB, N-API addon)
  ↓
extension/src/extension.ts (TypeScript)
```

**Why?** Zig produces static libraries in LLVM format, incompatible with MSVC's linker. By building the runtime as a DLL, we get an MSVC-compatible import lib that cmake-js can link against.

## Build

```bash
# Install dependencies
npm install

# Build (compiles N-API addon, links against runtime DLL)
npm run build

# Debug build (no optimizations)
npm run build:debug
```

### Requirements

- Runtime must be built first: `cd ../runtime && zig build -Ddll=true`
- Visual Studio Build Tools 2022 (MSVC compiler required for DLL import lib linking)
- cmake-js automatically handles Electron headers, `node.lib`, and `/DELAYLOAD:NODE.EXE`
- CMake 3.21+

## How It Works

1. `zig build -Ddll=true` produces:
   - `uzengine_runtime.dll` (runtime executable code)
   - `uzengine_runtime.lib` (import lib for MSVC linker)
2. cmake-js downloads Electron 39.6.0 headers and `node.lib`
3. MSVC compiles all `src/*.cpp` files + `win_delay_load_hook.cc` (from cmake-js)
4. Links against `uzengine_runtime.lib` (from runtime/build/lib/) and `node.lib`
5. `/DELAYLOAD:NODE.EXE` ensures napi symbols resolve from `Code.exe` (Electron) at runtime
6. Output: `build/Release/uzengine.node`
7. All dependent DLLs copied alongside `.node`: `uzengine_runtime.dll`, `d3dcompiler_47.dll`, etc.

## Exported API

The N-API addon exposes 25+ C API functions from the runtime via N-API bindings:

| Module | File | Functions |
|--------|------|-----------|
| **Lifecycle** | `napi_lifecycle.cpp` | `init`, `shutdown`, `getVersion`, `getBackend`, `tick` |
| **Viewport** | `napi_viewport.cpp` | `createViewport`, `destroyViewport`, `resizeViewport`, `renderFrame`, `captureScreenshot`, `setViewportPosition` |
| **Scene** | `napi_scene.cpp` | `loadScene`, `saveScene`, `getSceneTree`, `createEntity`, `destroyEntity`, `getComponentJson`, `setComponentProperty` |
| **Input** | `napi_input.cpp` | `onMouseMove`, `onMouseButton`, `onMouseWheel`, `onKeyDown`, `onKeyUp` |
| **Diagnostics** | `napi_diagnostics.cpp` | `setLogCallback`, `setEditorMode`, `setContentRoot`, `getEngineStats` |

All bindings are **asynchronous** — callbacks use `ThreadSafeFunction` to post messages back to the JavaScript event loop.

TypeScript declarations: [`extension/src/uzengine.d.ts`](../extension/src/uzengine.d.ts)

## Directory Structure

```
napi/
├── CMakeLists.txt          # cmake-js build (MSVC, delay-load, Electron headers)
├── package.json            # @uzmigames/napi — node-addon-api + cmake-js
└── src/
    ├── addon.cpp           # Module entry point (delegates to Register*Bindings)
    ├── napi_common.h       # Shared header (runtime.h + napi.h + Register* decls)
    ├── napi_lifecycle.cpp  # Version, backend, init, shutdown
    ├── napi_viewport.cpp   # Viewport lifecycle, rendering, screenshot
    ├── napi_scene.cpp      # Scene I/O, entity CRUD, component access
    ├── napi_input.cpp      # Mouse, keyboard, scroll forwarding
    └── napi_diagnostics.cpp# Log callback (ThreadSafeFunction), editor config
```

## Adding New Bindings

1. Create `src/napi_<domain>.cpp` with a `Register<Domain>Bindings(env, exports)` function
2. Add the declaration to `src/napi_common.h`
3. Add the file to `CMakeLists.txt` source list
4. Call `Register<Domain>Bindings(env, exports)` in `addon.cpp`

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
