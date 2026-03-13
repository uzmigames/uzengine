# Building & Testing

## Build Commands

### Zig (default)

| Command | Description |
|---------|-------------|
| `zig build` | Build all targets |
| `zig build test` | Build and run all unit tests |
| `zig build run` | Build and launch runtime |
| `zig build -Doptimize=ReleaseSafe` | Release build with safety checks |
| `zig build -Doptimize=ReleaseFast` | Release build |
| `zig build -Dtarget=x86_64-linux-gnu` | Cross-compile for Linux |

Clean build (after major changes):
```bash
rm -rf .zig-cache && zig build
```

### CMake (MSVC / Clang)

| Command | Description |
|---------|-------------|
| `cmake --preset msvc-debug` | Configure MSVC debug |
| `cmake --build build/msvc-debug` | Build MSVC debug |
| `cmake --preset clang-debug` | Configure Clang debug |
| `cmake --build build/clang-debug` | Build Clang debug |
| `cmake --preset msvc-release` | Configure MSVC release |
| `cmake --preset clang-release` | Configure Clang release |

Presets are defined in `CMakePresets.json`. CMake output goes to `build/`.

## Test Framework

Tests use [GoogleTest](https://github.com/google/googletest) compiled from source.
The build produces a single `test_runtime.exe` that links against the engine static library.

## Test Structure

Tests mirror the source directory structure:

```
tests/
├── test_main.cpp                  # GoogleTest entry point
├── core/
│   ├── test_runtime_api.cpp       # runtime_version, runtime_init
│   ├── test_logging.cpp           # Log categories, levels, output
│   ├── test_assertions.cpp        # UZ_CHECK, UZ_ENSURE, UZ_VERIFY
│   ├── test_profiler.cpp          # Tracy scopes, stat counters
│   ├── test_memory.cpp            # Own<T>, Ref<T>, RefMut<T>
│   ├── test_config.cpp            # JSON loading, CLI overrides, dotted paths
│   ├── test_allocators.cpp        # Linear, Pool, Frame allocators
│   ├── math/
│   │   ├── test_vec.cpp           # Vec2, Vec3, Vec4
│   │   ├── test_mat4.cpp          # Mat4 transforms, decomposition
│   │   ├── test_quat.cpp          # Quaternion ops, slerp
│   │   ├── test_transform.cpp     # Transform composition
│   │   ├── test_color.cpp         # Linear ↔ sRGB conversion
│   │   └── test_bounds.cpp        # AABB, Plane, Frustum
│   ├── containers/
│   │   ├── test_ring_buffer.cpp
│   │   ├── test_free_list.cpp
│   │   ├── test_handle_map.cpp
│   │   └── test_frame_array.cpp
│   ├── random/
│   │   └── test_random.cpp
│   ├── paths/
│   │   └── test_paths.cpp         # GetExeDir, ResolveEngineResource
│   ├── shader/
│   │   ├── test_shader_vfs.cpp          # ShaderVFS mount/resolve
│   │   ├── test_shader_source_manager.cpp # Include parsing, hashing
│   │   ├── test_shader_dependency_graph.cpp # Forward/reverse deps
│   │   ├── test_shader_cache.cpp        # Bytecode cache hit/miss
│   │   ├── test_shader_compiler.cpp     # DXC integration
│   │   ├── test_shader_pipeline.cpp     # End-to-end compile orchestration
│   │   ├── test_shader_permutation.cpp  # Variant expansion
│   │   ├── test_shader_reflection.cpp   # SPIRV-Reflect bindings
│   │   ├── test_shader_hot_reload.cpp   # File watch + dirty flags
│   │   └── test_shader_archive.cpp      # Precompiled archive
│   ├── asset/
│   │   ├── test_vfs.cpp                 # General VFS
│   │   ├── test_asset_cache.cpp         # Asset content cache
│   │   ├── test_asset_registry.cpp      # Global asset database
│   │   ├── test_asset_manifest.cpp      # Build manifest
│   │   ├── test_import_sidecar.cpp      # .import.json files
│   │   ├── test_pak_format.cpp          # .uzpak archive
│   │   └── test_uzshader_format.cpp     # .uzshader binary format
│   └── rhi/
│       ├── test_rhi_types.cpp     # Handle, enum, descriptor tests
│       ├── test_rhi_config.cpp    # Config resolution, CLI parsing
│       ├── test_dx12_device.cpp   # DX12 device creation (Windows only)
│       ├── test_dx12_resources.cpp # DX12 resource creation (Windows only)
│       └── test_dx12_render_loop.cpp # DX12 render loop (Windows only)
└── platform/
    └── test_input.cpp             # Input system (Windows only)
```

## Coverage Requirements

- **Minimum 95%** for all new code
- Every public function must have at least one test
- Test error paths and edge cases, not just the happy path
- No placeholder tests (`expect(true).toBe(true)`)
- No skipped tests (`it.skip(...)`)

## Writing Tests

Follow GoogleTest patterns:

```cpp
TEST(Vec3Test, DotProduct) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(a.Dot(b), 32.0f);
}

TEST(Vec3Test, NormalizedLength) {
    Vec3 v(3.0f, 4.0f, 0.0f);
    Vec3 n = v.Normalized();
    EXPECT_NEAR(n.Length(), 1.0f, 1e-5f);
}

TEST(ConfigTest, CLIOverridesTakesPrecedence) {
    Config config;
    config.LoadFromFile("test.json");       // sets rhi = "vulkan"
    const char* argv[] = {"--rhi=dx12"};
    config.ApplyCLIOverrides(1, argv);
    EXPECT_EQ(config.GetString("rhi"), "dx12");
}
```

## Test Cleanup

Tests must clean up any files they create (logs, temp files).
Never commit generated test artifacts to the repository.
