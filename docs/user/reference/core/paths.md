# Engine Path Resolution

Exe-relative path resolution for engine resources and configuration files.

> **Header**: `runtime/core/paths/paths.h` |
> **API**: [GetExeDir()](#GetExeDir) | [ResolveEngineResource()](#ResolveEngineResource)

## Overview

UzEngine follows the **UE5/Godot pattern** of discovering all resources relative to the executable directory, never relative to the current working directory.

This ensures:
- **Portability**: Moving the installation directory works without path updates
- **Determinism**: No surprises from CWD changes by other processes
- **Clarity**: All paths traceable to a fixed root (the executable)

---

## GetExeDir()

Returns the directory containing the running executable.

### Signature

```cpp
std::filesystem::path GetExeDir();
```

### Returns

Absolute path to the directory containing the executable.

**Examples:**

| Executable | GetExeDir() Return |
|------------|-------------------|
| `C:/project/build/bin/uzengine.exe` | `C:/project/build/bin` |
| `/usr/local/bin/uzengine` | `/usr/local/bin` |
| `/home/user/game/build/bin/uzengine` | `/home/user/game/build/bin` |

### Caching

The result is **cached after first call** for performance. Subsequent calls return the cached value.

### Platform Implementation

| Platform | API |
|----------|-----|
| Windows | `GetModuleFileNameW()` → exe path → parent directory |
| Linux | Read `/proc/self/exe` symlink → parent directory |
| macOS | `_NSGetExecutablePath()` → parent directory |

---

## ResolveEngineResource()

Resolves a resource path relative to the executable directory.

### Signature

```cpp
std::filesystem::path ResolveEngineResource(const std::string& relativePath);
```

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `relativePath` | `const std::string&` | Resource path (relative or absolute) |

### Returns

Absolute path to the resource.

**Behavior:**
- If input is absolute → returns unchanged
- If input is relative → prepends `GetExeDir()`

### Examples

```cpp
#include "core/paths/paths.h"

// Relative paths (common case)
uz::ResolveEngineResource("dxcompiler.dll");
// → "C:/project/build/bin/dxcompiler.dll"

uz::ResolveEngineResource("D3D12/D3D12Core.dll");
// → "C:/project/build/bin/D3D12/D3D12Core.dll"

uz::ResolveEngineResource("shaders/engine/pbr.hlsl");
// → "C:/project/build/bin/shaders/engine/pbr.hlsl"

// Absolute paths (returned unchanged)
uz::ResolveEngineResource("C:/custom/path/shader.hlsl");
// → "C:/custom/path/shader.hlsl"

uz::ResolveEngineResource("/usr/share/uzengine/data");
// → "/usr/share/uzengine/data"
```

---

## Build Layout

The build system (Zig) deploys resources to `build/bin/`:

```
build/bin/
├── uzengine.exe         # Main executable
├── test_runtime.exe              # Test runner
├── dxcompiler.dll                # DXC HLSL compiler (Windows)
├── dxil.dll                      # DXC SPIR-V validator (Windows)
├── libdxcompiler.so              # DXC HLSL compiler (Linux)
├── libdxil.so                    # DXC validator (Linux)
├── D3D12/                        # Agility SDK (Windows only)
│   ├── D3D12Core.dll
│   └── d3d12SDKLayers.dll
└── shaders/                      # Future: built-in shader sources
    ├── engine/
    │   ├── common.hlsli
    │   └── pbr.hlsl
    └── game/
        └── ...
```

The Zig build script uses `addInstallBinFile()` to deploy DLLs alongside executables.

---

## Common Use Cases

### Loading DXC (HLSL Compiler)

```cpp
#include "core/paths/paths.h"
#include <windows.h>

auto dxcPath = uz::ResolveEngineResource("dxcompiler.dll");
HMODULE dxcModule = LoadLibraryW(dxcPath.c_str());
if (!dxcModule) {
    UZ_LOG_ERROR(Core, "Failed to load DXC: {}", dxcPath.string());
    return false;
}
```

### Loading Agility SDK (DX12 Runtime)

```cpp
#include "core/paths/paths.h"
#include <windows.h>

// Set Agility SDK path before calling D3D12GetInterface()
auto agilitySdkPath = uz::ResolveEngineResource("D3D12");
SetDllDirectoryW(agilitySdkPath.c_str());

// Now D3D12Core.dll is found in D3D12/ subdirectory
HRESULT hr = D3D12GetInterface(CLSID_D3D12Device, IID_PPV_ARGS(&device));
```

### Loading Shader Files (Development Mode)

```cpp
#include "core/paths/paths.h"
#include "core/shader/shader_vfs.h"

uz::ShaderVFS vfs;

// Mount shader directories
auto engineShadersPath = uz::ResolveEngineResource("shaders/engine");
vfs.Mount("/engine", engineShadersPath.string());

auto gameShadersPath = uz::ResolveEngineResource("shaders/game");
vfs.Mount("/game", gameShadersPath.string());
```

### Loading Configuration Files

```cpp
#include "core/paths/paths.h"
#include "core/config/config.h"

// Load project config
auto projectPath = uz::ResolveEngineResource("project.uzproject");
Config config;
config.LoadFromFile(projectPath.string());
```

---

## Why Exe-Relative?

### Problem: CWD-Relative Paths

```cpp
// ❌ WRONG: Depends on where process is launched from
std::ifstream file("shaders/pbr.hlsl");
// Works from project root: ./build/bin/uzengine --scene scene.uzscene
// Fails from other dir: cd / && /path/to/project/build/bin/uzengine --scene scene.uzscene
```

### Problem: Absolute Paths

```cpp
// ❌ WRONG: Hardcoded, breaks on move
const char* path = "C:/Users/alice/project/shaders/pbr.hlsl";
// Works on Alice's machine
// Fails on Bob's machine (different user path)
// Fails if installation directory moves
```

### Solution: Exe-Relative

```cpp
// ✅ CORRECT: Always portable
auto path = uz::ResolveEngineResource("shaders/pbr.hlsl");
// Works anywhere: installation directory is self-contained
```

Benefits:
- **Portable**: Installation can be anywhere
- **Multi-user**: Works on any machine
- **Modular**: Editor, runtime, tests all find same resources
- **Clear**: Single source of truth (GetExeDir)

---

## Testing & CI

### Test Runner

Tests run from the `build/bin/` directory via `zig build test`:

```bash
$ zig build test
# Internally runs: ./build/bin/test_runtime
# GetExeDir() returns ./build/bin
# All paths resolve correctly
```

**Before:** Tests ran from `.zig-cache/`, breaking `GetExeDir()`.

**Fix:** `addInstallBinFile()` copies test executable to `build/bin/`, so all tests run from the same directory as production executables.

---

## Platform Notes

### Windows

`GetExeDir()` uses `GetModuleFileNameW()`:

```cpp
WCHAR buffer[MAX_PATH];
GetModuleFileNameW(nullptr, buffer, MAX_PATH);
// buffer = "C:\\project\\build\\bin\\uzengine.exe"
// Extract parent directory
```

### Linux / macOS

`GetExeDir()` reads the executable symlink:

```cpp
// Linux: /proc/self/exe
// macOS: _NSGetExecutablePath()
// Both return executable path, extract parent
```

---

## Complete Example

```cpp
#include "core/paths/paths.h"
#include "core/config/config.h"
#include "core/shader/shader_vfs.h"
#include <iostream>

int main() {
    // Get exe directory (cached)
    auto exeDir = uz::GetExeDir();
    std::cout << "Exe directory: " << exeDir << "\n";

    // Resolve DXC (shipped alongside exe)
    auto dxcPath = uz::ResolveEngineResource("dxcompiler.dll");
    std::cout << "DXC path: " << dxcPath << "\n";

    // Resolve project config
    auto projectPath = uz::ResolveEngineResource("project.uzproject");
    std::cout << "Project config: " << projectPath << "\n";

    // Resolve shader directories (shipped with exe)
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", uz::ResolveEngineResource("shaders/engine").string());
    vfs.Mount("/game", uz::ResolveEngineResource("shaders/game").string());

    // All paths are relative to the executable, always correct
    return 0;
}
```

---

## Related Pages

- [Architecture](../architecture.md) — System design
- [Getting Started](../../getting-started/first-window.md) — Practical walkthrough
- [Build System](../build-system/zig-build.md) — How Zig deploys resources
