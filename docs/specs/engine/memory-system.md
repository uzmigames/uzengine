# Memory System Spec

## Overview

UzEngine uses a **no-GC, no-smart-pointer-soup** memory model. All allocations are organized by lifetime, not individually freed. This gives deterministic performance (no GC pauses, no fragmentation), simpler ownership reasoning, and is well-suited to AI code generation (explicit ownership = readable intent).

## Core Allocators

### Arena Allocator

The fundamental building block. Bump-pointer allocation: O(1) alloc, O(1) reset. Entire arena freed in one operation.

```cpp
struct Arena {
    uint8_t* base;
    size_t   pos;
    size_t   size;

    void*   alloc(size_t bytes, size_t align = 8);
    template<typename T, typename... Args>
    T*      alloc_one(Args&&... args);          // placement-new
    template<typename T>
    T*      alloc_array(size_t count);          // zero-initialized
    void    reset();                            // free all (no destructors)
    Arena   child(size_t sub_size);             // sub-arena view
};

Arena make_arena(size_t capacity);              // VirtualAlloc / mmap
void  free_arena(Arena& a);                     // release OS memory
```

### Engine-wide Arena Lifetimes

| Arena | Lifetime | Contents |
|-------|----------|----------|
| `g_permanent` | Process lifetime | Engine subsystems, asset registries |
| `g_level` | Current level / scene | Scene nodes, loaded meshes, textures |
| `g_frame` | One render frame | Draw calls, debug overlays, temp transforms |
| `g_scratch` | One function call | Parsing buffers, temp strings, sort keys |

```cpp
// Accessing global arenas
Arena& permanent_arena();
Arena& level_arena();
Arena& frame_arena();
Arena& scratch_arena();   // thread-local
```

### Pool Allocator

Fixed-size object pool on top of an arena. O(1) alloc/free with a free-list.

```cpp
template<typename T, size_t N>
struct Pool {
    T       slots[N];
    uint32_t generations[N];  // bump on free → stale handle detection
    uint32_t free_list[N];
    uint32_t free_count;

    Handle<T> alloc();         // returns Handle, not T*
    void      free(Handle<T>);
    T*        get(Handle<T>);  // null on stale handle
};
```

### Scratch (Thread-local Temporary)

Short-lived allocations within a single call stack. Never outlives the function that created them.

```cpp
// Pattern: take a scratch arena for temp work
void parse_scene(const char* text, size_t len, SceneData* out) {
    Arena scratch = scratch_arena();     // thread-local
    char* lower = (char*)scratch.alloc(len + 1);
    to_lower(text, lower, len);
    // ... use lower for parsing ...
    // scratch is automatically reset by the caller's scratch scope
}
```

---

## Ownership Model

### Rule 1: Single Owner (`unique_ptr` or arena)

Every object has exactly one owner. Owner is responsible for destruction.

```cpp
// Arena-owned (no destructor, just freed with the arena)
Mesh* mesh = level_arena().alloc_one<Mesh>(vertices, vertex_count);

// unique_ptr-owned (destructor called on scope exit / reset)
std::unique_ptr<ShaderPipeline> pipeline = make_pipeline(desc);
```

### Rule 2: Raw Pointer = Borrow

A raw `T*` or `T&` means "borrowed — caller must not store this longer than the owner lives."

```cpp
// Function borrows the mesh, does not own it
void upload_mesh_to_gpu(const Mesh* mesh);

// Getter returns a borrow — caller must not hold this past arena reset
Node* SceneManager::get(NodeHandle h);
```

**Convention**: if a function stores a pointer in a field (persists beyond the call), it must be documented explicitly. If it just uses the value locally, a raw pointer is fine.

### Rule 3: Handles for Cross-Lifetime References

When an object needs to reference another object that might be destroyed independently, use a typed handle with generation counter instead of a raw pointer.

```cpp
template<typename T>
struct Handle {
    uint32_t index      = UINT32_MAX;
    uint32_t generation = 0;

    bool valid() const { return index != UINT32_MAX; }
};

// NodeHandle: stable reference to a scene node
using NodeHandle     = Handle<Node>;
// ResourceHandle: stable reference to a loaded resource (mesh, texture, etc.)
using ResourceHandle = Handle<Resource>;

// Pool resolves handle → pointer or nullptr
Node* scene.resolve(NodeHandle h) {
    if (h.index >= nodes_.capacity()) return nullptr;
    if (nodes_.generation(h.index) != h.generation) return nullptr;
    return nodes_.ptr(h.index);
}
```

### Rule 4: No `shared_ptr` in Hot Paths

`shared_ptr` uses atomic refcount operations — expensive on cache-miss-heavy workloads. Allowed only in:
- Extension plumbing (TypeScript FFI boundary objects)
- Async I/O callbacks (file loading futures)
- Debug / editor tooling (not game-critical paths)

Not allowed in:
- Render loop
- Scene update
- Physics tick
- Audio callback

---

## Lifetime Diagram

```
Process start
│
├── permanent_arena allocated (grows on demand)
│   ├── Engine singletons
│   ├── Asset registry tables
│   └── Shader compiler state
│
├── level_arena allocated (per-level)
│   ├── Scene nodes
│   ├── Loaded meshes
│   ├── Loaded textures
│   └── Physics world
│   ↓
│   (reset on scene/level unload)
│
├─── [game loop] ──────────────────────────────────────
│    │
│    ├── frame_arena reset()
│    │   ├── Draw call list
│    │   ├── Debug lines
│    │   ├── Sort keys
│    │   └── Temp transform cache
│    │
│    ├── Update scene
│    ├── Render → JPEG → WebSocket
│    └── repeat
│
Process end
    permanent_arena freed
```

---

## Anti-Patterns to Avoid

```cpp
// ❌ Scattered new/delete — ownership unclear
Node* n = new Node("Root");
// ... 500 lines later ...
delete n;  // who owns this? is it always reached?

// ❌ shared_ptr everywhere — performance, circular refs
std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>(...);
node->mesh = mesh;  // now two owners, refcount overhead

// ❌ Storing raw pointer from arena past arena reset
Node* cached = scene.get(handle);
scene.unload();     // level_arena.reset() called
cached->name;       // UAF — undefined behaviour

// ❌ Arena allocation of objects with destructors
// (arena.reset() doesn't call destructors)
ShaderPipeline* s = level_arena().alloc_one<ShaderPipeline>();
// ShaderPipeline destructor (releases DX12 resources) will never be called!
// Use unique_ptr for objects that own GPU resources.
```

```cpp
// ✅ Arena for plain data
Vertex* verts = level_arena().alloc_array<Vertex>(mesh.vertex_count);

// ✅ unique_ptr for objects with destructors (GPU resources)
auto pipeline = std::make_unique<ShaderPipeline>(desc);

// ✅ Handle for cross-system references
NodeHandle player_ref = scene.find("Player");
// Later, in render system:
Node* player = scene.resolve(player_ref);
if (player) render(player);  // null-safe

// ✅ Scratch for temp work
{
    Arena scratch = scratch_arena();
    char* buf = (char*)scratch.alloc(4096);
    snprintf(buf, 4096, "res://scenes/%s.uzscene", scene_name);
    scene_manager.load(buf, level_arena());
    // scratch freed automatically at scope end
}
```
