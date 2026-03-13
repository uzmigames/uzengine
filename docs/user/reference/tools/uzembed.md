# uzembed — Shader Embed Tool

Build-time tool that converts `.hlsl` shader sources into C++ `constexpr char[]` headers, plus generates an index header with enum and registry for compile-time shader access.

> **Source**: `runtime/tools/uzembed.cpp` | **Output**: `build/generated/shaders/`

---

## Usage

```bash
uzembed --input-dir <dir> --output-dir <dir> [--quiet]
```

| Flag | Description |
|------|-------------|
| `--input-dir <dir>` | Root directory to scan for `.hlsl` files (recursive) |
| `--output-dir <dir>` | Destination for generated `.gen.h` headers |
| `--quiet`, `-q` | Suppress stdout (errors still go to stderr) |
| `--help`, `-h` | Print usage |

---

## Generated Files

For each `.hlsl` file found recursively under `--input-dir`:

```
shaders/editor/overlay_vs.hlsl  →  build/generated/shaders/editor/overlay_vs.hlsl.gen.h
shaders/runtime/mesh_vs.hlsl    →  build/generated/shaders/runtime/mesh_vs.hlsl.gen.h
```

Each `.gen.h` contains:

```cpp
// overlay_vs.hlsl.gen.h -- AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
inline constexpr char kOverlayVS[] = R"hlsl(
// ... original HLSL source ...
)hlsl";
} // namespace uz::generated
```

Plus one index file at the output root:

```
build/generated/shaders/engine_shaders.gen.h
```

Contains:
- `#include` for every individual `.hlsl.gen.h`
- `enum class EngineShader` with one entry per shader
- `struct EngineShaders` with `GetSource()` and `GetName()` static accessors

---

## Name Conversion

Filenames are converted from `snake_case` to `PascalCase`. Shader stage suffixes (`_vs`, `_ps`, `_cs`, `_gs`, `_hs`, `_ds`, `_as`, `_ms`) are uppercased entirely.

| Filename | Const Name | Enum Name |
|----------|-----------|-----------|
| `overlay_vs.hlsl` | `kOverlayVS` | `OverlayVS` |
| `overlay_text_ps.hlsl` | `kOverlayTextPS` | `OverlayTextPS` |
| `mesh_vs.hlsl` | `kMeshVS` | `MeshVS` |
| `grid_ps.hlsl` | `kGridPS` | `GridPS` |

---

## C++ API

Include the generated index via the engine wrapper:

```cpp
#include "engine_shaders.h"

// Get shader source at compile time
const char* src = uz::EngineShaders::GetSource(uz::EngineShader::OverlayVS);

// Get original filename (for error messages and stage inference)
const char* name = uz::EngineShaders::GetName(uz::EngineShader::OverlayVS);

// Compile from embedded source
auto bytecode = pipeline.LoadBytecodeFromSource(
    EngineShaders::GetName(EngineShader::MeshVS),
    EngineShaders::GetSource(EngineShader::MeshVS));
```

---

## Adding New Shaders

1. Create a `.hlsl` file anywhere under `runtime/shaders/` (any subdirectory)
2. Rebuild — `uzembed` discovers it automatically
3. Use the new enum value in C++: `EngineShader::YourShaderName`

No manual registration, no header editing, no build system changes needed.

---

## Build Integration

### Zig (default)

`uzembed` is compiled and run automatically before the runtime library. The `--quiet` flag is passed by default so build output stays clean.

### CMake

```cmake
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/engine_shaders.gen.h
    COMMAND uzembed --quiet
            --input-dir ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            --output-dir ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders
    DEPENDS ${SHADER_SOURCES}
)
add_custom_target(embed_shaders DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/engine_shaders.gen.h)
add_dependencies(uzengine_runtime embed_shaders)
```

---

## Error Handling

- Errors are written to stderr with `[uzembed] error:` prefix
- Exit code 0 on success, 1 if any file failed to process
- Empty source files are treated as errors
- Missing input directory prints an error and exits with code 1
