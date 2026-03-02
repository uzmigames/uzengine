# UzEngine — C++ Code Architecture

> **Audience**: Engine developers and AI agents generating engine code
> **Scope**: Runtime C++ codebase structure, module contracts, coding patterns
> **Language**: C++23 (MSVC 19.40+, `/std:c++latest`)

---

## 1. Design Principles

| Principle | Rule |
|-----------|------|
| **No garbage** | Arena allocators only; `new`/`delete` only inside arena internals |
| **Explicit ownership** | `unique_ptr` = owner, raw ptr = borrow, `Handle<T>` = cross-lifetime |
| **No shared state** | Systems communicate via messages/queues, not global mutable singletons |
| **Cache-friendly** | SOA (struct-of-arrays) for hot paths; no virtual dispatch in tight loops |
| **Deterministic** | No undefined behavior; sanitizers always enabled in debug |
| **Hot-reloadable** | All game logic isolated in a `game.dll`; engine state survives reload |

---

## 2. Source Tree Layout

```
src/
├── core/                    # Engine kernel — no game logic, no rendering
│   ├── arena.h/cpp          # Arena allocator (linear, reset-on-scope)
│   ├── handle.h             # Handle<T> with generation checking
│   ├── result.h             # Result<T,E> error type (no exceptions)
│   ├── string_id.h          # StringId (FNV-1a hash, compile-time)
│   ├── math/
│   │   ├── vec.h            # Vec2/Vec3/Vec4 (GLM wrappers)
│   │   ├── mat.h            # Mat3/Mat4
│   │   ├── quat.h           # Quaternion
│   │   └── transform.h      # Transform3D (pos + rot + scale)
│   ├── containers/
│   │   ├── array.h          # Arena-backed dynamic array
│   │   ├── string.h         # Arena-backed string (no SSO)
│   │   ├── hash_map.h       # Open-address hash map (Robin Hood)
│   │   └── ring_buffer.h    # SPSC lock-free (existing, promote here)
│   └── platform/
│       ├── platform.h       # Abstraction interface (Win32/Linux stubs)
│       ├── win32.cpp        # Windows implementation
│       └── file_watcher.h/cpp # ReadDirectoryChangesW-based watcher
│
├── scene/                   # Scene graph and node types
│   ├── node.h/cpp           # NodeBase, NodeType enum, NodeHandle
│   ├── node_types.h         # All concrete node type definitions (POD)
│   ├── scene.h/cpp          # Scene: node pool, hierarchy, queries
│   ├── scene_loader.h/cpp   # .uzscene parser (INI-like)
│   ├── scene_serializer.h/cpp # .uzscene writer
│   └── scene_manager.h/cpp  # Active scene lifecycle, hot-reload of .uzscene
│
├── renderer/                # DX12 rendering (existing + extensions)
│   ├── renderer.h/cpp       # Device, swap chain, PSO (existing)
│   ├── fps_overlay.h/cpp    # Bitmap font overlay (existing)
│   ├── scene_params.h       # Thread-safe scene state (existing)
│   ├── render_graph.h/cpp   # Frame graph: passes, barriers, sync
│   ├── gpu_resources.h/cpp  # Buffer/Texture RAII wrappers
│   ├── shader_compiler.h/cpp # DXC wrapper: compile HLSL → DXIL
│   └── material.h/cpp       # Material: shader + parameter block
│
├── assets/                  # Resource management
│   ├── resource_handle.h    # ResourceHandle<T> with ref counting
│   ├── resource_manager.h/cpp # Load, cache, evict resources
│   ├── import/
│   │   ├── mesh_import.h/cpp # GLTF/FBX → .uzmesh
│   │   └── texture_import.h/cpp # PNG → BC7 DDS
│   └── formats/
│       ├── uz_mesh.h/cpp    # .uzmesh binary read/write
│       └── uz_mat.h/cpp     # .uzmat INI-like read/write
│
├── ecs/                     # Entity-Component-System (entt)
│   ├── components.h         # All component structs (POD, no methods)
│   ├── systems.h/cpp        # System functions (free functions, not classes)
│   └── world.h/cpp          # entt::registry wrapper
│
├── ipc/                     # IPC/MCP server (existing + scene handlers)
│   ├── ipc_registry.h/cpp   # Handler dispatch (existing)
│   ├── ipc_poller.h/cpp     # Polling thread (existing)
│   └── handlers/
│       ├── engine_handlers.cpp  # engine.* handlers
│       ├── scene_handlers.cpp   # scene.* handlers
│       ├── shader_handlers.cpp  # shader.* handlers
│       └── asset_handlers.cpp   # asset.* handlers
│
├── encoder/                 # JPEG encoding (existing)
│   └── jpeg_encoder.h/cpp
│
├── network/                 # WebSocket server (existing)
│   └── ws_server.h/cpp
│
└── main.cpp                 # Entry point, subsystem init/shutdown order
```

---

## 3. Core Layer (`src/core/`)

The core layer has **zero dependencies** on other engine modules. It can be unit-tested standalone.

### 3.1 Arena Allocator

```cpp
// core/arena.h
class Arena {
public:
    explicit Arena(size_t capacity);   // heap-backed
    Arena(void* buf, size_t cap);      // stack/pool-backed

    void* alloc(size_t bytes, size_t align = alignof(max_align_t));
    void  reset();                      // O(1) — all allocations invalidated
    size_t used() const;
    size_t capacity() const;

    // Convenience: alloc T with constructor args
    template<typename T, typename... Args>
    T* make(Args&&... args);

    // Convenience: alloc array of T (default-initialized)
    template<typename T>
    std::span<T> make_array(size_t count);

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
};

// Thread-local scratch arena (stack of arenas, LIFO)
Arena& scratch_arena();
struct ScratchScope {
    Arena& arena;
    size_t saved_pos;
    explicit ScratchScope(Arena& a);
    ~ScratchScope();                    // restores arena to saved position
};
```

**Usage pattern**:
```cpp
// Frame-scoped allocations
void render_frame(Arena& frame_arena, ...) {
    auto* draw_calls = frame_arena.make_array<DrawCall>(1024).data();
    // ... fill draw_calls ...
    // frame_arena.reset() called at end of frame by caller
}

// Temporary string manipulation
{
    ScratchScope scratch(scratch_arena());
    auto* path = scratch.arena.make_array<char>(256).data();
    snprintf(path, 256, "shaders/%s.dxil", name);
    load_shader(path);
    // path freed when ScratchScope goes out of scope
}
```

### 3.2 Handle System

```cpp
// core/handle.h
template<typename T>
struct Handle {
    uint32_t index      : 24;
    uint32_t generation : 8;

    bool valid() const { return index != 0 || generation != 0; }
    static Handle null() { return {0, 0}; }
};

// Pool backing Handles (used by Scene, ResourceManager, etc.)
template<typename T>
class Pool {
public:
    Handle<T>  insert(T&& item);
    void       remove(Handle<T> h);
    T*         resolve(Handle<T> h);        // nullptr on stale handle
    const T*   resolve(Handle<T> h) const;
    bool       valid(Handle<T> h) const;

private:
    struct Slot { T item; uint8_t gen; bool live; };
    std::vector<Slot> slots_;             // OK to use std::vector here (not hot)
    std::vector<uint32_t> free_list_;
};
```

### 3.3 Result Type (No Exceptions)

```cpp
// core/result.h
template<typename T, typename E = std::string>
class Result {
public:
    static Result ok(T val);
    static Result err(E error);

    bool     is_ok()  const;
    bool     is_err() const;
    T&       value();           // asserts is_ok()
    const E& error() const;    // asserts is_err()

    // Monadic ops
    template<typename F> auto map(F&& f) -> Result<decltype(f(value())), E>;
    template<typename F> auto and_then(F&& f);
};

// Usage
Result<Mesh*> load_mesh(const char* path) {
    auto* f = fopen(path, "rb");
    if (!f) return Result<Mesh*>::err("file not found: " + std::string(path));
    // ...
    return Result<Mesh*>::ok(mesh);
}

auto result = load_mesh("player.uzmesh");
if (result.is_err()) {
    log_error("[mesh] %s", result.error().c_str());
    return;
}
Mesh* mesh = result.value();
```

### 3.4 StringId (Interned Hashes)

```cpp
// core/string_id.h
struct StringId {
    uint64_t hash;

    constexpr StringId() : hash(0) {}
    explicit StringId(const char* str);   // runtime hashing (FNV-1a 64-bit)
    StringId(const StringId&) = default;

    bool operator==(const StringId&) const = default;
    bool operator< (const StringId&) const;
};

// Compile-time literal
consteval StringId operator""_sid(const char* str, size_t len);

// Usage
StringId id = "player_mesh"_sid;         // compile-time
StringId dyn = StringId(runtime_name);   // runtime
```

---

## 4. Scene System (`src/scene/`)

### 4.1 Node Types

All node types are **plain structs** (no virtual dispatch). The `NodeType` enum drives dispatch.

```cpp
// scene/node_types.h
enum class NodeType : uint16_t {
    Node,            // base / empty container
    Node3D,          // 3D transform
    StaticMesh,      // renderable mesh (no animation)
    SkeletalMesh,    // skinned mesh
    DirectionalLight,
    PointLight,
    SpotLight,
    Camera,
    SpringArm,
    CapsuleCollider,
    BoxCollider,
    SphereCollider,
    RigidBody,
    AudioSource,
    Script,          // game logic attachment
    Count
};

// Base data present in every node
struct NodeBase {
    Handle<NodeBase>  self;          // own handle
    Handle<NodeBase>  parent;        // null = root
    Handle<NodeBase>  first_child;
    Handle<NodeBase>  next_sibling;
    NodeType          type;
    char              name[64];      // max 63 chars + null
    bool              visible;
    bool              enabled;
};

// Per-type data (stored separately for cache friendliness)
struct Node3DData {
    glm::vec3  position;
    glm::quat  rotation;
    glm::vec3  scale;
    glm::mat4  world_transform;      // computed, not serialized
};

struct StaticMeshData {
    ResourceHandle<Mesh>     mesh;
    ResourceHandle<Material> material;
    bool                     cast_shadow;
    bool                     receive_shadow;
    uint8_t                  lod_bias;
};

struct DirectionalLightData {
    glm::vec3  color;
    float      intensity;
    bool       shadow_enabled;
    uint8_t    shadow_cascade_count; // 1-4
};

struct CameraData {
    float   fov;           // vertical, degrees
    float   near_plane;
    float   far_plane;
    bool    is_primary;    // only one camera active at a time
};
// ... (one struct per NodeType)
```

### 4.2 Scene Graph

```cpp
// scene/scene.h
class Scene {
public:
    explicit Scene(Arena& arena);

    // Node creation/destruction
    Handle<NodeBase> add_node(NodeType type, Handle<NodeBase> parent, const char* name);
    void             remove_node(Handle<NodeBase> h);   // removes subtree

    // Accessors
    NodeBase*       get_base(Handle<NodeBase> h);
    Node3DData*     get_3d(Handle<NodeBase> h);         // null if not Node3D+
    StaticMeshData* get_mesh(Handle<NodeBase> h);       // null if wrong type

    // Traversal
    void  for_each_child(Handle<NodeBase> parent, std::function<void(Handle<NodeBase>)> fn);
    void  for_each_depth_first(std::function<void(Handle<NodeBase>, int depth)> fn);

    // Properties
    void  set_property(Handle<NodeBase> h, const char* key, const char* value);
    bool  get_property(Handle<NodeBase> h, const char* key, char* out_buf, size_t buf_len);

    // Serialization
    Result<void>  save(const char* path, Arena& scratch);
    Result<void>  load(const char* path, Arena& scratch);

private:
    Arena&              arena_;
    Pool<NodeBase>      nodes_;
    Handle<NodeBase>    root_;

    // Per-type component arrays (parallel to node pool)
    Node3DData              node3d_data_[4096];
    StaticMeshData          static_mesh_data_[2048];
    DirectionalLightData    dir_light_data_[16];
    CameraData              camera_data_[8];
    // ...
};
```

---

## 5. Renderer Integration

### 5.1 Render Graph (Pass-Based)

The renderer operates on a **render graph**: each frame, systems submit render passes which are sorted, de-duplicated, and executed.

```cpp
// renderer/render_graph.h
struct RenderPass {
    StringId          name;
    uint32_t          sort_key;       // lower = earlier
    D3D12_BARRIER_GROUP barriers_in;
    D3D12_BARRIER_GROUP barriers_out;
    std::function<void(ID3D12GraphicsCommandList7*)> execute;
};

class RenderGraph {
public:
    void add_pass(RenderPass pass);
    void execute(ID3D12GraphicsCommandList7* cmd);
    void reset();   // called each frame
};
```

### 5.2 GPU Resource Wrappers

```cpp
// renderer/gpu_resources.h
class GpuBuffer {
public:
    GpuBuffer(ID3D12Device* dev, size_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES init_state);
    ~GpuBuffer();                          // releases ComPtr automatically
    GpuBuffer(GpuBuffer&&) noexcept;

    ID3D12Resource*  resource() const;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_va() const;
    void* map();                           // MAP_WRITE_NO_OVERWRITE shortcut
    void  unmap();

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> res_;
    void* mapped_ptr_ = nullptr;
};

class GpuTexture {
public:
    GpuTexture(ID3D12Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT fmt,
               D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES init_state);
    ~GpuTexture();

    ID3D12Resource* resource() const;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv() const;  // if created with RT flag
    D3D12_CPU_DESCRIPTOR_HANDLE srv() const;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> res_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_{};
    D3D12_CPU_DESCRIPTOR_HANDLE srv_{};
};
```

---

## 5b. Asset Identity — Path as Primary Key

**Rule**: The identity of any asset is its **path relative to `res://`**. No GUIDs. No `.meta` files.

### Why No GUIDs

Unity's `.meta` file (containing a GUID) is an external sidecar that must stay synchronized with its source file. Deleting or moving an asset without the `.meta` breaks every scene that referenced it — because the reference is the GUID, not the path.

UzEngine avoids this entirely:

```
# .uzscene — asset reference is a plain path
mesh     = "res://assets/player.glb"
material = "res://materials/wood.uzmat"
```

If the file moves, find-and-replace in text files fixes every reference. No GUID registry to update.

### Import Sidecar (Optional, Not Required)

When a source asset needs conversion (`.glb` → `.uzmesh`, `.png` → BC7 DDS), the engine generates a sidecar automatically:

```
res://assets/player.glb              ← source file (version-controlled)
res://assets/.import/player.uzmesh   ← generated (in .gitignore)
```

The `.import/` directory is **regenerated on demand**. Deleting it is safe — the engine re-imports on next launch. No broken references, no dangling GUIDs.

### ResourceHandle vs Path

Inside the engine, loaded assets are referenced by `ResourceHandle<T>` — an index into the resource cache. But the cache is keyed by the normalized path string, not a GUID:

```cpp
// Loading always goes through path
ResourceHandle<Mesh> h = resource_manager.load<Mesh>("res://assets/player.glb");

// ResourceManager internally: path → normalize → lookup cache → load if miss
// Eviction: LRU by handle ref count; path key is stable forever
```

---

## 5c. Script Attachment — Declarative, No Global Registry

**Rule**: Scripts are attached to nodes by file path in the scene file. There is no global script registry. Missing scripts degrade gracefully — they never crash the engine.

### The Unity Problem

Unity's `MonoBehaviour` system registers every script type in a global assembly registry. When a script class is deleted or renamed, Unity cannot find it by GUID anymore — the component silently breaks or throws `NullReferenceException` at runtime. In large projects this becomes unmanageable.

### UzEngine Approach

```ini
# In .uzscene — script attachment is a file path, nothing more
[node id=5 type=Node3D name="Player" parent=1]
script = "res://game/player.cpp"
```

```cpp
// engine/scene_manager.cpp — loading a scripted node
void attach_script(NodeBase& node, const char* script_path, GameContext* ctx) {
    if (!file_exists(script_path)) {
        log_warn("[scene] script not found: %s — node '%s' will run without logic",
                 script_path, node.name);
        return;   // node is alive, just has no logic — NOT a crash
    }
    // Register with game.dll to get a script instance
    node.script_state = game_api.create_script(ctx, script_path);
}
```

**Script removed from disk** → node logs a warning, continues to exist in the scene, can be re-attached later. The scene and the game keep running.

**Script renamed** → find-and-replace the path string in `.uzscene` files. No registry to update.

---

## 5d. Prefab System — Scene-Within-Scene

**Rule**: Prefabs are `.uzscene` files that get instantiated as a subtree. No separate prefab format. No GUID cross-references.

### Instantiation

```ini
# main.uzscene — reference a prefab by path
[node id=10 type=Node3D name="Enemy1" parent=1]
prefab = "res://prefabs/enemy.uzscene"
transform = [...]
```

At load time, the engine expands the prefab reference by inserting a copy of `enemy.uzscene`'s node tree as children of node 10. This is a **structural copy** — edits to the prefab file propagate when the scene is reloaded.

### Instantiation from Game Code

```cpp
// In game.dll — spawn a prefab at runtime
Handle<NodeBase> spawn_prefab(GameContext* ctx, const char* prefab_path, glm::vec3 pos) {
    // Clones subtree from loaded prefab — uses frame arena, fast
    Handle<NodeBase> root = ctx->scene->instantiate_prefab(prefab_path, ctx->frame_arena);
    ctx->scene->get_3d(root)->position = pos;
    return root;
}
```

### Object Pooling

Pooling is optional — the arena makes allocation cheap. For cases where object churn is high (bullets, particles), pooling is implemented by **disabling** and **re-enabling** nodes rather than destroy/recreate:

```cpp
// Pool of 100 bullet nodes pre-created at scene load
Handle<NodeBase> pool_acquire(NodePool& pool) {
    for (auto h : pool.inactive) {
        if (pool.inactive_count > 0) {
            auto h = pool.inactive[--pool.inactive_count];
            scene.get_base(h)->enabled = true;   // re-activate
            return h;
        }
    }
    // Pool exhausted → instantiate new (fallback, not common)
    return scene.instantiate_prefab(pool.prefab_path, level_arena);
}

void pool_release(NodePool& pool, Handle<NodeBase> h) {
    scene.get_base(h)->enabled = false;   // deactivate, not destroy
    pool.inactive[pool.inactive_count++] = h;
}
```

---

## 6. ECS Layer (`src/ecs/`)

The ECS layer is the **runtime bridge** between the scene graph (editor view) and the renderer/physics (runtime systems). When a scene loads, nodes are mapped to ECS entities.

```cpp
// ecs/components.h — all POD, no constructors, no virtual
struct CTransform {
    glm::mat4 local;
    glm::mat4 world;
};

struct CMeshRenderer {
    ResourceHandle<Mesh>     mesh;
    ResourceHandle<Material> material;
    bool cast_shadow;
};

struct CDirLight {
    glm::vec3 color;
    float     intensity;
    bool      shadow_enabled;
};

struct CCamera {
    float fov;
    float near_plane;
    float far_plane;
    bool  is_primary;
};

enum class ScriptKind : uint8_t { CPP, LUA };

struct CScriptRef {
    StringId    script_id;  // FNV-1a hash of res:// path (used by LuaScriptReloader)
    ScriptKind  kind;       // CPP: opaque game.dll state | LUA: Lua VM instance ref
    void*       state;      // CPP: opaque ptr owned by game.dll
                            // LUA: weak ref into LuaJIT VM (owned by LuaVM instance table)
};

// ecs/systems.h — all free functions, grouped by phase
namespace systems {
    // Phase: logic
    void update_scripts(entt::registry& reg, float dt);  // dispatches CPP and LUA scripts

    // Phase: pre-render
    void update_transforms(entt::registry& reg);
    void cull_frustum(entt::registry& reg, const CCamera& cam, Arena& frame_arena);
    void batch_draw_calls(entt::registry& reg, Arena& frame_arena, RenderGraph& graph);

    // Phase: physics (Phase 3)
    void step_physics(entt::registry& reg, float dt);
    void sync_physics_transforms(entt::registry& reg);
}
```

---

## 7. Error Handling Policy

| Context | Strategy |
|---------|----------|
| Engine initialization (fatal) | `assert(false)` / `MessageBoxA` + exit |
| Asset loading (recoverable) | `Result<T, std::string>` returned to caller |
| IPC handler errors | JSON `{ "error": "..." }` response, continue running |
| Game logic errors (debug) | `UZ_ASSERT(cond, msg)` macro → log + break |
| Game logic errors (release) | Logged, execution continues (no crash) |

```cpp
// core/assert.h
#ifdef UZ_SHIPPING
  #define UZ_ASSERT(cond, msg) ((void)(cond))
#else
  #define UZ_ASSERT(cond, msg) \
      do { if (!(cond)) { \
          log_error("[ASSERT] %s:%d — " msg, __FILE__, __LINE__); \
          __debugbreak(); \
      } } while(0)
#endif
```

---

## 8. Threading Model

```
Thread          Owns                              Communicates via
──────────────────────────────────────────────────────────────────
Main (Win32)    Window, message pump              signals WM_QUIT to render thread
Render          DX12 command queues, swap chain   writes to ring_buffer → Encoder
Encoder         JPEG compression, libjpeg-turbo   writes to ring_buffer → Network
Network         IXWebSocket server                reads from ring_buffer
IPC Poller      Reads command.json               calls handler registry (thread-safe)
File Watcher    ReadDirectoryChangesW             posts events to watcher queue
```

**Synchronization rules:**
- Render ↔ Encoder: SPSC `RingBuffer<FrameData>` (lock-free)
- IPC Poller → engine state: `std::atomic` for scalars, `std::mutex` for structs
- File Watcher → shader compiler: `std::deque` + `std::mutex` (low-frequency)
- No shared mutable state between Render and Game Logic threads in Phase 3

---

## 9. Subsystem Initialization Order

`main.cpp` initializes subsystems in this strict order:

```
1. Platform init         (timeBeginPeriod, SetProcessDpiAwareness, console)
2. Logger init           (OutputDebugString + file sink)
3. Permanent arena       (128 MB, never reset)
4. Resource manager      (needs permanent arena)
5. Renderer              (DX12 device, swap chain, PSO)
6. Shader compiler       (DXC, loads d3dcompiler_47.dll)
7. Scene manager         (loads project scene)
8. IPC registry          (register all handlers)
9. IPC poller            (start polling thread)
10. File watcher         (start watching shaders/, scenes/, materials/)
11. Network / WS server  (start WebSocket on port 9002)
12. Encoder thread       (start JPEG encode loop)
13. Game loop            (render thread starts here)
```

Shutdown is the exact reverse order.

---

## 10. Coding Conventions

### Naming

| Kind | Convention | Example |
|------|-----------|---------|
| Types | `PascalCase` | `SceneManager`, `NodeHandle` |
| Functions | `snake_case` | `load_scene()`, `add_node()` |
| Members | `snake_case_` (trailing `_`) | `root_node_`, `arena_` |
| Constants | `k_UPPER_SNAKE` | `k_MAX_NODES`, `k_WS_PORT` |
| Macros | `UZ_UPPER_SNAKE` | `UZ_ASSERT`, `UZ_SHIPPING` |
| Files | `snake_case.h/cpp` | `scene_loader.h`, `gpu_resources.cpp` |

### File Rules

- **One class / one major subsystem per file pair** (`.h` + `.cpp`)
- **Headers are `#pragma once`** — no include guards
- **No `using namespace std;`** — always explicit
- **No raw arrays in headers** — use `std::span` or `Arena::make_array`
- **`[[nodiscard]]` on all `Result<T>`** return types

### Include Order (clang-format enforced)

```cpp
// 1. Corresponding header
#include "scene_loader.h"

// 2. Engine headers (alphabetical)
#include "core/arena.h"
#include "core/result.h"
#include "scene/node.h"

// 3. Third-party (alphabetical)
#include <nlohmann/json.hpp>

// 4. System / Windows headers last
#include <windows.h>
```

---

## 10. LuaBinding API (`src/engine/scripting/`)

These helpers are compiled into `uz_engine.lib`. They are used by `game.dll` to set up the LuaJIT VM and drive per-node Lua scripts. `uz_engine.lib` does **not** link LuaJIT — that is done by `game.dll` at link time.

```cpp
// scripting/lua_sandbox.h
// Call once after luajit_newstate() — strips dangerous globals
void lua_sandbox_init(lua_State* L);

// scripting/lua_binding.h
// Register engine API metatables into the VM
// Call once after lua_sandbox_init()
void lua_binding_register_all(lua_State* L);

// Individual registration functions (for selective binding in tests)
void lua_binding_register_math(lua_State* L);       // Vec3, Quat
void lua_binding_register_scene(lua_State* L);      // Scene, NodeRef
void lua_binding_register_input(lua_State* L);      // InputState
void lua_binding_register_resources(lua_State* L);  // load_mesh, load_mat

// scripting/script_runner.h
struct LuaScriptState {
    int instance_ref;   // Lua registry reference to the per-node instance table
};

// Load and instantiate a .lua module for a specific node
// Returns LuaScriptState to store in CScriptRef.state
// On load error: logs error, returns {.instance_ref = LUA_NOREF}
LuaScriptState lua_script_load(lua_State* L, const char* res_path, Arena& scratch);

// Dispatch lifecycle methods — all wrapped in lua_pcall (never throw)
void lua_script_on_start (lua_State* L, LuaScriptState s, GameContext* ctx);
void lua_script_on_update(lua_State* L, LuaScriptState s, GameContext* ctx, float dt);
void lua_script_on_stop  (lua_State* L, LuaScriptState s, GameContext* ctx);

// Release the Lua registry reference (call on node removal)
void lua_script_release(lua_State* L, LuaScriptState s);
```

### VM Initialization Sequence (called inside `game.dll`)

```cpp
// game.cpp — on_start
void uz_game_start(GameContext* ctx) {
    L = luaL_newstate();
    lua_sandbox_init(L);       // strip dangerous globals
    lua_binding_register_all(L); // expose Vec3, Scene, InputState, etc.

    // Load scripts for all CScriptRef nodes in the scene
    auto view = ctx->world->view<CScriptRef>();
    for (auto [entity, ref] : view.each()) {
        if (ref.kind == ScriptKind::LUA) {
            auto state = lua_script_load(L, string_id_str(ref.script_id), scratch);
            ref.state = new LuaScriptState(state);  // stored in node
            lua_script_on_start(L, state, ctx);
        }
    }
}
```

See [specs/lua-scripting.md](../specs/lua-scripting.md) for the full Lua API surface, sandboxing rules, and hot-reload protocol.

---

## 11. LuaVM Lifecycle

The `LuaVM` class is created and owned by `game.dll`. It manages the LuaJIT VM lifecycle, script loading, timer scheduling, and state persistence across hot-reload.

```cpp
// game/lua_vm.h
class LuaVM {
public:
    static LuaVM* create();           // luajit_newstate, openlibs, sandbox_apply, register_bindings
    void destroy();                   // lua_close, release all VM state
    ~LuaVM();

    // Load and instantiate a script for a node
    void load_script(const char* res_path, entt::entity entity, GameContext* ctx);

    // Timer scheduling (called from Lua script via ctx.schedule_after, ctx.schedule_every, etc.)
    uint32_t schedule_after(float delay_s, int callback_ref);   // returns timer_id
    uint32_t schedule_every(float interval_s, int callback_ref);
    void     cancel_timer(uint32_t timer_id);
    void     schedule_next_frame(int callback_ref);

    // Frame update: drain all expired timers and call their callbacks
    void drain_timers(float dt);

    // State persistence across game.dll hot-reload
    void serialize_instances(Arena* out);      // msgpack all self tables to blob
    void deserialize_instances(void* blob);    // restore self tables from blob

private:
    lua_State* L_;
    struct LuaTimer {
        int      callback_ref;   // lua_ref to Lua function
        float    remaining;      // seconds until fire
        float    interval;       // 0 = one-shot, >0 = repeating
        bool     cancelled;
    };
    std::vector<LuaTimer> timers_;
    uint32_t next_timer_id_;
};
```

### VM Initialization (called in game.dll::uz_game_start)

```cpp
void uz_game_start(GameContext* ctx) {
    // Create and initialize the LuaJIT VM
    g_lua_vm = LuaVM::create();

    // Register all engine bindings (Vec3, NodeRef, Scene, InputState, etc.)
    g_lua_vm->register_bindings(L);

    // Load and instantiate all Lua scripts attached to nodes
    auto view = ctx->world->view<CScriptRef>();
    for (auto [entity, ref] : view.each()) {
        if (ref.kind == ScriptKind::LUA) {
            g_lua_vm->load_script(string_id_str(ref.script_id), entity, ctx);
            // Call M:on_start(ctx)
        }
    }

    // Drain initial timer queue (in case any on_start callbacks scheduled timers)
    g_lua_vm->drain_timers(0);
}
```

---

## 12. Timer System API

Lua scripts can schedule callbacks to run after a delay or repeatedly. Timers are managed by the `LuaVM` and drained every frame before `on_update()`.

```cpp
// In Lua scripts (accessed via ctx binding):
local timer_id = ctx:schedule_after(2.5, function()
    print("2.5 seconds have passed")
end)

local repeating_id = ctx:schedule_every(0.5, function()
    -- Called every 0.5 seconds indefinitely
end)

ctx:cancel_timer(timer_id)     -- cancel by ID

ctx:schedule_next_frame(function()
    -- Called at the start of the next on_update()
end)
```

**C++ backing (inside game.dll):**

```cpp
struct LuaTimer {
    int      callback_ref;     // Lua registry reference to the callback function
    float    remaining;        // Seconds until this timer fires (decremented each frame)
    float    interval;         // 0 = one-shot; >0 = repeating with this interval
    bool     cancelled;        // Marked for deletion; will be removed on next drain
};

// LuaVM::drain_timers() is called BEFORE CScriptRef::on_update() every frame:
void LuaVM::drain_timers(float dt) {
    for (auto& timer : timers_) {
        if (timer.cancelled) continue;

        timer.remaining -= dt;
        if (timer.remaining <= 0) {
            lua_rawgeti(L_, LUA_REGISTRYINDEX, timer.callback_ref);
            if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
                log_error("[lua] timer callback error: %s", lua_tostring(L_, -1));
                lua_pop(L_, 1);
            }

            if (timer.interval > 0) {
                // Repeating timer: reschedule
                timer.remaining = timer.interval;
            } else {
                // One-shot: mark for removal
                timer.cancelled = true;
            }
        }
    }

    // Remove cancelled timers
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                  [](const LuaTimer& t) { return t.cancelled; }),
                  timers_.end());
}
```

---

## 13. EventBus<T> API

The `EventBus<T>` template enables loose coupling between game systems. Systems can subscribe to typed events and react without knowing about each other.

```cpp
// core/event_bus.h
template<typename T>
class EventBus {
public:
    using Handle = uint32_t;

    // Subscribe a callback; returns a handle for unsubscribe
    Handle subscribe(std::function<void(const T&)> cb);

    // Unsubscribe by handle
    void unsubscribe(Handle h);

    // Emit an event (safe to call from Game Logic thread only)
    void emit(const T& event);

private:
    struct Subscription {
        uint32_t handle;
        std::function<void(const T&)> callback;
    };
    std::vector<Subscription> subscribers_;
    uint32_t next_handle_ = 0;
};

// RAII subscription wrapper (auto-unsubscribe on destruction)
template<typename T>
struct AutoSub {
    EventBus<T>& bus;
    typename EventBus<T>::Handle handle;

    AutoSub(EventBus<T>& b, std::function<void(const T&)> cb)
        : bus(b), handle(b.subscribe(cb)) {}

    ~AutoSub() { bus.unsubscribe(handle); }

    AutoSub(const AutoSub&) = delete;
    AutoSub& operator=(const AutoSub&) = delete;
};
```

### Predefined Engine Events

```cpp
// events/engine_events.h
struct SceneLoadedEvent {
    std::string_view scene_path;
};

struct SceneUnloadedEvent {
    std::string_view scene_path;
};

struct NodeAddedEvent {
    entt::entity entity;
    const char* node_name;
};

struct NodeRemovedEvent {
    entt::entity entity;
    const char* node_name;
};

struct CollisionEvent {
    Handle<NodeBase> node_a;
    Handle<NodeBase> node_b;
    glm::vec3 contact_point;
    glm::vec3 contact_normal;
    float     impulse;           // magnitude of collision impulse
};

struct TriggerEvent {
    Handle<NodeBase> trigger;    // the trigger volume
    Handle<NodeBase> other;      // the object that entered/exited
    bool             entered;    // true = entered, false = exited
};
```

### Global Event Bus Instances

```cpp
// In game.cpp
EventBus<SceneLoadedEvent>  g_event_scene_loaded;
EventBus<SceneUnloadedEvent> g_event_scene_unloaded;
EventBus<NodeAddedEvent>     g_event_node_added;
EventBus<NodeRemovedEvent>   g_event_node_removed;
EventBus<CollisionEvent>     g_event_collision;
EventBus<TriggerEvent>       g_event_trigger;
```

### Usage Example

```cpp
// In a game system (e.g., enemy spawner)
AutoSub<NodeAddedEvent> on_node_added(
    g_event_node_added,
    [this](const NodeAddedEvent& evt) {
        if (strcmp(evt.node_name, "Treasure") == 0) {
            this->on_treasure_spawned(evt.entity);
        }
    }
);
// Auto-unsubscribes when on_node_added goes out of scope
```

---

## 14. TagRegistry API

Tags are a lightweight way to mark nodes with string identifiers, used for queries ("find all nodes tagged 'enemy'"). Each tag is stored as a bit index in a 64-bit bitmask.

```cpp
// ecs/tag_registry.h
class TagRegistry {
public:
    static TagRegistry& get();

    // Register a new tag; returns bit index (0–63)
    // Max 64 unique tags per project
    uint8_t register_tag(std::string_view name);

    // Lookup tag index by name
    uint8_t get_index(std::string_view name) const;

    // Reverse lookup: get name by index
    std::string_view get_name(uint8_t index) const;

private:
    static constexpr size_t k_MAX_TAGS = 64;
    std::array<std::string, k_MAX_TAGS> tag_names_;
    std::unordered_map<std::string_view, uint8_t> tag_index_;
    uint8_t tag_count_ = 0;
};

// Component that stores tags
struct CTags {
    std::bitset<64> bits;   // one bit per possible tag
};
```

### Query Helpers

```cpp
// ecs/tag_queries.h
void scene_find_by_tag(entt::registry& world, std::string_view tag,
                       std::vector<entt::entity>& out);

void scene_for_each_with_tag(entt::registry& world, std::string_view tag,
                             std::function<void(entt::entity)> fn);

// Usage
std::vector<entt::entity> enemies;
scene_find_by_tag(ctx->world, "enemy", enemies);
for (auto e : enemies) {
    auto* mesh = ctx->world->try_get<CMeshRenderer>(e);
    if (mesh) mesh->cast_shadow = true;
}
```

---

## 15. SaveManager API

The `SaveManager` handles serialization of game state to save slots, including scene state, node transforms, and custom game data (via callbacks in `game.dll`).

```cpp
// engine/save_system/save_manager.h
class SaveManager {
public:
    static SaveManager& get();

    // Write a save slot (slot -1 = autosave, with rotating buffer)
    void write_slot(int slot, std::function<void(SaveWriter&)> fn);

    // Read a save slot (returns false if save doesn't exist)
    bool read_slot(int slot, std::function<void(SaveReader&)> fn);

    // Check if a save exists
    bool slot_exists(int slot) const;

    // Delete a save
    void delete_slot(int slot);

    // Get metadata for a slot (timestamp, active scene, thumbnail)
    SaveMetadata get_metadata(int slot) const;

private:
    // Save files stored in %APPDATA%/UzEngine/<project>/saves/
    // Format: binary msgpack at runtime, JSON in debug mode
};

struct SaveMetadata {
    int64_t  timestamp;           // Unix timestamp of save creation
    char     scene_path[256];     // res:// path of active scene
    char     thumbnail_path[256]; // path to .uzsave_N.jpg (320x180 JPEG)
    uint32_t version;             // game version at save time
};
```

See [specs/save-system.md](../specs/save-system.md) for the full specification including binary format, Lua API, and save slot management.

---

## 16. LocaleManager API

Localization is managed by a central `LocaleManager` that loads translations from CSV files and provides a fallback chain (current locale → English → key itself).

```cpp
// engine/i18n/locale_manager.h
class LocaleManager {
public:
    static LocaleManager& get();

    // Load CSV translations (supports glob pattern, e.g., "res://locales/*.csv")
    bool load(std::string_view csv_path);

    // Set active locale (e.g., "en", "pt_br", "ja")
    void set_locale(std::string_view code);

    // Get current locale
    std::string_view get_locale() const;

    // Translate a key; fallback chain: current → "en" → key itself
    const char* translate(std::string_view key) const;

    // Formatted translation (printf-style)
    std::string translatef(std::string_view key, ...) const;

private:
    // CSV parsed into flat hash map: "ui.menu.play" → "Play"
    // One table per locale (locale code → key → value)
};
```

### CSV Format Example

```csv
# res://locales/en.csv
key,value
ui.menu.play,Play Game
ui.menu.settings,Settings
ui.menu.quit,Quit
game.objective,Reach the castle
npc.dialog.greeting,Hello, traveler!

# res://locales/pt_br.csv
key,value
ui.menu.play,Jogar
ui.menu.settings,Configurações
ui.menu.quit,Sair
game.objective,Alcance o castelo
npc.dialog.greeting,Olá, viajante!
```

See [specs/localization.md](../specs/localization.md) for the full specification including fallback rules, font overrides, and CSV parsing details.

---

## 17. ConsoleCommandRegistry API

In-game console commands can be registered by game systems and executed via the editor console or in-game debug overlay.

```cpp
// engine/debug/console_command_registry.h
class ConsoleCommandRegistry {
public:
    static ConsoleCommandRegistry& get();

    // Register a console command
    // fn receives argv (span of command words) and returns output string
    void add(std::string_view name,
             std::function<std::string(std::span<std::string_view>)> fn);

    // Execute a command line (e.g., "spawn_enemy player 100")
    std::string dispatch(std::string_view command_line);

    // List all registered commands
    std::vector<std::string_view> list_commands() const;

private:
    std::unordered_map<std::string, std::function<...>> commands_;
};
```

### Usage Example

```cpp
// Register a command to spawn an enemy
ConsoleCommandRegistry::get().add("spawn_enemy", [ctx](std::span<std::string_view> args) {
    if (args.size() < 2) return "Usage: spawn_enemy <prefab> <count>";

    std::string prefab = std::string(args[0]);
    int count = std::stoi(std::string(args[1]));

    for (int i = 0; i < count; ++i) {
        ctx->scene->instantiate_prefab(prefab.c_str(), ctx->level_arena);
    }

    return fmt::format("Spawned {} enemies from {}", count, prefab);
});

// Call from debug console:
// > spawn_enemy res://prefabs/goblin.uzscene 5
// Output: "Spawned 5 enemies from res://prefabs/goblin.uzscene"
```

---

## 18. CScriptRef — Updated Definition

The `CScriptRef` component stores a reference to a script (C++ or Lua) attached to a node. It is updated to reflect both CPP and Lua script states:

```cpp
enum class ScriptKind : uint8_t { CPP, LUA };

struct CScriptRef {
    StringId    script_id;       // FNV-1a hash of res:// path (used for hot-reload matching)
    ScriptKind  kind;            // CPP or LUA
    void*       state;           // CPP: opaque pointer owned by game.dll
                                 // LUA: Lua registry reference (cast from int), owned by LuaVM
};
```

The hot-reload system uses `script_id` to match nodes with their script files during reload. When a Lua script file changes on disk, the reloader finds all nodes with a matching `script_id` and re-runs `on_start` with the new code.

---

## TEMPORARY: Engine Audio System

> **NOTE**: This section is a placeholder. See [engine/audio-system.md](./audio-system.md) for the full audio system technical documentation.

The audio system uses XAudio2 for low-latency playback, voice pooling for efficiency, and XAPO for per-bus DSP effects. See the dedicated audio-system.md document for complete architecture details, voice pool management, 3D spatial audio, virtual voice handling, and performance characteristics.
