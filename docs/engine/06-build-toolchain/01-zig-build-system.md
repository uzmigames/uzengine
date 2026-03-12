# Build Toolchain — Zig Build System

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/build.zig`
> **Depends on**: Zig 0.14+, C++17 toolchain

## Purpose

UzEngine compiles all C++ runtime code using **Zig's build system** (`zig build`), which invokes `zig cc` — a drop-in Clang-based C/C++ compiler with integrated cross-compilation and reproducible builds. This is an unconventional choice for a game engine. The rationale:

1. **Cross-compilation out of the box** — Zig can target Windows/Linux/macOS from any host without installing platform-specific toolchains. A single `build.zig` replaces per-platform CMake/MSBuild/Xcode projects.
2. **Hermetic builds** — Zig ships its own libc and C++ standard library. No "works on my machine" dependency drift.
3. **Clang frontend** — `zig cc` is Clang under the hood, so all Clang diagnostics, sanitizers, and optimizations are available.
4. **Deterministic linking** — Zig's linker produces identical binaries given identical inputs. Critical for content-addressed caching.
5. **Zero-config toolchain** — no Visual Studio installation required for development (MSVC is only needed for the N-API bridge, which uses cmake-js separately).

### Why Not CMake?

CMake is the industry standard, but it has significant drawbacks for a project targeting AI-driven development:

| Aspect | CMake | Zig Build |
|--------|-------|-----------|
| Toolchain setup | Requires generator + compiler install (VS, Ninja, GCC) | Single `zig` binary |
| Cross-compilation | Requires toolchain files, sysroots, manual configuration | Built-in: `-Dtarget=aarch64-linux` |
| Reproducibility | Depends on system state (env vars, installed SDKs) | Hermetic — ships its own libc/libcxx |
| Build script language | CMake DSL (brittle, hard to debug) | Zig (real programming language with IDE support) |
| AI-friendliness | AI agents struggle with CMake generator expressions | Straightforward Zig code — easy for LLMs to read/modify |

The N-API addon layer (`napi/`) still uses CMake via `cmake-js` because Electron requires MSVC compilation with specific delay-load hooks. This is isolated — the engine runtime itself is 100% Zig-built.

## Build Script Architecture

The `build.zig` file is organized into five sections:

```
build.zig
  │
  ├── Include path tables (shared between runtime and tests)
  │   ├── engine_include_dirs[]      — 14 engine source directories
  │   ├── thirdparty_include_dirs[]  — 15 cross-platform thirdparty paths
  │   ├── windows_engine_include_dirs[]  — 1 Windows platform path
  │   └── windows_thirdparty_include_dirs[] — 4 Windows-only thirdparty paths
  │
  ├── uzembed tool (shader → C++ header generator)
  │   └── Runs before runtime compilation
  │
  ├── Runtime static library (uzengine_runtime.lib)
  │   ├── Engine C++ sources (auto-discovered per directory)
  │   ├── Thirdparty sources (spdlog, Tracy, SPIRV-Reflect, astcenc, basis, miniz)
  │   └── Windows-only: DX12 backend + D3D12MA
  │
  ├── Test executable (test_runtime.exe)
  │   ├── GoogleTest compiled from source
  │   ├── Test sources (auto-discovered per directory)
  │   └── Links uzengine_runtime.lib (no recompilation)
  │
  └── Tools & DLL deployment (Windows only)
      ├── uzengine.exe (standalone runtime)
      ├── uzcook.exe (asset cook tool)
      └── DLL deployment (Agility SDK, DXC)
```

## Source Discovery

Rather than maintaining manual file lists (which go stale), the build system auto-discovers `.cpp` files:

```zig
/// Scans a directory for all .cpp files (non-recursive) and adds them
/// as C++ source files to the given module.
fn addCppDir(mod, b, dir, flags) void {
    // Iterate directory, collect all .cpp files
    // Special case: dx12_d3d12ma.cpp gets separate flags (-fno-sanitize=undefined)
    // to suppress upstream warnings from D3D12MA
}
```

This means adding a new source file is just "drop a `.cpp` in the right directory" — no build script edits needed. The trade-off is that you must be careful not to leave temporary `.cpp` files in source directories.

### Directory → Module Mapping

```
addCppDir("src")                  → runtime entry point
addCppDir("core/logging")        → log system
addCppDir("core/assertions")     → assertion macros
addCppDir("core/profiling")      → Tracy + stat counters
addCppDir("core/config")         → config/project file loading
addCppDir("core/math")           → math types (Vec3, Mat4, Quat, Transform)
addCppDir("core/rhi")            → RHI abstraction layer
addCppDir("core/paths")          → path resolution
addCppDir("core/shader")         → shader compiler, VFS, cache, hot-reload
addCppDir("core/asset")          → asset pipeline
addCppDir("core/renderer")       → high-level renderer

# Windows only:
addCppDir("platform/windows")    → Win32 window, message pump
addCppDir("core/rhi/dx12")       → DX12 backend implementation
```

## Compiler Flags

```zig
const cpp_flags = &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB" };
```

- **C++17** — required for `std::filesystem`, `std::optional`, `if constexpr`, structured bindings
- **SPDLOG_COMPILED_LIB** — spdlog is compiled as a static library (not header-only), reducing compile times by ~40%

### Special Cases

| Source | Extra Flags | Why |
|--------|------------|-----|
| `dx12_d3d12ma.cpp` | `-fno-sanitize=undefined`, `-Wno-switch`, `-Wno-unused-private-field` | D3D12MA upstream code triggers Clang warnings/UBSan false positives |
| `TracyClient.cpp` | `-DTRACY_ENABLE`, `-DTRACY_ON_DEMAND` | Tracy must be compiled with its feature defines |
| `basisu_transcoder.cpp` | `-DBASISD_SUPPORT_KTX2_ZSTD=0` | Disable zstd dependency (not needed for transcoding) |
| `spirv_reflect.c` | `-std=c11` | C source, not C++ |
| `miniz.c` | `-std=c11` | C source (zlib-compatible compression for tinyexr) |
| `test_image_libs.cpp` | `-fno-sanitize=alignment` | stb_image_resize2 uses intentional unaligned loads |

## Build Targets

### `zig build` (default)

Builds everything: uzembed → runtime lib → test exe → tools → DLL deployment.

Output directory: `runtime/build/` (overridden from Zig's default `zig-out/`):

```
runtime/build/
├── bin/
│   ├── uzengine.exe          # Standalone runtime
│   ├── uzcook.exe            # Asset cook tool
│   ├── uzembed.exe           # Shader embed tool (build-time only)
│   ├── test_runtime.exe      # Unit tests
│   ├── dxcompiler.dll        # DXC (shader compiler)
│   ├── dxil.dll              # DXIL validation
│   └── D3D12/
│       ├── D3D12Core.dll     # Agility SDK runtime
│       └── d3d12SDKLayers.dll # Agility SDK debug layer
└── lib/
    └── uzengine_runtime.lib  # Static library (linked by tools + tests)
```

### `zig build test`

Builds and runs `test_runtime.exe` from the install directory (`build/bin/`). Running from the install directory ensures `uz::GetExeDir()` finds deployed DLLs (DXC, Agility SDK).

### `zig build run`

Builds and runs `uzengine.exe` — the standalone runtime with animated clear screen.

### `zig build cook`

Builds and runs `uzcook.exe` — passes through CLI arguments after `--`:
```bash
zig build cook -- --source assets/ --output build/cooked/ --pak game
```

## Build Dependencies

The runtime compilation step depends on `uzembed` having run first:

```
uzembed.exe
    ↓ (generates build/generated/shaders/*.gen.h)
uzengine_runtime.lib
    ↓ (links into)
├── test_runtime.exe
├── uzengine.exe
└── uzcook.exe
```

This is enforced via `lib.step.dependOn(&run_embed.step)` — Zig's build graph ensures correct ordering.

## System Libraries (Windows)

The DX12 backend requires three system libraries:

```zig
lib.linkSystemLibrary("d3d12");   // Direct3D 12 API
lib.linkSystemLibrary("dxgi");    // DXGI (adapter enumeration, swap chain factory)
lib.linkSystemLibrary("dxguid");  // GUID definitions for COM interfaces
```

Tools additionally link `gdi32` and `user32` for window creation.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Build system | UnrealBuildTool (C#) + MSBuild/Xcode/Make | SCons (Python) | **Zig build** (Zig) |
| Compiler | MSVC/Clang/GCC per platform | MSVC/Clang/GCC per platform | **zig cc (Clang)** everywhere |
| Cross-compilation | Requires per-platform toolchain install | Requires per-platform toolchain install | Built-in (`-Dtarget=...`) |
| Source discovery | UnrealBuildTool scans `.Build.cs` modules | SCons globs per `SCsub` | Auto-glob `.cpp` per directory |
| Build reproducibility | Non-deterministic (timestamps, env) | Non-deterministic | **Deterministic** (hermetic toolchain) |
| Thirdparty handling | Git submodules + custom build scripts | Git submodules + SCons integration | Git submodules compiled in `build.zig` |

### Why UE5 Uses Its Own Build Tool

Unreal's `UnrealBuildTool` exists because the engine predates modern build systems and needs to manage:
- Module dependency graphs across hundreds of modules
- Shader permutation compilation (thousands of variants)
- Hot-reload support for editor modules
- Per-platform SDK detection and configuration

UzEngine avoids this complexity by having a flat module structure (one static library) and handling shader compilation as a separate tool (`uzcook`).

### Why Godot Uses SCons

Godot chose SCons for its Python scriptability and cross-platform support. The downside is build speed — SCons is significantly slower than Ninja/Zig for incremental builds. Godot partially mitigates this with compilation databases and ccache.

## Rules

1. **NEVER register demos in `runtime/build.zig`** — each demo under `demos/` has its own standalone `build.zig`
2. **ALWAYS use `addCppDir` for source discovery** — don't maintain manual file lists (except for thirdparty with specific flag requirements)
3. **ALWAYS deploy DLLs via `addInstallBinFile`** — ensures tools find their dependencies relative to the exe
4. **NEVER use MSVC for the runtime** — Zig/Clang is the only supported compiler. MSVC is reserved for the N-API bridge only
5. **ALWAYS keep compiler flags minimal** — add per-file flags only when upstream code requires warning suppression
