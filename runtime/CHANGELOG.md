# Changelog — runtime

> **Repository**: https://github.com/uzmigames/uzengine

## [0.2.0] — 2026-03-10

### Milestone 1.2 — RHI (Rendering Hardware Interface)
- **RHI abstract interfaces** (`core/rhi/rhi.h`, `rhi_types.h`, `rhi_backend.h`):
  - IRHIDevice, IRHICommandList, IRHISwapChain, IRHIFence — pure virtual interfaces
  - RHIHandle<Tag> generational handles (Buffer, Texture, Pipeline, Shader, Sampler)
  - 20+ enums, 15+ descriptor structs, backend factory (CreateRHIDevice/DestroyRHIDevice)
- **DX12 SM6.7 backend** (`core/rhi/dx12/`):
  - DX12Device: IDXGIFactory6 + IDXGIAdapter4 + ID3D12Device, feature level auto-detect (12_2→12_0)
  - D3D12MA v3.1.0 integration for GPU memory management
  - Global descriptor heaps: CBV_SRV_UAV (1M, SM6.6 bindless), Sampler (2048), RTV (256), DSV (64)
  - Direct + Copy command queues, triple-buffered command allocators
  - SM6.6 Resource Binding Tier 3 validation
  - DescriptorAllocator: O(1) free-list allocator for all heap types
  - ResourcePool<T>: generational handle validation with use-after-free protection
  - CreateBuffer: vertex/index/constant/structured via D3D12MA, auto SRV/UAV for bindless
  - CreateTexture: 2D/Cube/Array with SRV + optional RTV/DSV/UAV, typeless depth for SRV+DSV
  - CreateShader: bytecode copy with pool storage
  - CreateSampler: sampler descriptor in global sampler heap
  - CreateGraphicsPipeline: full PSO from RHIGraphicsPipelineDesc with root signature
  - Global bindless root signature (SM6.6 DIRECTLY_INDEXED, one root constant at b0)
  - State conversion: cull, fill, blend, depth-stencil, topology → D3D12 enums
  - No input layout — SM6.6 bindless pulls vertex data from SRV
  - Persistent CPU mapping for Upload/Readback buffers
- **DX12 swapchain** (`dx12_swapchain.h/.cpp`):
  - IDXGISwapChain4 (FLIP_DISCARD), triple-buffered, tearing support
  - Resize handling (release + recreate back buffers)
- **DX12 fence** (`dx12_fence.h/.cpp`):
  - ID3D12Fence with Win32 event, Signal/WaitCPU/IsComplete/GetCompletedValue
- **DX12 command list** (`dx12_command_list.h/.cpp`):
  - ID3D12GraphicsCommandList (base interface for Clang/MinGW compat)
  - SM6.6 bindless heap binding on Begin(), SetDescriptorIndex for root constants
- **Clear screen demo** (`demos/clear_screen/`):
  - Animated sine-wave RGB clear color, 6069 FPS on RTX 4090 (0.16ms/frame)
  - Triple-buffered frame pacing with fence synchronization
- **Agility SDK activation** (`core/rhi/dx12/dx12_agility.cpp`):
  - Exports D3D12SDKVersion + D3D12SDKPath for runtime SDK loading
  - DLL deployment: D3D12Core.dll + d3d12SDKLayers.dll → zig-out/bin/D3D12/
  - Unlocks FL 12_2 + SM6.6 on supported hardware (RTX 4090: FL 12_1→12_2, SM6.5→SM6.6)
  - Linked into every executable (demo + runtime), not the static lib
- **Standalone runtime executable** (`tools/uzengine_runtime.cpp`, `demos/triangle/`):
  - CLI flags: --scene, --width, --height, --headless, --rhi, --shader-model, --gpu-validation
  - Windowed mode with DX12 clear screen render loop, headless mode for CI/testing
  - `zig build run` step for quick iteration
- **Debug & validation**:
  - D3D12 debug layer + GPU-based validation (optional)
  - DRED enabled (auto-breadcrumbs + page fault tracking)
- **DX12 Clang/MinGW compatibility**:
  - Microsoft directx-headers (SDK 614) with correct GUIDs
  - Struct-return ABI: out-parameter form for GetCPUDescriptorHandleForHeapStart etc.
  - Base COM interfaces only (ID3D12Device, ID3D12GraphicsCommandList)
- **Cross-platform build** (`build.zig`):
  - Platform-aware: Windows-only code (DX12, platform/windows) guarded behind `is_windows`
  - DirectXMath compiles on Linux via dotnet/runtime sal.h stub (SAL annotations as no-ops)
  - Cross-compile validated: `zig build -Dtarget=x86_64-linux-gnu` passes
  - Setup script (`setup.ps1`) downloads sal.h automatically after DirectXMath clone
- **Backend selection** (`core/rhi/rhi_config.h/.cpp`):
  - RHIConfig resolution: CLI → .uzproject → platform auto-detect
  - ShaderModel enum: SM5_0, SM5_1, SM6_0, SM6_6
  - Flags: --rhi, --shader-model, --gpu-validation, --gpu-breadcrumbs, --adapter
  - ToDeviceDesc() conversion to RHIDeviceDesc
- **Shader model negotiation** (DX12Device):
  - NegotiateShaderModel(): probes SM6.6 → SM6.0 → SM5.1 → SM5.0
  - Root signature adapts: DIRECTLY_INDEXED for SM6.6, standard for lower
- **DXC DLL deployment** (`build.zig`):
  - `dxcompiler.dll` + `dxil.dll` copied to `zig-out/bin/` alongside executables
  - Test runner executes from install directory so `GetExeDir()` finds deployed DLLs
- **302 unit tests** — 77 shader pipeline tests + 22 RHI config + 35 RHI type/resource + 8 paths + 160 core/math/container

### Shader Pipeline
- **Shader VFS** (`core/shader/shader_vfs.h/.cpp`):
  - Virtual path mounting: `Mount("/engine", "shaders/engine")`
  - Path resolution, exists check, binary file reading
  - Backslash normalization, last-mounted-wins priority
  - 14 unit tests
- **Shader source manager** (`core/shader/shader_source_manager.h/.cpp`):
  - Load source via VFS, parse `#include "path"` recursively (max depth 32)
  - xxHash64 identity hash: source + sorted includes + defines + target + compiler version
  - Content-addressed — no timestamps, no stale cache bugs
  - Include deduplication, relative/absolute path resolution
  - 14 unit tests
- **Shader dependency graph** (`core/shader/shader_dependency_graph.h/.cpp`):
  - Forward graph: file → its includes (direct dependencies)
  - Reverse graph: file → all shaders that depend on it (transitive BFS)
  - Incremental edge rebuild on file change, diamond dependency handling
  - 13 unit tests
- **Shader cache** (`core/shader/shader_cache.h/.cpp`):
  - Disk-based bytecode cache: `cache/shaders/{backend}_{sm}/{hash}.bin`
  - Lookup by xxHash64 — hash changes on source change, no explicit invalidation
  - Store/Lookup/Contains/Remove, entry count and total size statistics
  - 11 unit tests
- **DXC shader compiler** (`core/shader/shader_compiler.h/.cpp`):
  - Dynamic loading of dxcompiler.dll via LoadLibrary/GetProcAddress
  - HLSL → DXIL compilation (SM6.6, SM6.0)
  - HLSL → SPIRV compilation (Vulkan 1.3 target via `-spirv`)
  - Preprocessor defines, debug info (`-Zi`), optimization control (`-Od`)
  - DXC version query (IDxcVersionInfo) — DXC 1.8 confirmed
  - Auto-extracted IIDs via constexpr ParseUUID from CROSS_PLATFORM_UUIDOF
  - 17 unit tests
- **Engine path resolution** (`core/paths/paths.h/.cpp`):
  - `GetExeDir()`: cached executable directory via platform-native API
  - `ResolveEngineResource()`: resolve relative paths against exe directory
  - Follows UE5/Godot pattern — never CWD, never hardcoded absolute paths
  - Build system deploys DLLs to `zig-out/bin/` via `addInstallBinFile`
  - 8 unit tests
- **Shader pipeline spec**: `docs/specs/1.5.0-shader-pipeline.md`
  - Two-mode design: UZ_SHADER_DEV (VFS + disk load + hot-reload) / UZ_SHADER_SHIP (precompiled archive)
  - Learned from POC 1 failures: never embed shaders as C header strings during dev

### Runtime CLI & Backend Selection (docs only)
- **Runtime executable design**: CLI flags (--scene, --screenshot, --rhi, --shader-model, --headless)
- **Backend selection priority**: CLI → .uzproject → user config → auto-detect
- **Post-switch shader compilation**: compile missing shaders before first frame when backend changes

### Milestone 1.1 — Math & Containers
- **Math foundation** (`core/math/engine_math.h`) — SIMD-accelerated vectors and matrices via DirectXMath:
  - Vec2, Vec3, Vec4 with zero-initialization, SIMD operations (dot, cross, normalize)
  - Mat4 with identity default, transforms (point/direction), decomposition, comparisons
  - Quat with slerp, lerp, axis-angle, vector rotation
  - Transform with position/rotation/scale composition, decomposition, interpolation
- **Geometry types** (`core/math/geometry.h`) — Frustum culling and collision:
  - Plane (half-space, signed distance for frustum clipping)
  - AABB (axis-aligned bounds with containment, intersection, expansion tests)
  - Frustum (6-plane view frustum extraction from projection matrix, conservative culling)
- **Color space conversions** (`core/math/color.h`) — Linear ↔ sRGB gamma correction:
  - LinearColor (float4, linear RGB for rendering)
  - sRGBColor (uint8_t RGBA, sRGB for storage/UI)
  - Gamma-correct conversions, hex color support
- **Containers** (`core/containers/`) — Zero-copy, allocation-friendly data structures:
  - RingBuffer: Lock-free SPSC circular queue with acquire/release semantics
  - FreeList: Intrusive free list for element-stable allocation
  - HandleMap: Generation-based handle → T mapping (safe indices)
  - FrameArray: Temporary allocations with frame lifetime scoping
- **Memory allocators** (`core/memory/allocators.h`) — Named, budget-tracked:
  - LinearAllocator: Bump pointer with reset (frame/streaming allocation)
  - PoolAllocator: Fixed-size block pool with free chain
  - FrameAllocator: Linear allocator with auto-reset per frame
- **Random number generation** (`core/random/random.h`) — PCG generator:
  - PCG (Permuted Congruential Generator) XSH-RR variant
  - 64-bit state, 32-bit output, statistically excellent (passes BigCrush)
  - Seeded and stream variants for deterministic parallel generation
  - Utility methods: Float, Range, Int, UInt, Bool, UnitSphere, UnitDisk
- **160 unit tests** — Complete coverage of all math/container/memory types (27 test suites)
- **Rust-style documentation** — All public types/methods documented with `///` comments

### Added
- Initial Zig build system (`build.zig`)
- Thirdparty dependency setup via `scripts/setup.ps1`
- Stub N-API entry point linking to extension
- Unit tests via `zig build test` (`tests/test_runtime.cpp`)
- clang-format config (`.clang-format`)
- clang-tidy config with warnings-as-errors (`.clang-tidy`)

### Milestone 1.0 — Bootstrap & Project Setup
- **Logging system** (`core/logging/`) — spdlog-backed, 12 categories (Core, RHI, Renderer, Scene, Physics, Audio, Input, Platform, Memory, IO, Config, Window), compile-time level filtering, UZ_LOG macro
- **Assertion system** (`core/assertions/`) — UZ_CHECK (fatal), UZ_ENSURE (non-fatal), UZ_VERIFY (never stripped), stack trace via CaptureStackBackTrace, debugger break support
- **CPU profiler** (`core/profiling/`) — Tracy integration (compile-time toggleable), StatCounter with atomic int64, 6 stat groups
- **Memory foundation** (`core/memory/`) — Rust-inspired ownership: Own<T>, Ref<T>, RefMut<T> with debug borrow checker (std::atomic)
- **Platform window** (`platform/windows/`) — Win32 HWND creation, WndProc message pump, fullscreen toggle, show/hide
- **Input system** (`platform/windows/`) — Keyboard (VK codes), mouse (move/button/scroll), action mapping with RegisterAction/IsActionDown/IsActionJustPressed
- **Config system** (`core/config/`) — .uzproject JSON loader (nlohmann/json), dotted-path resolution, CLI override cascade (--key=value), typed getters with defaults
- **JSON schema** (`schemas/uzproject.schema.json`) — JSON Schema 2020-12 for .uzproject validation
- **25 test functions, 78 assertions** — full coverage of all 1.0 systems
