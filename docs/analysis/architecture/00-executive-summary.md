<!-- doc-meta
title: Godot Architecture Analysis — Executive Summary
description: Quick reference of Godot 4 patterns for UzEngine Application class design
category: engine-architecture
tags: [godot, application-architecture, executive-summary, quick-reference]
status: complete
date: 2026-03-14
author: claude
-->

# Godot 4 Application Architecture — Executive Summary

## The Core Insight

Godot allows game developers to write **only game logic** with zero awareness of:
- GPU device initialization
- Render passes or framebuffers
- Descriptor heaps or root signatures
- Shader compilation
- Threading or synchronization

This is achieved through **7 core architectural patterns**. UzEngine should adapt all of them.

---

## Pattern 1: Two-Phase Initialization ⭐ Critical

**Goal**: Avoid circular dependencies (RenderingServer depends on DisplayServer, which depends on Engine).

**Godot Implementation**:
```
Main::setup()     → ProjectSettings, Engine, Input, ClassDB, Modules
Main::setup2()    → DisplayServer, RenderingServer, RenderingDevice, Scene
```

**Why This Matters**:
- Phase 1 can run in headless mode (no GPU)
- Phase 2 requires a window context
- Each phase is idempotent (can be deferred or skipped)

**UzEngine Adaptation**:
```cpp
Application::Setup()        // Core services only
Application::Setup2()       // Rendering + display
Application::RunFrame()     // Main loop
Application::Shutdown()     // Reverse-order cleanup
```

---

## Pattern 2: Engine Singleton Registry

**Goal**: Central ownership of all server lifetimes; avoid global singletons scattered everywhere.

**Godot Implementation**:
```cpp
Engine::get_singleton()->get_singleton("RenderingServer")
// Returns: RenderingServer*, tracked by Engine
```

**Why This Matters**:
- Engine owns the lifetime of DisplayServer, RenderingServer, PhysicsServer, etc.
- Shutdown is guaranteed reverse-order (dependencies cleanup first)
- Type-safe retrieval possible via templates

**UzEngine Adaptation**:
```cpp
Engine::Get()->GetSingleton<RenderingServer>("RenderingServer")
Engine::Get()->RegisterSingleton("DisplayServer", display_server)
```

---

## Pattern 3: RenderingServer Deferred API ⭐ Game-Changer

**Goal**: Game thread enqueues rendering commands; render thread executes them asynchronously (no locks, no GPU hazards).

**Godot Implementation**:
```cpp
// Game thread: enqueue immediately, returns instantly
RID texture = RenderingServer::get_singleton()->texture_2d_create(image);
RenderingServer::get_singleton()->mesh_set_surface_override_material(mesh, surface_idx, material);

// Render thread: drains queue, executes GPU work
// No game thread touches GPU API after this point
```

**Why This Matters**:
- **No mutex overhead**: Game thread appends to lock-free queue, render thread drains it
- **No GPU types exposed**: Game code never sees `ID3D12Device`, `VkBuffer`, descriptors
- **No shader compilation blocks**: Shaders compiled asynchronously on render thread
- **Automatic sync**: RenderingServer::Sync() waits for queue to drain

**UzEngine Adaptation**:
```cpp
// Game thread
RenderingServer::Get()->Texture2DUpdate(rid, image);  // Enqueued, returns instantly

// Render thread (in Background)
rendering_device->UploadTexture(...);  // Executes GPU work

// Main thread (after physics/logic)
RenderingServer::Get()->Sync();  // Wait for render thread
```

**Critical Property**: The command queue is **opaque** to game code. No game developer ever touches `ID3D12CommandList`, `VkCommandBuffer`, or descriptor sets.

---

## Pattern 4: ProjectSettings Configuration Spine

**Goal**: Single source of truth for ALL engine configuration (rendering, physics, scripting, etc.).

**Godot Implementation**:
```ini
# project.godot
[rendering]
lights_and_shadows/positional_shadow/atlas_size = 4096
global_illumination/sdfgi/enabled = true

[physics]
physics/common/physics_ticks_per_second = 60
```

**Why This Matters**:
- No rendering parameters in C++ code
- Easy to tweak from file without recompilation
- Enables data-driven pipelines
- Game code never touches ProjectSettings directly

**UzEngine Adaptation**:
```cpp
// Initialize (Phase 1)
ProjectSettings::Load("project.uz");

// In RenderingServer init (Phase 2)
int shadow_atlas = ProjectSettings::Get<int>("rendering/shadow_atlas_size", 4096);
uz::RhiFormat g_buffer_fmt = ParseFormat(
    ProjectSettings::Get<std::string>("rendering/g_buffer_format", "rgba8")
);
```

---

## Pattern 5: SceneTree as Trivial MainLoop

**Goal**: Game loop that is **minimal and node-unaware**.

**Godot Implementation**:
```cpp
class SceneTree : public MainLoop {
    bool process(double p_time) override {
        // Step 1: Input
        Input::get_singleton()->flush_frame();

        // Step 2: Physics (fixed timestep)
        root->_call_physics_process(physics_dt);

        // Step 3: Logic
        root->_call_process(p_time);

        // Step 4: Render sync
        RenderingServer::get_singleton()->sync();
    }
};
```

**Why This Matters**:
- Game loop is ~30 lines, not 300
- Nodes walk is a simple recursive tree traversal
- No rendering state touched in main loop
- Render thread runs completely asynchronously

**UzEngine Adaptation**:
```cpp
bool SceneTree::Process(double dt) {
    if (paused) return false;

    InputPhase_();
    PhysicsPhase_(dt);
    LogicPhase_(dt);
    // RenderingServer::Sync() happens in Application::RunFrame(), not here
}
```

---

## Pattern 6: Notification-Based Lifecycle

**Goal**: Nodes never know about rendering; they only implement game logic hooks.

**Godot Nodes Implement**:
```cpp
void _ready()                  // Called once when entering tree
void _process(double delta)    // Called every frame
void _physics_process(double delta)  // Called every physics tick
void _input(InputEvent e)      // Called for input
```

**Why This Matters**:
- Clear separation: game logic vs. rendering
- No inheritance tree explosion (Actor + Component + Renderable + Collidable)
- Game code has zero awareness of GPU/rendering pipeline
- Rendering happens via **high-level components** (MeshInstance, Light, etc.)

**UzEngine Adaptation**:
```cpp
class Node : public RefCounted {
    virtual void OnReady() {}
    virtual void OnProcess(double delta) {}
    virtual void OnPhysicsProcess(double delta) {}
    virtual void OnInput(const InputEvent& e) {}
};

class Node3D : public Node {
    // Attached components (MeshRenderer, RigidBody, Light)
    std::unordered_map<std::string, uz::Ref<Component>> components_;
};
```

---

## Pattern 7: Component Abstraction (Rendering Opacity)

**Goal**: Rendering components are **pure facades** — game code never touches GPU state.

**Godot Example**:
```gdscript
# Game developer sees only this:
var mesh = MeshInstance3D.new()
mesh.mesh = my_mesh
mesh.material_override = my_material
add_child(mesh)

# Engine handles (game never sees):
# - RenderingServer RID creation
# - Viewport registration
# - Render list building
# - Transform synchronization
# - Material parameter updates
```

**Why This Matters**:
- Game code is **100% rendering-unaware**
- Components manage their own GPU state
- Updates are **deferred** (safe with render thread)

**UzEngine Adaptation**:
```cpp
class MeshRenderer : public Component {
private:
    RenderingServer::ResourceId instance_rid_;

public:
    void SetMesh(uz::Ref<Mesh> mesh) {
        // Internally enqueues deferred command
        RenderingServer::Get()->InstanceSetMesh(instance_rid_, mesh->GetRID());
    }

    void OnAttach() override {
        instance_rid_ = RenderingServer::Get()->InstanceCreate();
    }

    void OnDetach() override {
        RenderingServer::Get()->InstanceDestroy(instance_rid_);
    }
};
```

---

## Pattern 8: GDExtension C ABI (Plugin Safety)

**Goal**: Plugins can be compiled independently; they can't access render thread or internals.

**Why This Matters**:
- Plugin receives stable interface vtable at runtime
- Never includes `Engine.h` or links engine
- Only high-level, safe operations available
- No render thread hazards possible

**UzEngine Adaptation**:
```cpp
struct uz_plugin_interface_t {
    // Safe operations only (high-level)
    uz_entity_t (*scene_find_by_name)(const char* name);
    uz_raycast_result_t (*physics_raycast)(float ox, float oy, float oz, ...);
    void (*log_info)(const char* fmt, ...);
};

extern "C" int uz_plugin_init(uz_plugin_interface_t* interface) {
    // Plugin ONLY gets interface; no engine linkage
    interface->log_info("Plugin loaded");
}
```

---

## Comparison: UE5 vs Godot vs UzEngine

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| **Entry Point Complexity** | FEngineLoop (~2000 lines) | Main::setup/setup2 (phased) | Application::Setup/Setup2 |
| **Rendering Thread Model** | 2 threads (separate by default) | 1 thread (configurable) | 1 thread (DX12-first) |
| **Command Queueing** | ENQUEUE_RENDER_COMMAND macro | RenderingServer::*() (deferred) | RenderingServer::*() (deferred) |
| **Scripting** | Blueprint VM + C++ | GDScript VM + C++ | Lua (LuaJIT) + C++ |
| **Plugin System** | Source-only (rebuilds engine) | GDExtension (stable C ABI) | uz_plugin_t (stable C ABI) |
| **Scene Representation** | World/Actor hierarchy | Node tree | Node tree + Components |
| **Configuration Source** | .ini + code | ProjectSettings | ProjectSettings |

---

## Implementation Roadmap for UzEngine

### Phase 1 (Week 1): Core Setup
- [ ] `Engine::Get()` singleton registry
- [ ] `ProjectSettings::Load()` from `.uz` INI
- [ ] `Input` singleton
- [ ] `MessageQueue` for deferred callbacks

### Phase 2 (Week 2): Rendering Setup
- [ ] `DisplayServer` (window management)
- [ ] `RenderingDevice` (DX12 context)
- [ ] `RenderingServer` deferred API (depends on RHI spec)
- [ ] `PhysicsWorld` (Jolt)

### Phase 3 (Week 3): Scene System
- [ ] `SceneTree` node hierarchy
- [ ] `Node` / `Node3D` base classes
- [ ] Component system (MeshRenderer, Light, RigidBody)
- [ ] Lifecycle (OnReady, OnProcess, OnPhysicsProcess)

### Phase 4 (Week 4): Main Loop & Scripting
- [ ] `Application::RunFrame()` main loop
- [ ] Lua API bindings (uz.scene.*, uz.physics.*, etc.)
- [ ] Plugin system (uz_plugin_interface_t)

### Phase 5 (Week 5): Integration
- [ ] Full scene loading (.uzscene)
- [ ] Asset hot-reload
- [ ] Render thread startup/shutdown
- [ ] Demo scene validation

---

## Critical Success Criteria

**Game Code Simplicity**:
```cpp
// This should be the ENTIRE interface a game developer needs
class MyGame : public Node3D {
    void OnReady() { /* init */ }
    void OnProcess(double dt) { /* update */ }
    void OnPhysicsProcess(double dt) { /* physics */ }
};
```

**No GPU Types Above RHI.h**:
- ❌ No `ID3D12Device` in game code
- ❌ No `VkBuffer` in game code
- ❌ No `D3D12_ROOT_SIGNATURE_DESC` in game code
- ❌ No `RHI_ROOT_SIGNATURE_DESC` in game code (only RHI-internal)
- ✅ Only `RenderingServer::Texture2DCreate()` (returns opaque RID)

**Thread Safety**:
- ❌ Game thread never calls GPU APIs
- ✅ RenderingServer enqueues, render thread executes
- ✅ No locks (lock-free SPSC queue)

**Render Thread Opacity**:
- ❌ Game code can't access render thread state
- ✅ Only high-level deferred operations
- ✅ GDExtension plugins can't even link engine

---

## Key Takeaways

1. **Two-phase startup** avoids circular dependencies and enables headless mode
2. **Engine singleton registry** owns server lifetimes; safe shutdown guaranteed
3. **RenderingServer deferred API** is the cornerstone of thread safety
4. **ProjectSettings spine** means game code never touches rendering parameters
5. **SceneTree simplicity** — the main loop is ~30 lines, walks node tree
6. **Notification-based lifecycle** replaces large inheritance hierarchies
7. **Component abstraction** lets rendering happen invisibly to game logic
8. **GDExtension C ABI** teaches safe plugin boundaries without engine exposure

---

## Files to Read

- **Full Analysis**: `01-godot-application-architecture.md`
- **Code Patterns**: `02-godot-patterns-for-uzengine.md`
- **Godot Source** (reference):
  - `E:\godot\main\main.cpp` — Initialization
  - `E:\godot\scene\main\scene_tree.h` — Game loop
  - `E:\godot\servers\rendering\rendering_server.h` — Deferred API
  - `E:\godot\core\config\engine.h` — Singleton registry
  - `E:\godot\core\extension\gdextension.h` — Plugin system

---

## UzEngine Adaptation Status

✅ **Analyzed**: Godot 4 application architecture (7 patterns identified)
✅ **Documented**: Full analysis + code examples in C++23/DX12/Lua
⏳ **Next Step**: Implement Application::Setup() + Setup2() (depends on RHI spec completion)

**Critical Blocker**: RHI abstraction spec must be complete before RenderingServer deferred API can be implemented.

**Expected Result**: Game developers write only game logic (OnReady/OnProcess/OnPhysicsProcess) with zero GPU/rendering awareness.
