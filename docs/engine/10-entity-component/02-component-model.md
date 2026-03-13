# Entity/Component — Component Model

> **Status**: Done (Phase 3b-3.1b MVP)
> **Source**: `runtime/engine/ecs/component.h` and component subclasses
> **Depends on**: World (§10-01), Memory (`Own<T>`), RenderWorld
> **References**: UE5 `UActorComponent`/`USceneComponent`/`UPrimitiveComponent`, Godot `Node`/`Node3D`/`VisualInstance3D`

## Purpose

UE5-style class hierarchy for type safety and shared behavior, with data-oriented sparse set storage for performance. Physics and rendering are **independent pipelines** — a `SceneComponent` can have physics without rendering, and vice versa.

## Class Hierarchy (Phase 3b-3.1b MVP)

Implemented in Phase 3b-3.1b:

```
Component                              — base: lifecycle callbacks, owner entity
├── ActorComponent                     — no transform, pure logic/data
│   └── (game-defined: InputComponent, MovementComponent, HealthComponent, ...)
│
└── SceneComponent                     — HAS transform, parent/child hierarchy
    ├── CameraComponent                — perspective/ortho, view/projection matrices ✓ IMPLEMENTED
    ├── AudioSourceComponent           — (planned Phase 3b-3.3b)
    ├── AudioListenerComponent         — (planned Phase 3b-3.3b)
    ├── SpringArmComponent             — (planned Phase 3b-3.3b)
    ├── CollisionObject                — (planned Phase 3b-3.2b)
    │   ├── PhysicsBody
    │   │   ├── RigidBody
    │   │   ├── CharacterBody
    │   │   └── StaticBody
    │   ├── TriggerVolume
    │   └── CollisionShape
    ├── NavAgent                       — (planned Phase 4.0)
    │
    └── PrimitiveComponent             — HAS render proxy → RenderWorld (visible) ✓ IMPLEMENTED
        ├── StaticMeshComponent        — static geometry, instancing-friendly ✓ IMPLEMENTED
        ├── SkeletalMeshComponent      — (planned Phase 3b-3.3b)
        ├── LightComponent             — base for all lights ✓ IMPLEMENTED
        │   ├── DirectionalLightComponent ✓ IMPLEMENTED
        │   ├── PointLightComponent    ✓ IMPLEMENTED
        │   └── SpotLightComponent     ✓ IMPLEMENTED
        ├── DecalComponent             — (planned Phase 3b-3.4b)
        └── ParticleSystemComponent    — (planned Phase 3b-3.5b)
```

✓ = Fully implemented in this milestone
(planned Phase X) = Design complete, deferred to later phase

## Design Rules

| Rule | Description |
|------|-------------|
| **Physics ≠ Rendering** | `CollisionObject` inherits from `SceneComponent`, NOT `PrimitiveComponent`. An entity can collide without rendering, and render without colliding. |
| **Movement is ActorComponent** | Like UE5 — `MovementComponent` has no transform. It holds a pointer to an `UpdatedComponent` (a SceneComponent) and modifies that component's transform. |
| **SceneComponent owns hierarchy** | Parent/child attachment, local→world transform propagation, dirty flags — all live in `SceneComponent`. |
| **PrimitiveComponent owns render proxy** | Creates/updates/destroys a proxy in `RenderWorld`. Game code never touches the proxy directly. |
| **One CollisionShape per shape** | Compound collision = multiple `CollisionShape` children under one `PhysicsBody`. |

## Base Classes (Phase 3b-3.1b Implementations)

All base classes are defined in `runtime/engine/ecs/component.h`:

```cpp
/// Base component. Owns lifecycle callbacks and a reference to the owning entity.
/// All components inherit from this — never used directly.
class Component {
public:
    virtual ~Component() = default;

    /// Called when component is added to entity.
    virtual void OnRegister() {}
    /// Called when world is preparing for play (all components registered).
    virtual void OnInitialize() {}
    /// Called when gameplay starts (safe to access other components).
    virtual void OnBeginPlay() {}
    /// Called when gameplay stops.
    virtual void OnEndPlay() {}
    /// Called during world teardown.
    virtual void OnUninitialize() {}
    /// Called when component is removed from entity.
    virtual void OnUnregister() {}

    /// Returns the entity that owns this component.
    Entity GetEntity() const;
    /// Returns the World that contains the owning entity.
    World& GetWorld() const;
};

/// Component without transform. Pure logic and data.
/// Use for: movement controllers, input binding, AI, game state.
class ActorComponent : public Component {
    // No transform. No scene hierarchy participation.
    // Access other components via GetWorld().Get<T>(GetEntity()).
};

/// Component with transform. Participates in parent/child scene hierarchy.
/// Local transform is relative to parent. World transform is cached + lazily recomputed.
/// Defined in `runtime/engine/ecs/scene_component.h`.
class SceneComponent : public Component {
public:
    Vec3 position = {0, 0, 0};
    Quat rotation = Quat::Identity();
    Vec3 scale    = {1, 1, 1};

    /// Attach this component to a parent, making this a child.
    void AttachTo(SceneComponent& parent);
    /// Detach from parent (becomes root).
    void Detach();

    /// Cached world-space matrix. Recomputed when dirty flag is set.
    const Mat4& GetWorldMatrix() const;

    /// Mark transform as dirty. Propagates down to all children.
    void MarkDirty();

    /// Get immediate parent (nullptr if root).
    SceneComponent* GetParent() const;
    /// Get all children.
    const std::vector<SceneComponent*>& GetChildren() const;

private:
    Mat4 world_matrix_;
    bool world_dirty_ = true;
    SceneComponent* parent_ = nullptr;
    std::vector<SceneComponent*> children_;
};

/// Component that is visible in the scene. Creates a render proxy in RenderWorld.
/// Defined in `runtime/engine/ecs/primitive_component.h`.
class PrimitiveComponent : public SceneComponent {
public:
    bool visible = true;
    bool cast_shadows = true;
    uint32_t render_layer = 0;   // bitmask — camera filters by layer

    /// Get the render proxy handle for this component.
    RenderProxyHandle GetRenderProxy() const;

protected:
    /// Subclasses override to create their specific proxy type.
    /// Called automatically when the component is registered.
    virtual RenderProxyHandle CreateRenderProxy() = 0;

private:
    RenderProxyHandle proxy_ = INVALID_HANDLE;  // managed by RenderWorld
};
```

## Component Lifecycle

Inspired by UE5's strict ordering guarantee (all components init before any BeginPlay):

```
1. OnRegister        — component added to entity (safe in editor, no gameplay)
2. OnInitialize      — world is preparing for play (all components registered)
3. OnBeginPlay       — world is live, gameplay starts
4. OnTick            — per-frame update (if tick enabled)
5. OnPhysicsTick     — per-physics-step (if physics tick enabled)
6. OnEndPlay         — gameplay stopping
7. OnUninitialize    — world teardown
8. OnUnregister      — component removed from entity
```

**Ordering guarantee:** ALL components call `OnInitialize` before ANY component calls `OnBeginPlay`. This lets components depend on each other during init.

## Entity Assembly Example

```
Player (entity)
├── SceneComponent (root)                    — transform root
│   ├── SkeletalMeshComponent                — PrimitiveComponent → RenderWorld
│   ├── CameraComponent                      — SceneComponent → view/projection
│   │   └── SpringArmComponent               — SceneComponent → collision avoidance
│   ├── CollisionShape (capsule)             — SceneComponent → PhysicsWorld
│   │   └── CharacterBody                    — SceneComponent → PhysicsWorld (kinematic)
│   └── AudioListenerComponent               — SceneComponent → AudioEngine
├── CharacterMovementComponent               — ActorComponent (moves the CharacterBody)
├── InputComponent                           — ActorComponent (binds actions)
└── HealthComponent                          — ActorComponent (game data)
```

## Concrete Components (Phase 3b-3.1b Implementations)

### StaticMeshComponent — Mesh Rendering

Defined in `runtime/engine/ecs/static_mesh_component.h`.

```cpp
/// Static mesh + material for rendering.
/// Instancing-friendly — identical mesh+material combos auto-batch.
class StaticMeshComponent : public PrimitiveComponent {
public:
    uint32_t mesh_id = INVALID_ID;        // Handle to mesh geometry
    uint32_t material_id = INVALID_ID;    // Handle to PBR material

    RenderProxyHandle CreateRenderProxy() override;
};
```

### CameraComponent — View/Projection

Defined in `runtime/engine/ecs/camera_component.h`.

```cpp
/// Perspective or orthographic camera. Only one active camera per World.
/// World queries for the active camera to render the view.
class CameraComponent : public SceneComponent {
public:
    float fov = 60.0f;                // Field of view (degrees), ignored if is_ortho
    float near_plane = 0.1f;          // Near clipping plane
    float far_plane = 1000.0f;        // Far clipping plane
    bool  is_ortho = false;           // Use orthographic projection if true
    float ortho_size = 10.0f;         // Half-height for ortho mode
    bool  is_active = false;          // If true, this is the World's active camera

    /// Compute projection matrix (perspective or ortho).
    Mat4 GetProjectionMatrix(float aspect) const;
    /// Compute view matrix from world transform.
    Mat4 GetViewMatrix() const;
    /// Combined view-projection matrix.
    Mat4 GetViewProjection(float aspect) const;
};
```

### LightComponent — Lighting

Defined in `runtime/engine/ecs/light_component.h`.

```cpp
/// Light base class. Do not instantiate directly.
class LightComponent : public PrimitiveComponent {
public:
    LinearColor color = LinearColor::White();
    float intensity = 1.0f;
};

/// Directional light (sun, moonlight). Casts parallel rays from world position.
class DirectionalLightComponent : public LightComponent {
public:
    RenderProxyHandle CreateRenderProxy() override;
};

/// Point light. Radial light with falloff.
class PointLightComponent : public LightComponent {
public:
    float radius = 10.0f;
    RenderProxyHandle CreateRenderProxy() override;
};

/// Spot light. Directional cone light with falloff.
class SpotLightComponent : public LightComponent {
public:
    float radius = 10.0f;
    float inner_cone_angle = 10.0f;
    float outer_cone_angle = 20.0f;
    RenderProxyHandle CreateRenderProxy() override;
};
```

### Future Components (Deferred Phases)

These are planned but not yet implemented:

- **SkeletalMeshComponent** — Skinned mesh with bone transforms (Phase 3b-3.3b)
- **RigidBody** — Simulated physics body (Phase 3b-3.2b, depends on Jolt integration)
- **CharacterBody** — Kinematic body with ground detection (Phase 3b-3.2b)
- **CharacterMovementComponent** — Capsule movement with gravity + stepping (Phase 3b-3.2b)
- **AudioSourceComponent** — 3D spatial sound playback (Phase 3b-3.3b)
- **AudioListenerComponent** — Spatial audio listener (Phase 3b-3.3b)

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Component base | `UActorComponent` (GC, reflected) | `Node` (tree node) | **`Component` (lightweight, no GC)** |
| Transform component | `USceneComponent` | `Node3D` | **`SceneComponent`** |
| Render component | `UPrimitiveComponent` → `FPrimitiveSceneProxy` | `VisualInstance3D` → `RenderingServer` RID | **`PrimitiveComponent` → `RenderProxy`** |
| Physics separation | `UPrimitiveComponent` has both collision AND render | Separate `CollisionShape3D` + `MeshInstance3D` | **Separate: `CollisionObject` (SceneComponent) vs `PrimitiveComponent`** |
| Movement pattern | `UMovementComponent` (ActorComponent, moves UpdatedComponent) | Custom scripts on Node3D | **`MovementComponent` (ActorComponent, same as UE5)** |

## PBR Rendering System (Phase 3b-3.1b MVP)

Implemented alongside components for physically-based rendering:

### PBRMaterial

Defined in `runtime/engine/renderer/material.h` (48 bytes):

```cpp
struct PBRMaterial {
    LinearColor base_color = LinearColor::White();    // Albedo
    float metallic = 0.0f;                            // 0=dielectric, 1=metal
    float roughness = 0.5f;                           // 0=mirror, 1=very rough
    float ao = 1.0f;                                  // Ambient occlusion
};
```

### Shader Implementation

Cook-Torrance PBR shader (`mesh_ps.hlsl`):

- **NDF** (Normal Distribution Function): GGX
- **Geometry** (G): Smith joint approximation
- **Fresnel** (F): Schlick with F0 derived from metallic parameter
- **Result**: Energy-conserving physically plausible materials

### Lighting

Per-frame constant buffer (`constant_buffers.h`):

```cpp
struct PerFrameCB {
    Mat4 view_matrix;
    Mat4 projection_matrix;
    Vec3 light_direction;      // Sun direction (normalized)
    float _pad0;
    LinearColor light_color;   // Sun color
    float light_intensity;     // Sun brightness
    LinearColor ambient_color; // Global ambient
    float _pad1;
};
```

All lights currently baked into these per-frame values. Per-light deferred shading deferred to Phase 3b-3.2b.

## Rules

1. **NEVER inherit from `Component` directly** for game code — use `ActorComponent` or `SceneComponent`
2. **ALWAYS separate physics from rendering** — use `CollisionObject` hierarchy for physics, `PrimitiveComponent` hierarchy for rendering
3. **ALWAYS use `MovementComponent` pattern** for movement — don't modify transforms directly from input handlers
4. **NEVER access render proxies from game code** — `PrimitiveComponent` manages the proxy lifecycle automatically
5. **PBR parameters are per-instance** — use PBRMaterial on each StaticMeshComponent, not globally
