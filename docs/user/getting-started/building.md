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

This clones and configures:
- **spdlog** — Structured logging
- **Tracy** — CPU/GPU profiling
- **mimalloc** — Memory allocator
- **DirectXMath** — SIMD math (+ sal.h stub for cross-platform)
- **nlohmann/json** — JSON parsing
- **GoogleTest** — Unit testing
- **D3D12 Memory Allocator** — GPU memory management
- **DirectX Headers** — DX12 API headers for Clang/MinGW
- **Agility SDK** — Latest DX12 runtime (enables SM6.6 + FL 12.2)
- **spirv-reflect** — Shader reflection
- **xxHash** — Fast hashing
- **atomic_queue** — Lock-free queues

## Build

All builds use Zig from the `runtime/` directory:

```bash
cd runtime
```

### Debug Build (default)

```bash
zig build
```

Output goes to `zig-out/bin/`:
- `uzengine_runtime.exe` — Standalone runtime
- `clear_screen_demo.exe` — DX12 animated clear demo
- `test_runtime.exe` — Unit test runner
- `D3D12/D3D12Core.dll` — Agility SDK runtime (auto-deployed)

### Release Build

```bash
zig build -Doptimize=ReleaseFast
```

### Cross-Compile to Linux

```bash
zig build -Dtarget=x86_64-linux-gnu
```

> **Note**: Linux builds compile the math library and cross-platform core. DX12 backend
> and platform/windows sources are automatically excluded.

## Build Targets

| Command | Description |
|---------|-------------|
| `zig build` | Build all targets (library + executables) |
| `zig build test` | Build and run all unit tests |
| `zig build run` | Build and launch the runtime executable |
| `zig build demo` | Build and run the DX12 clear screen demo |

## Run Tests

```bash
zig build test
```

Currently **223 tests** across:
- Core systems (logging, assertions, config, profiling, memory)
- Math (vectors, matrices, quaternions, transforms, color, bounds)
- Containers (ring buffer, free list, handle map, frame array)
- RHI (types, config resolution, DX12 resource creation)
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
`zig-out/bin/D3D12/D3D12Core.dll` exists. The build deploys it automatically.

**Stale build cache** — If you see unexpected behavior after code changes:
```bash
rm -rf .zig-cache && zig build
```

## Next Steps

Continue to [Your First Window](first-window.md) to understand the render loop.
