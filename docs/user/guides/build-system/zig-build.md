# Build System — Zig

UzEngine uses Zig as its primary build system and C/C++ compiler toolchain.
CMake is also supported for MSVC and Clang builds (see `CMakePresets.json`).
Three compilers are verified: Zig, MSVC, and Clang.

> **File**: `runtime/build.zig`

## Why Zig?

| Advantage | Description |
|-----------|-------------|
| Cross-compilation | Build for Linux from Windows with `-Dtarget=x86_64-linux-gnu` |
| Bundled C/C++ compiler | No MSVC or Clang install required |
| Reproducible builds | Same binary on every machine |
| Fast incremental builds | Zig's cache tracks file content hashes |
| Single file build definition | `build.zig` replaces CMakeLists.txt + toolchain files |

## Build Targets

| Step | Command | Output |
|------|---------|--------|
| `zig build` | Build everything | `build/bin/` |
| `zig build test` | Run unit tests | Test results on stdout |
| `zig build run` | Launch runtime | Window with clear screen |
| `zig build demo` | Launch demo | Animated color demo |

## Build Structure

The build produces one static library and multiple executables:

```
build.zig
├── uzengine (static lib) ─── All engine code
│   ├── core/ sources
│   ├── platform/windows/ sources (Windows only)
│   ├── core/rhi/dx12/ sources (Windows only)
│   └── thirdparty/ (spdlog, Tracy, D3D12MA)
│
├── uzengine.exe ─── Standalone runtime (links static lib)
├── clear_screen_demo.exe ── DX12 demo (links static lib)
└── test_runtime.exe ─────── GoogleTest runner (links static lib)
```

## Platform Guards

Windows-only code is guarded with `is_windows`:

```zig
const is_windows = target.result.os.tag == .windows;

if (is_windows) {
    // DX12 backend, platform/windows, system libraries
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/rhi/dx12"),
        .files = &.{ "dx12_device.cpp", "dx12_swapchain.cpp", ... },
    });
    lib.linkSystemLibrary("d3d12");
    lib.linkSystemLibrary("dxgi");
}
```

Cross-platform code (math, logging, config, containers) compiles on all targets.

## Include Path Organization

Include paths are split into four tables:

| Table | Scope | Examples |
|-------|-------|---------|
| `engine_include_dirs` | Cross-platform engine | `src`, `core/logging`, `core/math` |
| `thirdparty_include_dirs` | Cross-platform deps | `spdlog/include`, `directxmath/Inc` |
| `windows_engine_include_dirs` | Windows-only engine | `platform/windows` |
| `windows_thirdparty_include_dirs` | Windows-only deps | `d3d12ma/include`, `agility/include` |

## Agility SDK Deployment

On Windows, the build automatically copies Agility SDK DLLs:

```zig
b.getInstallStep().dependOn(&b.addInstallBinFile(
    b.path(tp ++ "agility/bin/x64/D3D12Core.dll"),
    "D3D12/D3D12Core.dll",
).step);
```

This ensures `D3D12Core.dll` is in `build/bin/D3D12/` next to the executables,
which is where the Agility SDK activation code (`dx12_agility.cpp`) expects it.

## DXC Shader Compiler Deployment

DXC (DirectX Shader Compiler) DLLs are deployed to `build/bin/` alongside executables:

```
build/bin/
    dxcompiler.dll    # HLSL → DXIL/SPIRV compiler
    dxil.dll          # DXIL validator
```

The `ShaderCompiler` class loads `dxcompiler.dll` at runtime via `LoadLibrary`.
Path resolution is handled by `uz::ResolveEngineResource()`, which resolves
relative paths against the executable directory — never CWD, never absolute paths.

## Engine Resource Path Convention

All engine-internal resources (DLLs, shaders, configs) are discovered relative
to the running executable via `uz::GetExeDir()` (defined in `core/paths/paths.h`).

| Pattern | Description |
|---------|-------------|
| Same-dir DLLs | `dxcompiler.dll`, `dxil.dll` — loaded via `LoadLibrary` |
| Subdirectory DLLs | `D3D12/D3D12Core.dll` — loader-specific convention |
| Future: shaders | `shaders/` — built-in shader sources |

**Rules:**
- NEVER use absolute paths for engine resources
- NEVER search relative to CWD (`../` chains are fragile)
- ALWAYS resolve relative to the executable directory
- Build system MUST deploy all runtime dependencies to `build/bin/`

This follows the same pattern used by Unreal Engine (`FPlatformProcess::BaseDir()`)
and Godot (`OS::get_executable_path().get_base_dir()`).

## Cross-Compilation

Build for Linux from Windows:

```bash
zig build -Dtarget=x86_64-linux-gnu
```

This compiles all cross-platform code (math, containers, logging, config, RHI
types). DX12 backend and Windows platform code are automatically excluded.

## Compiler Flags

All C++ sources use:
- `-std=c++17`
- `-DSPDLOG_COMPILED_LIB`

D3D12MA has additional flags for Clang/MinGW compatibility:
- `-Wno-undefined-bool-conversion`
- `-Wno-switch`
- `-Wno-unused-private-field`

## Related Pages

- [Building from Source](../../getting-started/building.md) — Step-by-step setup
- [Architecture](../architecture.md) — System design
