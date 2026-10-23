# Build Toolchain — Build Configurations & Deployment

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/build.zig`, `runtime/engine/rhi/dx12/dx12_agility.cpp`
> **Depends on**: Zig build system

## Purpose

UzEngine supports multiple build configurations that control optimization levels, debug features, and diagnostic overhead. The Zig build system exposes these via the standard `-Doptimize=` flag.

## Optimization Levels

| Zig Flag | C++ Equivalent | Use Case | Debug Info | Assertions | Tracy | GPU Breadcrumbs |
|----------|----------------|----------|------------|------------|-------|-----------------|
| `Debug` (default) | `-O0 -g` | Development, debugging | Full | All (CHECK, ENSURE, VERIFY) | ON_DEMAND | Enabled |
| `ReleaseSafe` | `-O2 -g` | Testing, profiling | Full | VERIFY only (others stripped) | ON_DEMAND | Enabled |
| `ReleaseFast` | `-O3` | Shipping, benchmarks | None | VERIFY only | Disabled | Disabled |
| `ReleaseSmall` | `-Os` | Embedded/mobile | None | VERIFY only | Disabled | Disabled |

### Debug Build

```bash
zig build                          # Default = Debug
zig build -Doptimize=Debug         # Explicit
```

- Full debug symbols for step-debugging with VS Code / WinDbg
- All assertion levels active: `UZ_CHECK` (fatal), `UZ_ENSURE` (non-fatal), `UZ_VERIFY` (fatal)
- D3D12 Debug Layer enabled (validation messages in output)
- GPU breadcrumbs + DRED enabled
- Tracy profiling available (connects on demand)
- spdlog minimum level: Trace

### Release Build

```bash
zig build -Doptimize=ReleaseFast   # Shipping performance
```

- Full optimization (`-O3`), no debug symbols
- `UZ_CHECK` and `UZ_ENSURE` stripped (zero cost in release)
- `UZ_VERIFY` preserved (side-effect expressions must still execute)
- D3D12 Debug Layer disabled
- Tracy compiled out (`TRACY_ENABLE` not defined in release builds)
- spdlog minimum level: Info

## Build Options

| Zig option | CMake cache variable | Default | Effect |
|------------|----------------------|---------|--------|
| `-Dtracy=true\|false` | `UZ_TRACY=AUTO\|ON\|OFF` | On for `Debug`/`ReleaseSafe` (CMake `Debug`/`RelWithDebInfo`), off for `ReleaseFast`/`ReleaseSmall` (CMake `Release`/`MinSizeRel`) | Compiles the Tracy profiler client (`TRACY_ENABLE`, `TRACY_ON_DEMAND`). When off, `UZ_PROFILE_*` CPU macros expand to nothing and no profiler socket exists. |

When Tracy is on, the build always adds `TRACY_ONLY_LOCALHOST` and
`TRACY_NO_BROADCAST`: the client listens on loopback only and sends no UDP
discovery broadcasts. The Tracy protocol lets a connected viewer read arbitrary
process memory (symbol code queries) and open files by path (source queries),
so the listener must never be reachable from the network. To profile a remote
machine, forward the port over SSH (`ssh -L 8086:localhost:8086 host`) instead
of widening the bind.

```bash
zig build -Doptimize=ReleaseFast -Dtracy=true   # Profile an optimized build
zig build -Dtracy=false                          # Debug build without Tracy
```

The engine's CMake path ignores Tracy on MSVC (`TRACY_ENABLE` is never defined
for the MSVC compiler).

## DLL Deployment

### Agility SDK

The DX12 Agility SDK allows UzEngine to use the latest D3D12 runtime features regardless of the user's Windows version. Without it, features like Enhanced Barriers or Shader Model 6.7 require Windows 11 updates.

Deployment layout:
```
build/bin/
├── uzengine.exe
└── D3D12/
    ├── D3D12Core.dll          # Runtime (contains the actual D3D12 implementation)
    └── d3d12SDKLayers.dll     # Debug layer (validation, error messages)
```

The engine declares the Agility SDK version via an exported symbol in `dx12_agility.cpp`:

```cpp
extern "C" {
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = 619;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}
```

The D3D12 loader reads these exports at runtime and loads `D3D12Core.dll` from the specified path instead of the system-installed version.

### DXC (DirectX Shader Compiler)

DXC is loaded dynamically at runtime (not linked at build time). The engine uses `LoadLibrary`/`DxcCreateInstance` to create compiler and utility objects.

```
build/bin/
├── uzengine.exe
├── dxcompiler.dll             # HLSL → DXIL/SPIR-V compiler
└── dxil.dll                   # DXIL validation (required for signed shader bytecode)
```

DXC is resolved via `uz::GetExeDir()` — the engine finds DLLs relative to its own executable path, never via system PATH or working directory.

## Cross-Platform Considerations

### Current: Windows x86_64

The primary development platform. All features available:
- DX12 SM6.6 bindless rendering
- D3D12MA GPU memory allocation
- Media Foundation H.264 encoding (for webview streaming)
- Win32 window management

### Planned: Linux x86_64

When the Vulkan backend is implemented:
- Zig cross-compiles from Windows: `zig build -Dtarget=x86_64-linux`
- Vulkan replaces DX12 (via SPIRV-Cross from HLSL)
- SDL2 or native X11/Wayland for window management
- No Agility SDK or DXC deployment needed (Vulkan uses SPIR-V directly)

### Planned: macOS aarch64

When the Metal backend is implemented:
- Zig cross-compiles: `zig build -Dtarget=aarch64-macos`
- Metal replaces DX12 (via SPIRV-Cross HLSL → MSL)
- Cocoa/AppKit for window management

## Output Directory Structure

```
runtime/build/
├── bin/
│   ├── uzengine.exe              # Standalone runtime
│   ├── uzcook.exe                # Asset cook tool
│   ├── uzembed.exe               # Shader embed tool
│   ├── test_runtime.exe          # Unit tests (GoogleTest)
│   ├── dxcompiler.dll            # DXC shader compiler
│   ├── dxil.dll                  # DXIL validation
│   └── D3D12/
│       ├── D3D12Core.dll         # Agility SDK runtime
│       └── d3d12SDKLayers.dll    # Agility SDK debug layer
├── lib/
│   └── uzengine_runtime.lib      # Static library
└── generated/
    └── shaders/
        ├── *.hlsl.gen.h          # Embedded shader headers
        └── engine_shaders.gen.h  # Master shader index
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Build configs | Development, DebugGame, Shipping (+ Test, Editor) | `debug`, `release`, `release_debug` | Zig's 4 optimize levels |
| SDK deployment | Bundled in engine install (multi-GB) | Vulkan SDK via system install | **Agility SDK DLLs** next to exe |
| Shader compiler | Bundled DXC/FXC (editor-time) | GLSL at import, SPIR-V cached | **DXC DLLs** (runtime dynamic load) |
| Cross-compilation | Requires per-platform toolchain | Requires per-platform toolchain | **Zig built-in** (`-Dtarget=...`) |

## Rules

1. **ALWAYS use `uz::GetExeDir()` for DLL paths** — never hardcode paths or rely on working directory
2. **NEVER ship with D3D12 debug layer enabled** — `d3d12SDKLayers.dll` adds significant overhead
3. **ALWAYS strip `UZ_CHECK`/`UZ_ENSURE` in release** — only `UZ_VERIFY` survives (has side effects)
4. **ALWAYS export `D3D12SDKVersion` and `D3D12SDKPath`** — the Agility SDK requires these symbols
5. **NEVER rely on system-installed DX12 runtime** — always deploy Agility SDK for feature consistency
