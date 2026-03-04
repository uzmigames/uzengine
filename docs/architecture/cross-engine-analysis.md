# Cross-Engine Architectural Analysis: UE5, Unity, Godot 4 → UzEngine

**Document Version**: 1.0
**Date**: 2026-03-04
**Classification**: Architecture Standard
**Status**: Active — Binds Phase 1+ Implementation

---

## Executive Summary

This document synthesizes deep technical analysis of three AAA game engines (Unreal Engine 5, Unity, Godot 4) to derive universal architectural patterns and actionable directives for UzEngine. The analysis covers 8 critical dimensions where engines make foundational decisions:

1. **Module System & Decoupling**
2. **Type System & Polymorphism**
3. **Render Thread Isolation**
4. **Task Scheduling & Parallelism**
5. **Memory Management**
6. **Component System & Extensibility**
7. **Asset System & Identifiers**
8. **Threading Model & Synchronization**

**Finding**: All three engines converge on 5 universal patterns (Section 2), diverge strategically on 3 dimensions (Section 3), and propose orthogonal approaches to 2 emerging domains (Section 4).

**Outcome**: 8 binding rules (XE-1 through XE-8) that UzEngine MUST implement to achieve AAA quality. Implementation roadmap spans Phase 1 (critical), Phase 2 (high-value), and Phase 3+ (polish).

---

## 1. Comparative Architecture Matrix

| Dimension | UE5 | Unity | Godot 4 | UzEngine Target |
|-----------|-----|-------|---------|-----------------|
| **Module System** | `IModuleInterface` + `FModuleManager`, reverse-order shutdown | `ScriptingAPI` + loose coupling via events | `ClassDB` + `_bind_methods()` reflection | Hybrid: RHI Plugin (loose), Core Modules (tight) |
| **Type Registration** | `DECLARE_CLASS` + `UClass` self-registration | TypeIndex as uint16, linear scan ComponentList | `ClassDB::register_class<T>()` in `_bind_methods()` | C-ABI Plugin System + C++ Introspection |
| **Polymorphism** | `TAutoCppStructOps` vtable, `TSubclassOf<T>` hierarchy checks | `ICppStructOps` pattern via components | `_notification(int)` single virtual | Explicit Vtable Pattern (see XE-3) |
| **Render Thread** | `FPrimitiveSceneProxy`, lock-free command queue | CommandBuffer arena, SRP callbacks | RenderingServer RIDs, MT wrapper macros | RhiCommandBuffer queue (Phase 2) |
| **Task Scheduler** | `TGraphTask` + `FBaseGraphTask`, atomic counters | Job system with Native Collections | Async/await via `CallDeferred()` | Job System (Phase 2) + Task Graph (Phase 3) |
| **Memory Model** | `FMemStack` bump allocator, `TInlineAllocator<N>`, pluggable `GMalloc` | Burst compiler + NativeCollections | ResourceCache + `.import` processing | Arena Allocators (Frame/Level/Permanent) + Handle<T> |
| **Asset Identifiers** | Asset Registry + GUIDs conceptually, path-based loading | GUID 128-bit (immutable), .meta files | ResourceUID int64_t, ResourceCache WeakRef | stringId 64-bit hash (current) → ResourceUID migration (Phase 2) |
| **Thread Isolation** | `_GameThread` / `_RenderThread` naming convention, ENQUEUE_RENDER_COMMAND | NativeArray<T> with AtomicSafetyHandle | RID handles crossing thread boundary | RhiHandle<T> semantic ownership (Phase 1) |

---

## 2. Five Universal Patterns (Consensus Across All 3 Engines)

These patterns represent independent convergence by three major engines on the same architectural solution. This is the strongest evidence of correctness.

### 2.1 Pattern: Explicit Thread Ownership Semantics

**What all 3 do:**
- **UE5**: `FPrimitiveSceneProxy` snapshot captured once, owned exclusively by render thread after game-thread creation. Method names explicitly tagged `_GameThread()` / `_RenderThread()`. No cross-thread calls except via command queue.
- **Unity**: `NativeArray<T>` with `AtomicSafetyHandle` marks writer/reader during job execution. CommandBuffer append-only during frame, consumed by RenderingContext.
- **Godot**: RID handles (opaque `uint64_t`) cross thread boundary; RenderingServer never dereferences RID on calling thread — always enqueues work.

**Why**: Cross-thread memory access is the #1 source of race conditions and deadlocks. Explicit ownership eliminates ambiguity about who may touch what data when.

**UzEngine Current State**: RhiHandle<T> exists conceptually; no enforced ownership semantics yet.

---

### 2.2 Pattern: Type Registration as Decoupled Extension Points

**What all 3 do:**
- **UE5**: `DECLARE_CLASS` generates `StaticClass()` and static constructor function pointer; `UClass::IsChildOf()` checks hierarchy. Zero switch statements on type.
- **Unity**: `TypeIndex` as uint16 (or generated int32); PlayerLoop is tree of `{function_pointer, ComponentList}` — adding system = adding tree node, never modifying central switch.
- **Godot**: `_bind_methods()` static method populates method_map; `ClassDB::register_class<T>()` registers class once. Instantiation: `ClassDB::instantiate("ClassName")` — no switch.

**Why**: Switches on type are O(N), couple all callers to the switch statement, and break with plugin systems. Type registration decouples creation from type definition.

**UzEngine Current State**: C ABI plugin system exists (plugin_api_t), but C++ introspection is minimal. No unified registration for components/systems.

---

### 2.3 Pattern: Arena Allocators with RAII Cleanup Markers

**What all 3 do:**
- **UE5**: `FMemStack` is bump allocator; `FMemMark` is RAII checkpoint that restores stack pointer on destruction. `TInlineAllocator<N>` avoids heap for small counts. `QuantizeSize()` eliminates fragmentation.
- **Unity**: Memory pools and slab allocators; Burst compiler inlines allocations into stack where possible.
- **Godot**: ResourceCache + memory pooling; GDExtension uses `GodotFreeString` macro for deferred cleanup.

**Why**: Frame-based cleanup via stack unwinding is O(1), avoids fragmentation, and prevents memory leaks without GC overhead. RAII ensures deterministic cleanup.

**UzEngine Current State**: Arena allocators partially implemented. No RAII cleanup markers yet.

---

### 2.4 Pattern: Command Queue for Cross-Thread Work Enqueuing

**What all 3 do:**
- **UE5**: `ENQUEUE_RENDER_COMMAND` macro: game thread appends command to lock-free ring buffer, render thread pops and executes. No locks; strict FIFO.
- **Unity**: CommandBuffer with arena of tagged-union structs `{type, size, data[]}` — game thread appends, render thread consumes. Zero heap allocation per command.
- **Godot**: RenderingServer::call_deferred() for deferred calls; `RenderingServerWrapMT` generates command enqueuing code via macro.

**Why**: Lock-free FIFO avoids deadlock, scales with multiple producers/consumers, and gives work ordering guarantees (critical for render consistency).

**UzEngine Current State**: IPC/WebSocket exists; no internal cross-thread command queue yet.

---

### 2.5 Pattern: Immutable Snapshots for Cross-Thread Data Passing

**What all 3 do:**
- **UE5**: `FPrimitiveSceneProxy` is constructed as snapshot of primitive state (transform, bounds, material) on game thread. Render thread never calls back to game thread objects.
- **Unity**: CullingOutput is produced by game thread, consumed immutably by render thread. RenderGraph compiles into immutable command buffer.
- **Godot**: `RenderingServerWrapMT::draw_list_*()` captures all arguments by value into command struct. Render thread cannot see game-thread objects.

**Why**: Immutability eliminates synchronization, prevents use-after-free, and makes it impossible to accidentally read stale data. Snapshots are O(N) once, then lock-free for the rest of the frame.

**UzEngine Current State**: JPEG snapshots exist for viewport rendering; no architectural pattern for general immutable snapshots.

---

## 3. Three Strategic Divergences (Intentional Design Trade-offs)

### 3.1 Divergence: Static Type Hierarchy vs. Flat Type Registry

**UE5 chooses**: Static hierarchy. `UClass::IsChildOf(TargetClass)` traverses parent chain (O(depth)). Compile-time `TSubclassOf<T>` template ensures type safety. Zero runtime polymorphism in the registry itself.

**Unity chooses**: Flat registry. `TypeIndex` is just an int; all components are discovered via linear scan of component array (O(N) but N ~ 10-15, cache-friendly). No inheritance; composition only.

**Godot chooses**: Static hierarchy via `ClassDB`. `ClassDB::get_parent_class("ClassName")` returns StringName parent. `ClassDB::is_parent_class_of(class1, class2)` walks hierarchy like UE5, but through StringName lookup.

**Trade-off**:
- **Hierarchy** (UE5, Godot): O(depth) inheritance checks, compile-time safety via `TSubclassOf<T>`, but modders must understand MRO.
- **Flat** (Unity): O(N) linear scan is negligible for N=10-15, composition forces better design, but requires runtime type discovery.

**For UzEngine**: We currently have no component hierarchy. Game.dll uses C ABI (opaque components). **Recommendation**: Stay flat. Add `EnumAllComponentsOfType()` rather than hierarchy checks. See XE-3 for details.

---

### 3.2 Divergence: Render Thread as Exclusive Consumer vs. Multi-Consumer Ring Buffer

**UE5 chooses**: Exclusive render thread. One thread reads command queue, no other thread touches it. Eliminates synchronization almost entirely.

**Unity chooses**: Multi-consumer job system. CommandBuffer can be filled by game thread and multiple job threads. Render thread picks up completed commands when ready.

**Godot chooses**: Deferred calls. `call_deferred()` is queue entry. Render thread pulls as part of normal RenderingServer::run() loop.

**Trade-off**:
- **Exclusive** (UE5): Simplest synchronization (one reader = no lock needed), best latency, but pipeline must be single-threaded rendering.
- **Multi-consumer** (Unity): Better for heterogeneous work, but requires careful synchronization and loss of strict ordering.

**For UzEngine**: We target single-threaded rendering initially (Phase 1). Phase 3 may add async compute. **Recommendation**: Exclusive render thread + lock-free FIFO command queue (like UE5). See XE-6 for details.

---

### 3.3 Divergence: Opaque Handles vs. Tagged Pointers

**UE5 chooses**: Direct pointers + GC/ref-counting for lifetime management. `AActor*` is a real pointer; `UE_LOG()`-style debugging shows actual addresses.

**Unity chooses**: Generational indices for entities (int32 index + int32 version). Zero pointer arithmetic, cache-friendly, prevents dangling refs via version check.

**Godot chooses**: Opaque RID handles (`uint64_t`). RenderingServer never dereferences on game thread. Prevents use-after-free by design (RID is never a real pointer outside the server).

**Trade-off**:
- **Pointers** (UE5): Direct memory access, instant dereferencing, but UAF possible, cannot move objects in memory.
- **Generational indices** (Unity): Can detect UAF with version check, enables entity reuse, but one indirection per access.
- **Opaque handles** (Godot): Cross-thread safe by design, no possibility of dereferencing stale handle, but two indirections (handle lookup + data access).

**For UzEngine**: We use opaque `Handle<T>` in specs. Phase 1 targets single-threaded. **Recommendation**: Implement generational indices for entities (like Unity DOTS), opaque RIDs for GPU resources (like Godot). See XE-8 for details.

---

## 4. Binding Rules: UzEngine MUST Implement

### RULE XE-1: Module Decoupling via Explicit Startup/Shutdown Order

**Universal Consensus**: All 3 engines use explicit module startup/shutdown with guaranteed reverse-order teardown.

**UE5 Pattern**:
```cpp
class IModuleInterface {
    virtual void StartupModule() = 0;
    virtual void ShutdownModule() = 0;
};

// FModuleManager maintains startup order, shutdown in reverse
FModuleManager::Get().LoadModule("ModuleName");
// Later...
FModuleManager::Get().UnloadModule("ModuleName"); // reverse order
```

**UzEngine MUST Implement**:

```cpp
// e:\UzEngine\src\engine\core\module-manager.h
enum class ModulePhase { Startup, Running, Shutdown };

struct ModuleInterface {
    // Pure virtual interface
    virtual void startup() = 0;      // Called in registration order
    virtual void shutdown() = 0;     // Called in REVERSE order
    virtual const char* name() const = 0;
    virtual int priority() const { return 0; } // Higher = earlier startup
};

class ModuleManager {
private:
    Vec<Module> modules;  // Maintains insertion order
public:
    void register_module(ModuleInterface* mod) {
        // Insert by priority, maintain list for reverse iteration
        modules.push(mod);
    }

    void startup_all() {
        std::sort(modules.begin(), modules.end(),
                 [](auto& a, auto& b) { return a->priority() > b->priority(); });
        for (auto& m : modules) {
            m->startup();
            // Handle exceptions: if m->startup() throws, auto-shutdown
            // all modules that started before it, in reverse order
        }
    }

    void shutdown_all() {
        // CRITICAL: reverse iteration
        for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
            (*it)->shutdown();  // No exceptions; log and continue
        }
    }
};

// RHI Plugin Module Example (decoupled from core)
class RhiD12Module : public ModuleInterface {
    void startup() override;
    void shutdown() override;
    const char* name() const override { return "RhiD12"; }
    int priority() const override { return 100; } // High priority
};
```

**UzEngine Implementation File**: `docs/specs/engine/module-system.md` (CREATE NEW)

**Anti-pattern**: Circular dependencies between modules (e.g., ModuleA::startup() calls ModuleB::get()). Use event broadcasts instead (see XE-2).

**Acceptance Criteria**:
- [ ] `ModuleManager::startup_all()` guarantees reverse-order shutdown
- [ ] Exception in startup automatically shuts down prior modules
- [ ] No module directly depends on another; all cross-module calls go through service registry
- [ ] RHI plugin loads/unloads independently without core reload
- [ ] Test: Load 5 modules, verify shutdown order is reverse of startup

**Phase**: Phase 1 (Critical — unblocks everything else)

---

### RULE XE-2: Type Registration via Decoupled Extension Registry (No Type Switches)

**Universal Consensus**: All 3 engines eliminated central type switches via registration + virtual lookup.

**UE5 Pattern**:
```cpp
class UClass {
    static UClass* Create(const TCHAR* Name) {
        // No switch; iterate registered classes
        return GetClassByName(Name)->GetDefaultObject();
    }
};

DECLARE_CLASS(AMyActor, AActor) {
    // Auto-registers in StaticClass()
};
```

**UzEngine MUST Implement**:

```cpp
// e:\UzEngine\src\engine\core\component-registry.h
using ComponentCreatorFn = UniquePtr<IComponent>(*)();

class ComponentRegistry {
private:
    Map<stringId, ComponentCreatorFn> registry;
public:
    void register_component(stringId typeId, ComponentCreatorFn creator) {
        registry[typeId] = creator;
    }

    UniquePtr<IComponent> create(stringId typeId) {
        auto it = registry.find(typeId);
        if (it != registry.end()) {
            return it->second();
        }
        return nullptr;
    }

    // Iterate all registered types (for editor)
    void enumerate_types(FunctionRef<void(stringId, const ComponentMetadata&)> fn) {
        for (auto& [typeId, creator] : registry) {
            ComponentMetadata meta;
            // ... populate from component's static metadata
            fn(typeId, meta);
        }
    }
};

// Component Self-Registration Macro
#define DECLARE_COMPONENT(Type) \
    namespace { \
        struct Type##_Registrar { \
            Type##_Registrar() { \
                ComponentRegistry::get().register_component( \
                    stringId(#Type), \
                    []() -> UniquePtr<IComponent> { return make_unique<Type>(); } \
                ); \
            } \
        }; \
        static Type##_Registrar Type##_reg; \
    }

// C++ Usage
class TransformComponent : public IComponent {
    DECLARE_COMPONENT(TransformComponent);
    // ...
};

// C ABI Plugin Usage
// plugins/my_plugin.c can call:
extern const char* game_dll_register_component(const char* typeId, GameComponentCreator creator);
game_dll_register_component("MyCustomComponent", my_custom_creator_fn);
```

**UzEngine Implementation File**: Update `docs/specs/engine/game-dll-abi.md` to include component registration ABI.

**Anti-pattern**: A switch statement in scene loader that enumerates all possible component types. Instead, call registry.create().

**Acceptance Criteria**:
- [ ] Zero `if (typeId == TRANSFORM_ID) { ... } else if (typeId == MESH_ID) { ... }` in codebase
- [ ] New component type added via registration macro; no code edits required outside component file
- [ ] Editor can enumerate all registered components without hardcoding list
- [ ] C ABI plugins can register custom components dynamically
- [ ] Test: Register 20 mock components, verify registry lookup O(1) average, enumerate succeeds

**Phase**: Phase 1 (Critical — enables extensibility)

---

### RULE XE-3: Explicit Vtable Pattern for Polymorphism (No Dynamic Cast)

**Universal Consensus**: All 3 engines use explicit vtables; dynamic_cast is rare/forbidden in hot paths.

**UE5 Pattern**:
```cpp
class ICppStructOps {
    virtual void Construct(void* Data) = 0;
    virtual void Destruct(void* Data) = 0;
    virtual void Copy(void* Dst, const void* Src) = 0;
    // etc.
};
```

**UzEngine MUST Implement**:

```cpp
// e:\UzEngine\src\engine\core\component-interface.h

// Base interface with explicit virtual methods (no RTTI)
class IComponent {
public:
    virtual ~IComponent() = default;

    // Lifecycle
    virtual void on_attach() {}
    virtual void on_detach() {}
    virtual void on_enable() {}
    virtual void on_disable() {}

    // Query (explicit, not dynamic_cast)
    // If you need to downcast, use these:
    virtual TransformComponent* as_transform() { return nullptr; }
    virtual MeshComponent* as_mesh() { return nullptr; }
    virtual RigidbodyComponent* as_rigidbody() { return nullptr; }

    // Per-component type ID (avoids RTTI)
    virtual uint32_t component_type_id() const = 0;
};

// Concrete implementation
class TransformComponent : public IComponent {
public:
    void on_attach() override {
        // Set up transform system callbacks
    }

    TransformComponent* as_transform() override { return this; }
    uint32_t component_type_id() const override { return COMPONENT_ID_TRANSFORM; }

private:
    Vec3 position;
    Quat rotation;
    Vec3 scale;
};

// Usage (NO dynamic_cast):
void update_all(Node& node) {
    for (auto& comp : node.components()) {
        if (auto* transform = comp->as_transform()) {
            // Update transform
            transform->position += velocity * dt;
        }
    }
}

// Alternative: Visitor pattern for multi-method dispatch
class ComponentVisitor {
public:
    virtual ~ComponentVisitor() = default;
    virtual void visit(TransformComponent& c) {}
    virtual void visit(MeshComponent& c) {}
    virtual void visit(RigidbodyComponent& c) {}
};

class TransformComponent : public IComponent {
    void accept(ComponentVisitor& v) override { v.visit(*this); }
};
```

**UzEngine Implementation File**: Update `docs/specs/engine/game-dll-abi.md` with component vtable pattern.

**Anti-pattern**: `dynamic_cast<TransformComponent*>(comp)` in update loops. Forbidden (RTTI is slow).

**Acceptance Criteria**:
- [ ] Zero `dynamic_cast` in hot paths (update, render, physics)
- [ ] Component queries use explicit `as_*()` methods
- [ ] No RTTI enabled (`-fno-rtti` in build flags)
- [ ] Visitor pattern used for multi-dispatch (e.g., collision handling)
- [ ] Test: 1000 components in scene, verify update cost is vtable call (1 indirect jump) per component

**Phase**: Phase 1 (Critical — performance requirement)

---

### RULE XE-4: Lock-Free Ring Buffer Command Queue for Render Thread

**Universal Consensus**: All 3 engines use lock-free FIFO for cross-thread command enqueuing.

**UE5 Pattern**:
```cpp
#define ENQUEUE_RENDER_COMMAND(Name) \
    if (1) { /* game thread code */ } \
    else if (1) { /* render thread code */ }
// Macro generates command struct + enqueue on FIFO
```

**UzEngine MUST Implement**:

```cpp
// e:\UzEngine\src\engine\rhi\rhi-command-queue.h

// Lock-free ring buffer (Treiber stack variant for FIFO)
template<typename Cmd, size_t Capacity = 65536>
class RhiCommandQueue {
private:
    // Store commands in pre-allocated ring buffer
    alignas(64) std::array<Cmd, Capacity> commands;
    alignas(64) std::atomic<size_t> write_pos{0};  // Game thread writes here
    alignas(64) std::atomic<size_t> read_pos{0};   // Render thread reads here

public:
    // Game thread: append command (may block if queue full)
    template<typename F>
    void enqueue(F&& fn) {
        size_t pos = write_pos.fetch_add(1, std::memory_order_release);
        if (pos >= Capacity) {
            // Queue full; spin/sleep until render thread catches up
            while (write_pos.load(std::memory_order_acquire) - read_pos.load(std::memory_order_acquire) >= Capacity) {
                std::this_thread::yield();
            }
            pos = write_pos.fetch_add(1, std::memory_order_release);
        }
        new (&commands[pos % Capacity]) Cmd();
        fn(commands[pos % Capacity]);
    }

    // Render thread: consume commands
    void flush() {
        size_t read = read_pos.load(std::memory_order_acquire);
        size_t write = write_pos.load(std::memory_order_acquire);
        for (size_t i = read; i < write; ++i) {
            Cmd& cmd = commands[i % Capacity];
            cmd.execute();
            cmd.~Cmd();
        }
        read_pos.store(write, std::memory_order_release);
    }
};

// Usage: Wrapper macro for readability
#define RHI_ENQUEUE(func_name, ...) do { \
    g_rhi_command_queue.enqueue([](RhiCommand& cmd) { \
        cmd.type = RhiCommandType::func_name; \
        cmd.data.func_name##_args args{}; \
        /* populate args */ \
    }); \
} while(0)

// Example: Game thread issues draw call
void game_thread_draw() {
    RhiDevice* device = get_rhi_device();
    RhiHandle<RhiBuffer> vertex_buffer = ...;
    RhiHandle<RhiPipeline> pipeline = ...;

    RHI_ENQUEUE(SetPipeline, pipeline);
    RHI_ENQUEUE(SetVertexBuffer, 0, vertex_buffer, 0);
    RHI_ENQUEUE(Draw, 36, 1, 0, 0);
}

// Render thread (or dedicated RHI thread)
void rhi_thread_main() {
    while (is_running) {
        g_rhi_command_queue.flush();
    }
}
```

**UzEngine Implementation File**: Create `docs/specs/engine/rhi-command-queue.md` (NEW).

**Anti-pattern**: Locking mutex before every RHI call (defeats lock-free purpose). Passing non-POD types through queue (use handles instead).

**Acceptance Criteria**:
- [ ] Game thread can enqueue commands without blocking render thread
- [ ] Render thread consumes queue in FIFO order, O(1) per command
- [ ] Queue capacity checked; game thread spins if full (indicates render thread is slow)
- [ ] No mutex/condition variable for enqueue (lock-free)
- [ ] Test: 1000 frames × 100 commands/frame, verify enqueue latency < 100ns avg

**Phase**: Phase 2 (High priority — enables render scalability)

---

### RULE XE-5: Arena Allocators with RAII Cleanup Markers

**Universal Consensus**: All 3 engines use stack-based bump allocators for frame-local and temporary allocations.

**UE5 Pattern**:
```cpp
FMemStack Stack;
FMemMark Mark(Stack);  // RAII checkpoint
void* Ptr = Stack.Alloc(Size);  // Fast O(1) bump
// ~FMemMark() restores stack to prior state O(1)
```

**UzEngine MUST Implement**:

```cpp
// e:\UzEngine\src\engine\core\arena-allocator.h

class Arena {
private:
    alignas(64) uint8_t* base;
    size_t capacity;
    size_t offset;  // Atomic for lock-free multi-thread case (Phase 2)
public:
    Arena(size_t cap) : capacity(cap), offset(0) {
        base = static_cast<uint8_t*>(std::malloc(cap));
    }

    ~Arena() {
        if (base) std::free(base);
    }

    // Fast allocation: O(1) bump
    void* alloc(size_t size, size_t alignment = 16) {
        size_t aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
        if (aligned_offset + size > capacity) {
            return nullptr;  // OOM; caller must handle
        }
        void* ptr = base + aligned_offset;
        offset = aligned_offset + size;
        return ptr;
    }

    // Checkpoint for RAII cleanup
    struct Checkpoint {
        Arena& arena;
        size_t saved_offset;

        Checkpoint(Arena& a) : arena(a), saved_offset(a.offset) {}
        ~Checkpoint() { arena.offset = saved_offset; }  // O(1) cleanup
    };

    Checkpoint save() { return Checkpoint(*this); }
    void reset() { offset = 0; }
};

// Usage: Frame allocator
Arena g_frame_arena(16 * 1024 * 1024);  // 16 MB per frame

void game_update() {
    Arena::Checkpoint frame_mark(g_frame_arena);
    {
        // Allocate temporary data
        auto* collision_pairs = g_frame_arena.alloc<CollisionPair>(num_pairs);
        // ... physics queries ...
    }
    // ~frame_mark() automatically resets arena to start of frame
}

// Template helpers
template<typename T>
T* arena_new(Arena& a, Args&&... args) {
    void* ptr = a.alloc(sizeof(T), alignof(T));
    if (!ptr) return nullptr;
    return new (ptr) T(std::forward<Args>(args)...);
}

template<typename T>
void arena_delete(T* ptr) {
    ptr->~T();
    // Don't call std::free; arena cleanup is batch at checkpoint
}

// Inline allocator (avoid heap for small arrays)
template<typename T, size_t InlineCount = 16>
class SmallVec {
    T inline_storage[InlineCount];
    T* heap_storage = nullptr;
    size_t size = 0;
    size_t capacity = InlineCount;
public:
    void push_back(const T& v) {
        if (size >= capacity) {
            // Allocate from arena
            size_t new_cap = capacity * 2;
            T* new_data = arena_alloc<T>(new_cap);
            for (size_t i = 0; i < size; ++i) new_data[i] = heap_storage ? heap_storage[i] : inline_storage[i];
            if (heap_storage) arena_free(heap_storage);
            heap_storage = new_data;
            capacity = new_cap;
        }
        (heap_storage ? heap_storage : inline_storage)[size++] = v;
    }
};
```

**UzEngine Implementation File**: Update `docs/specs/engine/memory-system.md` with arena patterns.

**Anti-pattern**: Using `new`/`delete` per-frame for temporary allocations. malloc() inside hot loops.

**Acceptance Criteria**:
- [ ] Frame allocator resets in O(1) at frame boundary
- [ ] No heap fragmentation from frame allocations (measured via virtual memory size)
- [ ] `SmallVec<T, 16>` avoids heap for 16 elements (typical for small arrays)
- [ ] Checkpoint RAII pattern used throughout codebase
- [ ] Test: 100K temporary allocations per frame, verify arena resets < 1μs

**Phase**: Phase 1 (Critical — memory performance)

---

### RULE XE-6: Immutable Snapshots for Cross-Thread Data Passing

**Universal Consensus**: All 3 engines pass snapshots (value copies) instead of pointers across thread boundaries.

**UE5 Pattern**:
```cpp
class FPrimitiveSceneProxy {
    // Snapshot captured on game thread
    FVector PrimitiveLocation;  // copied from AActor
    FQuat PrimitiveQuat;
    FVector Bounds;
    // Render thread reads these; game thread does NOT
};
```

**UzEngine MUST Implement**:

```cpp
// e:\UzEngine\src\engine\scene\scene-snapshot.h

// Render-thread-side snapshot (immutable after game thread hands off)
struct RenderFrameSnapshot {
    struct NodeSnapshot {
        NodeId id;
        Mat4 world_transform;
        Vec3 world_position;
        bool is_visible;
        RhiHandle<RhiMesh> mesh;
        RhiHandle<RhiMaterial> material;
        // NO pointers to game-thread data
    };

    struct LightSnapshot {
        LightType type;
        Vec3 position;
        Vec3 direction;
        Color color;
        float intensity;
        float range;
    };

    Vec<NodeSnapshot> nodes;
    Vec<LightSnapshot> lights;
    Mat4 camera_view;
    Mat4 camera_proj;
};

// Game thread: build snapshot
RenderFrameSnapshot build_frame_snapshot(Scene& scene) {
    RenderFrameSnapshot snap{};
    snap.camera_view = scene.camera.view_matrix();
    snap.camera_proj = scene.camera.proj_matrix();

    scene.for_each_node([&](Node& node) {
        if (!node.is_visible()) return;

        RenderFrameSnapshot::NodeSnapshot node_snap{};
        node_snap.id = node.id();
        node_snap.world_transform = node.world_transform();
        node_snap.mesh = node.mesh_component->mesh_handle;  // Copy handle, not pointer
        node_snap.material = node.mesh_component->material_handle;
        snap.nodes.push_back(node_snap);
    });

    scene.for_each_light([&](Light& light) {
        RenderFrameSnapshot::LightSnapshot light_snap{};
        light_snap.position = light.position();
        light_snap.color = light.color();
        light_snap.intensity = light.intensity();
        snap.lights.push_back(light_snap);
    });

    return snap;  // Move semantics: O(size of snapshot)
}

// Render thread: consume snapshot (zero touching game thread)
void render_snapshot(const RenderFrameSnapshot& snap) {
    set_camera_matrices(snap.camera_view, snap.camera_proj);

    for (const auto& node_snap : snap.nodes) {
        // Dereference handles (via RhiDevice), not pointers
        auto* mesh = g_rhi_device->get_mesh(node_snap.mesh);
        auto* material = g_rhi_device->get_material(node_snap.material);

        set_world_matrix(node_snap.world_transform);
        draw_mesh(mesh, material);
    }
}

// Thread handoff: game thread produces, render thread consumes
DoubleBuffer<RenderFrameSnapshot> g_frame_snapshots;

void game_thread_end_frame() {
    RenderFrameSnapshot snap = build_frame_snapshot(g_scene);
    g_frame_snapshots.write() = std::move(snap);
    g_frame_snapshots.swap();
}

void render_thread_main() {
    while (is_running) {
        const RenderFrameSnapshot& snap = g_frame_snapshots.read();
        render_snapshot(snap);
    }
}
```

**UzEngine Implementation File**: Create `docs/specs/engine/frame-snapshot.md` (NEW).

**Anti-pattern**: Passing `Node*` or `Scene*` pointers to render thread. Render thread calling back to game thread code.

**Acceptance Criteria**:
- [ ] Render thread never dereferences pointers from game thread (only handles and values)
- [ ] Snapshot size < 50% of total per-frame memory (avoid memory bloat)
- [ ] Build snapshot cost < 2ms on typical scene (10K nodes)
- [ ] Double-buffer uses non-blocking swap (render thread never stalls game thread)
- [ ] Test: 10K nodes, verify snapshot build + swap < 5ms total

**Phase**: Phase 1 (Critical — thread safety)

---

### RULE XE-7: Generational Indices for Entity References (No Dangling Pointers)

**Universal Consensus**: Unity DOTS uses generational indices; Godot uses ResourceUID; UE5 uses ref-counting + weak ptrs. All prevent UAF.

**UzEngine MUST Implement**:

```cpp
// e:\UzEngine\src\engine\core\handle.h

// Generational index: detects when handle points to recycled entity
struct GenerationId {
    uint32_t index : 20;    // Entity index (0..1M)
    uint32_t generation : 12; // Generation counter (0..4K)

    constexpr bool operator==(const GenerationId& other) const {
        return index == other.index && generation == other.generation;
    }
};

template<typename T>
class Handle {
    GenerationId id;
public:
    Handle() : id{0xFFFFF, 0xFFF} {}  // Invalid handle
    explicit Handle(GenerationId gen_id) : id(gen_id) {}

    bool is_valid() const {
        return id.index != 0xFFFFF;
    }

    GenerationId gen_id() const { return id; }
};

// Storage: SoA with generation tracking
template<typename T>
class EntityStorage {
private:
    struct Slot {
        T data;
        uint32_t generation;
    };
    Vec<Slot> slots;
    Vec<uint32_t> free_list;  // Indices of recycled slots

public:
    Handle<T> create(T value) {
        uint32_t index;
        uint32_t gen;
        if (!free_list.empty()) {
            index = free_list.back();
            free_list.pop_back();
            gen = slots[index].generation + 1;
            slots[index].generation = gen;
        } else {
            index = slots.size();
            gen = 1;
            slots.push_back({value, gen});
        }
        return Handle<T>(GenerationId{index, gen});
    }

    T* get(Handle<T> h) {
        if (!h.is_valid()) return nullptr;
        auto [index, gen] = h.gen_id();
        if (index >= slots.size() || slots[index].generation != gen) {
            return nullptr;  // Stale handle
        }
        return &slots[index].data;
    }

    void destroy(Handle<T> h) {
        if (!h.is_valid()) return;
        auto [index, gen] = h.gen_id();
        if (index < slots.size()) {
            slots[index].generation++;  // Bump generation
            free_list.push_back(index);
        }
    }
};

// Usage
EntityStorage<Node> g_node_storage;

Handle<Node> node_h = g_node_storage.create(Node{"MyNode"});
Node* node_ptr = g_node_storage.get(node_h);
node_ptr->update();

g_node_storage.destroy(node_h);
node_ptr = g_node_storage.get(node_h);  // Returns nullptr (stale)
```

**UzEngine Implementation File**: Update `docs/specs/engine/entity-system.md` (CREATE NEW).

**Anti-pattern**: Storing raw `Node*` pointers that outlive the node lifetime. Passing pointers across frame boundaries.

**Acceptance Criteria**:
- [ ] Generational index validation O(1) per handle dereference (one comparison)
- [ ] Entity recycling tested: create, destroy, create again with different generation
- [ ] No dangling pointer dereferences (test harness for UAF detection)
- [ ] Handle size 32-bit (fits in register)
- [ ] Test: 100K entities, create/destroy/reuse, verify zero UAF bugs

**Phase**: Phase 1 (Critical — correctness/stability)

---

### RULE XE-8: ResourceUID Asset Identifiers (Decouple Path from Identity)

**Universal Consensus**: Unity uses GUID; Godot uses ResourceUID; UE5 uses Asset Registry GUID concept. All make identity immutable while allowing path renames.

**UzEngine MUST Implement**:

```cpp
// e:\UzEngine\src\engine\asset\asset-id.h

// Immutable unique identifier for any asset
struct ResourceUID {
    uint64_t value;

    // File path can change; UID does not
    bool operator==(const ResourceUID& other) const { return value == other.value; }
    bool operator!=(const ResourceUID& other) const { return !(*this == other); }
};

// Asset metadata (separate from data)
struct AssetMetadata {
    ResourceUID uid;
    fs::path source_path;    // May change (renamed)
    fs::path import_path;    // .uzimport cache
    StringId asset_type;     // "mesh", "texture", "scene"
    uint32_t version;        // Bumped on reimport
    Time last_import_time;
};

// Cache: UID → metadata (primary)
class AssetRegistry {
private:
    Map<ResourceUID, AssetMetadata> by_uid;
    Map<fs::path, ResourceUID> by_path;  // Secondary
public:
    void register_asset(const AssetMetadata& meta) {
        by_uid[meta.uid] = meta;
        by_path[meta.source_path] = meta.uid;
    }

    AssetMetadata* lookup_by_uid(ResourceUID uid) {
        auto it = by_uid.find(uid);
        return it != by_uid.end() ? &it->second : nullptr;
    }

    ResourceUID lookup_by_path(const fs::path& path) {
        auto it = by_path.find(path);
        return it != by_path.end() ? it->second : ResourceUID{0};
    }

    void on_asset_renamed(const fs::path& old_path, const fs::path& new_path) {
        auto it = by_path.find(old_path);
        if (it != by_path.end()) {
            ResourceUID uid = it->second;
            by_path.erase(it);
            by_path[new_path] = uid;
            by_uid[uid].source_path = new_path;  // Update metadata
            // No broken references; UID is stable
        }
    }
};

// Scene file: save by UID, not path
// Example: scene.uzscene
/*
[Scene]
name = "TestScene"

[Node "Root"]
mesh = uid:0x1a2b3c4d5e6f7890

[Node "Light"]
light = uid:0x9876543210fedcba
*/

// Loading: UID → current path
void load_scene(const fs::path& scene_path) {
    ini::Section nodes = parse_scene_file(scene_path);
    for (const auto& node_entry : nodes) {
        if (node_entry.has("mesh")) {
            ResourceUID mesh_uid = parse_uid(node_entry["mesh"]);
            AssetMetadata* meta = g_asset_registry.lookup_by_uid(mesh_uid);
            if (meta) {
                // Load from current path (may differ from when scene was saved)
                load_mesh(meta->import_path);
            }
        }
    }
}

// Anti-corruption: .uzasset file (immutable metadata)
/*
version = 1
uid = 0x1a2b3c4d5e6f7890
source_path = "assets/meshes/character.gltf"
type = "mesh"
import_time = 1709554800  # Unix timestamp
*/
```

**UzEngine Implementation File**: Create `docs/specs/engine/asset-system.md` (NEW).

**Anti-pattern**: Saving asset file paths directly in scene files. Moving asset files breaks all references.

**Acceptance Criteria**:
- [ ] Asset UID generated once at import, never changes
- [ ] Asset renamed in disk; scene files still load correctly
- [ ] Lookup by UID O(1), lookup by path O(1) secondary index
- [ ] `.uzasset` metadata file created alongside each imported asset
- [ ] Test: Move mesh file, rename directory, verify scene still loads mesh via UID

**Phase**: Phase 2 (High priority — asset workflow)

---

## 5. Implementation Roadmap: Phased Rollout

### Phase 1: Critical Foundation (Weeks 1-6)

**Goal**: Establish architectural patterns that prevent correctness issues.

| Rule | Task | Owner | Effort | Blockers |
|------|------|-------|--------|----------|
| XE-1 | Implement ModuleManager with reverse-order shutdown | ECS Engineer | 2d | None |
| XE-2 | Component registry + auto-registration macro | ECS Engineer | 2d | XE-1 |
| XE-3 | Remove all `dynamic_cast`, implement explicit `as_*()` vtables | ECS Engineer | 3d | XE-2 |
| XE-5 | Arena allocators + checkpoint RAII cleanup | ECS Engineer | 2d | None |
| XE-6 | Frame snapshot + double-buffer handoff | Render Engineer | 3d | None |
| XE-7 | Generational indices for entity handles | ECS Engineer | 2d | XE-5 |

**Outcome**: Safer threading, extensibility, zero dangling pointers.

**Test Coverage Target**: 95% (all new code)

---

### Phase 2: Scalability & Tools (Weeks 7-14)

**Goal**: Enable render scaling and asset workflows.

| Rule | Task | Owner | Effort | Blockers |
|------|------|-------|--------|----------|
| XE-4 | RHI command queue + lock-free enqueue | Render Engineer | 3d | XE-1, XE-6 |
| XE-8 | ResourceUID + asset registry + import cache | Asset Engineer | 4d | None |
| - | Render graph (async compute, barriers) | Render Engineer | 5d | XE-4 |
| - | Editor asset browser with UID display | UI Engineer | 3d | XE-8 |

**Outcome**: Render thread can run async, asset pipeline decoupled from runtime.

**Test Coverage Target**: 95%+ (regression testing for threading)

---

### Phase 3: Polish & Optimization (Weeks 15+)

**Goal**: Advanced patterns (virtual geometry, job system, etc.).

| Rule | Task | Owner | Effort | Blockers |
|------|------|-------|--------|----------|
| - | Job system (expand task graph from Phase 1) | Engine Architect | 5d | XE-1 |
| - | Virtual instancing (GPU-driven rendering) | Render Engineer | 7d | XE-4 |
| - | Particle system (GPU compute) | Renderer Engineer | 4d | XE-4 |

**Outcome**: Next-gen scalability (10K+ entities, GPU-driven rendering).

---

## 6. Critical Anti-Patterns (NEVER DO THIS)

```cpp
// ❌ WRONG: Type switch in core loop
void update_all_components(Node& node) {
    for (auto& comp : node.components()) {
        switch (comp->type()) {
            case COMPONENT_TRANSFORM: static_cast<TransformComponent*>(comp)->update(); break;
            case COMPONENT_MESH: static_cast<MeshComponent*>(comp)->update(); break;
            // 50 more cases... breaks with plugins
        }
    }
}

// ✅ CORRECT: Virtual dispatch
void update_all_components(Node& node) {
    for (auto& comp : node.components()) {
        comp->update();  // Virtual call; each component owns its logic
    }
}

// ❌ WRONG: Dangling pointers
Handle<Node> get_node_by_name(StringView name) {
    Node* ptr = find_node_in_scene(name);  // Pointer invalidated when scene reloads
    return Handle<Node>(ptr);
}

// ✅ CORRECT: Generational indices
Handle<Node> get_node_by_name(StringView name) {
    uint32_t index = find_node_index_in_scene(name);
    return Handle<Node>(GenerationId{index, g_entity_storage.gen(index)});
}

// ❌ WRONG: Cross-thread pointer dereference
void render_scene(Scene* scene) {  // Game thread's scene pointer
    for (auto& node : scene->nodes()) {  // RACE: game thread may modify
        draw_node(node);
    }
}

// ✅ CORRECT: Immutable snapshot
void render_snapshot(const RenderFrameSnapshot& snap) {
    for (const auto& node_snap : snap.nodes) {  // Read-only
        draw_node(node_snap);
    }
}

// ❌ WRONG: Malloc in hot path
void physics_step(Solver& solver) {
    Vec<Contact>* contacts = new Vec<Contact>();  // Memory fragmentation
    solver.find_contacts(*contacts);
    // ... physics ...
    delete contacts;
}

// ✅ CORRECT: Frame arena
void physics_step(Solver& solver) {
    Arena::Checkpoint frame_mark(g_frame_arena);
    Vec<Contact>* contacts = arena_new<Vec<Contact>>(g_frame_arena);
    solver.find_contacts(*contacts);
    // ... physics ...
    // ~frame_mark destructor resets arena; no individual deletes
}
```

---

## 7. Measurement & Validation

### Metrics (Track Every Phase)

1. **Memory Fragmentation**: Virtual memory footprint / allocated footprint (target: < 1.2x)
2. **Frame Time Stability**: Standard deviation of frame times (target: < 5%)
3. **GC Pause Time**: (N/A — no GC, but frame allocator reset time target: < 100μs)
4. **Render Thread Latency**: Command queue depth at frame start (target: < 100 commands queued)
5. **Asset Load Time**: UID lookup + import cache hit rate (target: > 95% cache hits)
6. **Code Extensibility**: Number of files modified when adding new component type (target: 1 file)

### Validation Checklist (Before Each Phase Gate)

```
Phase 1 Gate (Week 6):
□ ModuleManager tested with 10+ modules, reverse shutdown verified
□ Zero dynamic_cast in hot loops (grep validation)
□ Arena allocator resets < 100μs per frame
□ Frame snapshot build < 2ms for 10K nodes
□ Generational index UAF detection verified (test harness)
□ All new code 95%+ coverage
□ No regressions in existing tests

Phase 2 Gate (Week 14):
□ RHI command queue enqueue latency < 100ns average
□ ResourceUID lookup O(1) verified (100K queries)
□ Render graph compiles DAG < 1ms per frame
□ Asset reimport workflow tested (rename, move, verify load)
□ All new code 95%+ coverage
□ Render thread utilization 90%+ (not stalled on game thread)

Phase 3 Gate (Week 20+):
□ Job system scales 2-8 threads linearly
□ Virtual instancing renders 100K+ entities 60 FPS
□ GPU compute particle system smoke-tested
□ All new code 95%+ coverage
□ No regressions from Phase 1/2
```

---

## 8. References & Further Reading

- **UE5 Architecture**: [Unreal Engine 5 Source](W:\UE_5.6_Source) — See `Runtime/Core/Public/Containers/List.h` (FList), `Runtime/Core/Public/Async/TaskGraph.h` (FGraphTask)
- **Unity DOTS**: [ECS Concepts](https://docs.unity3d.com/Packages/com.unity.entities@latest) — Generational indices, Archetype storage
- **Godot 4 Internals**: [Godot Source](https://github.com/godotengine/godot) — ClassDB, RenderingServer RID system
- **UzEngine Specs**:
  - `docs/specs/engine/game-dll-abi.md` — Component vtable + registration
  - `docs/specs/engine/memory-system.md` — Arena allocators
  - `docs/specs/engine/ecs-and-scene.md` — Node/Component architecture

---

## Appendix: Example Implementation (TransformComponent)

```cpp
// e:\UzEngine\src\engine\scene\transform-component.h
#pragma once
#include "engine/core/component-interface.h"
#include "engine/core/math-types.h"

class TransformComponent : public IComponent {
public:
    // Self-registering (XE-2)
    DECLARE_COMPONENT(TransformComponent);

    // Explicit vtable (XE-3)
    void on_attach() override;
    void on_detach() override;
    uint32_t component_type_id() const override { return COMPONENT_ID_TRANSFORM; }

    // Downcast support
    TransformComponent* as_transform() override { return this; }

    // Transform accessors
    Vec3 local_position() const { return local_position_; }
    void set_local_position(Vec3 pos) { local_position_ = pos; dirty_ = true; }

    Mat4 local_matrix() const { return local_matrix_; }
    Mat4 world_matrix() const { return world_matrix_; }

private:
    Vec3 local_position_ = Vec3::zero;
    Quat local_rotation_ = Quat::identity;
    Vec3 local_scale_ = Vec3::one;

    Mat4 local_matrix_;
    Mat4 world_matrix_;
    bool dirty_ = true;

    void recompute_matrices();
};

// e:\UzEngine\src\engine\scene\transform-component.cpp
#include "transform-component.h"
#include "engine/core/component-registry.h"

// Registration (XE-2)
DECLARE_COMPONENT(TransformComponent) {
    ComponentRegistry::get().register_component(
        stringId("TransformComponent"),
        []() -> UniquePtr<IComponent> { return make_unique<TransformComponent>(); }
    );
}

void TransformComponent::on_attach() {
    // Called when component added to node
    recompute_matrices();
}

void TransformComponent::on_detach() {
    // Called when component removed
}

void TransformComponent::recompute_matrices() {
    // Build local-to-world matrix
    local_matrix_ = Mat4::trs(local_position_, local_rotation_, local_scale_);

    // TODO: Query parent transform via node hierarchy
    // world_matrix_ = parent_world_matrix * local_matrix_;
    world_matrix_ = local_matrix_;
}
```

**Test File** (`tests/scene/test_transform_component.cpp`):
```cpp
#include <gtest/gtest.h>
#include "engine/scene/transform-component.h"
#include "engine/core/component-registry.h"

class TransformComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure component is registered
    }
};

TEST_F(TransformComponentTest, LocalPositionSet) {
    auto comp = make_unique<TransformComponent>();
    comp->set_local_position(Vec3{1.0f, 2.0f, 3.0f});
    EXPECT_EQ(comp->local_position(), Vec3{1.0f, 2.0f, 3.0f});
}

TEST_F(TransformComponentTest, DowncastSucceeds) {
    IComponent* base = static_cast<IComponent*>(new TransformComponent());
    TransformComponent* derived = base->as_transform();
    EXPECT_NE(derived, nullptr);
    delete base;
}

TEST_F(TransformComponentTest, RegistryLookup) {
    auto comp = ComponentRegistry::get().create(stringId("TransformComponent"));
    EXPECT_NE(comp, nullptr);
    EXPECT_NE(comp->as_transform(), nullptr);
}

TEST_F(TransformComponentTest, LocalMatrixComputation) {
    auto comp = make_unique<TransformComponent>();
    comp->set_local_position(Vec3{5.0f, 10.0f, 15.0f});
    Mat4 local = comp->local_matrix();
    EXPECT_TRUE(local.almost_equal(Mat4::translation(Vec3{5.0f, 10.0f, 15.0f}), 1e-6f));
}
```

---

## Conclusion

UzEngine MUST implement all 8 rules (XE-1 through XE-8) to achieve AAA quality. The patterns are not theoretical; they represent the convergent wisdom of three major engines, each with millions of lines of battle-tested code.

**Immediate Next Steps**:
1. Create ModuleManager (XE-1) — unblocks everything
2. Implement component registry (XE-2) — enables extensibility
3. Remove dynamic_cast (XE-3) — performance gate
4. Deploy frame arena (XE-5) — memory stability
5. Implement frame snapshot (XE-6) — thread safety

**Success Criteria**: All Phase 1 rules implemented and tested by end of Week 6, with 95%+ coverage on all new code.

---

**Document Owner**: Project Director
**Last Updated**: 2026-03-04
**Next Review**: After Phase 1 completion (Week 6)
