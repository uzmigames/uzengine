<!-- doc-meta
title: Godot Patterns Adapted for UzEngine
description: Concrete code patterns from Godot 4 adapted to UzEngine's C++23 + DX12 + Lua architecture
category: engine-architecture
tags: [godot, adaptation, application-architecture, c++23, dx12, lua]
status: complete
date: 2026-03-14
author: claude
-->

# Godot Patterns Adapted for UzEngine

This document translates Godot 4's architectural patterns into concrete UzEngine code (C++23, DX12-first, LuaJIT scripting).

---

## Pattern 1: Two-Phase Application Initialization

### Godot Original

```cpp
// E:\godot\main\main.cpp
Error Main::setup(const char *p_execpath, int p_argc, char *p_argv[], bool p_second_phase);
Error Main::setup2(bool p_show_boot_logo);
```

### UzEngine Implementation

**File**: `runtime/engine/application/uz_application.h`

```cpp
/// Top-level application abstraction.
/// Separates core initialization (configs, singletons) from rendering setup.
class Application {
public:
    static Application* Create();
    static Application* Get() { return instance_; }

    /// Phase 1: Initialize core services (ProjectSettings, Engine, Input, etc.)
    /// No rendering context created yet. Can be called in headless mode.
    uz::Result<void> Setup(
        const char* exe_path,
        int argc,
        char* argv[],
        bool phase2_deferred = true
    );

    /// Phase 2: Initialize rendering and display.
    /// Called after Setup() completes. Initializes RenderingServer, DisplayServer, etc.
    uz::Result<void> Setup2();

    /// Run one frame: input -> physics -> logic -> render sync.
    bool RunFrame(double dt);

    /// Graceful shutdown (reverse-order cleanup).
    void Shutdown();

    /// Query engine state
    bool IsRunning() const { return running_; }
    bool IsEditor() const { return engine_->IsEditorHint(); }

private:
    Application() = default;
    static inline Application* instance_ = nullptr;

    bool setup1_complete_ = false;
    bool setup2_complete_ = false;
    bool running_ = false;

    uz::Ref<Engine> engine_;
    uz::Ref<ProjectSettings> project_settings_;
    uz::Ref<DisplayServer> display_server_;
    uz::Ref<RenderingServer> rendering_server_;
    uz::Ref<RenderingDevice> rendering_device_;
    uz::Ref<PhysicsWorld> physics_world_;
    uz::Ref<SceneTree> scene_tree_;
    uz::Ref<Input> input_;
    uz::Ref<LuaVM> lua_vm_;
};
```

**File**: `runtime/engine/application/uz_application.cpp`

```cpp
#include "uz_application.h"
#include "core/engine.h"
#include "core/project_settings.h"
#include "core/input.h"
#include "renderer/rendering_server.h"
#include "scene/scene_tree.h"
#include "scripting/lua_vm.h"

uz::Result<void> Application::Setup(
    const char* exe_path,
    int argc,
    char* argv[],
    bool phase2_deferred
) {
    UZ_LOG(Engine, Info, "Setup Phase 1: Core initialization");

    // Step 1: Initialize Engine singleton (owns all server lifetimes)
    engine_ = Engine::Create();
    UZ_CHECK(engine_, "Failed to create Engine");

    // Step 2: Parse command line and load ProjectSettings
    project_settings_ = ProjectSettings::Create();
    UZ_TRY(project_settings_->Load(exe_path, argc, argv));

    // Step 3: Initialize core subsystems (Input, LuaVM, etc.)
    input_ = Input::Create();
    lua_vm_ = LuaVM::Create();
    UZ_TRY(lua_vm_->Initialize());

    setup1_complete_ = true;
    UZ_LOG(Engine, Info, "Setup Phase 1: Complete");

    if (phase2_deferred) {
        return Setup2();
    }
    return uz::Ok();
}

uz::Result<void> Application::Setup2() {
    UZ_LOG(Engine, Info, "Setup Phase 2: Rendering & display initialization");
    UZ_CHECK(setup1_complete_, "Setup() must be called before Setup2()");

    // Step 1: Create DisplayServer (OS window, input integration)
    display_server_ = DisplayServer::Create(
        project_settings_->Get<std::string>("display/window/title", "UzEngine"),
        project_settings_->Get<int>("display/window/width", 1280),
        project_settings_->Get<int>("display/window/height", 720)
    );
    UZ_TRY(engine_->RegisterSingleton("DisplayServer", display_server_.Get()));

    // Step 2: Create RenderingDevice (Vulkan/DX12 context)
    auto rhi_backend = project_settings_->Get<std::string>("rendering/rhi_backend", "dx12");
    rendering_device_ = RenderingDevice::Create(rhi_backend, display_server_->GetWindowHandle());
    UZ_TRY(engine_->RegisterSingleton("RenderingDevice", rendering_device_.Get()));

    // Step 3: Create RenderingServer (deferred API)
    rendering_server_ = RenderingServer::Create(rendering_device_.Get());
    UZ_TRY(engine_->RegisterSingleton("RenderingServer", rendering_server_.Get()));

    // Step 4: Create PhysicsWorld (Jolt or other)
    physics_world_ = PhysicsWorld::Create(
        project_settings_->Get<int>("physics/gravity", 9.81)
    );
    UZ_TRY(engine_->RegisterSingleton("PhysicsWorld", physics_world_.Get()));

    // Step 5: Create SceneTree (root of node hierarchy)
    scene_tree_ = SceneTree::Create();
    UZ_TRY(engine_->RegisterSingleton("SceneTree", scene_tree_.Get()));

    // Step 6: Load initial scene from ProjectSettings
    auto main_scene = project_settings_->Get<std::string>("scene/main_scene", "");
    if (!main_scene.empty()) {
        UZ_TRY(scene_tree_->Load(main_scene));
    }

    // Step 7: Initialize render thread (if separate thread enabled)
    bool separate_render_thread = project_settings_->Get<bool>(
        "rendering/separate_render_thread", false
    );
    if (separate_render_thread) {
        rendering_server_->StartRenderThread();
    }

    setup2_complete_ = true;
    running_ = true;
    UZ_LOG(Engine, Info, "Setup Phase 2: Complete");

    return uz::Ok();
}

bool Application::RunFrame(double dt) {
    if (!running_) return false;

    UZ_PROFILE_SCOPE("Application::RunFrame");

    // Step 1: Input (dispatch OS events)
    input_->Poll();

    // Step 2: Physics (fixed timestep)
    static double physics_accumulator = 0.0;
    physics_accumulator += dt;
    while (physics_accumulator >= 1.0 / 60.0) {
        physics_world_->Step(1.0 / 60.0);
        physics_accumulator -= 1.0 / 60.0;
    }

    // Step 3: Logic (update scene)
    scene_tree_->Process(dt);

    // Step 4: Render sync (wait for render thread to drain queue)
    rendering_server_->Sync();

    return !input_->IsQuitRequested();
}

void Application::Shutdown() {
    UZ_LOG(Engine, Info, "Shutdown: Starting");

    running_ = false;

    // Reverse-order cleanup
    if (scene_tree_) scene_tree_->Unload();
    if (rendering_server_) rendering_server_->Shutdown();
    if (rendering_device_) rendering_device_->Shutdown();
    if (display_server_) display_server_->Shutdown();
    if (physics_world_) physics_world_->Shutdown();
    if (lua_vm_) lua_vm_->Shutdown();
    if (input_) input_->Shutdown();
    if (engine_) engine_->Shutdown();

    UZ_LOG(Engine, Info, "Shutdown: Complete");
}
```

---

## Pattern 2: Engine Singleton Registry

### Godot Original

```cpp
// E:\godot\core\config\engine.h
class Engine {
private:
    List<Singleton> singletons;
    HashMap<StringName, Object *> singleton_ptrs;

public:
    Object *get_singleton(const StringName &p_name) const;
};

// Usage: Engine::get_singleton("RenderingServer")
```

### UzEngine Implementation

**File**: `runtime/engine/core/engine.h`

```cpp
/// Central singleton registry and lifecycle heartbeat.
/// All engine servers (RenderingServer, PhysicsWorld, etc.) are registered here.
class Engine : public RefCounted {
public:
    static uz::Ref<Engine> Create();

    /// Register a singleton (called during Setup phase)
    void RegisterSingleton(std::string_view name, void* ptr, const std::type_info& type_info) {
        auto key = std::string(name);
        singletons_[key] = ptr;
        singleton_types_[key] = &type_info;
    }

    /// Retrieve singleton by name (type-unsafe, returns void*)
    void* GetSingleton(std::string_view name) const {
        auto it = singletons_.find(std::string(name));
        return it != singletons_.end() ? it->second : nullptr;
    }

    /// Retrieve singleton with type check (safe)
    template <typename T>
    T* GetSingleton(std::string_view name) const {
        auto it = singletons_.find(std::string(name));
        if (it == singletons_.end()) return nullptr;

        // Runtime type check (compare type_info)
        auto type_it = singleton_types_.find(std::string(name));
        if (type_it != singleton_types_.end() && *type_it->second != typeid(T)) {
            UZ_LOG(Engine, Warn, "GetSingleton<{}>('{}') type mismatch", typeid(T).name(), name);
            return nullptr;
        }

        return static_cast<T*>(it->second);
    }

    /// Frame state (updated by Application::RunFrame)
    uint64_t GetFramesDrawn() const { return frames_drawn_; }
    double GetDeltaTime() const { return delta_time_; }
    bool IsInPhysicsFrame() const { return in_physics_; }
    void UpdateFrameState(double dt, bool is_physics) {
        if (!in_physics_ && is_physics) {
            frames_drawn_++;
        }
        delta_time_ = dt;
        in_physics_ = is_physics;
    }

    /// Hints for editor/tool mode
    void SetEditorHint(bool hint) { editor_hint_ = hint; }
    bool IsEditorHint() const { return editor_hint_; }

    void Shutdown();

private:
    Engine() = default;

    std::unordered_map<std::string, void*> singletons_;
    std::unordered_map<std::string, const std::type_info*> singleton_types_;

    uint64_t frames_drawn_ = 0;
    double delta_time_ = 0.0;
    bool in_physics_ = false;
    bool editor_hint_ = false;

    friend class Application;
};
```

---

## Pattern 3: ProjectSettings — Configuration Spine

### Godot Original

```cpp
// Load from project.godot INI
ProjectSettings::get_singleton()->setup(project_path, main_pack);

// Access in code
int shadow_size = GLOBAL_GET("lights_and_shadows/positional_shadow/atlas_size");
```

### UzEngine Implementation

**File**: `runtime/engine/core/project_settings.h`

```cpp
/// Engine-wide configuration loaded from project file.
/// All rendering parameters (shadow atlas, G-Buffer format, etc.) are here.
class ProjectSettings : public RefCounted {
public:
    static uz::Ref<ProjectSettings> Create();

    /// Load project file from path
    uz::Result<void> Load(const char* exe_path, int argc, char* argv[]);

    /// Get setting with fallback
    template <typename T>
    T Get(std::string_view key, const T& default_value) const {
        auto it = settings_.find(std::string(key));
        if (it == settings_.end()) return default_value;

        try {
            return std::get<T>(it->second);
        } catch (const std::bad_variant_access&) {
            UZ_LOG(ProjectSettings, Warn, "Type mismatch for key '{}'", key);
            return default_value;
        }
    }

    // Common accessors (for convenience)
    std::string GetMainScene() const {
        return Get<std::string>("scene/main_scene", "");
    }

    uz::RhiFormat GetGBufferAlbedoFormat() const {
        auto fmt_str = Get<std::string>("rendering/g_buffer_albedo_format", "rgba8");
        // Parse format string to RhiFormat enum
        return uz::RhiFormat::RGBA8Unorm;  // placeholder
    }

    int GetShadowAtlasSize() const {
        return Get<int>("rendering/shadow_atlas_size", 4096);
    }

    bool IsBindlessEnabled() const {
        return Get<bool>("rendering/bindless_enabled", false);
    }

private:
    ProjectSettings() = default;

    // Variant-like storage (or use std::any)
    std::unordered_map<std::string, std::variant<
        bool, int, float, double, std::string
    >> settings_;

    friend class Application;
};
```

**Example `project.uz` (INI-like)**:

```ini
[scene]
main_scene = "res://scenes/main.uzscene"

[rendering]
g_buffer_albedo_format = "rgba8"
g_buffer_normal_format = "rg11b10f"
shadow_atlas_size = 4096
bindless_enabled = false

[physics]
gravity = -9.81
max_bodies = 10000

[script]
lua_sandbox_enabled = true
```

---

## Pattern 4: RenderingServer Deferred API

### Godot Original

```cpp
// Game thread enqueues commands (returns immediately)
RID texture = RenderingServer::get_singleton()->texture_2d_create(image);
RenderingServer::get_singleton()->mesh_set_surface_override_material(mesh, surface_idx, material);

// Render thread executes commands asynchronously
RenderingServer::get_singleton()->sync();  // Wait for queue to drain
```

### UzEngine Implementation

**File**: `runtime/engine/renderer/rendering_server.h`

```cpp
/// Deferred rendering API (lock-free, thread-safe).
/// Game thread enqueues commands; render thread executes them.
class RenderingServer : public RefCounted {
public:
    /// Opaque resource ID (similar to RID in Godot)
    using ResourceId = uint64_t;

    static uz::Ref<RenderingServer> Create(RenderingDevice* rd);

    // ───────────────────────────────────────────────────────────────
    // Texture API (deferred)
    // ───────────────────────────────────────────────────────────────

    /// Create texture (returns handle immediately, upload deferred)
    ResourceId Texture2DCreate(const uz::Image& image);

    /// Update texture (enqueues command, returns immediately)
    void Texture2DUpdate(ResourceId texture, const uz::Image& image, int layer = 0);

    // ───────────────────────────────────────────────────────────────
    // Mesh API (deferred)
    // ───────────────────────────────────────────────────────────────

    ResourceId MeshCreate(
        const std::vector<Vec3>& vertices,
        const std::vector<uint32_t>& indices
    );

    void MeshSetSurface(
        ResourceId mesh,
        int surface_idx,
        const uz::Ref<Mesh>& surface_mesh
    );

    // ───────────────────────────────────────────────────────────────
    // Material API (deferred)
    // ───────────────────────────────────────────────────────────────

    ResourceId MaterialCreate(const uz::Ref<Shader>& shader);

    void MaterialSetParam(
        ResourceId material,
        std::string_view param_name,
        const Variant& value
    );

    // ───────────────────────────────────────────────────────────────
    // Instance API (render-list building)
    // ───────────────────────────────────────────────────────────────

    /// Create instance (adds to render list)
    ResourceId InstanceCreate();

    /// Set instance transform (deferred)
    void InstanceSetTransform(ResourceId instance, const uz::Transform& xform);

    /// Set instance material (deferred)
    void InstanceSetMaterial(ResourceId instance, ResourceId material);

    /// Set instance visibility (deferred)
    void InstanceSetVisible(ResourceId instance, bool visible);

    // ───────────────────────────────────────────────────────────────
    // Synchronization
    // ───────────────────────────────────────────────────────────────

    /// Submit all enqueued commands and wait for render thread to drain
    void Sync();

    /// Check if current thread is render thread
    bool IsOnRenderThread() const;

    /// Start separate render thread (async rendering)
    void StartRenderThread();

    void Shutdown();

private:
    RenderingServer(RenderingDevice* rd);

    RenderingDevice* rendering_device_ = nullptr;
    std::thread render_thread_;

    // Lock-free command queue (game thread appends, render thread consumes)
    moodycamel::ConcurrentQueue<Command> command_queue_;

    struct Command {
        enum Type { TEXTURE_CREATE, TEXTURE_UPDATE, MESH_CREATE, ... } type;
        std::function<void()> execute;  // Deferred closure
    };

    void RenderThreadMain_();  // Runs on render thread

    friend class Application;
};
```

**File**: `runtime/engine/renderer/rendering_server.cpp`

```cpp
void RenderingServer::Texture2DUpdate(ResourceId texture, const uz::Image& image, int layer) {
    // Capture by value (safe for deferred execution)
    command_queue_.enqueue(Command{
        .type = Command::TEXTURE_UPDATE,
        .execute = [this, texture, image_copy = image, layer]() {
            // Executed on render thread
            UZ_PROFILE_SCOPE("RenderingServer::Texture2DUpdate");
            rendering_device_->UploadTexture(texture, image_copy, layer);
        }
    });
}

void RenderingServer::RenderThreadMain_() {
    // Render thread main loop
    while (running_) {
        Command cmd;
        while (command_queue_.try_dequeue(cmd)) {
            cmd.execute();  // Execute GPU command
        }

        // Render frame
        rendering_device_->Present();

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void RenderingServer::Sync() {
    // Wait for command queue to be empty
    while (!command_queue_.size_approx() == 0) {
        std::this_thread::yield();
    }

    // Optional: explicit GPU sync (vkDeviceWaitIdle / d3d12 sync)
}
```

---

## Pattern 5: SceneTree Game Loop

### Godot Original

```cpp
// E:\godot\scene\main\scene_tree.h
class SceneTree : public MainLoop {
    Window *root = nullptr;
    bool process(double p_time) override;
};

// Each frame:
// 1. Input phase
// 2. Physics phase (fixed timestep)
// 3. Logic phase (OnProcess)
// 4. Render phase (RenderingServer::Sync)
```

### UzEngine Implementation

**File**: `runtime/engine/scene/scene_tree.h`

```cpp
/// Root of scene hierarchy and main game loop.
class SceneTree : public RefCounted {
public:
    static uz::Ref<SceneTree> Create();

    /// Load scene from file
    uz::Result<void> Load(std::string_view path);

    /// Process one frame
    void Process(double delta_time);

    /// Scene hierarchy access
    uz::Ref<Node> GetRoot() const { return root_; }
    uz::Ref<Node> FindNode(std::string_view name) const;

    void Unload();

private:
    SceneTree() = default;

    uz::Ref<Node> root_;
    std::vector<ProcessGroup> process_groups_;

    double physics_accumulator_ = 0.0;
    int physics_ticks_per_second_ = 60;
    bool paused_ = false;

    void InputPhase_();
    void PhysicsPhase_(double dt);
    void LogicPhase_(double dt);

    friend class Application;
};
```

**File**: `runtime/engine/scene/node.h`

```cpp
/// Base node class (game developers extend this for game logic).
class Node : public RefCounted {
public:
    virtual ~Node() = default;

    /// Called when node enters scene tree
    virtual void OnReady() {}

    /// Called every frame
    virtual void OnProcess(double delta) {}

    /// Called every physics tick (fixed timestep)
    virtual void OnPhysicsProcess(double delta) {}

    /// Called when input event occurs
    virtual void OnInput(const InputEvent& event) {}

    // Scene hierarchy
    void AddChild(uz::Ref<Node> child);
    std::vector<uz::Ref<Node>>& GetChildren() { return children_; }

    // Transform
    virtual Transform GetTransform() const { return {}; }
    virtual void SetTransform(const Transform& xform) {}

    // Enabled/disabled
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

protected:
    bool enabled_ = true;
    std::vector<uz::Ref<Node>> children_;
};

/// 3D node (has transform)
class Node3D : public Node {
public:
    Transform GetTransform() const override { return local_transform_; }
    void SetTransform(const Transform& xform) override {
        local_transform_ = xform;
        // Notify RenderingServer of transform change (deferred)
        if (instance_rid_ != 0) {
            RenderingServer::Get()->InstanceSetTransform(instance_rid_, xform);
        }
    }

private:
    Transform local_transform_;
    uint64_t instance_rid_ = 0;  // RenderingServer instance handle
};
```

**File**: `runtime/engine/scene/scene_tree.cpp`

```cpp
void SceneTree::Process(double delta_time) {
    UZ_PROFILE_SCOPE("SceneTree::Process");

    if (paused_) return;

    // Step 1: Input
    InputPhase_();

    // Step 2: Physics (fixed timestep)
    PhysicsPhase_(delta_time);

    // Step 3: Logic
    LogicPhase_(delta_time);
}

void SceneTree::PhysicsPhase_(double dt) {
    physics_accumulator_ += dt;
    double physics_step = 1.0 / physics_ticks_per_second_;

    while (physics_accumulator_ >= physics_step) {
        // Walk tree, call OnPhysicsProcess
        WalkTree_([physics_step](uz::Ref<Node>& node) {
            if (node->IsEnabled()) {
                node->OnPhysicsProcess(physics_step);
            }
        });

        physics_accumulator_ -= physics_step;
    }
}

void SceneTree::LogicPhase_(double dt) {
    // Walk tree, call OnProcess
    WalkTree_([dt](uz::Ref<Node>& node) {
        if (node->IsEnabled()) {
            node->OnProcess(dt);
        }
    });
}

void SceneTree::WalkTree_(std::function<void(uz::Ref<Node>&)> callback) {
    std::function<void(uz::Ref<Node>&)> walk = [&](uz::Ref<Node>& node) {
        callback(node);
        for (auto& child : node->GetChildren()) {
            walk(child);
        }
    };

    if (root_) {
        walk(root_);
    }
}
```

---

## Pattern 6: Game Code Example (Minimal Boilerplate)

### Godot Example

```gdscript
# Game developer writes ONLY game logic
extends Node3D

@onready var mesh: MeshInstance3D = $Mesh
@onready var light: DirectionalLight3D = $Light

func _ready():
    mesh.rotation.y = 0

func _process(delta):
    mesh.rotation.y += delta
```

### UzEngine Example (C++23)

**File**: `game/rotation_demo.h`

```cpp
/// Game developer implements one class (OnReady, OnProcess, OnPhysicsProcess).
/// No GPU code, no rendering awareness.
class RotationDemoNode : public uz::Node3D {
public:
    uz::Ref<uz::MeshRenderer> mesh_renderer;
    uz::Ref<uz::Light> light;

    void OnReady() override {
        // Initialize game logic
        mesh_renderer = GetComponent<uz::MeshRenderer>("MeshRenderer");
        light = GetComponent<uz::Light>("Light");

        mesh_renderer->SetMesh(LoadMesh("res://models/cube.uzmodel"));
    }

    void OnProcess(double delta) override {
        // Update game state
        auto xform = GetTransform();
        xform.rotation.y += delta;
        SetTransform(xform);
        // ↑ RenderingServer is updated automatically (deferred command)
    }
};
```

### UzEngine Example (Lua)

**File**: `game/rotation_demo.lua`

```lua
-- Game developer writes even less boilerplate in Lua
local RotationDemo = {}

function RotationDemo:OnReady()
    self.rotation = 0
end

function RotationDemo:OnProcess(delta)
    self.rotation = self.rotation + delta
    uz.scene.set_rotation(self.entity_id, 0, self.rotation, 0)
end

return RotationDemo
```

---

## Pattern 7: Component Architecture

### Godot Components (Nodes)

```gdscript
# In scene file (res://scenes/player.tscn):
PlayerNode (Node3D)
├── MeshInstance3D (rendered)
├── CollisionShape3D (physics)
├── Camera3D (viewport)
└── PlayerController.gd (game logic)
```

Each node is "pure rendering" or "pure physics" — no mixing.

### UzEngine Components

**File**: `runtime/engine/scene/components.h`

```cpp
/// Component base class (all rendering/physics happens through components).
class Component : public RefCounted {
public:
    virtual ~Component() = default;
    virtual void OnAttach() {}
    virtual void OnDetach() {}
};

/// Rendering component (game code never implements this)
class MeshRenderer : public Component {
private:
    uz::RenderingServer::ResourceId instance_rid_;

public:
    void SetMesh(uz::Ref<Mesh> mesh) {
        RenderingServer::Get()->InstanceSetMesh(instance_rid_, mesh->GetRID());
    }

    void OnAttach() override {
        instance_rid_ = RenderingServer::Get()->InstanceCreate();
    }

    void OnDetach() override {
        RenderingServer::Get()->InstanceDestroy(instance_rid_);
    }
};

/// Physics component (auto-integrated with Jolt)
class RigidBody3D : public Component {
private:
    uz::PhysicsWorld::BodyId body_id_;

public:
    void SetMass(float mass) {
        PhysicsWorld::Get()->SetBodyMass(body_id_, mass);
    }
};

/// Node can attach multiple components
class Node3D : public Node {
public:
    void AttachComponent(std::string_view type_name, uz::Ref<Component> component) {
        components_[std::string(type_name)] = component;
        component->OnAttach();
    }

    template <typename T>
    uz::Ref<T> GetComponent(std::string_view type_name) const {
        auto it = components_.find(std::string(type_name));
        if (it != components_.end()) {
            return uz::Cast<T>(it->second);
        }
        return nullptr;
    }

private:
    std::unordered_map<std::string, uz::Ref<Component>> components_;
};
```

---

## Pattern 8: GDExtension C ABI (Plugin System)

### Godot Original

```cpp
// plugin.cpp
extern "C" {
    GDExtensionBool gdextension_init(
        GDExtensionInterface *p_interface,
        GDExtensionClassLibraryPtr p_library,
        GDExtensionInitialization *r_init
    ) {
        // Receives stable vtable (p_interface)
        // Never includes Engine.h or links libgodot.a
        // Cannot access render thread internals
    }
}
```

### UzEngine Adaptation

**File**: `runtime/engine/plugin/uz_plugin.h`

```cpp
/// Stable C ABI for game plugins (like GDExtension).
/// Plugins receive interface at runtime; they never link engine.
struct uz_plugin_interface_t {
    // Logging
    void (*log_info)(const char* category, const char* fmt, ...);
    void (*log_error)(const char* category, const char* fmt, ...);

    // Scene queries (safe, high-level)
    uz_entity_t (*scene_find_by_name)(const char* name);
    void (*scene_instantiate)(const char* scene_path, float x, float y, float z);

    // Component access (safe, deferred)
    void (*component_set_enabled)(uz_entity_t entity, const char* type, bool enabled);

    // Physics queries (safe, no render thread access)
    typedef struct {
        bool hit;
        float distance;
        float x, y, z;  // Impact position
    } uz_raycast_result_t;

    uz_raycast_result_t (*physics_raycast)(
        float ox, float oy, float oz,
        float dx, float dy, float dz,
        float max_dist
    );

    // Lua scripting
    void (*lua_call)(const char* function_name, ...);
};

/// Plugin entry point (single exported C symbol)
extern "C" {
    typedef int (*uz_plugin_init_fn_t)(uz_plugin_interface_t* interface);

    // Plugin must export this
    int uz_plugin_init(uz_plugin_interface_t* interface);
}
```

**Example Plugin**:

```cpp
// game_logic_plugin.cpp (can be compiled separately)
#include "uz_plugin.h"

int uz_plugin_init(uz_plugin_interface_t* interface) {
    // Plugin only receives interface; never includes full Engine

    interface->log_info("Game", "Plugin initialized");

    // Safe operations only (no render thread access)
    uz_entity_t player = interface->scene_find_by_name("Player");
    interface->component_set_enabled(player, "MeshRenderer", true);

    // Raycast (physics query)
    uz_raycast_result_t result = interface->physics_raycast(
        0, 0, 0,      // Origin
        1, 0, 0,      // Direction
        100.0         // Max distance
    );

    if (result.hit) {
        interface->log_info("Game", "Hit at (%.1f, %.1f, %.1f)", result.x, result.y, result.z);
    }

    return 0;  // UZ_PLUGIN_OK
}
```

---

## Key Implementation Order for UzEngine

1. **Engine singleton** (owns all server lifetimes)
2. **ProjectSettings** (configuration spine)
3. **DisplayServer** (OS window)
4. **RenderingDevice** (GPU context, deferred API in RenderingServer)
5. **RenderingServer** (deferred commands)
6. **PhysicsWorld** (Jolt)
7. **SceneTree** (node hierarchy root)
8. **Load main scene** (from ProjectSettings)
9. **Main loop** (Application::RunFrame every frame)

---

## References

- **Godot 4 source**: `E:\godot\main\main.cpp`, `E:\godot\scene\main\scene_tree.h`
- **UzEngine specs**: See `/docs/specs/` for complete subsystem specifications
- **RHI abstraction**: `/docs/architecture/rhi-abstraction.md` (RenderingServer depends on this)
