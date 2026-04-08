# Project Structure

## Top Level

```
UzEngine/
├── docs/                    # Internal specs, architecture, roadmap
│   ├── user/                # Public developer documentation (this site)
│   ├── specs/               # Internal implementation specifications
│   ├── observability/       # Logging/profiling specs
│   └── benchmarks/          # Performance analysis
├── runtime/                 # C++ engine core
│   ├── build.zig            # Build system
│   ├── src/                 # Runtime C API
│   ├── engine/              # Engine subsystems
│   ├── platform/            # OS-specific code
│   ├── tools/               # Standalone executables
│   ├── tests/               # Unit tests
│   ├── demos/               # Demo projects (shared build_common.zig)
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
├── engine/
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
│   │   ├── ownership.h       # Own<T>, Ref<T>, RefMut<T>
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
│   │   ├── frame_array.h     # Frame-scoped dynamic array
│   │   └── fnv_hash.h        # FNV-1a hash utility
│   ├── ecs/
│   │   ├── world.h/.cpp      # ECS World container
│   │   ├── entity.h          # Entity type and utilities
│   │   ├── world_subsystem.h # ECS subsystem lifecycle
│   │   ├── component.h       # Component base class + lifecycle
│   │   ├── scene_component.h/.cpp # Transform hierarchy + parent/child
│   │   ├── primitive_component.h/.cpp # Renderable with RenderProxy bridge
│   │   ├── static_mesh_component.h/.cpp # Mesh + material rendering
│   │   ├── camera_component.h/.cpp # Camera with projection matrices
│   │   └── light_component.h/.cpp # Directional/Point/Spot light sources
│   ├── event/
│   │   ├── delegate.h        # MulticastDelegate<Args...> type-safe dispatch
│   │   └── event_queue.h     # EventQueue<TEvent> deferred per-frame dispatch
│   ├── random/
│   │   └── random.h          # PCG random generator
│   ├── shader/
│   │   ├── shader_vfs.h/.cpp            # Virtual filesystem for shader sources
│   │   ├── shader_source_manager.h/.cpp # Include parsing, content hashing
│   │   ├── shader_dependency_graph.h/.cpp # Forward/reverse include tracking
│   │   ├── shader_cache.h/.cpp          # Disk-based bytecode cache
│   │   ├── shader_compiler.h/.cpp       # DXC integration (HLSL→DXIL/SPIRV)
│   │   ├── shader_pipeline.h/.cpp       # High-level compile orchestrator
│   │   ├── shader_permutation.h/.cpp    # Feature flag variant expansion
│   │   ├── shader_reflection.h/.cpp     # SPIRV-Reflect binding extraction
│   │   ├── shader_hot_reload.h/.cpp     # FileWatcher + dirty flags
│   │   └── shader_archive.h/.cpp        # Shipping precompiled archive
│   ├── asset/
│   │   ├── vfs.h/.cpp                   # General-purpose virtual filesystem
│   │   ├── asset_cache.h/.cpp           # Asset content cache
│   │   ├── asset_manifest.h/.cpp        # Build manifest tracking
│   │   ├── import_sidecar.h/.cpp        # .import.json sidecar files
│   │   ├── importers/                   # Asset importers + registry
│   │   │   ├── asset_registry.h/.cpp    # Global importer registry
│   │   │   └── *.h/.cpp                 # Format-specific importers
│   │   └── formats/                     # Binary format definitions
│   │       └── *.h/.cpp                 # Format specs (.uzshader, etc.)
│   ├── renderer/
│   │   ├── upload_context.h/.cpp        # GPU upload ring buffer
│   │   ├── vertex.h                     # Vertex format definitions
│   │   ├── render_world.h/.cpp          # RenderWorld with proxy handles
│   │   ├── material.h                   # PBRMaterial struct (Cook-Torrance)
│   │   ├── draw_list.h                  # DrawList with 64-bit sort keys
│   │   ├── constant_buffers.h/.cpp      # Per-frame CB (lights, ambient)
│   │   ├── camera.h/.cpp                # Camera matrix computation
│   │   ├── mesh.h/.cpp                  # Mesh topology and rendering
│   │   └── render_graph/                # Render graph system
│   │       ├── render_graph_builder.h   # Graph builder API
│   │       ├── render_graph_pass.h      # Pass definition
│   │       ├── render_graph_resources.h # Resource binding
│   │       └── render_graph_types.h     # Type definitions
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
│   ├── uzengine.cpp          # Standalone runtime executable
│   └── uzcook.cpp            # Asset cooker CLI
│
├── demos/
│   ├── common/               # Shared build system (build_common.zig)
│   │   ├── build.zig         # Re-exports build_common.zig
│   │   └── build_common.zig  # Shared build logic (DLL deploy, includes, etc.)
│   ├── clear_screen/         # DX12 animated clear screen
│   │   ├── build.zig.zon     # Declares uz_demo_common dependency
│   │   ├── build.zig         # Minimal build script
│   │   ├── main.cpp
│   │   └── dx12_clear_screen.h/.cpp
│   └── triangle/             # DX12 triangle demo (SM6.6 bindless)
│       ├── build.zig.zon     # Declares uz_demo_common dependency
│       ├── build.zig         # Minimal build script
│       ├── main.cpp
│       ├── triangle_demo.h/.cpp
│       ├── app.rc            # Win32 resource file (icon)
│       └── shaders/          # HLSL sources (simple_vs.hlsl, simple_ps.hlsl)
│
└── tests/                    # Mirrors core/ structure
```

## Where to Add New Code

| New code for... | Location |
|-----------------|----------|
| New engine system | `engine/<system>/<system>.h` + `.cpp` |
| New component type | `engine/ecs/<component_name>.h` + `.cpp` (inherits from Component/ActorComponent/SceneComponent/PrimitiveComponent) |
| New event type | `engine/event/` + add to `event_queue.h` or use `MulticastDelegate` |
| New RHI backend | `engine/rhi/<backend>/` (e.g., `engine/rhi/vulkan/`) |
| New platform | `platform/<os>/` (e.g., `platform/linux/`) |
| New tool | `tools/<name>.cpp` + add to `build.zig` |
| New demo | `demos/<name>/` with `build.zig` (uses shared `build_common.zig`) |
| New tests | `tests/engine/<area>/test_<name>.cpp` + add to `build.zig` |
| New third-party dep | `thirdparty/<name>/` + add to `scripts/setup.ps1` + `build.zig` |

## Rules

1. **Max 1500 lines** per file — split at ~1000
2. **One file = one concern** — `dx12_device.cpp` does device init, `dx12_resource_creation.cpp` does resources
3. **Tests mirror source** — `tests/engine/math/test_vec.cpp` for `engine/math/engine_math.h`
4. **No generated artifacts** — tests clean up logs/temp files
5. **Headers in same directory** — `.h` and `.cpp` live together
6. **Demos share build system** — all demos use `common/build_common.zig` via path dependency
