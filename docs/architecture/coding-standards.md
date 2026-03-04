# Coding Standards — Architecture Directives

> **Status**: MANDATORY — applies to all new code. Existing violations are tracked as technical debt.
> **Enforced by**: Code review, compiler warnings (/W4 MSVC, -Wall -Wextra Clang).

---

## 1. Language Standards

- **C backend files** (`rhi.h`, `rhi_dx12.c`, `rhi_common.c`): C11 (`/std:c11`)
- **C++ engine code**: C++23 (`/std:c++latest` on MSVC, `-std=c++23` on Clang/GCC)
- **No compiler extensions**: MSVC `__declspec(property)`, GCC `__attribute__` — only where
  genuinely required and clearly documented

### RULE CS-1: C headers use C-compatible declarations

Any header included by both C and C++ code (e.g., `rhi.h`) MUST wrap C++ keywords:

```c
// ✅ CORRECT
#ifdef __cplusplus
extern "C" {
#endif
// ... C declarations ...
#ifdef __cplusplus
}
#endif

// ❌ WRONG — C++ default arguments in a C header
void my_fn(int x = 0);  // FORBIDDEN in C headers
```

---

## 2. Math Types

### RULE CS-2: Use engine math types, NEVER backend math types

```cpp
// ✅ CORRECT — engine types (src/engine/core/math.h)
uz::Vec3  position;
uz::Vec4  color;
uz::Mat4  world_transform;
uz::Quat  rotation;

// ❌ WRONG — backend math types
DirectX::XMFLOAT3  position;     // FORBIDDEN above rhi_dx12.c
DirectX::XMFLOAT4X4 transform;   // FORBIDDEN above rhi_dx12.c
glm::vec3          position;     // FORBIDDEN (use uz:: types)
```

### RULE CS-3: Plain `float[3]` / `float[16]` for GPU-uploaded data

GPU constant buffer structs use plain arrays for GPU layout control:

```cpp
// ✅ CORRECT — GPU constant buffer struct
struct MeshConstants {
    float world_view_proj[16];  // plain float for std140/scalar layout
    float color[4];
};

// ❌ WRONG — C++ types in GPU struct
struct MeshConstants {
    uz::Mat4 world_view_proj;  // May have C++ constructors, padding issues
};
```

---

## 3. Memory Management

### RULE CS-4: Use arena allocators for frame/level-lifetime allocations

```cpp
// ✅ CORRECT — frame arena
void* frame_alloc = g_frame_arena.alloc(sizeof(MyStruct), alignof(MyStruct));
new (frame_alloc) MyStruct{...};
// Frame arena is reset at end of frame — no destructor call needed

// ✅ CORRECT — Level arena
LevelObject* obj = level_arena.alloc_one<LevelObject>();

// ❌ WRONG — heap allocation for per-frame data
MyStruct* per_frame = new MyStruct{...};  // Heap allocation every frame = GC pressure
std::vector<DrawCommand> cmds;            // Reallocates every frame
```

### RULE CS-5: `std::unique_ptr` only for objects with unclear lifetime

Use `unique_ptr` only when heap ownership is genuinely required and the lifetime is
not frame/level/permanent:

```cpp
// ✅ CORRECT — unique_ptr for subsystem with complex lifetime
std::unique_ptr<RenderGraph> frame_graph_ = std::make_unique<RenderGraph>();

// ❌ WRONG — unique_ptr for per-frame objects
std::unique_ptr<DrawBatch> batch = std::make_unique<DrawBatch>(); // Use arena instead
```

### RULE CS-6: Handle<T> for cross-lifetime references

Never store raw pointers across potential lifetime boundaries (between game thread and
render thread, between level loads, etc.). Use `Handle<T>` with generation counters.

```cpp
// ✅ CORRECT
Handle<SceneNode> node_ref = scene.get_handle(node_id);
if (SceneNode* node = scene.resolve(node_ref)) {
    // node is valid — handle matched generation
}

// ❌ WRONG — raw pointer that may dangle
SceneNode* raw = &scene.nodes[id];  // Dangling after array realloc
```

---

## 4. Error Handling

### RULE CS-7: Use `uz::Result<T, E>` for fallible operations, not exceptions

```cpp
// ✅ CORRECT
uz::Result<Mesh, LoadError> load_mesh(const char* path);

auto result = load_mesh("assets/cube.uzm");
if (!result.ok()) {
    log_error("Failed to load mesh: %s", result.error().message);
    return;
}
Mesh& mesh = result.value();

// ❌ WRONG — exceptions for flow control
try {
    Mesh mesh = load_mesh("assets/cube.uzm");
} catch (const LoadError& e) {  // FORBIDDEN — no exceptions
    log_error("...");
}
```

### RULE CS-8: Assert for programmer errors, log for runtime errors

```cpp
// ✅ CORRECT — assert for precondition violations (programmer errors)
void set_mesh_data(NodeId id, const StaticMeshData& data) {
    UZ_ASSERT(id != kNodeIdInvalid, "Cannot set data on invalid node");
    UZ_ASSERT(id < scene_.capacity(), "Node ID out of bounds");
    // ...
}

// ✅ CORRECT — log + Result for runtime errors
uz::Result<Texture, LoadError> load_texture(const char* path) {
    if (!file_exists(path)) {
        UZ_LOG_WARN("Texture not found: %s — using fallback", path);
        return uz::Err(LoadError::FileNotFound);
    }
    // ...
}

// ❌ WRONG — abort on recoverable errors
if (!file_exists(path)) {
    abort();  // FORBIDDEN — should return error
}
```

---

## 5. Naming Conventions

| Item | Convention | Example |
|------|-----------|---------|
| Types (structs, classes, enums) | `PascalCase` | `SceneNode`, `RhiBuffer`, `NodeType` |
| Functions / methods | `snake_case` | `load_scene()`, `buffer_create()` |
| Member variables | `snake_case_` (trailing underscore) | `width_`, `scene_ptr_` |
| Constants / enum values | `kPascalCase` (structs) or `SCREAMING_SNAKE` (C enums) | `kNodeIdInvalid`, `RHI_NULL`, `RHI_FORMAT_RGBA8_UNORM` |
| Namespaces | `snake_case` | `namespace uz`, `namespace uz::scene` |
| Template parameters | `PascalCase` | `template<typename T>` |
| Macros | `UZ_SCREAMING_SNAKE` | `UZ_ASSERT()`, `UZ_LOG_WARN()` |

### RULE CS-9: No abbreviations in public API names

```cpp
// ✅ CORRECT — clear, spelled out
void set_render_flags(uint32_t flags);
RhiBuffer create_vertex_buffer(const RhiBufferDesc* desc);

// ❌ WRONG — cryptic abbreviations
void set_rflgs(uint32_t f);
RhiBuffer crt_vb(const RhiBufferDesc* d);
```

---

## 6. File Organization

### RULE CS-10: One class per file, file named after the class

```
StaticMeshProxy → static_mesh_proxy.h + static_mesh_proxy.cpp
RenderGraph     → render_graph.h     + render_graph.cpp
GBufferPass     → gbuffer_pass.h     + gbuffer_pass.cpp
```

### RULE CS-11: Headers have include guards, NOT `#pragma once` for C files

C++ files may use `#pragma once`. C files (`.c`) must use traditional include guards:

```c
// ✅ CORRECT — C header
#ifndef UZ_RHI_H
#define UZ_RHI_H
// ...
#endif /* UZ_RHI_H */
```

```cpp
// ✅ CORRECT — C++ header
#pragma once
// ...
```

### RULE CS-12: No circular includes

If A includes B and B includes A, at least one must use a forward declaration.
Circular includes are a build error waiting to happen.

```cpp
// ✅ CORRECT — forward declare to break cycle
// render_graph.h
namespace uz { struct ISceneProxy; }  // Forward declare, no include
class RenderGraph {
    std::vector<ISceneProxy*> proxies_;  // Pointer OK with forward decl
};
```

---

## 7. Thread Safety

### RULE CS-13: Document thread ownership for every mutable shared resource

Every non-trivial data structure that is accessed from multiple threads MUST have
a comment documenting which thread owns it and how access is synchronized.

```cpp
// ✅ CORRECT — thread ownership documented
class Renderer {
    // Owned by RENDER THREAD. Read-only from other threads after init.
    RhiBuffer frame_constants_buf_ = RHI_NULL;

    // Owned by GAME THREAD. Written by game thread, read by render thread
    // via mutable double-buffer. Protected by frame_mutex_.
    mutable std::mutex frame_mutex_;
    FrameData          latest_frame_;
};
```

### RULE CS-14: No data races — use explicit synchronization

```cpp
// ✅ CORRECT — explicit mutex
{
    std::lock_guard<std::mutex> lock(editor_nodes_mutex_);
    editor_nodes_ = std::move(new_nodes);
}

// ❌ WRONG — unsynchronized write from another thread
editor_nodes_ = new_nodes;  // Data race if render thread reads simultaneously
```

---

## 8. Forbidden Patterns

These patterns are absolutely forbidden in UzEngine code:

```cpp
// ❌ FORBIDDEN: Raw new/delete for anything other than placement new
// Use arena allocators, unique_ptr, or Handle<T>
MyObject* obj = new MyObject();  // FORBIDDEN

// ❌ FORBIDDEN: std::shared_ptr in hot paths
// shared_ptr atomic ref counting is expensive in game loops
std::shared_ptr<Mesh> mesh_ptr;  // FORBIDDEN in render loop

// ❌ FORBIDDEN: Exceptions
throw std::runtime_error("...");  // FORBIDDEN (use Result<T,E>)
try { } catch (...) { }           // FORBIDDEN

// ❌ FORBIDDEN: RTTI
dynamic_cast<Derived*>(base);    // FORBIDDEN (use virtual dispatch or Handle<T>)
typeid(x).name();                // FORBIDDEN

// ❌ FORBIDDEN: goto (except in C error-unwinding with very clear structure)
goto error;  // FORBIDDEN in C++; limited to C error handling only

// ❌ FORBIDDEN: Thread-local storage (except debug logging)
thread_local SomeState state;    // FORBIDDEN — use explicit ownership

// ❌ FORBIDDEN: Global mutable state outside of explicitly designated singletons
static int my_global = 0;        // FORBIDDEN — use dependency injection
```

---

## 9. Shader Code (HLSL)

### RULE CS-15: Shaders live in `shaders/` as `.hlsl` files, never in C++ strings

```cpp
// ❌ FORBIDDEN — shader as string literal in C++ header
constexpr const char* kVertexShaderSrc = R"hlsl(
    // FORBIDDEN — embedded HLSL
)hlsl";

// ✅ CORRECT — shader as standalone file
// shaders/gbuffer.vs.hlsl
// shaders/gbuffer.ps.hlsl
```

### RULE CS-16: Shaders are compiled offline by the build system

Shader compilation MUST happen at build time via DXC (DirectX Shader Compiler), not
at runtime via `D3DCompile`. Runtime compilation is acceptable ONLY for development
hot-reload scenarios.

### RULE CS-17: All shaders use the standard constant buffer layout

```hlsl
// ✅ CORRECT — standard register layout (from docs/specs/rendering/shader-library.md)
cbuffer FrameConstants    : register(b15) { ... }  // Always b15
cbuffer DrawConstants     : register(b0)  { ... }  // Per-draw
cbuffer MaterialConstants : register(b1)  { ... }  // Per-material
```

---

## 10. C-specific Rules (for RHI backend files)

### RULE CS-18: C files use snake_case for ALL identifiers (no PascalCase)

```c
// ✅ CORRECT — C naming in .c files
typedef struct rhi_backend_t rhi_backend_t;
static void dx12_create_device(void);

// ❌ WRONG — C++ naming in .c files
typedef struct RhiBackend RhiBackend;  // PascalCase in .c is wrong
```

### RULE CS-19: Structs in C are typedef'd

```c
// ✅ CORRECT
typedef struct {
    uint32_t width;
    uint32_t height;
} rhi_texture_desc_t;

// ❌ WRONG — must use struct keyword everywhere
struct rhi_texture_desc { ... };  // Requires 'struct' prefix in C
```

### RULE CS-20: Return error codes, not void, from fallible C functions

```c
// ✅ CORRECT — C error return
bool rhi_dx12_init(void* window_handle, uint32_t w, uint32_t h);
// Returns false on failure, caller checks

// ❌ WRONG — void, failure is silent
void rhi_dx12_init(void* window_handle, uint32_t w, uint32_t h);
```
