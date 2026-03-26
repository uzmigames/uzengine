<!-- doc-meta
title: Build and Deployment Guide
category: guide
theme: toolchain
status: active
updated: 2026-03-25
author: docs-operator
-->

# Build and Deployment Guide

Complete step-by-step instructions for building UzEngine from source, including the runtime, N-API bridge, and VSCode extension.

---

## Prerequisites

- **Zig 0.15+** — [Download](https://ziglang.org/download/)
- **Node.js 22+** — [Download](https://nodejs.org/)
- **Visual Studio Build Tools 2022** — C++ workload required
- **CMake 3.21+** — [Download](https://cmake.org/)
- **Git** — for version control

### Verify Installation

```bash
zig version                    # Should be 0.15.0 or later
node --version                 # Should be 22.22.0 or later
npm --version                  # Should be 10.0.0 or later
cmake --version                # Should be 3.21 or later
"C:\Program Files\Git\cmd\git" --version   # Git for Windows
```

---

## Quick Build (Automated)

The easiest way is to use the build scripts, which handle all steps automatically.

### PowerShell (Recommended)

```bash
# Navigate to repo root
cd e:\UzEngine

# Build everything (Zig by default)
scripts/build.ps1

# Or with MSVC compiler (for static lib builds)
scripts/build.ps1 --compiler=msvc

# Or with Clang
scripts/build.ps1 --compiler=clang
```

### CMD

```bash
cd e:\UzEngine
scripts/build.bat
scripts/build.bat --compiler=msvc
```

The script performs:
1. Pre-flight checks (locked files, dependencies)
2. Build runtime (`zig build -Ddll=true`)
3. Build N-API addon (`cmake-js compile`)
4. Deploy artifacts to extension
5. Build extension TypeScript

**Output**:
- `runtime/build/bin/uzengine.exe` — standalone runtime executable
- `extension/native/uzengine.node` — VSCode extension addon
- `extension/native/*.dll` — all runtime dependencies

---

## Manual Build (Step-by-Step)

### Step 1: Build Runtime as DLL

The runtime is built as a **DLL** (not static lib) because Zig's LLVM archive format is incompatible with MSVC's linker (LNK1143 COMDAT error). The DLL approach produces an MSVC-compatible import lib.

```bash
cd runtime
zig build -Ddll=true
```

**Output**:
- `runtime/build/bin/uzengine.exe` — standalone executable
- `runtime/build/bin/uzengine_runtime.dll` — runtime code (41MB)
- `runtime/build/lib/uzengine_runtime.lib` — import lib for MSVC (6KB)
- `runtime/build/bin/*.dll` — dependencies (d3dcompiler_47.dll, dxcompiler.dll, dxil.dll, lua51.dll, etc.)

### Step 2: Build N-API Addon

The N-API addon is compiled with MSVC and links against the runtime DLL import lib.

```bash
cd napi
npm install                  # Install node-addon-api + cmake-js
npx cmake-js compile --runtime=electron --runtime-version=39.6.0
```

**cmake-js arguments**:
- `--runtime=electron` — Target Electron (VSCode uses Electron)
- `--runtime-version=39.6.0` — Match VSCode 1.111.0 (Electron 39.6.0 = Node 22.22.0 = ABI 140)

**Output**:
- `napi/build/Release/uzengine.node` — N-API addon (162KB)
- `napi/build/Release/*.dll` — copies of runtime dependencies

### Step 3: Deploy to Extension

Copy the addon and all DLLs to the extension's native directory:

```bash
# Create native directory
mkdir -p extension/native/D3D12

# Copy N-API addon
cp napi/build/Release/uzengine.node extension/native/

# Copy runtime DLL and dependencies
cp napi/build/Release/*.dll extension/native/
cp napi/build/Release/D3D12/*.dll extension/native/D3D12/

# Verify files are in place
ls -la extension/native/
# Should show: uzengine.node, uzengine_runtime.dll, d3dcompiler_47.dll, etc.
```

### Step 4: Build Extension

Compile TypeScript and finalize the extension package:

```bash
cd extension
npm install                  # Install dependencies (vscode, TypeScript, etc.)
npm run build                # Compile TypeScript
```

**Output**:
- `extension/out/extension.js` — compiled extension
- `extension/native/uzengine.node` — verified in place
- All dependencies copied alongside

---

## Architecture: Why DLL?

```
┌──────────────────────────────────────────────────────────┐
│  UzEngine Runtime (Zig)                                  │
│  ├─ C++ engine core (via C API)                          │
│  └─ Produces uzengine_runtime.dll + import lib           │
└──────────────────────────────────────────────────────────┘
                         │
                         │ MSVC linker consumes .lib
                         ↓
┌──────────────────────────────────────────────────────────┐
│  N-API Bridge (C++, compiled by MSVC)                    │
│  ├─ napi_lifecycle.cpp (init/shutdown)                  │
│  ├─ napi_viewport.cpp (rendering)                       │
│  ├─ napi_scene.cpp (scene I/O)                          │
│  ├─ napi_input.cpp (input forwarding)                   │
│  └─ Produces uzengine.node (N-API addon)                │
└──────────────────────────────────────────────────────────┘
                         │
                         │ require('uzengine.node')
                         ↓
┌──────────────────────────────────────────────────────────┐
│  VSCode Extension (TypeScript)                           │
│  ├─ extension.ts (command registration)                 │
│  ├─ panels/* (UI components)                            │
│  └─ Loads .node addon in-process                        │
└──────────────────────────────────────────────────────────┘
                         │
                         │ Native rendering
                         ↓
┌──────────────────────────────────────────────────────────┐
│  GPU (Direct3D 12 / Vulkan / Metal)                      │
│  ├─ Native HWND viewport (zero readback)                │
│  └─ Frame output directly to screen                      │
└──────────────────────────────────────────────────────────┘
```

**Why DLL instead of static lib?**
- Zig produces libraries in LLVM archive format (`.lib` from `ar` command)
- MSVC's `link.exe` cannot read LLVM archives → LNK1143 COMDAT error
- Solution: Zig builds a **DLL** instead, producing an MSVC-compatible import lib
- Import lib is just a symbol table, small (6KB), MSVC-native format

---

## Clean Build

If you encounter stale artifacts or build errors, perform a clean build:

```bash
# Full clean (all caches)
cd runtime && zig build --clean
cd ../napi && rm -rf build/ node_modules/
cd ../extension && rm -rf out/ node_modules/

# Rebuild from scratch
cd ../runtime && zig build -Ddll=true
cd ../napi && npm install && npx cmake-js compile --runtime=electron --runtime-version=39.6.0
cd ../extension && npm install && npm run build
```

### Known Issues

**Issue**: `napi/build/` is stale, addon doesn't load
```bash
rm -rf napi/build && npx cmake-js compile --runtime=electron --runtime-version=39.6.0
```

**Issue**: `uzengine_runtime.dll` is locked (already loaded by VSCode)
- Close VSCode Extension Host
- Or run **Developer: Reload Window** in VSCode
- Script will auto-detect and error

**Issue**: MSVC compiler not found
- Install Visual Studio Build Tools 2022 with C++ workload
- Ensure `cl.exe` is on PATH: `"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\bin\Hostx64\x64\cl.exe"`

---

## Testing the Build

### Run Standalone Runtime

```bash
# Load a demo scene
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject

# With editor overlays (grid, stats)
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject --editor

# Headless screenshot
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject \
  --headless \
  --screenshot output.png \
  --screenshot-frame 5 \
  --frames 8
```

### Run VSCode Extension

```bash
# Open VSCode
code e:\UzEngine

# Press F5 to launch Extension Development Host
# Or: npm run build:debug
```

The Extension Development Host will:
1. Load the extension
2. Require the `.node` addon (triggers runtime initialization)
3. Register commands: `uzengine.openViewport`, `uzengine.openScene`, etc.
4. Open webview panels (scene tree, inspector, console)

### Verify N-API Bridge

Check that the addon loads correctly:

```bash
cd extension/native
node -e "const uz = require('./uzengine.node'); console.log(uz.getVersion());"
```

Should output version string, e.g., `0.2.49`.

---

## Compiler Variants

### Zig Build (Default)

Zig handles all C++ compilation internally via clang. Fast and reliable.

```bash
cd runtime && zig build -Ddll=true
```

### MSVC Build (Windows)

Compile C++ source directly with MSVC (alternative to Zig's internal clang):

```bash
cd runtime && zig build -Dcompiler=msvc -Ddll=true
```

Requires `cl.exe` on PATH.

### Clang Build (Experimental)

```bash
cd runtime && zig build -Dcompiler=clang -Ddll=true
```

Requires `clang++` on PATH.

---

## Deployment

### Package for Distribution

```bash
# Build release artifacts
scripts/build.ps1 --compiler=msvc

# Create distribution package
mkdir dist
cp -r extension dist/
cp -r runtime/build/bin/*.exe dist/
cp -r runtime/build/bin/*.dll dist/
# extension/native/ already contains all .node + .dll files

# Zip
tar.exe -a -c -f uzengine-v0.2.49-win64.zip dist/
```

### VSCode Marketplace

The extension can be published to the VSCode Marketplace:

```bash
cd extension
npm install -g vsce                    # Install packaging tool
npx vsce package                       # Create .vsix
npx vsce publish --token <token>       # Publish to marketplace
```

See [VSCode Publishing Guide](https://code.visualstudio.com/api/working-with-extensions/publishing-extension) for details.

---

## Troubleshooting

### Build Errors

**Error: `LNK1143: COMDAT already defined`**
- Zig built a static `.lib` instead of DLL
- Solution: Use `zig build -Ddll=true` (note the flag)

**Error: `cmake-js: cmake not found`**
- CMake not installed or not on PATH
- Solution: Install [CMake 3.21+](https://cmake.org/) and add to PATH

**Error: `MSVC compiler not found`**
- Visual Studio Build Tools not installed
- Solution: Install from [Visual Studio Installer](https://visualstudio.microsoft.com/downloads/)
- Select "Desktop development with C++"

### Runtime Errors

**Error: `uzengine.node` not found**
- N-API addon not deployed to `extension/native/`
- Solution: Run manual deploy step (Step 3 above)

**Error: `uzengine_runtime.dll` not found**
- Runtime DLL not copied alongside addon
- Solution: Check `extension/native/` contains both `.node` and `.dll`

**Error: `Could not load HWND viewport`**
- Extension failed to create native window
- Check console logs: **View → Output → UzEngine**
- Verify DirectX 12 drivers are up to date

---

## Related Documentation

- [Runtime Architecture](../architecture.md) — system design
- [Extension Architecture](../../extension/README.md) — UI/UX design
- [N-API Bridge](../../napi/README.md) — C/JavaScript boundary
- [Specs 2.0 (N-API Foundation)](../specs/2.0-napi-foundation.md) — detailed design doc

---

*Last updated 2026-03-25*
