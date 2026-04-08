# Rendering — Render World & Sync

> **Status**: Done (v0.3.0+)
> **Source**: `runtime/engine/renderer/`
> **Depends on**: Entity/Component (§10), RHI (§03)
> **References**: UE5 `FScene` + `FPrimitiveSceneProxy`, Godot `RenderingServer` + RID pattern

## Purpose

The Render World is a parallel, GPU-optimized representation of the game world. Game code describes **what** to render (components), the Render World decides **how** (draw calls, batching, culling). The two worlds are decoupled by a single sync point per frame.

## Why Dual Worlds

If game code touches render data directly:
- **Thread safety impossible** — game tick and render submission fight over the same data
- **Cache layout wrong** — components are entity-centric, rendering needs material-centric sorting
- **Culling requires spatial structures** that game code doesn't maintain

## Architecture

```
Game Thread                              Render Thread (future — single-thread MVP)
───────────────────────────              ──────────────────────────────────────────
World                                    RenderWorld
  │                                        │
  │ PrimitiveComponent (game data)         │ RenderProxy (GPU-ready data)
  │   - ResourceHandle<Mesh>               │   - GPU vertex/index buffers
  │   - ResourceHandle<Material>           │   - PSO handle
  │   - visible, cast_shadows              │   - world matrix (copied from sync)
  │                                        │
  │ CameraComponent                        │ ViewState
  │   - fov, near, far, is_active          │   - view/proj matrices
  │                                        │   - frustum planes
  │                                        │
  │◄──── Sync Point ─────────────────────►│
  │   1. Added primitives → CreateProxy    │
  │   2. Dirty transforms → SetTransform   │
  │   3. Changed props → SetMaterial/etc   │
  │   4. Removed primitives → DestroyProxy │
  │   5. Active camera → SetViewState      │
```

## RenderProxy — POD Struct

Unlike UE5's `FPrimitiveSceneProxy` (vtable + per-proxy heap allocation), our proxy is a **flat POD struct** in a dense `HandleMap`:

```cpp
struct RenderProxy {
    RenderProxyHandle handle;
    RenderProxyType   type;

    // Transform
    Mat4   world_matrix;
    Sphere bounding_sphere;

    // Visibility
    bool     visible;
    bool     cast_shadows;
    uint32_t render_layer;

    // Material
    ResourceHandle<Material> material;
    MaterialParamBlock       param_overrides;

    // LOD
    uint8_t  current_lod;
    float    screen_size;

    // Type-specific data (union)
    union {
        struct { ResourceHandle<Mesh> mesh; uint32_t instance_batch_key; } static_mesh;
        struct { ResourceHandle<Mesh> mesh; JointPalette* joint_palette; } skeletal_mesh;
        struct { float intensity; Color color; float range; float inner_angle; float outer_angle; bool shadow_enabled; } light;
    };
};
```

**Why POD instead of virtual?** For 10k proxies, vtable pointer chasing during visibility is expensive. Dense array + linear iteration = CPU prefetcher handles it. Type-specific behavior lives in the draw call generator (`switch` on `type`), not per-proxy virtual dispatch.

## RenderWorld API

```cpp
class RenderWorld {
public:
    // Proxy lifecycle (called during sync)
    RenderProxyHandle CreateProxy(const RenderProxyDesc& desc);
    void              DestroyProxy(RenderProxyHandle handle);

    // Proxy updates (called during sync)
    void SetTransform(RenderProxyHandle h, const Mat4& world, const Sphere& bounds);
    void SetVisible(RenderProxyHandle h, bool visible);
    void SetMaterial(RenderProxyHandle h, ResourceHandle<Material> material);
    void SetParamOverrides(RenderProxyHandle h, const MaterialParamBlock& params);

    // View state (called during sync)
    void SetActiveView(const ViewState& view);

    // Rendering pipeline
    void CullVisibility();
    void GenerateDrawCalls(DrawCallList& opaque, DrawCallList& transparent);
    void GatherShadowCasters(RenderProxyHandle light, DrawCallList& out);
};
```

## Sync Point — The Bridge

`RenderWorldSubsystem::Sync()` runs at the end of every game tick, before rendering. This is the **only place** where game data touches render data:

1. **New primitives** → `CreateProxy()`
2. **Dirty transforms** → `SetTransform()` for all SceneComponents with dirty flags
3. **Changed properties** → `SetMaterial()`, `SetVisible()`, etc.
4. **Removed primitives** → `DestroyProxy()`
5. **Active camera** → `SetActiveView()` with view/projection matrices

**Key invariant**: After `Sync()`, the `RenderWorld` is a complete, self-contained snapshot. The renderer reads it without any reference back to the game `World`.

## Material Parameter Override

Game code changes material properties per-instance without creating new Material assets:

```cpp
auto& mesh = GetWorld().Get<StaticMeshComponent>(GetPawn());
mesh.SetParameter("base_color", Vec4(1, 0, 0, 1));  // per-instance tint
mesh.SetParameter("dissolve_amount", progress);       // animate effect
mesh.material = GetResources().Load<Material>("burning.uzmat");  // swap material
```

Internally, `SetParameter()` writes to a `MaterialParamBlock` on the component. The sync point copies it to the proxy. The renderer binds it as a per-instance constant buffer.

## Render Layers

Bitmask-based visibility filtering:

```cpp
namespace RenderLayer {
    constexpr uint32_t Default     = 1 << 0;
    constexpr uint32_t UI3D        = 1 << 1;
    constexpr uint32_t Minimap     = 1 << 2;
    constexpr uint32_t FirstPerson = 1 << 3;
    constexpr uint32_t DebugDraw   = 1 << 5;  // stripped in shipping
}

camera.layer_mask = RenderLayer::Default | RenderLayer::FirstPerson;
```

A proxy is visible to a camera only if `(proxy.render_layer & camera.layer_mask) != 0`.

## Thread Model — MVP to Full

```
MVP (Phase 1-2): Single-Thread
│ Game Tick │ Sync │ Cull │ Draw │ Present │

Phase 3: Dual-Thread (Double-Buffered)
Game Thread: │ Tick N │ Sync N→buf │ Tick N+1 │
Render Thread:          │ Read buf │ Cull │ Draw │ Present │

Phase 4: Triple-Buffered + Job System
Game Thread: │ Tick │ Sync │ Tick │ Sync │
Render Thread:       │ Cull+Draw N-1 │ Cull+Draw N │
Worker Pool:    │ AnimUpdate │ PhysicsSync │
```

Game code API is identical across all models — the engine handles double-buffering internally.

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine** |
|---------|----------|---------|--------------|
| Render proxy | `FPrimitiveSceneProxy` (vtable, heap) | RID → `RenderingServer` | **POD struct in HandleMap** |
| Sync mechanism | `ENQUEUE_RENDER_COMMAND` (lambda queue) | Scattered `RS::instance_set_*()` | **Single sync point (subsystem)** |
| Material override | Dynamic Material Instance | `set_instance_shader_parameter` | **`SetParameter()` → MaterialParamBlock** |
| Render layers | Bitmask (32 layers) | Bitmask (20 layers) | **Bitmask (32 layers)** |
| Thread model | Separate render thread | Threaded RenderingServer | **Single-thread MVP → dual-thread Phase 3** |

## Rules

1. **NEVER access RenderWorld from game code** — only `RenderWorldSubsystem::Sync()` touches it
2. **NEVER store raw pointers to RenderProxies** — use `RenderProxyHandle` (generation-checked)
3. **ALWAYS use `SetParameter()`** for per-instance material changes — never create new Material assets per entity
