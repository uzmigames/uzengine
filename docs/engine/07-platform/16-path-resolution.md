# Platform — File I/O & Path Resolution

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/paths/paths.h/.cpp`
> **Depends on**: None (foundation system)

## Purpose

Game engines constantly access files — shaders, textures, meshes, configs, logs. A robust path resolution system prevents the most common class of "works on my machine" bugs: paths that resolve differently depending on the working directory, install location, or OS.

UzEngine resolves all paths relative to the executable location via `uz::GetExeDir()`, never from the current working directory.

## Path Resolution API

```cpp
namespace uz {

/// Returns the directory containing the running executable.
///
/// On Windows, uses GetModuleFileNameW to find the exe path,
/// then strips the filename to get the parent directory.
///
/// # Returns
/// Absolute path with trailing separator (e.g., "E:/UzEngine/0.2/runtime/build/bin/")
///
/// # Thread Safety
/// Thread-safe — result is computed once and cached.
std::string GetExeDir();

/// Resolves an engine resource path relative to the executable directory.
///
/// # Arguments
/// * `relativePath` — Forward-slash path relative to exe dir
///   (e.g., "shaders/pbr.hlsl", "D3D12/D3D12Core.dll")
///
/// # Returns
/// Absolute path (e.g., "E:/UzEngine/0.2/runtime/build/bin/shaders/pbr.hlsl")
std::string ResolveEngineResource(const std::string& relativePath);

} // namespace uz
```

## Why Exe-Relative (Not CWD)?

Consider two scenarios:

```
# Scenario 1: Run from build directory
cd E:/UzEngine/0.2/runtime/build/bin
./uzengine.exe --project ../../assets/game.uzproject
# CWD = build/bin/ — relative paths resolve correctly ✓

# Scenario 2: Run from project root
cd E:/UzEngine/0.2
runtime/build/bin/uzengine.exe --project assets/game.uzproject
# CWD = 0.2/ — "shaders/pbr.hlsl" resolves to wrong location ✗
```

CWD-relative paths fail in Scenario 2. Exe-relative paths always resolve to the same location regardless of where the user runs the command from. This is especially important for:

- **VSCode extension**: the working directory is the VSCode workspace, not the engine install
- **`zig build test`**: the test runner's CWD varies by build system configuration
- **CI/CD**: build agents run from arbitrary directories

## DLL Resolution

The engine finds deployed DLLs using `GetExeDir()`:

```cpp
// In ShaderCompiler — loading DXC
std::string dxcPath = uz::GetExeDir() + "dxcompiler.dll";
HMODULE hDxc = LoadLibraryA(dxcPath.c_str());

// The Agility SDK uses exported symbols:
extern "C" {
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}
// D3D12 loader resolves this relative to the exe automatically
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Path resolution | `FPaths::EngineDir()`, `FPaths::ProjectDir()`, etc. | `OS::get_executable_path()`, `ProjectSettings::globalize_path()` | `uz::GetExeDir()`, `uz::ResolveEngineResource()` |
| Base directory | Engine install + project root (separate) | `res://` (project root) + `user://` (user data) | Exe directory (single root) |
| Virtual filesystem | `FPakPlatformFile` (layered, with priority) | `FileAccess` with `res://` and `user://` schemes | ShaderVFS (mount points, section 05) |
| Path separators | Forward slashes internally, platform-native for OS calls | Forward slashes everywhere | Forward slashes everywhere |

### UE5's Path Abstraction

UE5 has an elaborate path system with 20+ `FPaths::*` methods because the engine and project live in separate directory trees. UzEngine's simpler model (everything relative to exe) works because:

1. The engine is distributed as a compiled library, not an installed application with a separate project structure
2. During development, the exe and all resources are in the same `build/bin/` directory
3. In shipping, a build tool (uzcook) packs everything into a self-contained directory

## Rules

1. **ALWAYS use `uz::GetExeDir()` for engine resources** — never hardcode paths or use CWD
2. **ALWAYS use forward slashes** in path strings — Windows APIs accept them, and they're cross-platform
3. **NEVER use `../` chains** to navigate out of the exe directory — fragile and fails when directory structure changes
4. **ALWAYS log the resolved path on failure** — "Failed to load: path='{}'" must show the absolute path that was tried
