# UzEngine — Build and Development Guide

## Prerequisites

### System Requirements

- **OS:** Windows 10 or later (64-bit)
- **Compiler:** Visual Studio 2022 (or later)
  - C++ workload required
  - DirectX 12 SDK (usually included)
- **Build system:** CMake 3.20 or later
- **Package manager:** vcpkg (installed at `C:\vcpkg`)
- **Node.js:** v18 or later (for VSCode extension build)

### Installing vcpkg

If not already installed:

```bash
cd c:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

Set environment variable (PowerShell):
```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
[Environment]::SetEnvironmentVariable("Path", "$env:Path;C:\vcpkg", "User")
```

---

## Building the C++ Application

### Initial Setup

**First time only — configure the project:**

```bash
cd e:\UzEngine

# Configure: CMake generates Visual Studio project files
# Automatically runs vcpkg to install dependencies
cmake -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=c:\vcpkg\scripts\buildsystems\vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-windows

# Or use CMake preset (if available):
cmake --preset release
```

**CMAKE_TOOLCHAIN_FILE:** Tells CMake to use vcpkg's integration for finding packages.

**VCPKG_TARGET_TRIPLET:** Specifies the target platform. Options:
- `x64-windows` — Dynamic linking (DLLs), recommended for development
- `x64-windows-static` — Static linking

### Build Commands

**Debug build:**
```bash
cmake --build build --config Debug
# Output: build/Debug/render.exe
```

**Release build (recommended for distribution):**
```bash
cmake --build build --config Release
# Output: build/Release/render.exe
```

**Clean build:**
```bash
cmake --build build --config Release --clean-first
```

### CMake Presets (Optional)

Define build configurations in `CMakePresets.json` for convenience:

```bash
# Configure with preset
cmake --preset release    # Shorthand for Release config + vcpkg setup
cmake --preset debug      # Debug config
cmake --preset test       # Test-instrumented build (phase 3+)
cmake --preset coverage   # Coverage-instrumented build (phase 3+)

# Build with preset
cmake --build --preset release
```

**Note**: Presets are optional. Traditional cmake commands work fine.

### Build Artifacts

After successful build:

```
build/Release/
├── render.exe
├── turbojpeg.dll          ← Copied by POST_BUILD step
├── ixwebsocket.dll        ← (if using dynamic vcpkg triplet)
├── CMakeFiles/
├── cmake_install.cmake
├── Makefile
└── preview/               ← IPC base directory (created by UzEngine at runtime)
    ├── command.json       ← External IPC commands
    └── status.json        ← Engine writes metrics here
```

**DLL Dependency:** The `turbojpeg.dll` is critical for JPEG encoding. The CMake `POST_BUILD` step copies it automatically:

```cmake
add_custom_command(TARGET UzEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/turbojpeg.dll"
        "$<TARGET_FILE_DIR:UzEngine>"
    COMMENT "Copying turbojpeg.dll to output directory"
)
```

If `turbojpeg.dll` is missing:
- Engine will fail to load at startup
- Copy manually: `cp c:\vcpkg\installed\x64-windows\bin\turbojpeg.dll build\Release\`

---

## vcpkg Dependencies and Manifest Mode

### Understanding vcpkg.json (Manifest Mode)

UzEngine uses **vcpkg manifest mode**, where `vcpkg.json` is the single source of truth for dependencies. This approach is superior to global vcpkg installations because:

1. **Reproducible builds** — all developers use the exact same versions
2. **CI/CD friendly** — version lock prevents surprises in pipelines
3. **Per-project isolation** — multiple projects can use different versions
4. **No global state** — no hidden dependencies installed elsewhere

**Location:** `E:\UzEngine\vcpkg.json`

### Defined Dependencies

```json
{
  "dependencies": [
    "ixwebsocket",
    "libjpeg-turbo",
    "nlohmann-json",
    "zlib",
    "luajit",
    "jolt-physics",
    "fastgltf",
    "meshoptimizer",
    "ispc-texcomp",
    "msgpack-c",
    "zstd"
  ]
}
```

### Dependency Details

| Package | Version | Purpose | Notes |
|---------|---------|---------|-------|
| **ixwebsocket** | Latest | WebSocket server | Binary WebSocket frames for streaming |
| **libjpeg-turbo** | Latest | JPEG encoding | TurboJPEG API for fast RGBA→JPEG encoding |
| **nlohmann-json** | Latest | JSON parsing | Command/response format for IPC |
| **zlib** | Latest | Compression | Required by ixwebsocket |
| **luajit** | 2.1 | Lua scripting VM | Phase 3; embedded in game.dll |
| **jolt-physics** | 5.1 | Physics engine | Phase 3; Jolt Physics for rigid bodies, collision |
| **fastgltf** | Latest | glTF 2.0 parsing | Phase 2; meshes with LODs, materials |
| **meshoptimizer** | Latest | Mesh optimization | Phase 2; auto-generate LOD chain (4 levels) |
| **ispc-texcomp** | Latest | Texture compression | Phase 2; BC7, BC5, BC4, BC6H compression |
| **msgpack-c** | Latest | Binary serialization | Phase 3 save-system; msgpack format for saves |
| **zstd** | Latest | Compression | Phase 3 save-system; .uzpak archive compression |

> **Note**: Windows SDK provides **XAudio2** (no vcpkg needed). It's part of the standard Windows development kit.

### Installing/Updating vcpkg Packages

```bash
# Automatic (first build runs vcpkg install)
cmake --build build --config Release

# Manual install/update
vcpkg install ixwebsocket:x64-windows
vcpkg install libjpeg-turbo:x64-windows
```

**Check installed versions:**
```bash
vcpkg list
```

---

## Building the VSCode Extension

### Setup

Navigate to the extension directory:
```bash
cd e:\UzEngine\vscode-uzengine
npm install
```

This installs TypeScript compiler, ESLint, and the VSCode API types.

### Build Steps

**Compile TypeScript to JavaScript:**
```bash
npm run compile
# Output: out/extension.js, out/*.js
```

**In debug mode (F5 in VSCode):**
Opens a new VSCode window with the extension loaded from `src/` (via esbuild watch).

### Packaging

**Build the extension package (`.vsix` file):**
```bash
npm run package
# Installs vsce (Visual Studio Code Extension CLI) if needed
# Outputs: uzengine-<version>.vsix
```

**Install the extension:**
```bash
# Option 1: Via VSCode UI
# Extensions → Install from VSIX... → select .vsix file

# Option 2: Via command line
code --install-extension ./uzengine-0.1.0.vsix
```

### Troubleshooting Extension Builds

**"tsc: command not found"**
```bash
npm install  # Reinstall TypeScript locally
npm run compile
```

**"vsce: command not found"**
```bash
npm run package  # automatically installs vsce
```

**Extension doesn't load in VSCode:**
1. Check VSCode output panel (Help → Toggle Output → VSCode)
2. Look for activation errors
3. Ensure extension.ts exports `activate()` and `deactivate()` functions

---

## Running the Application

### From Command Line (Windowed Debug)

```bash
set UZENGINE_WINDOWED=1
.\build\Release\render.exe
```

Or:
```bash
$env:UZENGINE_WINDOWED='1'
& '.\build\Release\render.exe'  # PowerShell
```

Window title: "UzEngine PoC"

Press **ESC** to close the window.

### Headless Mode (Default, Used by VSCode Extension)

```bash
set UZENGINE_HEADLESS=1
.\build\Release\render.exe
```

No window appears. The HWND still exists (required for DX12 swap chain) but is hidden off-screen.

### Via VSCode Extension

1. Open VSCode in the UzEngine workspace: `code .`
2. Open VSCode command palette: `Ctrl+Shift+P`
3. Run: `UzEngine: Open Stream Viewer`
4. Extension automatically:
   - Starts `render.exe` (headless mode)
   - Opens webview panel with live JPEG stream
   - Shows status bar indicator

To stop the server:
- Click the status bar indicator (`$(debug-stop) UzEngine: running`)
- Or run command: `UzEngine: Stop Server`

---

## Development Workflow

### Typical Iteration Loop

1. **Edit C++ code** in `src/`
2. **Rebuild:**
   ```bash
   cmake --build build --config Release
   ```
   - Relinks render.exe
   - `POST_BUILD` step re-copies turbojpeg.dll
3. **Stop current server** (if running):
   ```bash
   # VSCode: run "UzEngine: Stop Server" command
   # Or kill process manually
   ```
4. **Restart:**
   ```bash
   # VSCode: run "UzEngine: Open Stream Viewer"
   # Or manually: .\build\Release\render.exe
   ```

### Editing Shaders

Shaders are embedded as `constexpr` strings in `src/renderer/renderer.h`:

```c++
constexpr const char* kVertexShaderSrc = R"hlsl(
cbuffer TransformCB : register(b0) {
    float4x4 worldViewProj;
};
// ... HLSL code
)hlsl";
```

**To modify:**
1. Edit the string in `renderer.h`
2. Rebuild: `cmake --build build --config Release`
3. Restart engine

Future versions will support shader hot-reload via IPC.

### Editing VSCode Extension

1. Edit TypeScript in `vscode-uzengine/src/`
2. Open the extension folder in VSCode
3. Press **F5** to launch extension in debug window
4. Edit, save, press **Ctrl+Shift+F5** to reload

Changes to `src/` are compiled on save (esbuild watch).

---

## Environment Variables

| Variable | Value | Effect |
|----------|-------|--------|
| `UZENGINE_HEADLESS` | `1` or empty | Hide window (default when started by extension) |
| `UZENGINE_WINDOWED` | `1` or empty | Show window regardless of HEADLESS setting |

**Example: windowed debug with logging**
```bash
set UZENGINE_WINDOWED=1
set UZENGINE_DEBUG=1
.\build\Release\render.exe
```

---

## Debugging

### C++ Debugging (Visual Studio)

**Open the project in Visual Studio:**
```bash
# After cmake configure
start build/UzEngine.sln
```

**Or debug from command line:**
```bash
# With Visual Studio debugger attached
devenv /debugexe .\build\Release\render.exe
```

### Logging

Engine writes debug output via `OutputDebugStringA()`. View in:
- **Visual Studio:** Debug → Windows → Output
- **DebugView (Sysinternals):** Download from Microsoft

Key logging prefixes:
- `[UzEngine]` — Main engine messages
- `[UzEngine/WS]` — WebSocket server events
- `[UzEngine/IPC]` — IPC poller (if enabled)

### Breaking on Errors

In VS debugger, set a breakpoint in error handlers:
- `ThrowIfFailed()` in `renderer.cpp`
- `JpegEncoder::encode()` in `jpeg_encoder.cpp`

---

## Asset Building with UzBuild.exe

**UzBuild.exe** is an offline asset compiler that prepares your game for distribution. It runs in the background during Phase 1+, but you should understand what it does.

### Purpose

UzBuild.exe:
1. **Imports assets** — scans `assets/` and converts to engine-native formats (`.uzmesh`, `.uztex`, etc.)
2. **Compiles shaders** — uses DXC to offline-compile all HLSL shaders
3. **Generates LODs** — meshoptimizer creates 4 LOD levels with appropriate screen coverage
4. **Compresses textures** — BC7/BC5/BC4 compression (optimized for distribution)
5. **Packs assets** — creates `.uzpak` (compressed asset archive)

### Usage

```bash
# Full asset pipeline
UzBuild.exe --project res://myproject --output dist/
  → Input: assets/ (meshes, textures, shaders)
  → Process: import, compile, optimize, compress
  → Output: dist/game.uzpak + dist/render.exe + runtime DLLs

# Verify assets in editor
# UzBuild runs automatically on file change via FileWatcher during Phase 1+
```

### Output Structure

```
dist/
├── render.exe          (engine executable)
├── game.dll              (game logic, linked against assets)
├── game.uzpak            (all imported assets, compressed)
├── turbojpeg.dll         (JPEG decoder for screenshots)
└── (other runtime DLLs)
```

> **Status**: UzBuild.exe is not yet implemented. Asset import pipeline begins in Phase 2. Phase 1 focuses on scene system and basic rendering.

---

## game.dll Hot-Reload

Once Phase 2 is implemented, you can rebuild `game.dll` while the editor is running, and it will auto-reload:

```bash
# Terminal 1: VSCode with engine running
# Terminal 2: Rebuild game.dll
cmake --build build --config Release

# Watch the VSCode output channel → "game.dll reloaded" message
# All Lua scripts, physics, and game logic refresh instantly
# No editor restart needed
```

This tight iteration loop is essential for rapid AI-driven development.

---

## Common Build Issues

### "turbojpeg.dll not found"

**Cause:** POST_BUILD step didn't run or failed.

**Fix:**
```bash
# Manually copy DLL
xcopy c:\vcpkg\installed\x64-windows\bin\turbojpeg.dll build\Release\
```

Then rebuild to verify the step executes.

### "CMake cannot find vcpkg"

**Cause:** `-DCMAKE_TOOLCHAIN_FILE=` path is incorrect.

**Fix:** Ensure `C:\vcpkg` exists and the path is correct:
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

### "D3DCompiler.lib not found"

**Cause:** Windows SDK not installed.

**Fix:** Reinstall Visual Studio and ensure "DirectX 12" workload is selected.

### "ixwebsocket not found" or linker errors

**Cause:** vcpkg dependencies not installed.

**Fix:** Ensure `vcpkg.json` exists and re-run cmake:
```bash
rm -r build/
cmake -B build -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

### "npm ERR! code ERESOLVE" when installing extension

**Cause:** Node.js package dependency conflict.

**Fix:**
```bash
cd vscode-uzengine
npm install --legacy-peer-deps
```

---

## Build Performance

Typical build times (Release):

| Target | Time |
|--------|------|
| Clean build (first time) | 1–2 minutes (vcpkg installs deps) |
| Incremental build (code change) | 5–15 seconds |
| Full rebuild (Clean + build) | 30–60 seconds |

**To speed up:**
- Use Release config (optimization enabled)
- Ensure SSD (vs HDD)
- Disable antivirus real-time scanning on build directory

---

## Further Reading

- **CMake:** [cmake.org](https://cmake.org)
- **vcpkg:** [github.com/microsoft/vcpkg](https://github.com/microsoft/vcpkg)
- **DirectX 12:** [Microsoft Docs](https://docs.microsoft.com/en-us/windows/win32/direct3d12/)
- **libjpeg-turbo:** [TurboJPEG API](https://libjpeg-turbo.org/About/TurboJPEG)
- **IXWebSocket:** [github.com/machinezone/IXWebSocket](https://github.com/machinezone/IXWebSocket)
