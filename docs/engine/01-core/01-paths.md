# Core — Path Resolution

> **Status**: Done
> **Source**: `runtime/engine/paths/paths.h`, `runtime/engine/paths/paths.cpp`
> **Depends on**: Nothing (leaf module)

## Purpose

All engine resources — DLLs, shaders, configs, SDK binaries — MUST be discovered relative to the running executable. Never relative to the current working directory, never via hardcoded absolute paths. This is a foundational invariant that ensures the engine works regardless of where it's launched from.

## Why Exe-Relative Resolution

Every major engine uses this pattern:

| Engine | Function | Behavior |
|--------|----------|----------|
| **UE5** | `FPlatformProcess::BaseDir()` | Returns directory of the running executable |
| **Godot** | `OS::get_executable_path().get_base_dir()` | Same — exe directory |
| **UzEngine** | `uz::GetExeDir()` | Same — exe directory |

The reason is simple: games (and engines) are distributed as a directory tree rooted at the executable. The player doesn't control CWD — it depends on how they launched the app (shortcut, terminal, file association). Only the exe path is reliable.

## API

### `uz::GetExeDir()`

Returns the directory containing the running executable. Result is cached after the first call (thread-safe — the first call computes it, subsequent calls return the cached value).

```cpp
#include "runtime/engine/paths/paths.h"

std::filesystem::path dir = uz::GetExeDir();
// If exe is at "C:/project/build/bin/test_runtime.exe":
// dir == "C:/project/build/bin"
```

**Platform implementation:**
- **Windows**: `GetModuleFileNameW(nullptr, ...)`
- **Linux**: `/proc/self/exe` (readlink)
- **macOS**: `_NSGetExecutablePath()`

### `uz::ResolveEngineResource(relativePath)`

Resolves a resource path relative to the executable directory. If the input is already absolute, returns it unchanged.

```cpp
auto dxc = uz::ResolveEngineResource("dxcompiler.dll");
// → "C:/project/build/bin/dxcompiler.dll"

auto d3d12 = uz::ResolveEngineResource("D3D12/D3D12Core.dll");
// → "C:/project/build/bin/D3D12/D3D12Core.dll"

auto abs = uz::ResolveEngineResource("C:/absolute/path.dll");
// → "C:/absolute/path.dll" (unchanged — already absolute)
```

## Layout Convention

The build system deploys all engine resources to `zig-out/bin/` (or `build/bin/` depending on toolchain):

```
build/bin/
    uzengine.exe             # Engine executable
    test_runtime.exe         # Test executable
    dxcompiler.dll           # DXC shader compiler
    dxil.dll                 # DXC validator
    D3D12/                   # Agility SDK subdirectory
        D3D12Core.dll
        d3d12SDKLayers.dll
    shaders/                 # Built-in shaders (future)
```

The Zig build system uses `addInstallBinFile` to deploy DLLs alongside the executable, ensuring `ResolveEngineResource()` always finds them.

## Design Decisions

### Why Not CWD-Relative

POC 0.1 used `../` chains relative to CWD. This broke constantly:
- Running from IDE vs command line produced different CWDs
- Tests run from `.zig-cache/` which is nowhere near the resource directory
- Deployed builds would need the user to cd to the right directory

### Why Not Environment Variables

Environment variables are invisible state. They cause "works on my machine" bugs that are hard to diagnose. The exe path is deterministic and self-contained.

### Test Runner Integration

The test runner (`zig build test`) is configured to run from `zig-out/bin/` so `GetExeDir()` finds deployed DLLs. This was a specific fix — previously tests ran from `.zig-cache/` and couldn't find DXC.

## Rules

1. **NEVER** use CWD-relative paths (`../`, `./`) for engine resources
2. **NEVER** hardcode absolute paths
3. **ALWAYS** use `uz::GetExeDir()` or `uz::ResolveEngineResource()` for locating engine files
4. **ALWAYS** deploy resources alongside the executable via the build system
