# Entity/Component — Component Model

> **Status**: Planned (Milestone 2.0)
> **Source**: Spec in `docs/ENGINE.md` §4
> **Depends on**: World (§10-01), Memory (`Own<T>`)
> **References**: UE5 `UActorComponent`/`USceneComponent`/`UPrimitiveComponent`, Godot `Node`/`Node3D`/`VisualInstance3D`

## Purpose

UE5-style class hierarchy for type safety and shared behavior, with data-oriented sparse set storage for performance. Physics and rendering are **independent pipelines** — a `SceneComponent` can have physics without rendering, and vice versa.

## Class Hierarchy

```
Component                              — base: lifecycle callbacks, owner entity
├── ActorComponent                     — no transform, pure logic/data
│   ├── MovementComponent              — moves another component's transform
│   │   ├── CharacterMovementComponent — ground movement, gravity, stepping
│   │   └── ProjectileMovementComponent— ballistic trajectory
│   ├── InputComponent                 — binds InputActions to entity
│   └── (game-defined: HealthComponent, InventoryComponent, ...)
│
└── SceneComponent                     — HAS transform, parent/child hierarchy
    ├── CameraComponent                — perspective/ortho, active camera resolution
    ├── AudioSourceComponent           — 3D positional playback → AudioEngine
    ├── AudioListenerComponent         — spatial listener (usually on camera entity)
    ├── SpringArmComponent             — collision-avoiding arm for camera follow
    ├── CollisionObject                — base physics presence → PhysicsWorld (Jolt)
    │   ├── PhysicsBody                — simulated (mass, velocity, forces)
    │   │   ├── RigidBody              — fully simulated dynamic body
    │   │   ├── CharacterBody          — kinematic with ground/wall detection
    │   │   └── StaticBody             — immovable, infinite mass
    │   ├── TriggerVolume              — overlap detection, no physics response
    │   └── CollisionShape             — geometry definition (box, sphere, capsule, mesh)
    ├── NavAgent                       — pathfinding agent → NavigationServer
    │
    └── PrimitiveComponent             — HAS render proxy → RenderWorld (visible)
        ├── MeshComponent              — base for mesh rendering
        │   ├── StaticMeshComponent    — static geometry, instancing-friendly
        │   └── SkeletalMeshComponent  — skinned mesh, bone transforms from Animator
        ├── LightComponent             — base for all lights
        │   ├── DirectionalLightComponent
        │   ├── PointLightComponent
        │   └── SpotLightComponent
        ├── DecalComponent             — projected material onto surfaces
        └── ParticleSystemComponent    — GPU/CPU particle emitter
```

## Design Rules

| Rule | Description |
|------|-------------|
| **Physics ≠ Rendering** | `CollisionObject` inherits from `SceneComponent`, NOT `PrimitiveComponent`. An entity can collide without rendering, and render without colliding. |
| **Movement is ActorComponent** | Like UE5 — `MovementComponent` has no transform. It holds a pointer to an `UpdatedComponent` (a SceneComponent) and modifies that component's transform. |
| **SceneComponent owns hierarchy** | Parent/child attachment, local→world transform propagation, dirty flags — all live in `SceneComponent`. |
| **PrimitiveComponent owns render proxy** | Creates/updates/destroys a proxy in `RenderWorld`. Game code never touches the proxy directly. |
| **One CollisionShape per shape** | Compound collision = multiple `CollisionShape` children under one `PhysicsBody`. |

## Base Classes

```cpp
/// Base component. Owns lifecycle callbacks and a reference to the owning entity.
/// All components inherit from this — never used directly.
class Component {
public:
    virtual ~Component() = default;

    virtual void OnRegister() {}       // added to entity
    virtual void OnInitialize() {}     // world preparing for play
    virtual void OnBeginPlay() {}      // gameplay starts
    virtual void OnEndPlay() {}        // gameplay stopping
    virtual void OnUninitialize() {}   // world teardown
    virtual void OnUnregister() {}     // removed from entity

    Entity GetEntity() const;
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
class SceneComponent : public Component {
public:
    Vec3 position = {0, 0, 0};
    Quat rotation = Quat::Identity();
    Vec3 scale    = {1, 1, 1};

    void AttachTo(SceneComponent& parent);
    void Detach();

    /// Cached world-space matrix. Recomputed when dirty flag is set.
    const Mat4& GetWorldMatrix() const;

    /// Mark transform as dirty. Propagates down to all children.
    void MarkDirty();

private:
    Mat4 world_matrix_;
    bool world_dirty_ = true;
    SceneComponent* parent_ = nullptr;
    std::vector<SceneComponent*> children_;
};

/// Component that is visible in the scene. Creates a render proxy in RenderWorld.
class PrimitiveComponent : public SceneComponent {
public:
    bool visible = true;
    bool cast_shadows = true;
    uint32_t render_layer = 0;   // bitmask — camera filters by layer

protected:
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

## Concrete Components (Key Examples)

```cpp
/// Static mesh + material for rendering.
/// Instancing-friendly — identical mesh+material combos auto-batch.
class StaticMeshComponent : public PrimitiveComponent {
public:
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Material> material;

    /// Per-instance material override (like UE5's dynamic material instance).
    void SetParameter(StringView name, const Vec4& value);
    void SetParameter(StringView name, ResourceHandle<Texture> texture);
};

/// Perspective or orthographic camera. Only one active camera per World.
class CameraComponent : public SceneComponent {
public:
    float fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    bool  is_ortho = false;
    float ortho_size = 10.0f;
    bool  is_active = false;

    Mat4 GetViewProjection() const;
};

/// Light base. Concrete: DirectionalLightComponent, PointLightComponent, SpotLightComponent.
class LightComponent : public PrimitiveComponent {
public:
    Color color = Color::White();
    float intensity = 1.0f;
};

/// Physics body with full simulation.
class RigidBody : public PhysicsBody {
public:
    float mass = 1.0f;
    float linear_damping = 0.01f;
    float angular_damping = 0.05f;

    void AddForce(const Vec3& force);
    void AddImpulse(const Vec3& impulse);
    Vec3 GetLinearVelocity() const;
};

/// Movement controller — ActorComponent (no transform of its own).
class CharacterMovementComponent : public MovementComponent {
public:
    float max_speed = 600.0f;
    float acceleration = 2048.0f;
    float gravity = -981.0f;
    float jump_velocity = 420.0f;
    bool is_grounded = false;

    SceneComponent* updated_component = nullptr;
};
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Component base | `UActorComponent` (GC, reflected) | `Node` (tree node) | **`Component` (lightweight, no GC)** |
| Transform component | `USceneComponent` | `Node3D` | **`SceneComponent`** |
| Render component | `UPrimitiveComponent` → `FPrimitiveSceneProxy` | `VisualInstance3D` → `RenderingServer` RID | **`PrimitiveComponent` → `RenderProxy`** |
| Physics separation | `UPrimitiveComponent` has both collision AND render | Separate `CollisionShape3D` + `MeshInstance3D` | **Separate: `CollisionObject` (SceneComponent) vs `PrimitiveComponent`** |
| Movement pattern | `UMovementComponent` (ActorComponent, moves UpdatedComponent) | Custom scripts on Node3D | **`MovementComponent` (ActorComponent, same as UE5)** |

## Rules

1. **NEVER inherit from `Component` directly** for game code — use `ActorComponent` or `SceneComponent`
2. **ALWAYS separate physics from rendering** — use `CollisionObject` hierarchy for physics, `PrimitiveComponent` hierarchy for rendering
3. **ALWAYS use `MovementComponent` pattern** for movement — don't modify transforms directly from input handlers
4. **NEVER access render proxies from game code** — `PrimitiveComponent` manages the proxy lifecycle automatically
