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
  ├── Runtime static library (runtime.lib)
  │   ├── Engine C++ sources (auto-discovered per directory)
  │   ├── Thirdparty sources (spdlog, Tracy, SPIRV-Reflect, astcenc, basis, miniz)
  │   └── Windows-only: DX12 backend + D3D12MA
  │
  ├── Editor static library (editor.lib) — NEW
  │   ├── Editor/debug visualizations (grid, font rendering, stats overlay)
  │   ├── Screenshot capture subsystem
  │   ├── Compare grid image composition tool
  │   └── Linked via -Deditor build flag (optional)
  │
  ├── Test executable (test_runtime.exe)
  │   ├── GoogleTest compiled from source
  │   ├── Test sources (auto-discovered per directory)
  │   └── Links runtime.lib (no recompilation)
  │
  └── Tools & DLL deployment (Windows only)
      ├── uzengine.exe (standalone runtime, links runtime.lib + editor.lib)
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
addCppDir("src")                    → runtime entry point
addCppDir("engine/logging")         → log system
addCppDir("engine/assertions")      → assertion macros
addCppDir("engine/profiling")       → Tracy + stat counters
addCppDir("engine/config")          → config/project file loading
addCppDir("engine/math")            → math types (Vec3, Mat4, Quat, Transform)
addCppDir("engine/rhi")             → RHI abstraction layer
addCppDir("engine/paths")           → path resolution
addCppDir("engine/shader")          → shader compiler, VFS, cache, hot-reload
addCppDir("engine/asset")           → asset pipeline
addCppDir("engine/ecs")             → ECS system (EnTT)
addCppDir("engine/job")             → job system / thread pool
addCppDir("engine/renderer")        → renderer, render graph, vertex formats

# Platform-specific:
addCppDir("platform/windows")       → Win32 window, message pump
addCppDir("engine/rhi/dx12")        → DX12 backend (Windows only)
addCppDir("engine/rhi/vulkan")      → Vulkan backend (cross-platform)
addCppDir("engine/rhi/metal")       → Metal backend (macOS/iOS only)
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
| `metal_swapchain.mm` | `-fno-objc-arc`, `-fobjc-arc-exceptions`, `-std=c++17` | Objective-C++ file requiring Metal framework access |
| `TracyClient.cpp` | `-DTRACY_ENABLE`, `-DTRACY_ON_DEMAND`, `-DTRACY_ONLY_LOCALHOST`, `-DTRACY_NO_BROADCAST` (only with `-Dtracy=true`, see [Build Options](05-build-configs.md#build-options)) | Tracy must be compiled with its feature defines; the client is loopback-only |
| `basisu_transcoder.cpp` | `-DBASISD_SUPPORT_KTX2_ZSTD=0` | Disable zstd dependency (not needed for transcoding) |
| `spirv_reflect.c` | `-std=c11` | C source, not C++ |
| `miniz.c` | `-std=c11` | C source (zlib-compatible compression for tinyexr) |
| `test_image_libs.cpp` | `-fno-sanitize=alignment` | stb_image_resize2 uses intentional unaligned loads |

## Library Architecture: runtime.lib + editor.lib

As of commit 493e7dd, the build system produces two static libraries:

### runtime.lib (Core Engine)

Contains all essential engine systems:
- ECS world, entity, component system
- Renderer, render graph, RHI backends (DX12, Vulkan, Metal)
- Physics (Jolt), Audio (XAudio2), Input, Camera
- Lua scripting VM and scene loader
- Asset pipeline, shader compiler, profiling, logging

**Consumers**: `uzengine.exe`, `uzcook.exe`, `test_runtime.exe`, custom game DLLs

### editor.lib (Editor Features) — Optional

Contains debug/visualization features:
- Grid renderer — infinite floor grid for level editing
- Font rendering — debug text overlay
- Stats overlay — frame time, memory, draw calls, entity count
- Screenshot capture — PNG export via libjpeg-turbo
- Compare grid — multi-image composition tool (for RHI backend comparison)
- uzeditor.h umbrella header — includes all above subsystems

**Consumers**: Only `uzengine.exe` (editor mode) and demos (via `-Deditor` build flag)

**Linkage model**:
- Core tools (`uzcook.exe`, `test_runtime.exe`) link ONLY `runtime.lib` — smaller binaries, no debug overhead
- Editor (`uzengine.exe`) links BOTH `runtime.lib` + `editor.lib` — full feature set
- Demos auto-link both libraries (via `build_common.zig`) when `-Deditor` is set

### Demo Build Configuration

Demos previously required manual `extra_sources` configuration in each `build.zig`. As of commit 493e7dd:

```zig
// OLD (per-demo build.zig):
const extra_sources = &.{
    "../../editor/grid/grid_renderer.cpp",
    "../../editor/font/font_renderer.cpp",
    // ... manual list of editor sources
};

// NEW (via build_common.zig):
const editor_lib = "editor";  // Auto-linked via -Deditor flag
// Demo gets grid, font, stats, screenshot, compare_grid automatically
```

When building a demo with `-Deditor`:
```bash
zig build -Deditor        # Includes editor.lib
zig build                  # runtime.lib only (smaller binary for headless testing)
```

## macOS Build Configuration

Building on macOS requires Metal framework detection and conditional compilation:

### Platform Detection in build.zig

```zig
const is_apple = target.os.tag == .macos or target.os.tag == .ios;

if (is_apple) {
    // Link Metal frameworks
    lib.linkFramework("Metal");
    lib.linkFramework("QuartzCore");
    lib.linkFramework("Foundation");
    lib.linkFramework("AppKit");
    lib.linkFramework("Cocoa");

    // Add Metal backend sources
    addCppDir("core/rhi/metal");

    // Note: Objective-C++ files (.mm) are handled separately with special flags
    lib.addCppSource(.{
        .file = b.path("runtime/core/rhi/metal/metal_swapchain.mm"),
        .flags = &.{
            "-fno-objc-arc",
            "-fobjc-arc-exceptions",
            "-std=c++17",
        },
    });
}
```

### Metal Source Compilation

The Metal backend has four source categories:

1. **Pure C++ files** (normal compilation):
   - `metal_device.cpp` — GPU initialization, feature detection
   - `metal_command_list.cpp` — Command recording with encoder switching
   - `metal_fence.cpp` — Synchronization primitives
   - Compiled with standard `-std=c++17` flags

2. **Objective-C++ file** (requires special flags):
   - `metal_swapchain.mm` — CAMetalLayer integration (only Objective-C code in the backend)
   - Compiled with `-fno-objc-arc` (manual lifetime management) and `-fobjc-arc-exceptions` (C++ exception support)

3. **All Metal headers** (internal to backend):
   - `runtime/core/rhi/metal/*.h` — Never included outside `rhi/metal/`
   - Use forward declarations to hide Metal types from the RHI interface

### Cross-Compilation to Apple Platforms

When building on Linux/Windows for macOS, use Zig's cross-compilation:

```bash
# Build for macOS arm64 (Apple Silicon) from any host
zig build -Dtarget=aarch64-macos.13

# Build for macOS x86_64 (Intel) from any host
zig build -Dtarget=x86_64-macos.13

# Build universal binary (arm64 + x86_64)
zig build -Dtarget=aarch64-macos.13 && \
zig build -Dtarget=x86_64-macos.13 && \
lipo -create build-arm64/bin/uzengine build-x86_64/bin/uzengine -output uzengine-universal
```

**Note:** Cross-compilation to macOS requires the Xcode command-line tools to be available on the build host (for Metal framework headers and Objective-C runtime). Pure remote cross-compilation from Linux without macOS SDK is not yet supported.

## Build Targets

### `zig build` (default)

Builds everything: uzembed → runtime lib → test exe → tools → DLL deployment.

Output directory: `runtime/build/` (overridden from Zig's default `zig-out/`):

```
runtime/build/
├── bin/
│   ├── uzengine.exe          # Standalone runtime (links runtime.lib + editor.lib)
│   ├── uzcook.exe            # Asset cook tool (links runtime.lib)
│   ├── uzembed.exe           # Shader embed tool (build-time only)
│   ├── test_runtime.exe      # Unit tests (links runtime.lib)
│   ├── dxcompiler.dll        # DXC (shader compiler)
│   ├── dxil.dll              # DXIL validation
│   └── D3D12/
│       ├── D3D12Core.dll     # Agility SDK runtime
│       └── d3d12SDKLayers.dll # Agility SDK debug layer
└── lib/
    ├── runtime.lib           # Static library — core engine
    └── editor.lib            # Static library — editor features (grid, stats, screenshot, compare_grid)
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

The build follows this dependency chain:

```
uzembed.exe
    ↓ (generates build/generated/shaders/*.gen.h)
runtime.lib + editor.lib (compiled in parallel)
    ├─ runtime.lib (core engine: ECS, renderer, physics, scripting, etc.)
    └─ editor.lib (editor features: grid, font rendering, stats, screenshot, compare_grid)
    ↓ (linked into)
├── test_runtime.exe (links runtime.lib)
├── uzengine.exe (links runtime.lib + editor.lib)
└── uzcook.exe (links runtime.lib)
```

The `uzembed` dependency is enforced via `lib.step.dependOn(&run_embed.step)` — Zig's build graph ensures shaders are embedded before runtime compilation starts.

## System Libraries (Platform-Specific)

### Windows

The DX12 backend requires three system libraries:

```zig
lib.linkSystemLibrary("d3d12");   // Direct3D 12 API
lib.linkSystemLibrary("dxgi");    // DXGI (adapter enumeration, swap chain factory)
lib.linkSystemLibrary("dxguid");  // GUID definitions for COM interfaces
```

Tools additionally link `gdi32` and `user32` for window creation.

### macOS

The Metal backend requires native Apple framework linking:

```zig
lib.linkFramework("Metal");          // Core Metal GPU API
lib.linkFramework("QuartzCore");     // CAMetalLayer (drawable surface)
lib.linkFramework("Foundation");     // NSString, NSWindow utilities
lib.linkFramework("AppKit");         // Window management (NSWindow)
lib.linkFramework("Cocoa");          // Objective-C runtime integration
```

Additionally, Metal source files (`.mm` files) must be compiled with Objective-C++ support:

```zig
// In build.zig, when adding Metal backend sources:
lib.addCppSource(.{
    .file = b.path("runtime/core/rhi/metal/metal_swapchain.mm"),
    .flags = &.{"-fno-objc-arc", "-fobjc-arc-exceptions", "-std=c++17"},
});
```

**Key compiler flags for Objective-C++:**
- `-fno-objc-arc` — Disable Automatic Reference Counting (we manage lifetimes manually)
- `-fobjc-arc-exceptions` — Allow C++ exceptions to cross Objective-C boundaries
- `-std=c++17` — Ensure C++17 support within Objective-C++ context

### Linux

No special framework linking required. The Vulkan backend uses dynamically loaded functions via `volk` (no link-time dependency on `libvulkan.so`).

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
