# Building from Source

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| [Zig](https://ziglang.org/download/) | 0.15+ | Build system and C/C++ compiler |
| [Git](https://git-scm.com/) | 2.40+ | Source control |
| Windows 10/11 | 19041+ | Required for DX12 (Linux cross-compile supported) |
| [PowerShell](https://github.com/PowerShell/PowerShell) | 7+ | Setup script (Windows) |

Optional:
- [Tracy](https://github.com/wolfpld/tracy) — CPU/GPU profiler viewer
- GPU with DX12 support (Feature Level 12.0 minimum, 12.2 recommended for SM6.6)

## Clone and Setup

```bash
git clone https://github.com/uzmigames/uzengine.git
cd UzEngine
```

Run the setup script to download third-party dependencies:

```powershell
./scripts/setup.ps1
```

This clones and configures all third-party dependencies. See [`runtime/thirdparty/README.md`](../../../runtime/thirdparty/README.md) for the full list with versions and licenses.

## Build

All builds run from the `runtime/` directory:

```bash
cd runtime
```

### Option 1: Zig (default, recommended)

```bash
zig build                              # Debug build
zig build -Doptimize=ReleaseFast       # Release build
zig build -Dtarget=x86_64-linux-gnu    # Cross-compile to Linux
```

Output goes to `zig-out/bin/`:
- `uzengine.exe` — Standalone runtime
- `test_runtime.exe` — Unit test runner
- `dxcompiler.dll` / `dxil.dll` — DXC shader compiler (auto-deployed)
- `D3D12/D3D12Core.dll` — Agility SDK runtime (auto-deployed)

Demos (`clear_screen`, `triangle`) are standalone projects in `demos/` with their own `build.zig`.

### Option 2: CMake (MSVC or Clang)

CMake presets are provided for MSVC and Clang on Windows:

```bash
# MSVC
cmake --preset msvc-debug
cmake --build build/msvc-debug

# Clang
cmake --preset clang-debug
cmake --build build/clang-debug

# Release variants
cmake --preset msvc-release
cmake --build build/msvc-release
```

Available presets: `msvc-debug`, `msvc-release`, `clang-debug`, `clang-release`.

Output goes to `build/<preset>/Debug/` (or `Release/`).

> **Note**: Linux builds (Zig only) compile the math library and cross-platform core.
> DX12 backend and platform/windows sources are automatically excluded.

## Build Targets

| Command | Description |
|---------|-------------|
| `zig build` | Build all targets (library + executables) |
| `zig build test` | Build and run all unit tests |
| `zig build run` | Build and launch the runtime executable |

## Run Tests

```bash
zig build test
```

42 test files across:
- Core systems (logging, assertions, config, profiling, memory, paths)
- Math (vectors, matrices, quaternions, transforms, color, bounds)
- Containers (ring buffer, free list, handle map, frame array)
- Shader pipeline (VFS, source manager, dependency graph, cache, compiler, pipeline, permutations, reflection, hot-reload, archive)
- Asset pipeline (VFS, cache, registry, manifest, importer sidecar, pak format, uzshader format)
- RHI (types, config resolution, DX12 device, DX12 resources, DX12 render loop)
- Platform (input system)

## Verify the Build

Run the standalone runtime:

```bash
zig build run
```

You should see a window with an animated color-cycling clear screen and log output:

```
[Engine] UzEngine Runtime starting
[Engine] RHI backend:       DX12
[Engine] Shader model:      SM6.6
[Engine] Adapter: NVIDIA GeForce RTX 4090
[Engine] Feature level: D3D_FEATURE_LEVEL_12_2
```

## Troubleshooting

**`error: unable to find 'd3d12'`** — You're on a non-Windows platform. DX12 builds
require Windows. Use cross-compilation target or build on Windows.

**Agility SDK not loading (FL < 12.2 on capable hardware)** — Ensure
`build/bin/D3D12/D3D12Core.dll` exists. The build deploys it automatically.

**Stale build cache** — If you see unexpected behavior after code changes:
```bash
rm -rf .zig-cache && zig build
```

## Next Steps

Continue to [Your First Window](first-window.md) to understand the render loop.
