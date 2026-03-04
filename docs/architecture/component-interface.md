# Component Interface Design — Architecture Directives

> **Status**: MANDATORY — applies to all component and rendering code from Phase 1 onwards.
> **Enforced by**: Code review, AGENTS.md rule §COMP-INTERFACE.

---

## 1. The Problem This Solves

The classic engine anti-pattern is `switch(component.type)` inside render loops:

```cpp
// ❌ THE ANTI-PATTERN — this code will be found and deleted
for (auto& node : scene.nodes) {
    switch (node.type) {
        case NodeType::StaticMesh:   render_static_mesh(node);    break;
        case NodeType::DirectionalLight: render_dir_light(node);  break;
        case NodeType::PointLight:   render_point_light(node);    break;
        case NodeType::Camera:       /* nothing */                 break;
        // Every new component type requires editing THIS file
        // After 100 component types, this file is unmaintainable
    }
}
```

Every engine starts with 5 node types and ends with 500. If every subsystem
(renderer, physics, audio, animation, scripting) has its own switch on `NodeType`,
the codebase becomes unmaintainable after ~20 node types.

**Evidence of this problem in UzEngine**:
- `src/engine/scene/scene_node.h:52` — `node_type_to_string()` uses switch (acceptable — serialization only)
- `src/engine/scene/scene_node.h:67` — `node_type_from_string()` uses switch (acceptable — serialization only)
- Risk: these patterns will be copied into rendering/physics when those systems add node-type dispatch

---

## 2. The SceneProxy Pattern (Adapted from Unreal Engine 5)

### Core Principle

A **renderable component** does NOT expose its internal type to the renderer.
Instead, it creates a **SceneProxy** — a render-thread-safe snapshot of
its rendering data — that the renderer reads without knowing the source component type.

```
Game Thread                    Render Thread
──────────                     ─────────────
SceneNode (ECS)                RenderGraph
  + StaticMeshComponent   →    UzMeshProxy (implements ISceneProxy)
  + LightComponent        →    UzLightProxy (implements ISceneProxy)
  + ParticleComponent     →    UzParticleProxy (implements ISceneProxy)
                               ↑
                               No switch() needed — virtual dispatch
```

### RULE COMP-1 (ABSOLUTE): No switch(NodeType) in any render path

`switch(node.type)` or `if (node.type == NodeType::X)` is **FORBIDDEN** in:
- Any file in `src/engine/renderer/`
- Any file in `src/engine/renderer/passes/`
- The render graph (`render_graph.cpp`)
- Any system that executes per-frame

```cpp
// ❌ WRONG — switch in renderer
void RenderPass::collect_drawcalls(const Scene& scene) {
    for (auto& node : scene.nodes()) {
        switch (node.type) {
            case NodeType::StaticMesh: submit_mesh(node);   break;  // FORBIDDEN
            case NodeType::Camera:     update_camera(node); break;  // FORBIDDEN
        }
    }
}

// ✅ CORRECT — virtual dispatch via proxy
void RenderPass::collect_drawcalls(const Scene& scene) {
    for (ISceneProxy* proxy : scene.proxies()) {
        proxy->collect_draw_calls(this);  // Each proxy type knows what to submit
    }
}
```

---

## 3. The ISceneProxy Interface

Every component that participates in rendering MUST implement `ISceneProxy`:

```cpp
// src/engine/renderer/scene_proxy.h

namespace uz {

/// Renderer-facing view of a scene component.
/// Created on game thread, consumed on render thread.
/// Proxies are IMMUTABLE during a frame — created/destroyed between frames.
struct ISceneProxy {
    virtual ~ISceneProxy() = default;

    /// Called during render graph setup phase.
    /// Proxy declares what render graph resources it reads/writes.
    virtual void setup(RgPassBuilder& builder) {}

    /// Called during render graph execute phase.
    /// Issues actual draw calls via rhi()->cmd_*().
    virtual void execute(RhiCmdList cmd, const FrameConstants& fc) = 0;

    /// Returns the axis-aligned bounding box for frustum culling.
    /// Return empty AABB to always render (e.g., directional lights).
    [[nodiscard]] virtual uz::Aabb bounds() const = 0;

    /// Returns the render layer flags (opaque, transparent, shadow, etc.)
    [[nodiscard]] virtual uint32_t render_flags() const = 0;

    /// Called when the source component's data changes (game thread, between frames).
    /// The proxy MUST copy all data it needs — no shared pointers to game state.
    virtual void sync_from_component(const void* component_data) = 0;
};

} // namespace uz
```

### Implementation Example

```cpp
// src/engine/renderer/proxies/static_mesh_proxy.h

class StaticMeshProxy : public ISceneProxy {
public:
    void setup(RgPassBuilder& builder) override {
        builder.read(gbuffer_albedo_);
    }

    void execute(RhiCmdList cmd, const FrameConstants& fc) override {
        rhi()->cmd_set_pipeline(cmd, pipeline_);
        rhi()->cmd_set_vertex_buffer(cmd, 0, vertex_buf_, 0, sizeof(MeshVertex));
        rhi()->cmd_draw_indexed(cmd, index_count_, 1, 0, 0, 0);
    }

    uz::Aabb bounds() const override { return bounds_; }
    uint32_t render_flags() const override { return RENDER_LAYER_OPAQUE; }

    void sync_from_component(const void* data) override {
        const StaticMeshData* mesh = static_cast<const StaticMeshData*>(data);
        bounds_ = mesh->bounds;
        // Copy what we need — no reference to game thread data
    }

private:
    RhiBuffer  vertex_buf_  = RHI_NULL;
    RhiBuffer  index_buf_   = RHI_NULL;
    RhiPipeline pipeline_   = RHI_NULL;
    uint32_t   index_count_ = 0;
    uz::Aabb   bounds_      = {};
};
```

---

## 4. Component Registration Without Switch Statements

### RULE COMP-2: Components self-register their proxy factory

Instead of a central registry that switches on `NodeType`, each component type
registers a factory function. The renderer creates proxies WITHOUT knowing the type.

```cpp
// ✅ CORRECT — self-registration pattern

// In static_mesh_component.cpp (runs at startup)
UZENGINE_REGISTER_COMPONENT(StaticMeshComponent, []() -> ISceneProxy* {
    return new StaticMeshProxy();
});

// In the renderer (zero knowledge of specific types)
ISceneProxy* proxy = ComponentRegistry::create_proxy(component.type_id());
```

### RULE COMP-3: NodeType is for serialization ONLY

`NodeType` enum values are ONLY used for:
1. Scene serialization (`.uzscene` files)
2. Scene tree display (editor UI)
3. `node_type_to_string()` / `node_type_from_string()` for IPC

`NodeType` MUST NOT be used to drive rendering decisions.

```cpp
// ✅ CORRECT — NodeType used for serialization
std::string UzSceneSaver::serialize_node(const SceneNode& node) {
    return std::format("type={}", node_type_to_string(node.type));
}

// ❌ WRONG — NodeType drives rendering
if (node.type == NodeType::StaticMesh) {
    // This will never scale
}
```

---

## 5. ECS Component Data vs. Render Proxies

UzEngine uses two complementary systems:

```
Scene Graph (game thread)        Render Thread
────────────────────────         ─────────────
SceneNode (id, name, type)
  │
  ├── ComponentStore →────────► ISceneProxy (render data snapshot)
  │   StaticMeshData              StaticMeshProxy
  │   LightData                   LightProxy
  │   CameraData                  CameraProxy
  │
  └── ECS (EnTT) →─────────────► Game logic systems
      CPhysicsBody                 PhysicsSystem
      CAudioSource                 AudioSystem
```

### RULE COMP-4: Game-thread component data is plain structs

Component data structs in `src/engine/scene/component_data.h` MUST:
- Be plain data structs (no virtual functions, no inheritance)
- Contain only logical data (mesh path, material ID, light color, intensity)
- Never contain GPU handles (`RhiBuffer`, `RhiTexture`, etc.)
- Never contain DX12/Vulkan/Metal types

```cpp
// ✅ CORRECT — plain component data (game thread)
struct StaticMeshData {
    char     mesh_path[256];     // Asset path, resolved by asset system
    char     material_path[256]; // Asset path
    uz::Aabb local_bounds;       // Logical AABB in local space
    bool     cast_shadows = true;
};

// ❌ WRONG — GPU handles in component data
struct StaticMeshData {
    RhiBuffer  vertex_buf_;  // FORBIDDEN — GPU handle in game data
    RhiTexture albedo_tex_;  // FORBIDDEN — GPU handle in game data
};
```

### RULE COMP-5: Proxies own their GPU resources

GPU resources (`RhiBuffer`, `RhiTexture`) are OWNED by the proxy, not the component.
The proxy creates them when it receives asset data, destroys them in its destructor.

---

## 6. The GizmoRegistry — Existing Correct Pattern

`src/renderer/gizmos/gizmo_registry.h` already implements the correct pattern.
New node types add a `GizmoRenderer` without any switch statement changes.

This pattern MUST be used as the template for all future per-type dispatch needs.

```cpp
// ✅ EXISTING CORRECT PATTERN — copy this approach
class GizmoRegistry {
public:
    // Register a gizmo for a node type (done at startup)
    void register_gizmo(NodeType type, std::unique_ptr<GizmoRenderer> gizmo);

    // Draw all gizmos for all nodes — no switch() needed
    void draw_all(RhiCmdList cmd, const FrameConstants& fc,
                  const std::vector<EditorNode>& nodes);
};
```

---

## 7. Lifecycle Hooks Without Switch

All ECS systems MUST dispatch via table/vtable, never via switch:

```cpp
// ❌ WRONG — switch in system update
void PhysicsSystem::update(float dt) {
    for (auto [entity, node] : scene.view<SceneNode>()) {
        switch (node.type) {
            case NodeType::StaticMesh: physics_static_body(node);  break;
            case NodeType::Camera:     /* skip */                   break;
        }
    }
}

// ✅ CORRECT — EnTT component-based dispatch
void PhysicsSystem::update(float dt) {
    // Only entities with CRigidBody are processed — no switch needed
    auto view = registry.view<Transform, CRigidBody>();
    for (auto [entity, transform, body] : view.each()) {
        body.sync_from_jolt(transform);
    }
}
```

---

## 8. Migration Guide for Existing Code

### Files to migrate (Phase 1)

1. **`src/renderer/components/editor_component.h`**:
   - Remove `ID3D12Device*` / `ID3D12GraphicsCommandList*` params
   - Implement as an `ISceneProxy` subclass

2. **`src/renderer/components/camera_component.h`**:
   - Move camera data to `CameraData` in `component_data.h`
   - Implement `CameraProxy` that reads camera data and uploads view/proj matrices

3. **`src/renderer/components/light_component.h`**:
   - Move light data to `LightData` in `component_data.h`
   - Implement `DirectionalLightProxy`, `PointLightProxy`, `SpotLightProxy`

4. **Any file calling `scene_manager.cpp` render dispatch**:
   - Remove all `if (node.type == X)` checks
   - Replace with proxy collection loop

---

## 9. Checklist for New Components

Before committing a new component type:

```
□ Added struct XxxData to src/engine/scene/component_data.h (plain data, no GPU handles)
□ Added XxxProxy in src/engine/renderer/proxies/xxx_proxy.h (implements ISceneProxy)
□ Registered proxy factory via UZENGINE_REGISTER_COMPONENT()
□ No switch(NodeType) added anywhere in renderer
□ No if(node.type == NodeType::Xxx) added in any system
□ Component data contains only logical data (paths, values, flags — no GPU types)
□ Proxy explicitly destroys its GPU resources in destructor
□ Proxy's sync_from_component() copies all needed data (no shared ptrs to game state)
```
