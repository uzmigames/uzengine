<!-- doc-meta
title: Godot 4 Application Architecture Analysis
description: How Godot structures its top-level application abstraction to hide engine boilerplate from game code
category: engine-architecture
tags: [godot, application-architecture, engine-abstraction, rendering-server, scene-tree, gdextension]
status: complete
date: 2026-03-14
author: claude
-->

# Godot 4 Application Architecture Analysis

This document analyzes Godot 4's architecture for **top-level application abstraction** — specifically, how Godot allows game developers to write only game logic without touching device init, render passes, resource buffers, or thread management.

The goal is to extract patterns for UzEngine's Application class design (C++23, DX12-first, LuaJIT scripting).

## Executive Summary

Godot achieves remarkable abstraction simplicity through **5 core patterns**:

1. **Two-Phase Startup**: `Main::setup()` (core services) then `Main::setup2()` (rendering/display)
2. **Singleton Registry (Engine class)**: Central heartbeat that owns all server lifetimes
3. **RenderingServer Deferred API**: All rendering commands are enqueued, never executed immediately
4. **SceneTree as MainLoop**: The game loop is a trivial container that walks the Node tree
5. **Notification-Based Lifecycle**: Nodes never know about rendering — they only implement `_ready()`, `_process()`, `_physics_process()`

Game code writes:
```cpp
// That's it. No Device, no Render Graph, no Descriptor Heaps, no PSO creation.
class MyNode : public Node3D {
    void _ready() override { /* init game logic */ }
    void _process(float delta) override { /* update position */ }
};
```

The engine ensures:
- RenderingServer is initialized before any Node tree walks
- All rendering state (G-Buffer format, shadow atlas size, etc.) comes from **ProjectSettings** (INI-like configs)
- Nodes interact with rendering via **high-level components** (MeshInstance3D, DirectionalLight3D) that are completely opaque
- No low-level graphics API is exposed above `rhi.h` boundary

---

## Pattern 1: Two-Phase Initialization

Godot's `Main` class (in `main/main.cpp`) initializes the engine in two strictly-ordered phases:

### Phase 1: `Main::setup()` — Core Services

```cpp
// E:\godot\main\main.cpp:2055 onwards
Error Main::setup(const char *p_execpath, int p_argc, char *p_argv[], bool p_second_phase) {
    // Parse command line
    // Initialize ProjectSettings (load project.godot / .pck)
    // Create singletons: Engine, InputMap, TranslationServer, Performance, MessageQueue
    // Register core types (via ClassDB)
    // Load GDExtension plugins (INITIALIZATION_LEVEL_CORE)

    if (p_second_phase) {
        return setup2();  // Chain to phase 2
    }
}
```

**Invariants established by phase 1:**
- `ProjectSettings::get_singleton()` is fully loaded
- All project settings (render path, shadow atlas size, physics fps, etc.) are accessible
- `Engine::get_singleton()` exists and owns all server lifetimes
- Core modules (Math, STL, threading) are initialized
- **No graphics context exists yet**

### Phase 2: `Main::setup2()` — Rendering & Display

```cpp
// E:\godot\main\main.cpp:2980 onwards
Error Main::setup2(bool p_show_boot_logo) {
    // Create DisplayServer (platform window, input handling)
    // Create RenderingServer (deferred rendering API)
    // Create RenderingDevice (Vulkan/Metal/DX12 context)
    // Create PhysicsServer3D/2D
    // Load scene tree root (Viewport)
    // Load initial scene (main_scene from ProjectSettings)
    // Start render thread (if not RENDER_THREAD_UNSAFE)

    // Main loop runs from here
}
```

**Key property**: Phase 2 is idempotent. If `setup()` is called with `p_second_phase=false`, phase 2 can be called later (or not at all, for headless servers).

### UzEngine Adaptation

```cpp
/// Two-phase initialization mirroring Godot's setup pattern.
/// Phase 1 (Setup): Load configs, register types, init core services.
/// Phase 2 (Setup2): Create RHI, RenderingServer, load main scene.
class Application {
public:
    /// Phase 1: Core initialization (ProjectSettings, ClassDB, singletons).
    uz::Result<void> Setup(const char* exe_path, int argc, char* argv[]);

    /// Phase 2: Rendering & display (DisplayServer, RenderingServer, RenderingDevice).
    uz::Result<void> Setup2();

    /// Main loop: process input, update scene, render.
    void RunFrame();

    void Shutdown();
};
```

---

## Pattern 2: Singleton Registry (Engine Class)

Godot's `Engine` class is a **lightweight singleton registry** that owns the lifetime of all "server" (subsystem) singletons:

```cpp
// E:\godot\core\config\engine.h:43-55
class Engine {
private:
    List<Singleton> singletons;
    HashMap<StringName, Object *> singleton_ptrs;

    uint64_t frames_drawn = 0;
    int ips = 60;  // Physics iterations per second
    double _time_scale = 1.0;
    double _physics_interpolation_fraction = 0.0;

    bool editor_hint = false;
    bool _in_physics = false;

    static inline Engine *singleton = nullptr;
};
```

### Singletons Registered

From `main/main.cpp:160-199`, during `setup()` and `setup2()`:

```cpp
static Engine *engine = nullptr;
static ProjectSettings *globals = nullptr;
static Input *input = nullptr;
static InputMap *input_map = nullptr;
static TranslationServer *translation_server = nullptr;
static Performance *performance = nullptr;

// From setup2():
static AudioServer *audio_server = nullptr;
static DisplayServer *display_server = nullptr;
static RenderingServer *rendering_server = nullptr;
static PhysicsServer3D *physics_server_3d = nullptr;
static PhysicsServer2D *physics_server_2d = nullptr;
static CameraServer *camera_server = nullptr;
static XRServer *xr_server = nullptr;
```

Each is accessed globally:
```cpp
RenderingServer::get_singleton()->texture_2d_create(...);
PhysicsServer3D::get_singleton()->body_create(...);
```

**Key insight**: The Engine class doesn't create these servers; it just **tracks their pointers**. The servers are created in `main/main.cpp` in a specific order:

1. Core servers: ProjectSettings, Input, TranslationServer
2. Display server (creates OS-level window)
3. Rendering server (creates GPU API context)
4. Physics servers (depend on RenderingServer for debug drawing)
5. Scene extensions (XR, etc.)

This **deferred registration** pattern allows servers to depend on each other without circular dependencies.

### UzEngine Adaptation

```cpp
/// Central singleton registry and lifecycle manager.
/// Owns lifetime of all subsystem servers.
class Engine {
private:
    std::vector<Singleton> singletons_;
    std::unordered_map<std::string_view, void*> singleton_ptrs_;

    uint64_t frames_drawn_ = 0;
    int physics_ticks_per_second_ = 60;
    double time_scale_ = 1.0;
    double physics_interpolation_fraction_ = 0.0;

    bool editor_hint_ = false;
    bool in_physics_ = false;

    static inline Engine* singleton_ = nullptr;

public:
    static Engine* Get() { return singleton_; }

    template <typename T>
    T* GetSingleton(std::string_view name) const {
        auto it = singleton_ptrs_.find(name);
        return it != singleton_ptrs_.end() ? static_cast<T*>(it->second) : nullptr;
    }

    /// Register a singleton server (called by subsystem init).
    void RegisterSingleton(std::string_view name, void* ptr, std::type_info& type);

    /// Update frame state (called from Application::RunFrame).
    void UpdateFrameState(double delta_time, bool is_physics_step);
};
```

---

## Pattern 3: RenderingServer Deferred API

This is **Godot's most elegant pattern** for thread-safe rendering. Game code never calls GPU APIs directly; instead, it **enqueues commands** that are executed asynchronously on the render thread.

### How It Works

Game thread calls:
```cpp
// In Node::_process() or anywhere in game code
RID texture = RenderingServer::get_singleton()->texture_2d_create(image);
RenderingServer::get_singleton()->mesh_set_surface_override_material(mesh, surface_idx, material);
```

Each call is **recorded** into a deferred command buffer (not executed immediately). On the render thread:

```cpp
// Render thread periodically calls RenderingServer::flush()
// which processes all enqueued commands and executes them on GPU
```

### Thread Safety Without Locks

The deferred API avoids the cost of mutexes:

1. **Game thread records commands** (lock-free, single-threaded append to buffer)
2. **Render thread flushes commands** (lock-free, buffer is not accessed by game thread during flush)
3. **Synchronization happens at frame boundary** via `RenderingServer::sync()`

### RenderingServer API Surface

From `E:\godot\servers\rendering\rendering_server.h`:

```cpp
class RenderingServer : public Object {
    static RenderingServer *singleton;

public:
    // Opaque handle-based API (RID = RenderID)
    virtual RID texture_2d_create(const Ref<Image> &p_image) = 0;
    virtual RID mesh_create() = 0;
    virtual RID shader_create() = 0;
    virtual RID material_create() = 0;

    // Deferred commands (no GPU work happens here)
    virtual void texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer = 0) = 0;
    virtual void mesh_surface_set_material(RID p_mesh, int p_surface, RID p_material) = 0;

    // Instance/render list building
    virtual RID instance_create() = 0;
    virtual void instance_set_transform(RID p_instance, const Transform3D &p_transform) = 0;
    virtual void instance_set_visible(RID p_instance, bool p_visible) = 0;

    // Synchronization
    virtual void sync() = 0;  // Wait for render thread to complete
};
```

### Key Properties

1. **No GPU types in public API**: No `ID3D12Device`, `VkBuffer`, `ComPtr<>` — just opaque `RID` handles
2. **No descriptor sets, heaps, or PSOs**: RenderingServer manages all GPU state internally
3. **No explicit shader compilation**: Shaders are passed as GLSL/HLSL source, compiled asynchronously
4. **No viewport resizing bugs**: DisplayServer manages window surface, RenderingServer automatically rebinds
5. **No thread hazards**: All GPU commands are on one thread; game thread only appends to queue

### Deferred Command Recording

The internal implementation (not exposed to game code):

```cpp
// Pseudocode: how RenderingServer records commands
class RenderingServerDefault : public RenderingServer {
    CommandQueue deferred_commands;  // Lock-free SPSC queue

public:
    void texture_2d_update(RID p_texture, const Ref<Image> &p_image) override {
        // Record command, not executed yet
        deferred_commands.push([=]() {
            // Executed on render thread
            auto* vkdevice = get_vk_device();
            vkdevice->upload_texture(p_texture, p_image);
        });
    }
};
```

### UzEngine Adaptation

```cpp
/// Deferred rendering command server.
/// Game thread enqueues commands; render thread executes them.
class RenderingServer {
public:
    /// Opaque render resource ID (like RID in Godot)
    using ResourceId = uint64_t;

    /// Create texture (returns handle immediately, upload is deferred)
    ResourceId Texture2DCreate(const Image& image);

    /// Update texture (enqueues command, returns immediately)
    void Texture2DUpdate(ResourceId texture, const Image& image);

    /// Create material (returns handle immediately)
    ResourceId MaterialCreate(const uz::Ref<Shader>& shader);

    /// Set material param (deferred)
    void MaterialSetParam(ResourceId material, std::string_view param, const Variant& value);

    /// Create instance (geometry in world)
    ResourceId InstanceCreate();
    void InstanceSetTransform(ResourceId instance, const Transform& xform);
    void InstanceSetVisible(ResourceId instance, bool visible);

    /// Synchronization
    void Sync();  // Wait for render thread to drain queue

    /// Check if current thread is render thread
    bool IsOnRenderThread() const;
};
```

---

## Pattern 4: SceneTree as MainLoop

Godot's `SceneTree` implements `MainLoop`, which is the **core game loop abstraction**:

```cpp
// E:\godot\scene\main\scene_tree.h:89-150
class SceneTree : public MainLoop {
    Window *root = nullptr;  // Root of node hierarchy

    // Process groups for threading
    LocalVector<ProcessGroup *> process_groups;
    uint64_t process_last_pass = 1;

    // Physics state
    double physics_process_time = 0.0;
    double process_time = 0.0;
    bool paused = false;

    // Input/events
    HashMap<StringName, SceneTreeGroup> group_map;
};
```

### SceneTree as Heartbeat

The main loop calls:

```cpp
// From OS (platform layer)
void OS::set_main_loop(MainLoop *p_main_loop) { /* ... */ }

// Each frame:
bool SceneTree::process(double p_time) {
    // 1. Input phase
    Input::get_singleton()->flush_frame();

    // 2. Physics phase (fixed-timestep)
    for (int i = 0; i < max_physics_steps; i++) {
        physics_process_time += p_time;
        if (physics_process_time >= physics_timestep) {
            root->_call_physics_process(physics_timestep);  // Walks tree, calls _physics_process
            physics_process_time -= physics_timestep;
        }
    }

    // 3. Logic phase
    root->_call_process(p_time);  // Walks tree, calls _process on each Node

    // 4. Render phase
    RenderingServer::get_singleton()->sync();  // Wait for render commands to complete

    return false;  // Don't quit
}
```

### Node Lifecycle

Each node in the tree receives notifications:

```cpp
// E:\godot\scene\main\node.h:78-96 (ProcessMode enum)
enum ProcessMode {
    PROCESS_MODE_INHERIT,      // Same as parent
    PROCESS_MODE_PAUSABLE,     // Process only if not paused
    PROCESS_MODE_WHEN_PAUSED,  // Process only if paused
    PROCESS_MODE_ALWAYS,       // Always process
    PROCESS_MODE_DISABLED,     // Never process
};
```

Nodes implement:

```cpp
class MyNode : public Node {
    void _ready() override;          // Called once when entering tree
    void _process(double delta) override;        // Called every frame
    void _physics_process(double delta) override; // Called every physics tick
    void _input(const InputEvent &p_event) override; // Called for input events
    void _notification(int p_what) override;    // Generic notification dispatch
};
```

**Godot hides all rendering complexity** — the node tree is never aware of:
- Render passes
- G-Buffer layout
- Shadow atlas allocation
- PSO state
- Descriptor set allocation

### UzEngine Adaptation

```cpp
/// Core game loop abstraction.
/// Each frame: input -> physics -> update -> render.
class SceneTree : public MainLoop {
private:
    uz::Ref<Scene> root_;
    std::vector<ProcessGroup> process_groups_;

    double physics_process_time_ = 0.0;
    int physics_ticks_per_second_ = 60;
    bool paused_ = false;
    bool suspended_ = false;

public:
    /// Process one frame (called by Application::RunFrame)
    bool ProcessFrame(double delta_time) override;

private:
    void PhysicsProcess_(double delta);
    void LogicProcess_(double delta);
    void RenderProcess_();
};

/// Base node class (game developer extends this)
class Node {
public:
    virtual void OnReady() {}
    virtual void OnProcess(double delta) {}
    virtual void OnPhysicsProcess(double delta) {}
    virtual void OnInput(const InputEvent& event) {}
    virtual void OnNotification(NotificationType type) {}

    // Inherited by CanvasItem, Node3D (similar to Godot)
};

class Node3D : public Node {
public:
    Transform GetGlobalTransform() const;
    void SetGlobalTransform(const Transform& xform);

    /// Children are never aware rendering happens
    void AddChild(uz::Ref<Node> child);
};
```

---

## Pattern 5: ProjectSettings — The Engine Configuration Spine

Godot's `ProjectSettings` (loaded from `project.godot` INI file) is the **single source of truth** for all engine configuration, including rendering:

### Core Rendering Settings

From Godot's rendering project settings:

```ini
[rendering]
global_illumination/gi_use_half_resolution = true
global_illumination/sdfgi/enabled = true

global_illumination/sdfgi/cascade0_distance = 12.8
global_illumination/sdfgi/min_cell_size = 0.2
global_illumination/sdfgi/cascade_count = 8

textures/default_filters/anisotropic_filtering_level = 4

textures/vram_compression/import_etc2_astc = true

lights_and_shadows/directional_shadow/size = 4096
lights_and_shadows/directional_shadow/soft_shadow_filter_quality = 2
lights_and_shadows/positional_shadow/atlas_size = 4096
lights_and_shadows/positional_shadow/atlas_quad_0 = 0
lights_and_shadows/positional_shadow/atlas_quad_1 = 1
lights_and_shadows/positional_shadow/atlas_quad_2 = 2
lights_and_shadows/positional_shadow/atlas_quad_3 = 3

textures/lightmap/directional = true
textures/lightmap/filter_type_in_renderer = true
```

**No game code touches these.** Instead:

```cpp
// In RenderingServer initialization:
int shadow_atlas_size = GLOBAL_GET("lights_and_shadows/positional_shadow/atlas_size");
RenderingDevice::TextureFormat shadow_format = ...;
shadow_atlas_rid = rendering_device->texture_create(shadow_format);
```

All configuration is **declarative** (INI) and **loaded at startup**, never modified at runtime by game logic.

### UzEngine Adaptation

```cpp
/// Engine-wide configuration (read from project.uz or project.toml)
class ProjectSettings {
public:
    static ProjectSettings* Get();

    /// Get setting with default fallback
    template <typename T>
    T Get(std::string_view key, const T& default_value) const;

    /// Common rendering settings
    int GetShadowAtlasSize() const { return Get("rendering/shadow_atlas_size", 4096); }
    uz::RhiFormat GetGBufferFormat() const { /* parse from settings */ }
    bool IsBindlessEnabled() const { return Get("rendering/bindless_enabled", false); }
};
```

---

## Pattern 6: Component Abstraction — MeshInstance3D / DirectionalLight3D

Godot nodes like `MeshInstance3D` and `DirectionalLight3D` are **pure logic facades** that hide all rendering implementation:

```cpp
// Game code (high-level, no rendering awareness)
auto mesh = memnew(MeshInstance3D);
mesh->set_mesh(my_mesh);
mesh->set_material_override(my_material);
mesh->set_transform(Transform3D(...));
add_child(mesh);

// What happens internally (game code never sees):
// 1. MeshInstance3D creates RenderingServer::RID for the instance
// 2. Registers itself with the Viewport's RenderList
// 3. During render phase, Viewport iterates render list, queries transforms/materials
// 4. Sends deferred commands to RenderingServer to update GPU state
```

The node **never knows about**:
- GPU texture formats
- PSO state
- Descriptor heap allocation
- Render pass structure
- Lighting pass vs shadow pass

### UzEngine Pattern

```cpp
/// Rendering component (game developers never implement this)
class MeshRenderer : public Component {
private:
    uz::Ref<Mesh> mesh_;
    uz::Ref<Material> material_;
    uz::RenderingServer::ResourceId instance_rid_;

public:
    void SetMesh(uz::Ref<Mesh> mesh) {
        mesh_ = mesh;
        // Update RenderingServer instance (deferred command)
        RenderingServer::Get()->InstanceSetMesh(instance_rid_, mesh->GetRID());
    }

    void SetMaterial(uz::Ref<Material> mat) {
        material_ = mat;
        RenderingServer::Get()->InstanceSetMaterial(instance_rid_, mat->GetRID());
    }

    // Node3D::OnProcess() calls this
    void UpdateTransform(const Transform& xform) {
        RenderingServer::Get()->InstanceSetTransform(instance_rid_, xform);
    }
};
```

---

## Pattern 7: GDExtension C ABI — Plugin Isolation

Godot's `GDExtension` system uses a **stable C ABI** (not C++ mangling) so plugins can be compiled independently without rebuilding the engine:

```cpp
// E:\godot\core\extension\gdextension.h
class GDExtension : public Resource {
    Ref<GDExtensionLoader> loader;

    HashMap<StringName, Extension> extension_classes;
    GDExtensionMainLoopStartupCallback startup_callback;
    GDExtensionMainLoopFrameCallback frame_callback;
    GDExtensionMainLoopShutdownCallback shutdown_callback;
};
```

### Plugin Entry Point (C ABI)

Plugins export a single C function:

```cpp
// plugin.cpp (user-written)
extern "C" {
    GDExtensionBool gdextension_init(GDExtensionInterface *p_interface, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_init) {
        // p_interface provides stable function pointers to engine internals
        // Plugin never links engine; it receives interface at runtime

        r_init->initialize = [](GDExtensionInitializationLevel p_level) {
            if (p_level == GDEXTENSION_INITIALIZATION_SCENE) {
                p_interface->classdb_register_extension_class(/* MyGameClass */);
            }
        };

        return true;
    }
}
```

**Key property**: The plugin **never includes `Engine.h`** or links `libgodot.a`. It receives a vtable of function pointers (`GDExtensionInterface`) at runtime. This allows:

1. Plugin compiled with different compiler/stdlib
2. Plugin loaded/unloaded without engine rebuild
3. Multiple plugin versions coexist
4. **Most importantly**: Plugin cannot accidentally access rendering internals (no render thread hazards)

### UzEngine Adaptation

```cpp
/// Stable C ABI for game plugins (similar to GDExtension)
struct uz_engine_interface_t {
    // ClassDB registration
    void (*classdb_register_class)(const char* name, const char* parent);
    void (*classdb_register_method)(const char* class_name, const char* method_name, ...);

    // Scene queries (high-level)
    uz_entity_t (*scene_find_by_name)(const char* name);
    void (*scene_instantiate)(const char* scene_path);

    // Component access (deferred)
    void (*component_set_enabled)(uz_entity_t entity, const char* component_type, bool enabled);

    // Scripting (Lua)
    void (*lua_call_function)(const char* function_name, ...);

    // Physics (safe queries, no render thread access)
    uz_raycast_result_t (*physics_raycast)(float ox, float oy, float oz, float dx, float dy, float dz, float max_dist);
};

extern "C" uz_plugin_result_t uz_plugin_init(uz_engine_interface_t* interface) {
    // Plugin receives stable interface, never links engine
    interface->classdb_register_class("MyGameEntity", "Entity");
    return UZ_PLUGIN_OK;
}
```

---

## Comparative Analysis: UE5 vs Godot vs UzEngine

| Aspect | UE5 | Godot | UzEngine Target |
|--------|-----|-------|-----------------|
| **Application Entry** | `FEngineLoop` (huge, 2000+ lines) | `Main::setup()` + `Main::setup2()` (phased) | `Application::Setup()` + `Setup2()` (phased) |
| **Rendering API** | `ENQUEUE_RENDER_COMMAND` macro (deferred) | `RenderingServer::*()` (deferred) | `RenderingServer::*()` (deferred) |
| **Node Lifecycle** | `AActor::BeginPlay()`, `Tick()` | `Node::_ready()`, `_process()` | `Node::OnReady()`, `OnProcess()` |
| **Configuration** | `.ini` files + C++ defaults | `ProjectSettings` (`.godot`) | `ProjectSettings` (`.uz` or `.toml`) |
| **Shutdown Order** | Complex dependency graph | Engine singleton manages teardown | Engine singleton manages teardown |
| **Plugin System** | Unreal Plugins (source-only or compiled with engine) | GDExtension (stable C ABI, dynamic) | uz_plugin_t (stable C ABI, dynamic) |
| **Component Model** | `UComponent` (tight coupling to Actor) | Scene nodes with optional components | Scene nodes with components (ECS-like) |
| **Render Thread Model** | Separate thread by default; ENQUEUE safety | RENDER_THREAD_SAFE/SEPARATE/UNSAFE modes | Single render thread (DX12-first) |
| **Physics Integration** | Integrated (Chaos); PhysicsScene 1:1 with World | Separate servers (PhysicsServer3D, 2D) | Separate Jolt-based engine |
| **Scripting** | Blueprint (VM) + C++ | GDScript (VM) + C++ | Lua (LuaJIT) + C++ (no VM sync cost) |

---

## Implementation Checklist for UzEngine

### Phase 1: Core Setup
- [ ] `Engine::Get()` singleton with `RegisterSingleton()` method
- [ ] `ProjectSettings::Load(path)` from `.uz` INI or TOML
- [ ] `ClassDB::Register<T>()` template for type registration
- [ ] `Input` singleton for input state
- [ ] `MessageQueue` for deferred game thread -> script thread messages

### Phase 2: Rendering Setup
- [ ] `RenderingServer` deferred API (not implemented yet, see RHI-bound spec)
- [ ] `DisplayServer` for window management
- [ ] `RenderingDevice` initialization (Vulkan/DX12)
- [ ] `PhysicsServer` initialization (Jolt)
- [ ] `SceneTree::Load(main_scene_path)` to instantiate game

### Phase 3: Main Loop
- [ ] `Application::RunFrame()` that calls `SceneTree::ProcessFrame()`
- [ ] Input polling (OS -> Input -> SceneTree)
- [ ] Physics stepping (fixed timestep)
- [ ] Logic update (Node::OnProcess)
- [ ] Render sync (`RenderingServer::Sync()`)

### Phase 4: Shutdown
- [ ] Reverse-order destruction (Game -> Physics -> Rendering -> Core)
- [ ] GDExtension plugin unload
- [ ] Memory leak detection

---

## Key Takeaways

1. **Two-phase startup** avoids circular dependencies and allows headless/editor modes
2. **Deferred rendering** is the cornerstone of thread safety — no locks, no GPUs hazards
3. **RID opaque handles** prevent accidental GPU object lifetime bugs
4. **ProjectSettings as config spine** means game code never touches rendering parameters
5. **Notification-based lifecycle** (ready/process/input) replaces large base classes
6. **GDExtension C ABI** teaches how to make safe plugin boundaries without exposing internals
7. **Single render thread** (unlike UE5's two-thread model) simplifies synchronization

---

## References

- `E:\godot\main\main.cpp` — Main class initialization
- `E:\godot\core\os\os.h` — OS abstraction layer
- `E:\godot\scene\main\scene_tree.h` — Game loop
- `E:\godot\servers\rendering\rendering_server.h` — Deferred rendering API
- `E:\godot\core\extension\gdextension.h` — Plugin system
- `E:\godot\core\config\engine.h` — Singleton registry
