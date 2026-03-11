# Project Structure

## Top Level

```
UzEngine/0.2/
├── docs/                    # Internal specs, architecture, roadmap
│   ├── user/                # Public developer documentation (this site)
│   ├── specs/               # Internal implementation specifications
│   ├── observability/       # Logging/profiling specs
│   └── benchmarks/          # Performance analysis
├── runtime/                 # C++ engine core
│   ├── build.zig            # Build system
│   ├── src/                 # Runtime C API
│   ├── core/                # Engine subsystems
│   ├── platform/            # OS-specific code
│   ├── tools/               # Standalone executables
│   ├── tests/               # Unit tests
│   └── thirdparty/          # Third-party dependencies
├── schemas/                 # JSON schemas (.uzproject, etc.)
├── scripts/                 # Setup and utility scripts
├── AGENTS.md                # AI agent directives
└── CLAUDE.md                # Claude Code project instructions
```

## Runtime Directory

```
runtime/
├── src/
│   ├── runtime.h            # Public C API header
│   └── runtime.cpp          # C API implementation
│
├── core/
│   ├── logging/
│   │   ├── log.h             # Logging macros and categories
│   │   └── log.cpp
│   ├── assertions/
│   │   ├── engine_assert.h   # UZ_CHECK, UZ_ENSURE, UZ_VERIFY
│   │   └── engine_assert.cpp
│   ├── profiling/
│   │   ├── profiler.h        # Tracy + stat counters
│   │   └── profiler.cpp
│   ├── memory/
│   │   ├── engine_memory.h   # Own<T>, Ref<T>, RefMut<T>
│   │   └── allocators.h      # Linear, Pool, Frame allocators
│   ├── config/
│   │   ├── config.h          # .uzproject loader + CLI overrides
│   │   └── config.cpp
│   ├── math/
│   │   ├── engine_math.h     # Vec2-4, Mat4, Quat, Transform
│   │   ├── engine_math.cpp
│   │   ├── color.h           # LinearColor, sRGBColor
│   │   └── geometry.h        # Plane, AABB, Frustum
│   ├── containers/
│   │   ├── ring_buffer.h     # Lock-free SPSC queue
│   │   ├── free_list.h       # Intrusive free list
│   │   ├── handle_map.h      # Generational handle map
│   │   └── frame_array.h     # Frame-scoped dynamic array
│   ├── random/
│   │   └── random.h          # PCG random generator
│   └── rhi/
│       ├── rhi.h             # IRHIDevice, IRHICommandList, etc.
│       ├── rhi_types.h       # Handles, enums, descriptors
│       ├── rhi_backend.h     # CreateRHIDevice / DestroyRHIDevice
│       ├── rhi_config.h      # Backend/SM resolution
│       ├── rhi_config.cpp
│       └── dx12/
│           ├── dx12_device.h/.cpp
│           ├── dx12_swapchain.h/.cpp
│           ├── dx12_fence.h/.cpp
│           ├── dx12_command_list.h/.cpp
│           ├── dx12_descriptor_allocator.h/.cpp
│           ├── dx12_resource_creation.h/.cpp
│           ├── dx12_gpu_profiler.h/.cpp
│           ├── dx12_gpu_breadcrumbs.h/.cpp
│           ├── dx12_resources.h      # DX12Buffer, DX12Texture, etc.
│           ├── dx12_guids.h          # COM GUIDs for Clang/MinGW
│           ├── dx12_d3d12ma.cpp      # D3D12MA wrapper
│           └── dx12_agility.cpp      # Agility SDK activation exports
│
├── platform/
│   └── windows/
│       ├── platform_window.h/.cpp     # Win32 HWND
│       └── input.h/.cpp               # Keyboard/mouse/action mapping
│
├── tools/
│   ├── uzengine_runtime.cpp  # Standalone runtime executable
│   ├── clear_screen_demo.cpp # DX12 clear screen demo
│   └── dx12_clear_screen.cpp # Demo render implementation
│
└── tests/                    # Mirrors core/ structure
```

## Where to Add New Code

| New code for... | Location |
|-----------------|----------|
| New core system | `core/<system>/<system>.h` + `.cpp` |
| New RHI backend | `core/rhi/<backend>/` (e.g., `core/rhi/vulkan/`) |
| New platform | `platform/<os>/` (e.g., `platform/linux/`) |
| New tool/demo | `tools/<name>.cpp` + add to `build.zig` |
| New tests | `tests/core/<area>/test_<name>.cpp` + add to `build.zig` |
| New third-party dep | `thirdparty/<name>/` + add to `scripts/setup.ps1` + `build.zig` |

## Rules

1. **Max 1500 lines** per file — split at ~1000
2. **One file = one concern** — `dx12_device.cpp` does device init, `dx12_resource_creation.cpp` does resources
3. **Tests mirror source** — `tests/core/math/test_vec.cpp` for `core/math/engine_math.h`
4. **No generated artifacts** — tests clean up logs/temp files
5. **Headers in same directory** — `.h` and `.cpp` live together
