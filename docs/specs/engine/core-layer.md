# Spec: Core Layer — Primitive Types

> **Status**: Phase 1 (Required before any other system)
> **Owner**: `src/engine/core/`
> **Purpose**: Foundational types used throughout the entire engine codebase. Zero external dependencies. Must compile on all platforms.

---

## Overview

The core layer provides:
1. **Arena allocators** — fast bump allocators for frame/level/permanent lifetimes
2. **Handle<T>** — typed opaque handle (generational index) for cross-lifetime references
3. **StringId** — compile-time/runtime interned string hash for resource IDs and names
4. **Result<T, E>** — error-propagating return type (no exceptions)
5. **Math types** — Vec2, Vec3, Vec4, Mat4, Quat — SIMD-aligned, header-only

---

## File Structure

```
src/engine/core/
├── arena.h         — Arena allocator (frame, level, permanent lifetimes)
├── handle.h        — Handle<T> generational index
├── string_id.h     — StringId compile-time/runtime string interning
├── result.h        — Result<T, E> error type
├── math.h          — Vec2/Vec3/Vec4/Mat4/Quat — SIMD-friendly
└── core.h          — Single include: #include all of the above
```

---

## Arena Allocator

### Design

Arena allocators are the primary allocation strategy in UzEngine. There are three lifetime tiers:

| Arena | Lifetime | Reset frequency | Usage |
|-------|----------|-----------------|-------|
| `frame_arena` | One render frame | Every frame (at frame start) | Per-frame scratch: render graph resources, sorted draw lists, temp buffers |
| `level_arena` | One scene/level | On scene unload | Scene nodes, components, loaded assets for current level |
| `permanent_arena` | Engine lifetime | Never reset | Engine-wide singletons, system registries, shader bytecode |

### API

```cpp
// src/engine/core/arena.h

struct Arena {
    uint8_t* base;          // Start of memory block
    size_t   size;          // Total capacity
    size_t   offset;        // Current allocation position
    size_t   peak;          // Peak usage for profiling
    const char* name;       // Debug name
};

// Initialize an arena with a pre-allocated block
void arena_init(Arena* arena, void* memory, size_t size, const char* name);

// Allocate n bytes, aligned to `align` (must be power of 2)
void* arena_alloc(Arena* arena, size_t n, size_t align = alignof(max_align_t));

// Typed allocation helper
template<typename T>
T* arena_alloc_t(Arena* arena, size_t count = 1) {
    return static_cast<T*>(arena_alloc(arena, sizeof(T) * count, alignof(T)));
}

// Reset to beginning (frees all allocations)
void arena_reset(Arena* arena);

// Save/restore position for temporary sub-allocations
struct ArenaMark {
    size_t offset;
};
ArenaMark arena_mark(const Arena* arena);
void      arena_restore(Arena* arena, ArenaMark mark);
```

### Usage

```cpp
// Frame arena — reset at start of each frame
void engine_frame_begin(void) {
    arena_reset(&g_frame_arena);
}

// Allocate a sorted draw list (lives one frame)
DrawCommand* draw_cmds = arena_alloc_t<DrawCommand>(&g_frame_arena, max_draw_cmds);

// Save position before temporary scratch
ArenaMark mark = arena_mark(&g_frame_arena);
uint8_t* scratch = arena_alloc(&g_frame_arena, 1024 * 1024, 16);
// ... use scratch ...
arena_restore(&g_frame_arena, mark);  // Release scratch without a full reset
```

### Global Arenas

```cpp
// Declared in src/engine/core/arena.h, defined in src/engine/engine.cpp
extern Arena g_frame_arena;    // 64 MB frame scratch
extern Arena g_level_arena;    // 256 MB level data
extern Arena g_permanent_arena; // 64 MB permanent data
```

---

## Handle<T>

### Design

A `Handle<T>` is a generational index used to reference engine objects (entities, sounds, textures, etc.) across frame boundaries without raw pointers. The generation counter detects use-after-free.

```cpp
// src/engine/core/handle.h

template<typename Tag>
struct Handle {
    uint32_t index      : 24;   // Slot index in the pool
    uint32_t generation : 8;    // Generation counter (detects stale handles)

    static Handle null() { return {0, 0}; }
    bool is_null() const { return index == 0; }
    bool operator==(const Handle& o) const { return index == o.index && generation == o.generation; }
    bool operator!=(const Handle& o) const { return !(*this == o); }
    explicit operator bool() const { return !is_null(); }
};

// Usage: distinct handle types that can't be mixed up
using EntityHandle    = Handle<struct EntityTag>;
using TextureHandle   = Handle<struct TextureTag>;
using AudioSourceHandle = Handle<struct AudioSourceTag>;
using MeshHandle      = Handle<struct MeshTag>;
```

### Pool

Handles are managed by a corresponding pool:

```cpp
template<typename T, typename Tag>
struct HandlePool {
    struct Slot { T value; uint8_t generation; bool occupied; };

    std::vector<Slot> slots;
    std::vector<uint32_t> free_list;

    Handle<Tag> insert(T&& value);
    T*          get(Handle<Tag> h);          // Returns nullptr if stale
    const T*    get(Handle<Tag> h) const;
    void        remove(Handle<Tag> h);
    bool        is_valid(Handle<Tag> h) const;
};
```

---

## StringId

### Design

`StringId` is a compile-time-capable, runtime-internable string hash. Used for resource paths, node names, component type names, and event names throughout the engine.

```cpp
// src/engine/core/string_id.h

struct StringId {
    uint32_t hash;  // FNV-1a 32-bit hash of the string

    constexpr StringId() : hash(0) {}
    constexpr StringId(uint32_t h) : hash(h) {}

    bool operator==(StringId o) const { return hash == o.hash; }
    bool operator!=(StringId o) const { return hash != o.hash; }
    bool operator<(StringId o) const { return hash < o.hash; }  // For sorted containers
    explicit operator bool() const { return hash != 0; }
};

// Compile-time literal: "my_string"_sid
consteval StringId operator""_sid(const char* str, size_t len) {
    // FNV-1a 32-bit at compile time
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i)
        hash = (hash ^ (uint8_t)str[i]) * 16777619u;
    return StringId{hash};
}

// Runtime interning (also registers string for debug lookup)
StringId string_id(const char* str);
StringId string_id(std::string_view str);

// Debug lookup (only works if string was registered; returns "<unknown>" otherwise)
const char* string_id_lookup(StringId id);
```

### Usage

```cpp
StringId mesh_id = "res://meshes/hero.uzmesh"_sid;   // compile time
StringId event_id = string_id(dynamic_name);         // runtime

// Use as map key
std::unordered_map<StringId, Asset*, StringIdHash> asset_cache;
```

---

## Result<T, E>

### Design

`Result<T, E>` is a value-semantic error type. No exceptions. No null returns. Forces explicit error handling at call sites.

```cpp
// src/engine/core/result.h

template<typename T, typename E = std::string>
class Result {
    bool   ok_;
    union { T value_; E error_; };
public:
    static Result Ok(T v)  { Result r; r.ok_ = true; new(&r.value_) T(std::move(v)); return r; }
    static Result Err(E e) { Result r; r.ok_ = false; new(&r.error_) E(std::move(e)); return r; }

    bool        is_ok()    const { return ok_; }
    bool        is_err()   const { return !ok_; }
    const T&    value()    const { assert(ok_);  return value_; }
    T&&         take()           { assert(ok_);  return std::move(value_); }
    const E&    error()    const { assert(!ok_); return error_; }
};

// Convenience macros
#define TRY(expr) ({ auto _r = (expr); if (_r.is_err()) return Result::Err(_r.error()); _r.take(); })
#define OK(v)  Result::Ok(v)
#define ERR(msg) Result::Err(msg)
```

### Usage

```cpp
Result<Mesh*> load_mesh(StringId id) {
    auto path = resolve_asset_path(id);
    if (!path) return ERR("Asset not found: " + string_id_lookup(id));

    auto* mesh = arena_alloc_t<Mesh>(&g_level_arena);
    if (!parse_uzmesh(path, mesh)) return ERR("Parse failed");
    return OK(mesh);
}

// At call site:
auto result = load_mesh("hero_mesh"_sid);
if (result.is_err()) {
    log_error("Failed to load mesh: {}", result.error());
    return;
}
Mesh* mesh = result.take();
```

---

## Math Types

### Design

SIMD-aligned math types, header-only, no external dependencies. All types are POD.

```cpp
// src/engine/core/math.h

struct alignas(8)  Vec2 { float x, y; };
struct alignas(16) Vec3 { float x, y, z, _pad; };
struct alignas(16) Vec4 { float x, y, z, w; };
struct alignas(16) Quat { float x, y, z, w; };  // Unit quaternion

// Column-major 4x4 matrix (OpenGL convention, but DX12-compatible with transpose)
struct alignas(16) Mat4 {
    float m[4][4];
    static Mat4 identity();
    static Mat4 translate(Vec3 t);
    static Mat4 rotate(Quat q);
    static Mat4 scale(Vec3 s);
    static Mat4 perspective(float fov_y, float aspect, float near_z, float far_z);
    static Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up);
    Mat4 operator*(const Mat4& o) const;
};

// Free functions
float  vec3_dot(Vec3 a, Vec3 b);
float  vec3_len(Vec3 v);
Vec3   vec3_normalize(Vec3 v);
Vec3   vec3_cross(Vec3 a, Vec3 b);
Vec3   vec3_lerp(Vec3 a, Vec3 b, float t);
Vec3   vec3_transform(Vec3 v, const Mat4& m);   // Apply matrix to position
Vec3   vec3_transform_dir(Vec3 v, const Mat4& m); // Apply matrix to direction (no translation)

Quat   quat_identity(void);
Quat   quat_from_axis_angle(Vec3 axis, float angle);
Quat   quat_mul(Quat a, Quat b);
Quat   quat_slerp(Quat a, Quat b, float t);
Quat   quat_normalize(Quat q);
Mat4   quat_to_mat4(Quat q);
Vec3   quat_rotate_vec3(Quat q, Vec3 v);
```

---

## Dependencies

None. The core layer has **zero external dependencies** — it must compile standalone on every target platform (Windows, Linux, macOS, Android, iOS).

---

## Definition of Done

- [ ] `arena.h` — Arena with 3 global instances (frame/level/permanent), ArenaMark, reset, alloc
- [ ] `handle.h` — Handle<Tag>, HandlePool<T,Tag>, generational index, is_valid check
- [ ] `string_id.h` — StringId, `_sid` literal, string_id(const char*), string_id_lookup debug
- [ ] `result.h` — Result<T,E>, Ok/Err helpers, TRY macro
- [ ] `math.h` — Vec2/Vec3/Vec4/Quat/Mat4 + all free functions listed
- [ ] `core.h` — umbrella include
- [ ] 100% unit test coverage for all types
- [ ] Compiles with zero warnings on MSVC (Windows), Clang (macOS/iOS), GCC (Linux/Android)
