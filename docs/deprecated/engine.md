> **DEPRECATED**: This document has been fully migrated to `docs/engine/`.
> See [docs/engine/README.md](engine/README.md) for the structured index.
> Key sections: `09-application-lifecycle/`, `10-entity-component/`, `11-scene/`,
> `12-rendering/`, `20-gameplay/`, `21-property-system/`, `29-design-decisions/`, `30-game-examples/`.
> This file is kept for reference only — do not update it.

---

# Engine Layer — MVP Architecture

> **Status**: Planning
> **Goal**: Define the `engine/` layer — the complete game API with lifecycle, events, subsystems, and plugin support.
> **References**: UE5 (FEngineLoop, AActor, UWorldSubsystem, FTickFunction, IModuleInterface), Godot 4 (MainLoop, Node, SceneTree, Signal, GDExtension)

---

## 1. Problem

Every demo directly calls RHI, shader pipeline, mesh creation, and window management:
- **200+ lines of boilerplate** repeated per demo
- **No abstraction boundary** — game code touches DX12 descriptors, command lists, fences
- **No lifecycle** — no BeginPlay/Tick/EndPlay, no ordered init/shutdown
- **No events** — no way for systems to communicate without hardcoded calls
- **No extensibility** — adding a new system requires modifying the main loop

---

## 2. Layer Position

```
game code / demos / scripts / plugins
        ↓
   engine/          ← THIS LAYER (semantic game API)
        ↓
   core/            ← primitives (containers, math, logging, paths, config, memory, assertions)
   rhi/             ← GPU abstraction (DX12 backend, resource creation, command lists)
   renderer/        ← render pipeline (mesh, shaders, upload, constant buffers)
   platform/        ← OS abstraction (window, input)
```

**Invariants:**
- Game code imports ONLY from `engine/`
- `engine/` NEVER exposes RHI types in its public API
- `core/`, `rhi/`, `renderer/` NEVER depend on `engine/`
- All GPU resources accessed via opaque handles, never raw pointers

---

## 3. Application Lifecycle

Inspired by UE5's `FEngineLoop` (3-phase startup) and Godot's `Main::setup/start/iteration/cleanup`.

### 3.1 Startup Phases

```
Phase 0 — PreInit (before GPU)
  ├── Initialize logging, config, paths, memory
  ├── Mount VFS / asset registry
  ├── Fire: OnPreInit
  └── Load plugins at phase CORE

Phase 1 — Init (GPU + systems)
  ├── Create platform window
  ├── Initialize RHI (device, swapchain, command queues)
  ├── Initialize renderer (shader pipeline, global resources)
  ├── Load plugins at phase RHI
  ├── Fire: OnPostInit
  └── Load plugins at phase ENGINE

Phase 2 — Start (world goes live)
  ├── Create World (if scene specified)
  ├── Spawn initial entities
  ├── Call BeginPlay on all entities/components
  ├── Fire: OnWorldBeginPlay
  └── Load plugins at phase SCENE
```

### 3.2 Main Loop (per frame)

Fixed-timestep physics + variable render, inspired by Godot's `MainTimerSync`.

```
┌─────────────────────────────────────────────────┐
│  Frame Start                                     │
│  ├── Poll OS events (window, input)              │
│  ├── Update TimeManager (deltaTime, totalTime)   │
│  │                                               │
│  │  [Physics Loop — N steps per frame]           │
│  │  ├── Fire: OnPrePhysics                       │
│  │  ├── Tick group: PrePhysics                   │
│  │  ├── Step physics simulation                  │
│  │  ├── Tick group: PostPhysics                  │
│  │  └── Fire: OnPostPhysics                      │
│  │                                               │
│  │  [Game Update — once per frame]               │
│  │  ├── Fire: OnPreUpdate                        │
│  │  ├── Tick group: Update                       │
│  │  ├── Tick group: PostUpdate                   │
│  │  ├── Flush deferred events                    │
│  │  └── Fire: OnPostUpdate                       │
│  │                                               │
│  │  [Render — once per frame]                    │
│  │  ├── Fire: OnPreRender                        │
│  │  ├── RenderWorld collects visible entities     │
│  │  ├── Build draw calls, submit to RHI          │
│  │  ├── Present swapchain                        │
│  │  └── Fire: OnPostRender                       │
│  │                                               │
│  └── Process deferred entity destroy             │
└─────────────────────────────────────────────────┘
```

### 3.3 Shutdown

Strict reverse order (mirrors UE5's guarantee):

```
Phase 2 — Stop
  ├── Fire: OnWorldEndPlay
  ├── Call EndPlay on all entities/components (reverse order)
  ├── Unload plugins at phase SCENE (reverse)
  └── Destroy World

Phase 1 — Deinit
  ├── Unload plugins at phase ENGINE (reverse)
  ├── Unload plugins at phase RHI (reverse)
  ├── Fire: OnPreShutdown
  ├── Flush GPU, destroy renderer
  ├── Destroy RHI device
  └── Destroy platform window

Phase 0 — Cleanup
  ├── Unload plugins at phase CORE (reverse)
  ├── Fire: OnPostShutdown
  └── Shutdown logging, config, memory
```

---

## 4. Entity/Component Model

Hybrid approach: UE5-style Actor/Component hierarchy for familiarity + data-oriented storage for performance.

### 4.1 Entity

An **Entity** is a lightweight handle — an ID + generation counter (inspired by Godot's RID pattern). NOT a base class to inherit from.

```cpp
/// Opaque entity handle. 32-bit index + 32-bit generation for safe cross-reference.
/// Stale handles are detected: if the entity was destroyed and the slot reused,
/// the generation won't match and World::IsValid(entity) returns false.
struct Entity {
    uint32_t index;
    uint32_t generation;
};
```

Entities live in a **World**. They have:
- A **name** (debug-only, not used for lookup in shipping)
- A **parent entity** (optional, for hierarchy)
- Zero or more **Components** attached

### 4.2 Component Hierarchy

UE5-style class hierarchy for type safety and shared behavior, with Godot-style directory organization by server/subsystem. Physics and rendering are **independent pipelines** — a SceneComponent can have physics without rendering, and vice versa.

#### Class Hierarchy

```
Component                              — base: lifecycle callbacks, owner entity
├── ActorComponent                     — no transform, pure logic/data
│   ├── MovementComponent              — moves another component's transform (like UE5)
│   │   ├── CharacterMovementComponent — ground movement, gravity, stepping
│   │   └── ProjectileMovementComponent— ballistic trajectory
│   ├── InputComponent                 — binds InputActions to entity
│   └── (game-defined: HealthComponent, InventoryComponent, ...)
│
└── SceneComponent                     — HAS transform, parent/child hierarchy, attach/detach
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
    ├── NavAgent                       — pathfinding agent → NavigationServer (Recast/Detour)
    │
    └── PrimitiveComponent             — HAS render proxy → RenderWorld (visible in scene)
        ├── MeshComponent              — base for mesh rendering (material overrides)
        │   ├── StaticMeshComponent    — static geometry, instancing-friendly
        │   └── SkeletalMeshComponent  — skinned mesh, bone transforms from Animator
        ├── LightComponent             — base for all lights
        │   ├── DirectionalLightComponent — sun/moon, no position (direction only)
        │   ├── PointLightComponent    — omnidirectional, range-based attenuation
        │   └── SpotLightComponent     — cone, inner/outer angle
        ├── DecalComponent             — projected material onto surfaces
        └── ParticleSystemComponent    — GPU/CPU particle emitter
```

#### Design Rules

| Rule | Description |
|------|-------------|
| **Physics ≠ Rendering** | `CollisionObject` inherits from `SceneComponent`, NOT `PrimitiveComponent`. An entity can collide without rendering, and render without colliding. |
| **Movement is ActorComponent** | Like UE5 — `MovementComponent` has no transform. It holds a pointer to an `UpdatedComponent` (a SceneComponent) and modifies that component's transform. |
| **SceneComponent owns hierarchy** | Parent/child attachment, local→world transform propagation, dirty flags — all live in `SceneComponent`. |
| **PrimitiveComponent owns render proxy** | Creates/updates/destroys a proxy in `RenderWorld`. Game code never touches the proxy directly. |
| **One CollisionShape per shape** | Compound collision = multiple `CollisionShape` children under one `PhysicsBody`. |

#### Base Classes

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
/// Use for: movement controllers, input binding, AI, game state (health, inventory).
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

    /// Attach this component as child of another SceneComponent.
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
/// The proxy is automatically created on OnRegister, updated on transform change,
/// and destroyed on OnUnregister. Game code never touches the proxy directly.
class PrimitiveComponent : public SceneComponent {
public:
    bool visible = true;
    bool cast_shadows = true;
    uint32_t render_layer = 0;   // bitmask — camera filters by layer

protected:
    /// Called by the system to create the backend-specific render proxy.
    /// Subclasses override this (StaticMeshComponent creates a mesh proxy, etc.)
    virtual RenderProxyHandle CreateRenderProxy() = 0;

private:
    RenderProxyHandle proxy_ = INVALID_HANDLE;  // managed by RenderWorld
};
```

#### Concrete Components (key examples)

```cpp
/// Static mesh + material for rendering. Inherits PrimitiveComponent.
/// Instancing-friendly — identical mesh+material combos auto-batch.
class StaticMeshComponent : public PrimitiveComponent {
public:
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Material> material;

    /// Override material parameter at runtime without creating a new Material asset.
    /// Creates a per-instance material override (like UE5's dynamic material instance).
    void SetParameter(StringView name, const Vec4& value);
    void SetParameter(StringView name, ResourceHandle<Texture> texture);
};

/// Perspective or orthographic camera. Only one active camera per World.
/// Inherits SceneComponent (has position/rotation) but NOT PrimitiveComponent (no render proxy).
class CameraComponent : public SceneComponent {
public:
    float fov = 60.0f;            // degrees, perspective only
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    bool  is_ortho = false;
    float ortho_size = 10.0f;     // half-height, ortho only
    bool  is_active = false;      // set true to make this the active camera

    /// Computed view-projection matrix (world_matrix inverse * projection).
    Mat4 GetViewProjection() const;
};

/// Light base. Inherits PrimitiveComponent for shadow map rendering and culling.
/// Concrete types: DirectionalLightComponent, PointLightComponent, SpotLightComponent.
/// All three defined in light_component.h (variants change together, Godot pattern).
class LightComponent : public PrimitiveComponent {
public:
    Color color = Color::White();
    float intensity = 1.0f;
};

class DirectionalLightComponent : public LightComponent { /* no range, no position — direction only */ };
class PointLightComponent : public LightComponent { public: float range = 10.0f; };
class SpotLightComponent : public LightComponent { public: float range = 10.0f; float inner_angle = 30.0f; float outer_angle = 45.0f; };

/// Physics body with full simulation (mass, velocity, forces).
/// Inherits SceneComponent — has transform, syncs with PhysicsWorld (Jolt).
/// Does NOT inherit PrimitiveComponent — physics bodies are invisible by default.
/// Add a StaticMeshComponent to the same entity if you want it visible.
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
/// Holds a reference to the SceneComponent it moves (UpdatedComponent).
/// Reads input, applies movement rules, writes to UpdatedComponent's transform.
class CharacterMovementComponent : public MovementComponent {
public:
    float max_speed = 600.0f;
    float acceleration = 2048.0f;
    float gravity = -981.0f;
    float jump_velocity = 420.0f;
    bool is_grounded = false;     // updated by PhysicsWorld sweep

    /// The SceneComponent whose transform this movement controller drives.
    /// Typically the root SceneComponent or CharacterBody of the entity.
    SceneComponent* updated_component = nullptr;
};
```

#### Entity Assembly Example

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

### 4.3 Component Lifecycle

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

**Ordering guarantee:** ALL components call OnInitialize before ANY component calls OnBeginPlay. This lets components depend on each other during init.

### 4.4 Behaviors (Game Logic)

For game logic that needs lifecycle callbacks, use **Behaviors** — components with code:

```cpp
/// A Behavior is a component with lifecycle callbacks.
/// Override only the methods you need. Zero-cost for methods not overridden
/// (function pointer is nullptr, skipped during dispatch).
class Behavior {
public:
    virtual ~Behavior() = default;

    virtual void OnBeginPlay() {}
    virtual void OnTick(float delta) {}
    virtual void OnPhysicsTick(float delta) {}
    virtual void OnEndPlay() {}

    /// Access the owning entity and world
    Entity GetEntity() const;
    World& GetWorld() const;
};

// Usage:
class RotatorBehavior : public Behavior {
    float speed = 90.0f; // degrees/sec
public:
    void OnTick(float delta) override {
        auto& transform = GetWorld().Get<TransformComponent>(GetEntity());
        transform.rotation *= Quat::FromAxisAngle(Vec3::Up(), speed * delta);
    }
};
```

---

## 5. World

The **World** is the top-level simulation container. Inspired by UE5's `UWorld` + Godot's `SceneTree`.

```cpp
/// Owns all entities, components, subsystems, and the simulation state.
/// Multiple worlds can coexist (e.g., editor preview + PIE).
class World {
public:
    // --- Entity Management ---
    Entity CreateEntity(const char* name = nullptr);
    void DestroyEntity(Entity e);               // deferred to end of frame
    bool IsValid(Entity e) const;

    // --- Component Access ---
    template<typename T> T& Add(Entity e, T&& component = {});
    template<typename T> T& Get(Entity e);
    template<typename T> const T& Get(Entity e) const;
    template<typename T> bool Has(Entity e) const;
    template<typename T> void Remove(Entity e);

    // --- Hierarchy ---
    void SetParent(Entity child, Entity parent);
    Entity GetParent(Entity e) const;
    std::span<const Entity> GetChildren(Entity e) const;

    // --- Queries ---
    template<typename... Ts>
    void ForEach(std::function<void(Entity, Ts&...)> fn);

    // --- Subsystems (per-world services) ---
    template<typename T> T* GetSubsystem();
    template<typename T> T* AddSubsystem();

    // --- Lifecycle (called by Application) ---
    void BeginPlay();
    void Tick(float delta);
    void PhysicsTick(float fixed_delta);
    void EndPlay();
};
```

### 5.1 Entity Hierarchy & Transform Propagation

- Setting `SetParent(child, parent)` makes `child.TransformComponent.position` relative to parent
- World matrix is computed lazily: dirty flag set on position/rotation/scale change
- Dirty flag propagates to all children (like UE5's `FTransformUpdated` + Godot's `xform_change_list`)
- `GetWorldTransform(Entity)` recomputes if dirty, caches result

---

## 6. Subsystem Pattern

Inspired by UE5's `UWorldSubsystem` — scoped services that replace singletons.

```cpp
/// Base class for per-world subsystems.
/// Subsystems are auto-created when a World is created.
/// They tick, have lifecycle, and are destroyed with the World.
class WorldSubsystem {
public:
    virtual ~WorldSubsystem() = default;

    /// Return false to skip creation for this world (e.g., server-only subsystem).
    virtual bool ShouldCreate(const World& world) const { return true; }

    /// Called after all subsystems are created. Use InitializeDependency<T>()
    /// to ensure another subsystem initializes first.
    virtual void Initialize(World& world) {}

    /// Called after ALL subsystems have initialized.
    virtual void PostInitialize() {}

    /// Called when world gameplay starts.
    virtual void OnWorldBeginPlay() {}

    /// Per-frame tick. Only called if CanTick() returns true.
    virtual bool CanTick() const { return false; }
    virtual void Tick(float delta) {}

    /// Called when world gameplay stops.
    virtual void OnWorldEndPlay() {}

    /// Called before destruction. Reverse of Initialize order.
    virtual void Deinitialize() {}

protected:
    /// Ensure another subsystem type initializes before this one.
    template<typename T> T* InitializeDependency();

    World* world_ = nullptr;
};
```

**Built-in subsystems:**
- `RenderSubsystem` — collects visible entities, builds draw calls, manages render resources
- `PhysicsSubsystem` — owns the physics world (Jolt), syncs with entities
- `AudioSubsystem` — spatial audio, voice pool
- `InputSubsystem` — action mapping, device polling

---

## 7. Event System

Inspired by UE5's multicast delegates + Godot's signals. Two modes: **immediate** and **deferred**.

```cpp
/// Type-safe multicast delegate. Multiple listeners, broadcast to all.
/// Listeners identified by DelegateHandle for removal.
template<typename... Args>
class MulticastDelegate {
public:
    using Fn = std::function<void(Args...)>;

    DelegateHandle Add(Fn callback);
    DelegateHandle AddObject(void* obj, Fn callback);  // auto-removes if obj destroyed
    void Remove(DelegateHandle handle);
    void Broadcast(Args... args);                       // immediate call
    void BroadcastDeferred(Args... args);               // queued to frame flush
    void Clear();
};
```

### 7.1 Engine-Level Events (global)

```cpp
/// Global engine events. Systems subscribe during startup.
namespace EngineEvents {
    // Lifecycle
    extern MulticastDelegate<>              OnPreInit;
    extern MulticastDelegate<>              OnPostInit;
    extern MulticastDelegate<>              OnPreShutdown;
    extern MulticastDelegate<>              OnPostShutdown;

    // Frame
    extern MulticastDelegate<float>         OnPreUpdate;       // delta
    extern MulticastDelegate<float>         OnPostUpdate;      // delta
    extern MulticastDelegate<>              OnPreRender;
    extern MulticastDelegate<>              OnPostRender;

    // Physics
    extern MulticastDelegate<float>         OnPrePhysics;      // fixed_delta
    extern MulticastDelegate<float>         OnPostPhysics;     // fixed_delta
}
```

### 7.2 World-Level Events

```cpp
/// Per-world events. Scoped to world lifetime.
class World {
public:
    MulticastDelegate<Entity>               OnEntityCreated;
    MulticastDelegate<Entity>               OnEntityDestroyed;
    MulticastDelegate<>                     OnBeginPlay;
    MulticastDelegate<>                     OnEndPlay;
};
```

### 7.3 Deferred Event Queue

Inspired by Godot's `MessageQueue::flush()` — deferred events are flushed at known safe points (between tick groups, after update, before render).

```cpp
/// Frame-scoped event queue. Events pushed during the frame are flushed
/// at explicit points in the main loop. This avoids mutation during iteration.
class EventQueue {
public:
    void Push(std::function<void()> event);
    void Flush();  // called by the main loop at safe points
};
```

---

## 8. Tick Architecture — Hybrid Frequency Model

### 8.1 The Problem with Traditional Tick

Every major engine (Unreal, Unity, Godot) couples gameplay tick to frame rate. At 300 FPS, every behavior ticks 300 times per second — even AI that only needs 10Hz decisions. Developers work around this with manual timers and interval components, but the engine wastes CPU by default.

**Worse:** in action games where everything is on screen (Doom Eternal, Devil May Cry, Hades), all entities are close to the camera and must stay responsive. You can't just lower tick rates globally — the player **feels** 100ms AI reaction delay. Traditional tick LOD (reduce rate by distance) doesn't help when 50 enemies are all within 20m.

### 8.2 UzEngine Solution: Urgency-Layered Tick

Instead of one tick rate per entity, **split each entity's work by urgency**:

| Urgency | Runs At | Examples | Cost Target |
|---|---|---|---|
| **Immediate** (`OnTick`) | Every frame | Movement, input response, hit reaction, interpolation | < 0.1ms/entity |
| **Reactive** (`UZ_SCHEDULED 30-60Hz`) | Fixed frequency | Animation state machine, combat logic, targeting | < 0.5ms/entity |
| **Strategic** (`UZ_SCHEDULED 5-20Hz`) | Fixed frequency | Pathfinding, AI decisions, spawn logic | < 2ms/entity |
| **Background** (`UZ_SCHEDULED 1-5Hz`) | Fixed frequency | Buff timers, aggro decay, loot tables, stat regen | < 0.5ms/entity |

**Key insight:** A single NPC uses ALL layers simultaneously:
- **OnTick (every frame):** apply velocity, interpolate visual position — cheap, must be smooth
- **CombatUpdate (30Hz):** choose next attack, verify range — medium cost
- **NavigationUpdate (10Hz):** recalculate path via navmesh — expensive but infrequent
- **AggroUpdate (2Hz):** re-evaluate threat table — cheap but truly infrequent

### 8.3 Tick Groups (Frame Phases)

The main loop still has ordered phases for deterministic execution:

```cpp
/// Phases within a single frame. Executed in order with barriers between them.
/// OnTick and scheduled callbacks are dispatched within the appropriate group.
enum class TickGroup : uint8_t {
    PrePhysics   = 0,  // input processing, movement intent, AI decisions
    Physics      = 1,  // fixed-timestep physics simulation (engine-internal)
    PostPhysics  = 2,  // react to physics results, animation, combat
    Update       = 3,  // general game logic, camera, VFX
    PostUpdate   = 4,  // cleanup, UI sync, late effects, render prep
};
```

### 8.4 OnTick — Per-Frame Fast Path

`OnTick(float dt)` runs **every frame** within the behavior's tick group. It MUST be lightweight — apply velocities, interpolate positions, respond to input. Heavy work goes in scheduled functions.

```cpp
class MeleeEnemy : public uz::Behavior {
    UZ_BEHAVIOR(MeleeEnemy);

    Vec3 velocity_ = {};
    Vec3 visual_offset_ = {};

public:
    void OnBeginPlay() override {
        SetTickGroup(uz::TickGroup::PostPhysics);
    }

    /// Runs EVERY frame. Must be cheap (< 0.1ms).
    /// Apply velocity, interpolate visuals, react to immediate events.
    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();

        // Apply velocity computed by NavigationUpdate
        transform.position += velocity_ * dt;

        // Smooth visual interpolation between strategic updates
        visual_offset_ = uz::Lerp(visual_offset_, Vec3::Zero(), dt * 10.0f);
    }
};
```

### 8.5 UZ_SCHEDULED — Independent Frequency Functions

Scheduled functions run at a **fixed frequency independent of frame rate**. The engine accumulates time and dispatches them when their interval elapses. Multiple scheduled functions on the same behavior can run at different frequencies.

```cpp
class MeleeEnemy : public uz::Behavior {
    UZ_BEHAVIOR(MeleeEnemy);

    Vec3 velocity_ = {};
    uz::Entity target_ = uz::Entity::Invalid();
    float attack_cooldown_ = 0.0f;

public:
    /// Runs EVERY frame — apply movement, interpolate.
    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();
        transform.position += velocity_ * dt;
    }

    // ─── Reactive: 30 Hz ─────────────────────────────────────────────────
    /// Choose attacks, verify range, manage cooldowns.
    /// 30Hz is fast enough for responsive combat without per-frame waste.
    UZ_SCHEDULED(CombatUpdate, 30.0f);
    void CombatUpdate(float dt) {
        if (target_ == uz::Entity::Invalid()) return;

        auto& transform = Get<uz::TransformComponent>();
        auto& target_pos = GetWorld().Get<uz::TransformComponent>(target_).position;
        float dist = (target_pos - transform.position).Length();

        attack_cooldown_ -= dt;
        if (dist < 2.0f && attack_cooldown_ <= 0.0f) {
            GetWorld().Get<Health>(target_).current -= 15.0f;
            attack_cooldown_ = 1.2f;
        }
    }

    // ─── Strategic: 10 Hz ────────────────────────────────────────────────
    /// Recalculate navigation path. Expensive (navmesh query), so 10Hz.
    UZ_SCHEDULED(NavigationUpdate, 10.0f);
    void NavigationUpdate(float dt) {
        auto& transform = Get<uz::TransformComponent>();
        auto* nav = GetWorld().GetSubsystem<uz::NavSubsystem>();

        // Find nearest player
        target_ = uz::Entity::Invalid();
        float best = 50.0f;
        GetWorld().ForEach<PlayerTag, uz::TransformComponent>(
            [&](uz::Entity e, PlayerTag&, uz::TransformComponent& pt) {
                float d = (pt.position - transform.position).Length();
                if (d < best) { best = d; target_ = e; }
            }
        );

        if (target_ != uz::Entity::Invalid()) {
            auto& target_pos = GetWorld().Get<uz::TransformComponent>(target_).position;
            Vec3 path_dir = nav->FindDirection(transform.position, target_pos);
            velocity_ = path_dir * 3.0f;
        } else {
            velocity_ = Vec3::Zero();
        }
    }

    // ─── Background: 2 Hz ────────────────────────────────────────────────
    /// Re-evaluate threat priorities. Very cheap, very infrequent.
    UZ_SCHEDULED(AggroUpdate, 2.0f);
    void AggroUpdate(float dt) {
        // Decay aggro on all known targets, boost aggro on damage source
        // (implementation omitted for brevity)
    }
};
```

### 8.6 Scene-Level Frequency Override

The `.uzscene` file can override the default tick rate declared in `UZ_SCHEDULED`. This allows designers to tune per-instance without recompiling:

```json
{
    "name": "Boss_Dragon",
    "components": {
        "Transform": { "position": [0, 0, 50] },
        "MeshRenderer": { "mesh": "meshes/dragon.uzmesh" },
        "DragonAI": {
            "aggression": 0.8,
            "scheduled_overrides": {
                "CombatUpdate": 60.0,
                "NavigationUpdate": 20.0
            }
        }
    }
}
```

Priority: **scene override > macro default > system default**.

### 8.7 Job System — Parallel Scheduled Execution

Scheduled functions marked with `UZ_TICK_PARALLEL(true)` can execute on worker threads. The job system distributes work across all available cores.

**Rules for parallel tick:**
- Each entity accesses ONLY its own components (no cross-entity writes)
- Read-only access to other entities is allowed (ForEach queries)
- World mutation (CreateEntity, DestroyEntity) is deferred to end-of-frame

```cpp
class MeleeEnemy : public uz::Behavior {
    UZ_BEHAVIOR(MeleeEnemy);
    UZ_TICK_PARALLEL(true);  // OnTick + all scheduled functions can run on worker threads

    // ...
};
```

The engine batches all parallel-safe entities and distributes them across worker threads. Non-parallel entities run sequentially on the main thread.

### 8.8 Tick Budget — Frame Time Guard

Each frame has a configurable **tick budget** (default: 4ms). If scheduled functions exceed the budget, remaining work is deferred to the next frame (**staggered execution**). This guarantees stable frame rate even with thousands of entities.

**Budget applies to scheduled functions only** — `OnTick` always runs (it's the fast path).

**Priority when budget is tight:**
1. Entities with `UZ_TICK_PRIORITY(High)` — bosses, player-interacting NPCs
2. Entities visible to the camera
3. Entities close to the player
4. Entities far away or off-screen (may skip entire frames)

```cpp
class BossEnemy : public uz::Behavior {
    UZ_BEHAVIOR(BossEnemy);
    UZ_TICK_PRIORITY(uz::TickPriority::High);  // never deferred by budget

    // Boss scheduled functions always run on time
};
```

### 8.9 Tick LOD — Distance-Based Rate Scaling

For entities that support it, the engine automatically scales scheduled function rates based on camera distance:

| Distance | Rate Multiplier | Effect |
|---|---|---|
| < 20m | 1.0x | Full rate (10Hz stays 10Hz) |
| 20–50m | 0.5x | Half rate (10Hz → 5Hz) |
| 50–100m | 0.25x | Quarter rate (10Hz → 2.5Hz) |
| > 100m | 0.0x | Sleep (wake on event only) |

**Tick LOD applies ONLY to scheduled functions, NEVER to OnTick.** Visible entities always run OnTick at full frame rate for smooth visuals.

Opt-out per behavior:
```cpp
class AmbientBird : public uz::Behavior {
    UZ_BEHAVIOR(AmbientBird);
    UZ_TICK_LOD(false);  // always tick at full rate regardless of distance
};
```

### 8.10 Performance Budget — Action Game Scenario

**50 melee enemies on screen, all within 20m (worst case for action games):**

| Layer | Entities/Frame | Cost/Entity | Total |
|---|---|---|---|
| OnTick (every frame) | 50 | 0.05ms | 2.5ms |
| CombatUpdate (30Hz, staggered) | ~15/frame | 0.2ms | 3.0ms |
| NavigationUpdate (10Hz, staggered) | ~5/frame | 0.8ms | 4.0ms |
| AggroUpdate (2Hz, staggered) | ~1/frame | 0.1ms | 0.1ms |
| **Total per frame** | | | **~9.6ms** |

At 60 FPS (16.6ms budget), this leaves **7ms for rendering**. With job system parallelism on 8+ cores, the scheduled functions overlap with rendering, bringing effective gameplay cost to ~3-4ms.

**Compare to naive per-frame tick:** 50 entities × full AI+nav+combat every frame = 50 × 1.5ms = **75ms** — unplayable.

### 8.11 Comparison with Other Engines

| | Unreal 5 | Godot 4 | **UzEngine** |
|---|---|---|---|
| Tick coupled to frame | Yes (manual workaround) | Yes (_process = frame) | **No — OnTick + independent frequencies** |
| Per-function frequency | No (per-component only) | No (Timer nodes) | **Yes — UZ_SCHEDULED per method** |
| Parallel tick | Limited (opt-in per component) | No | **Job system, opt-in per behavior** |
| Tick budget | No | No | **Budget + stagger + priority** |
| Tick LOD | Significance Manager (plugin) | No | **Built-in, automatic, opt-out** |
| Config per instance | Tick interval (manual) | Timer nodes | **Scene override + macros** |

### 8.12 MVP vs Full Implementation

**MVP (Phase 2):**
- TickGroup phases (PrePhysics → Physics → PostPhysics → Update → PostUpdate)
- OnTick dispatched per-frame within tick groups
- UZ_SCHEDULED with time accumulator — single-threaded dispatch
- Per-behavior tick rate configuration

**Phase 2 — Parallelism:**
- Job system with worker thread pool
- UZ_TICK_PARALLEL for opt-in parallel execution
- Deferred world mutation queue

**Phase 3 — Budget & LOD:**
- Tick budget with staggered execution
- UZ_TICK_PRIORITY for critical entities
- Tick LOD based on camera distance
- Performance counters and profiling integration

---

## 9. Plugin System

Inspired by UE5's `IModuleInterface` + Godot's GDExtension (C ABI with name-lookup vtable).

### 9.1 Plugin Phases

```cpp
enum class PluginPhase : uint8_t {
    Core    = 0,  // after logging, config, memory — before GPU
    Rhi     = 1,  // after RHI init — GPU available
    Engine  = 2,  // after engine systems — World available
    Scene   = 3,  // after world BeginPlay — gameplay active
    Editor  = 4,  // editor-only, after editor init
};
```

### 9.2 Plugin Interface (C ABI for cross-compiler compatibility)

```c
/// Every plugin DLL exports this single function.
/// The engine calls it once at load time.
typedef struct uz_plugin_info_t {
    const char*   name;
    const char*   version;
    int           min_phase;     // PluginPhase enum

    int  (*startup)(const uz_engine_api_t* api);  // return 0 = success
    void (*shutdown)(void);
    void (*tick)(float delta);   // nullptr if no tick needed
} uz_plugin_info_t;

/// Exported symbol name: uz_plugin_init
typedef uz_plugin_info_t* (*uz_plugin_init_fn)(void);

/// The engine provides its API via a name-lookup function (Godot pattern).
/// Forward-compatible: old plugins simply don't request new functions.
typedef void* (*uz_get_api_fn)(const char* name);

typedef struct uz_engine_api_t {
    uz_get_api_fn get;  // get("world_create_entity") -> fn_ptr
} uz_engine_api_t;
```

### 9.3 Static Module Registration (for built-in systems)

```cpp
/// Built-in systems register as modules without being a DLL.
/// Same lifecycle as plugins but statically linked.
struct ModuleDescriptor {
    const char*   name;
    PluginPhase   phase;
    void (*startup)();
    void (*shutdown)();  // called in reverse startup order
};

/// Self-registration macro — creates a global that auto-registers.
#define UZ_REGISTER_MODULE(desc) \
    static ModuleRegistrar _module_reg_##__LINE__(desc)
```

---

## 10. Resource System

Inspired by Godot's `Resource` (ref-counted + `changed` signal) + UE5's `FStreamableManager`.

```cpp
/// Opaque handle to a loaded resource. Type-safe, ref-counted.
/// The handle is valid even if the resource is being reloaded (hot-reload).
template<typename T>
class ResourceHandle {
public:
    bool IsLoaded() const;
    T* Get() const;               // nullptr if not loaded
    T* operator->() const;
    const std::string& GetPath() const;

    /// Subscribe to hot-reload notifications.
    DelegateHandle OnReloaded(std::function<void(T&)> callback);
};

/// Central resource manager — loads, caches, hot-reloads.
class ResourceManager {
public:
    template<typename T>
    ResourceHandle<T> Load(const std::string& path);

    template<typename T>
    ResourceHandle<T> LoadAsync(const std::string& path);

    /// Register a custom loader for a file extension.
    void RegisterLoader(const char* extension, ResourceLoader* loader);

    /// Force reload of a resource (used by hot-reload system).
    void Reload(const std::string& path);
};
```

**Built-in resource types:**
- `Mesh` — vertex/index data (.uzmesh)
- `Material` — shader + parameters (.uzmat)
- `Texture` — image data (.uztexture)
- `Shader` — compiled shader (.uzshader)
- `Scene` — entity hierarchy (.uzscene)
- `Prefab` — reusable entity template (.uzprefab)

---

## 11. Bootstrap Model — Scene-Driven, No Manual main()

In every major engine (Unreal, Unity, Godot), game developers **never write bootstrap code**.
The entry point is a **project file** that declares the startup scene, and the engine loads it automatically.

### 11.1 How Other Engines Bootstrap

| Engine | Project File | Startup Config | Developer Writes |
|---|---|---|---|
| **Unreal** | `.uproject` | `DefaultEngine.ini` → `GameDefaultMap` + `GameMode` class | Behaviors (Actor/Component subclasses) |
| **Unity** | Build Settings | Startup Scene index 0 | Scripts (MonoBehaviour subclasses) |
| **Godot** | `project.godot` | `run/main_scene="res://main.tscn"` | Scripts (Node subclasses) |

**Common pattern:** project config → engine loads scene → entities/nodes already have behaviors attached → lifecycle runs automatically.

### 11.2 UzEngine Bootstrap

```
my_game/
  game.uzproject          ← project config (startup scene, window, settings)
  scenes/
    main.uzscene          ← startup scene (entities + components + behaviors)
  scripts/
    player_controller.h   ← behavior code (the only thing the dev writes)
    enemy_ai.h
  assets/
    meshes/
    materials/
```

**game.uzproject** (JSON):
```json
{
    "name": "My Game",
    "version": "1.0.0",
    "engine_version": "0.2.0",
    "startup_scene": "scenes/main.uzscene",
    "window": {
        "title": "My Game",
        "width": 1280,
        "height": 720,
        "vsync": true
    },
    "physics": {
        "fixed_timestep": 0.01667
    }
}
```

**main.uzscene** (JSON — LLM-friendly, human-editable):
```json
{
    "name": "Main",
    "entities": [
        {
            "name": "Player",
            "components": {
                "Transform": { "position": [0, 1.7, 0] },
                "Camera": { "fov": 90, "is_active": true },
                "FPSCamera": { "move_speed": 5.0, "look_sensitivity": 0.15 }
            }
        },
        {
            "name": "Floor",
            "components": {
                "Transform": { "position": [0, 0, 0], "scale": [50, 0.1, 50] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/gray.uzmat" }
            }
        },
        {
            "name": "Sun",
            "components": {
                "Transform": {},
                "Light": { "type": "Directional", "intensity": 2.0 }
            }
        }
    ],
    "subsystems": ["WaveSubsystem"]
}
```

### 11.3 Engine Startup Sequence (internal — dev never sees this)

```
1. uzengine.exe reads game.uzproject
2. Phase 0 — PreInit: logging, config, memory, VFS mount
3. Phase 1 — Init: window (from .uzproject), RHI, renderer, plugins
4. Phase 2 — Start:
   a. Load startup_scene from .uzproject
   b. Deserialize .uzscene → create entities + attach components
   c. Behavior types resolved by name → ClassRegistry lookup
   d. Call OnBeginPlay on all behaviors
5. Main loop runs until quit
6. Shutdown in reverse order
```

### 11.4 Application Class (internal engine API, not game-facing)

```cpp
/// Internal engine class. Game developers do NOT use this directly.
/// The engine executable (uzengine) creates and runs it.
class Application {
public:
    /// Load project and run. Called by uzengine main().
    static void Run(const char* project_path);

    /// Access during gameplay (from Behaviors and Subsystems).
    static World& GetWorld();
    static ResourceManager& GetResources();
    static InputManager& GetInput();
    static void Quit();
    static float GetDeltaTime();
    static float GetFixedDeltaTime();
    static float GetTotalTime();

    /// Load a different scene (async, replaces current world).
    static void LoadScene(const char* path);
};
```

### 11.5 Class Registry — Resolving Behaviors by Name

For .uzscene files to reference behavior types by string name, behaviors must self-register:

```cpp
/// Self-registration macro. Put in the .cpp file of each behavior.
/// This registers the type name so .uzscene can reference it.
#define UZ_REGISTER_BEHAVIOR(ClassName) \
    static uz::BehaviorRegistrar<ClassName> _reg_##ClassName(#ClassName)

// In player_controller.cpp:
UZ_REGISTER_BEHAVIOR(FPSCamera);
UZ_REGISTER_BEHAVIOR(PaddleController);
UZ_REGISTER_BEHAVIOR(BallLogic);
```

The `ClassRegistry` maps string names to factory functions:
```cpp
class ClassRegistry {
public:
    template<typename T>
    static void Register(const char* name);

    /// Create a behavior by name (used by scene loader).
    static Behavior* CreateBehavior(const char* name);

    /// Check if a name is registered.
    static bool IsRegistered(const char* name);
};
```

---

## 12. Proposed Directory Structure

```
runtime/
├── core/                                    ← primitives (no engine deps)
│   ├── assertions/
│   ├── config/
│   ├── containers/
│   ├── logging/
│   ├── math/
│   ├── memory/
│   ├── paths/
│   └── random/
│
├── rhi/                                     ← GPU abstraction (depends on core/)
│   ├── dx12/
│   ├── rhi.h
│   ├── rhi_backend.h
│   ├── rhi_types.h
│   └── rhi_config.h/.cpp
│
├── renderer/                                ← render pipeline (depends on core/, rhi/)
│   ├── camera/
│   ├── mesh/
│   ├── shader/
│   ├── upload/
│   ├── constant_buffers/
│   └── vertex/
│
├── engine/                                  ← game API (depends on core/, rhi/, renderer/, platform/)
│   ├── application/                         ← Application, GameLoop, TimeManager
│   ├── world/                               ← World, Entity, Component storage
│   ├── components/                          ← component hierarchy (by server/subsystem)
│   │   ├── component.h                      ← Component base (lifecycle)
│   │   ├── actor_component.h                ← ActorComponent (no transform, pure logic)
│   │   ├── scene_component.h                ← SceneComponent (transform, hierarchy)
│   │   ├── primitive_component.h            ← PrimitiveComponent (render proxy, visibility)
│   │   ├── rendering/                       ← → RenderWorld
│   │   │   ├── static_mesh_component.h/.cpp
│   │   │   ├── skeletal_mesh_component.h/.cpp
│   │   │   ├── light_component.h/.cpp       ← Directional + Point + Spot (one file)
│   │   │   ├── decal_component.h
│   │   │   └── particle_system_component.h/.cpp
│   │   ├── physics/                         ← → PhysicsWorld (Jolt)
│   │   │   ├── collision_object.h
│   │   │   ├── physics_body.h
│   │   │   ├── rigid_body.h/.cpp
│   │   │   ├── character_body.h/.cpp
│   │   │   ├── static_body.h
│   │   │   ├── collision_shape.h/.cpp
│   │   │   ├── trigger_volume.h
│   │   │   └── joints/
│   │   │       ├── joint.h
│   │   │       ├── hinge_joint.h
│   │   │       └── spring_joint.h
│   │   ├── audio/                           ← → AudioEngine
│   │   │   ├── audio_source_component.h/.cpp
│   │   │   └── audio_listener_component.h
│   │   ├── camera/                          ← SceneComponent (no render proxy)
│   │   │   ├── camera_component.h/.cpp
│   │   │   └── spring_arm_component.h
│   │   ├── navigation/                      ← → NavigationServer (Recast/Detour)
│   │   │   ├── nav_agent_component.h
│   │   │   ├── nav_obstacle_component.h
│   │   │   └── nav_region_component.h
│   │   ├── animation/                       ← → AnimationSystem
│   │   │   └── animator_component.h/.cpp
│   │   └── gameplay/                        ← ActorComponents (pure logic)
│   │       ├── movement/
│   │       │   ├── movement_component.h
│   │       │   ├── character_movement.h/.cpp
│   │       │   └── projectile_movement.h
│   │       └── input/
│   │           └── input_component.h
│   ├── behavior/                            ← Behavior base class
│   ├── subsystem/                           ← WorldSubsystem base + built-ins
│   ├── event/                               ← MulticastDelegate, EventQueue, EngineEvents
│   ├── tick/                                ← TickGroup, TickFunction, TickManager
│   ├── plugin/                              ← Plugin loader, ModuleDescriptor, C ABI
│   ├── resource/                            ← ResourceManager, ResourceHandle, ResourceLoader
│   ├── scene/                               ← SceneLoader, SceneSaver (.uzscene/.uzprefab)
│   └── input/                               ← InputAction, InputMap (wraps platform/)
│
├── editor/                                  ← editor-only (depends on engine/)
│   ├── font/
│   ├── grid/
│   ├── screenshot/
│   └── stats/
│
├── developer/                               ← dev-only, stripped in shipping (see DEVELOPER.md)
│   ├── profiling/
│   ├── debug_draw/
│   ├── hot_reload/
│   └── console/
│
├── platform/                                ← OS abstraction (depends on core/)
│   └── windows/
│
├── demos/                                   ← refactored to use engine/ API
├── tests/                                   ← mirrors source structure
└── tools/                                   ← uzcook, uzembed, uzengine
```

---

## 13. MVP Implementation Plan

What to build first to have a functional engine API, ordered by dependency:

### Phase 1 — Foundation (engine compiles, demos run through Application)
1. `engine/event/` — MulticastDelegate, DelegateHandle, EngineEvents
2. `engine/application/` — Application (wraps current demo boilerplate: window + RHI + render loop)
3. `engine/application/time_manager.h` — deltaTime, fixedDeltaTime, totalTime, timeScale
4. Move `core/rhi/` → `rhi/`, `core/renderer/` → `renderer/`, `core/shader/` → `renderer/shader/`
5. Refactor one demo (mesh_test) to use `Application::Run()`

### Phase 2 — Entity/Component (world exists, entities render)
1. `engine/world/` — World, Entity handle, component storage (sparse set)
2. `engine/components/` — TransformComponent, CameraComponent
3. `engine/tick/` — TickGroup, TickManager (simple priority-sorted dispatch)
4. `engine/behavior/` — Behavior base class with OnTick
5. Refactor mesh_test demo to use World + Entity + TransformComponent

### Phase 3 — Rendering Integration (entities auto-render)
1. `engine/components/` — MeshRendererComponent, LightComponent
2. `engine/subsystem/render_subsystem.h` — collects MeshRendererComponent + TransformComponent, builds draw calls
3. Remove manual draw-call code from demos

### Phase 4 — Resources (load assets by path)
1. `engine/resource/` — ResourceManager, ResourceHandle, ResourceLoader interface
2. Built-in loaders: Mesh (.uzmesh), Shader (.uzshader)
3. Demos load meshes by path instead of hardcoded creation

### Phase 5 — Input & Scene (gameplay possible)
1. `engine/input/` — InputAction, InputMap wrapping platform input
2. `engine/scene/` — SceneLoader for .uzscene files (JSON)
3. Demo: load a scene file, move camera with input

### Phase 6 — Subsystems & Plugins (extensible)
1. `engine/subsystem/` — WorldSubsystem base, registration, dependency ordering
2. `engine/plugin/` — Plugin loader, C ABI, phase-gated init
3. Refactor render/physics/audio into subsystems

---

## 14. Design Decisions

### Q: ECS vs Actor/Component?
**A: Hybrid.** Entity is a handle (like ECS), components are data structs in sparse sets (like ECS), but Behaviors provide OOP lifecycle (like Actor/Component). Best of both worlds: cache-friendly storage + familiar programming model.

### Q: Render World separation?
**A: Yes.** `RenderSubsystem` maintains a parallel render-side representation. Game-side `MeshRendererComponent` is a description; render-side is the actual draw call data. This decouples game logic from render thread — essential for future multi-threading.

### Q: Thread model?
**A: Single-threaded MVP, designed for multi-threading.** The EventQueue, RenderSubsystem separation, and TickGroup barriers are all designed to enable multi-threading later without API changes. MVP runs everything on the main thread.

### Q: Node tree vs flat ECS?
**A: Flat ECS with parent/child entity relationships.** No deep tree traversal. Parent-child is for transform propagation only. Systems query components directly (ForEach), not by walking a tree. This avoids Godot's tree-walk performance issues while keeping hierarchy for transforms.

### Q: Plugin ABI?
**A: C ABI with name-lookup vtable.** Inspired by Godot's GDExtension. `uz_get_api_fn("function_name") -> void*` is forward-compatible — old plugins work with new engine versions. No C++ linkage across the boundary.

---

## 15. Game Code Examples

The developer **never writes bootstrap code**. The engine loads `.uzscene` files automatically from `.uzproject`. The dev only writes **Behaviors** (game logic) and **Components** (data). Scene composition happens in the editor or by editing `.uzscene` JSON directly.

Each example shows: the `.uzscene` file (what the editor/LLM produces) + the C++ behavior code (what the dev writes).

---

### 15.1 Pong

Classic 2-player Pong. Demonstrates: input, collision via behavior, score state, ball reset.

**scenes/pong.uzscene:**
```json
{
    "name": "Pong",
    "subsystems": ["PongSubsystem"],
    "entities": [
        {
            "name": "Paddle_P1",
            "components": {
                "Transform": { "position": [-7, 0, 0], "scale": [0.3, 1.5, 0.3] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/white.uzmat" },
                "PaddleTag": { "player_index": 0 },
                "PaddleController": { "speed": 8.0 }
            }
        },
        {
            "name": "Paddle_P2",
            "components": {
                "Transform": { "position": [7, 0, 0], "scale": [0.3, 1.5, 0.3] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/white.uzmat" },
                "PaddleTag": { "player_index": 1 },
                "PaddleController": { "speed": 8.0 }
            }
        },
        {
            "name": "Ball",
            "components": {
                "Transform": { "position": [0, 0, 0], "scale": [0.2, 0.2, 0.2] },
                "MeshRenderer": { "mesh": "meshes/sphere.uzmesh", "material": "materials/white.uzmat" },
                "BallMovement": { "velocity": [5.0, 0.0, 3.0] },
                "BallLogic": {}
            }
        },
        {
            "name": "Camera",
            "components": {
                "Transform": { "position": [0, 12, 0], "rotation": [-90, 0, 0] },
                "Camera": { "fov": 60, "is_active": true }
            }
        },
        {
            "name": "Light",
            "components": {
                "Transform": { "position": [0, 10, 0] },
                "Light": { "type": "Directional", "intensity": 1.5 }
            }
        }
    ]
}
```

**scripts/pong.h** — the only code the developer writes:
```cpp
#pragma once
#include "engine/behavior.h"
#include "engine/components.h"
#include "engine/input.h"

// ─────────────────────────────────────────────────────────────────────────────
// Components — pure data, serialized from .uzscene
// ─────────────────────────────────────────────────────────────────────────────

struct PaddleTag {
    int player_index = 0;
    UZ_COMPONENT(PaddleTag);   // registers for .uzscene deserialization
};

struct BallMovement {
    Vec3 velocity = {5.0f, 0.0f, 3.0f};
    UZ_COMPONENT(BallMovement);
};

// ─────────────────────────────────────────────────────────────────────────────
// Behaviors — game logic, attached to entities in .uzscene
// ─────────────────────────────────────────────────────────────────────────────

class PaddleController : public uz::Behavior {
    UZ_BEHAVIOR(PaddleController);

    float speed = 8.0f;  // serialized from .uzscene

public:
    void OnBeginPlay() override {
        SetTickGroup(uz::TickGroup::PrePhysics);
    }

    void OnTick(float dt) override {
        auto& paddle = Get<PaddleTag>();
        auto& transform = Get<uz::TransformComponent>();
        auto& input = uz::Application::GetInput();

        float move = 0.0f;
        if (paddle.player_index == 0) {
            if (input.IsKeyDown(Key::W)) move = 1.0f;
            if (input.IsKeyDown(Key::S)) move = -1.0f;
        } else {
            if (input.IsKeyDown(Key::Up)) move = 1.0f;
            if (input.IsKeyDown(Key::Down)) move = -1.0f;
        }

        transform.position.y += move * speed * dt;
        transform.position.y = uz::Clamp(transform.position.y, -4.0f, 4.0f);
    }
};

class BallLogic : public uz::Behavior {
    UZ_BEHAVIOR(BallLogic);

    static constexpr float kFieldHalfWidth = 8.0f;
    static constexpr float kFieldHalfHeight = 5.0f;
    static constexpr float kPaddleHalfHeight = 1.0f;

public:
    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();
        auto& ball = Get<BallMovement>();

        transform.position += ball.velocity * dt;

        // Bounce off top/bottom walls
        if (std::abs(transform.position.y) > kFieldHalfHeight) {
            ball.velocity.y = -ball.velocity.y;
            transform.position.y = uz::Clamp(
                transform.position.y, -kFieldHalfHeight, kFieldHalfHeight
            );
        }

        // Check paddle collision
        GetWorld().ForEach<PaddleTag, uz::TransformComponent>(
            [&](uz::Entity, PaddleTag&, uz::TransformComponent& pt) {
                float dx = std::abs(transform.position.x - pt.position.x);
                float dy = std::abs(transform.position.y - pt.position.y);

                if (dx < 0.3f && dy < kPaddleHalfHeight) {
                    ball.velocity.x = -ball.velocity.x;
                    float offset = (transform.position.y - pt.position.y) / kPaddleHalfHeight;
                    ball.velocity.y += offset * 3.0f;
                    ball.velocity *= 1.05f;
                }
            }
        );

        // Score — ball passed a paddle
        if (std::abs(transform.position.x) > kFieldHalfWidth) {
            int scorer = (transform.position.x > 0) ? 0 : 1;
            auto* pong = GetWorld().GetSubsystem<PongSubsystem>();
            pong->scores[scorer]++;
            UZ_LOG(Game, Info, "Score: {} - {}", pong->scores[0], pong->scores[1]);

            // Reset ball to center
            transform.position = {0, 0, 0};
            ball.velocity = {5.0f * ((scorer == 0) ? 1.0f : -1.0f), 0.0f, 3.0f};
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Subsystem — game-wide state (score)
// ─────────────────────────────────────────────────────────────────────────────

class PongSubsystem : public uz::WorldSubsystem {
    UZ_SUBSYSTEM(PongSubsystem);
public:
    int scores[2] = {0, 0};
};
```

**No main(). No bootstrap. The developer runs:** `uzengine --project game.uzproject`

---

### 15.2 FPS Shooter

First-person camera, shooting projectiles, enemies that die. Demonstrates: mouse input, runtime entity spawning from behaviors, entity destruction, ForEach queries.

**scenes/fps.uzscene:**
```json
{
    "name": "FPS Arena",
    "entities": [
        {
            "name": "Player",
            "components": {
                "Transform": { "position": [0, 1.7, 0] },
                "Camera": { "fov": 90, "is_active": true },
                "FPSController": { "move_speed": 5.0, "look_sensitivity": 0.15 }
            }
        },
        {
            "name": "Floor",
            "components": {
                "Transform": { "scale": [50, 0.1, 50] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/gray.uzmat" }
            }
        },
        {
            "name": "Enemy_1",
            "components": {
                "Transform": { "position": [5, 1, 10] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/red.uzmat" },
                "Health": { "current": 50, "max": 50 },
                "EnemyTag": {}
            }
        },
        {
            "name": "Enemy_2",
            "components": {
                "Transform": { "position": [-3, 1, 15] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/red.uzmat" },
                "Health": { "current": 50, "max": 50 },
                "EnemyTag": {}
            }
        },
        {
            "name": "Sun",
            "components": {
                "Transform": {},
                "Light": { "type": "Directional", "intensity": 2.0 }
            }
        }
    ]
}
```

**scripts/fps.h:**
```cpp
#pragma once
#include "engine/behavior.h"
#include "engine/components.h"
#include "engine/input.h"

struct Health {
    float current = 100.0f;
    float max = 100.0f;
    UZ_COMPONENT(Health);
};

struct Projectile {
    Vec3 direction;
    float speed = 30.0f;
    float lifetime = 3.0f;
    UZ_COMPONENT(Projectile);
};

struct EnemyTag {
    UZ_COMPONENT(EnemyTag);
};

// ─────────────────────────────────────────────────────────────────────────────
// FPS Controller — attached to Player in .uzscene
// ─────────────────────────────────────────────────────────────────────────────

class FPSController : public uz::Behavior {
    UZ_BEHAVIOR(FPSController);

    float move_speed = 5.0f;       // serialized from .uzscene
    float look_sensitivity = 0.15f; // serialized from .uzscene
    float yaw = 0.0f, pitch = 0.0f;

public:
    void OnBeginPlay() override {
        uz::Application::GetInput().SetMouseCaptured(true);
    }

    void OnTick(float dt) override {
        auto& input = uz::Application::GetInput();
        auto& transform = Get<uz::TransformComponent>();

        // Mouse look
        Vec2 delta = input.GetMouseDelta();
        yaw -= delta.x * look_sensitivity;
        pitch = uz::Clamp(pitch - delta.y * look_sensitivity, -89.0f, 89.0f);
        transform.rotation = Quat::FromEuler(pitch, yaw, 0.0f);

        // WASD movement
        Vec3 move = {};
        if (input.IsKeyDown(Key::W)) move += transform.Forward();
        if (input.IsKeyDown(Key::S)) move -= transform.Forward();
        if (input.IsKeyDown(Key::D)) move += transform.Right();
        if (input.IsKeyDown(Key::A)) move -= transform.Right();
        if (move.LengthSq() > 0.0f)
            transform.position += move.Normalized() * move_speed * dt;

        // Shoot
        if (input.IsMouseButtonPressed(MouseButton::Left)) {
            auto bullet = GetWorld().CreateEntity("Bullet");
            GetWorld().Add<uz::TransformComponent>(bullet, {
                .position = transform.position + transform.Forward() * 0.5f,
                .scale = {0.1f, 0.1f, 0.1f}
            });
            GetWorld().Add<uz::MeshRendererComponent>(bullet, {
                .mesh = uz::Application::GetResources().Load<uz::Mesh>("meshes/sphere.uzmesh"),
                .material = uz::Application::GetResources().Load<uz::Material>("materials/yellow_emissive.uzmat")
            });
            GetWorld().Add<Projectile>(bullet, {.direction = transform.Forward()});
            GetWorld().Add<ProjectileLogic>(bullet);
        }

        if (input.IsKeyPressed(Key::Escape))
            uz::Application::Quit();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Projectile — spawned at runtime, not in .uzscene
// ─────────────────────────────────────────────────────────────────────────────

class ProjectileLogic : public uz::Behavior {
    UZ_BEHAVIOR(ProjectileLogic);

public:
    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();
        auto& proj = Get<Projectile>();

        transform.position += proj.direction * proj.speed * dt;

        proj.lifetime -= dt;
        if (proj.lifetime <= 0.0f) {
            GetWorld().DestroyEntity(GetEntity());
            return;
        }

        GetWorld().ForEach<EnemyTag, uz::TransformComponent, Health>(
            [&](uz::Entity enemy, EnemyTag&, uz::TransformComponent& et, Health& hp) {
                if ((transform.position - et.position).Length() < 1.0f) {
                    hp.current -= 25.0f;
                    if (hp.current <= 0.0f)
                        GetWorld().DestroyEntity(enemy);
                    GetWorld().DestroyEntity(GetEntity());
                }
            }
        );
    }
};
```

---

### 15.3 Flappy Bird

2D-style gameplay in 3D. Demonstrates: game state machine (waiting/playing/dead), subsystem with Tick for pipe spawning, runtime entity creation/destruction.

**scenes/flappy.uzscene:**
```json
{
    "name": "Flappy",
    "subsystems": ["FlappySubsystem"],
    "entities": [
        {
            "name": "Bird",
            "components": {
                "Transform": { "position": [-2, 0, 0], "scale": [0.4, 0.4, 0.4] },
                "MeshRenderer": { "mesh": "meshes/sphere.uzmesh", "material": "materials/yellow.uzmat" },
                "BirdController": {}
            }
        },
        {
            "name": "Camera",
            "components": {
                "Transform": { "position": [0, 0, -15] },
                "Camera": { "is_ortho": true, "ortho_size": 7, "is_active": true }
            }
        },
        {
            "name": "Sun",
            "components": {
                "Transform": {},
                "Light": { "type": "Directional" }
            }
        }
    ]
}
```

**scripts/flappy.h:**
```cpp
#pragma once
#include "engine/behavior.h"
#include "engine/subsystem.h"

// ─────────────────────────────────────────────────────────────────────────────
// Subsystem — game state + pipe spawning
// ─────────────────────────────────────────────────────────────────────────────

class FlappySubsystem : public uz::WorldSubsystem {
    UZ_SUBSYSTEM(FlappySubsystem);

public:
    enum Phase { WaitingToStart, Playing, Dead };
    Phase phase = WaitingToStart;
    int score = 0;
    float pipe_timer = 0.0f;

    bool CanTick() const override { return true; }

    void Tick(float dt) override {
        if (phase != Playing) return;

        pipe_timer += dt;
        if (pipe_timer >= 2.0f) {
            pipe_timer = 0.0f;
            SpawnPipePair();
        }
    }

private:
    void SpawnPipePair() {
        auto& res = uz::Application::GetResources();
        auto mesh = res.Load<uz::Mesh>("meshes/cube.uzmesh");
        auto mat = res.Load<uz::Material>("materials/green.uzmat");

        float gap_center = uz::RandomRange(-2.0f, 2.0f);
        float gap_half = 1.8f, pipe_h = 8.0f, x = 12.0f;

        auto top = GetWorld().CreateEntity("Pipe");
        GetWorld().Add<uz::TransformComponent>(top, {
            .position = {x, gap_center + gap_half + pipe_h * 0.5f, 0},
            .scale = {0.8f, pipe_h, 0.8f}
        });
        GetWorld().Add<uz::MeshRendererComponent>(top, {.mesh = mesh, .material = mat});
        GetWorld().Add<PipeScroller>(top);

        auto bot = GetWorld().CreateEntity("Pipe");
        GetWorld().Add<uz::TransformComponent>(bot, {
            .position = {x, gap_center - gap_half - pipe_h * 0.5f, 0},
            .scale = {0.8f, pipe_h, 0.8f}
        });
        GetWorld().Add<uz::MeshRendererComponent>(bot, {.mesh = mesh, .material = mat});
        GetWorld().Add<PipeScroller>(bot);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Behaviors
// ─────────────────────────────────────────────────────────────────────────────

class BirdController : public uz::Behavior {
    UZ_BEHAVIOR(BirdController);

    float velocity_y = 0.0f;
    static constexpr float kGravity = -12.0f;
    static constexpr float kFlapForce = 6.0f;

public:
    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();
        auto* gs = GetWorld().GetSubsystem<FlappySubsystem>();
        auto& input = uz::Application::GetInput();

        if (gs->phase == FlappySubsystem::WaitingToStart) {
            if (input.IsKeyPressed(Key::Space)) {
                gs->phase = FlappySubsystem::Playing;
                velocity_y = kFlapForce;
            }
            return;
        }
        if (gs->phase == FlappySubsystem::Dead) return;

        if (input.IsKeyPressed(Key::Space))
            velocity_y = kFlapForce;

        velocity_y += kGravity * dt;
        transform.position.y += velocity_y * dt;

        float tilt = uz::Clamp(velocity_y * 5.0f, -80.0f, 30.0f);
        transform.rotation = Quat::FromAxisAngle(Vec3::Right(), tilt);

        if (transform.position.y < -5.0f || transform.position.y > 6.0f) {
            gs->phase = FlappySubsystem::Dead;
            UZ_LOG(Game, Info, "Game Over! Score: {}", gs->score);
        }
    }
};

class PipeScroller : public uz::Behavior {
    UZ_BEHAVIOR(PipeScroller);

    bool scored = false;

public:
    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();
        auto* gs = GetWorld().GetSubsystem<FlappySubsystem>();
        if (gs->phase != FlappySubsystem::Playing) return;

        transform.position.x -= 3.0f * dt;

        if (!scored && transform.position.x < 0.0f) {
            scored = true;
            gs->score++;
        }

        if (transform.position.x < -12.0f)
            GetWorld().DestroyEntity(GetEntity());
    }
};
```

---

### 15.4 Turret Defense

Demonstrates: events/delegates, subsystem-driven wave spawning, AI targeting, inter-system communication, bullet tracking.

**scenes/turret_defense.uzscene:**
```json
{
    "name": "Turret Defense",
    "subsystems": ["WaveSubsystem"],
    "entities": [
        {
            "name": "Turret_1",
            "components": {
                "Transform": { "position": [-5, 0.5, -2], "scale": [0.6, 0.8, 0.6] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/blue.uzmat" },
                "Turret": { "range": 5, "fire_rate": 2, "damage": 10 },
                "TurretAI": {}
            }
        },
        {
            "name": "Turret_2",
            "components": {
                "Transform": { "position": [0, 0.5, 3], "scale": [0.6, 0.8, 0.6] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/blue.uzmat" },
                "Turret": { "range": 5, "fire_rate": 2, "damage": 10 },
                "TurretAI": {}
            }
        },
        {
            "name": "Ground",
            "components": {
                "Transform": { "scale": [30, 0.05, 20] },
                "MeshRenderer": { "mesh": "meshes/cube.uzmesh", "material": "materials/dark_gray.uzmat" }
            }
        },
        {
            "name": "Camera",
            "components": {
                "Transform": { "position": [0, 15, -10] },
                "Camera": { "fov": 50, "is_active": true }
            }
        },
        {
            "name": "Sun",
            "components": {
                "Transform": {},
                "Light": { "type": "Directional", "intensity": 2 }
            }
        }
    ]
}
```

**scripts/turret_defense.h:**
```cpp
#pragma once
#include "engine/behavior.h"
#include "engine/subsystem.h"
#include "engine/event.h"

// ─────────────────────────────────────────────────────────────────────────────
// Components
// ─────────────────────────────────────────────────────────────────────────────

struct Enemy {
    float speed = 2.0f;
    float health = 30.0f;
    int path_index = 0;
    UZ_COMPONENT(Enemy);
};

struct Turret {
    float range = 6.0f;
    float fire_rate = 1.0f;
    float cooldown = 0.0f;
    float damage = 10.0f;
    UZ_COMPONENT(Turret);
};

struct Bullet {
    uz::Entity target;
    float speed = 15.0f;
    UZ_COMPONENT(Bullet);
};

// ─────────────────────────────────────────────────────────────────────────────
// Events
// ─────────────────────────────────────────────────────────────────────────────

struct EnemyKilledEvent { uz::Entity enemy; Vec3 position; };
struct WaveCompleteEvent { int wave_number; };

// ─────────────────────────────────────────────────────────────────────────────
// Wave Spawner Subsystem
// ─────────────────────────────────────────────────────────────────────────────

static const Vec3 kWaypoints[] = {
    {-10, 0.5f, 0}, {-5, 0.5f, 0}, {-5, 0.5f, 5},
    {5, 0.5f, 5}, {5, 0.5f, -3}, {10, 0.5f, -3}
};

class WaveSubsystem : public uz::WorldSubsystem {
    UZ_SUBSYSTEM(WaveSubsystem);

public:
    int current_wave = 0;
    int enemies_alive = 0;

    uz::MulticastDelegate<EnemyKilledEvent> OnEnemyKilled;
    uz::MulticastDelegate<WaveCompleteEvent> OnWaveComplete;

    bool CanTick() const override { return true; }

    void OnWorldBeginPlay() override {
        // Subscribe to our own events for logging
        OnWaveComplete.Add([](WaveCompleteEvent e) {
            UZ_LOG(Game, Info, "Wave {} complete!", e.wave_number);
        });
        StartWave(1);
    }

    void Tick(float dt) override {
        if (enemies_to_spawn_ > 0) {
            spawn_timer_ -= dt;
            if (spawn_timer_ <= 0.0f) {
                SpawnEnemy();
                enemies_to_spawn_--;
                spawn_timer_ = 0.8f;
            }
        }
        if (enemies_to_spawn_ == 0 && enemies_alive == 0 && current_wave > 0) {
            OnWaveComplete.Broadcast({.wave_number = current_wave});
            StartWave(current_wave + 1);
        }
    }

private:
    int enemies_to_spawn_ = 0;
    float spawn_timer_ = 0.0f;

    void StartWave(int wave) {
        current_wave = wave;
        enemies_to_spawn_ = 5 + wave * 2;
        UZ_LOG(Game, Info, "Wave {} — {} enemies", wave, enemies_to_spawn_);
    }

    // Cached assets — loaded once at subsystem init
    UZ_ASSET(uz::Mesh, "meshes/sphere.uzmesh", enemy_mesh);
    UZ_ASSET(uz::Material, "materials/red.uzmat", enemy_mat);

    void SpawnEnemy() {
        auto e = GetWorld().CreateEntity("Enemy");
        GetWorld().Add<uz::TransformComponent>(e, {
            .position = {-10, 0.5f, 0}, .scale = {0.5f, 0.5f, 0.5f}
        });
        GetWorld().Add<uz::MeshRendererComponent>(e, {
            .mesh = enemy_mesh,      // UZ_ASSET — already loaded
            .material = enemy_mat
        });
        GetWorld().Add<Enemy>(e, {
            .speed = 2.0f + current_wave * 0.3f,
            .health = 20.0f + current_wave * 10.0f
        });
        GetWorld().Add<EnemyWalker>(e);
        enemies_alive++;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Behaviors
// ─────────────────────────────────────────────────────────────────────────────

class EnemyWalker : public uz::Behavior {
    UZ_BEHAVIOR(EnemyWalker);
    UZ_TICK_PARALLEL(true);  // enemies walk independently — safe for parallel execution

    // Cached references — resolved once at BeginPlay, ~1ns access per frame
    UZ_REF(uz::TransformComponent, transform);
    UZ_REF(Enemy, enemy);

    Vec3 move_dir_ = {};

public:
    /// OnTick (every frame): apply movement + check death.
    /// Cheap — just move along cached direction, check health.
    void OnTick(float dt) override {
        // Direct access via UZ_REF — no Get<T>() lookup per frame
        transform->position += move_dir_ * enemy->speed * dt;
        if (move_dir_.LengthSq() > 0.0f)
            transform->rotation = Quat::LookAt(move_dir_, Vec3::Up());

        // Death check — immediate response, not deferred
        if (enemy->health <= 0.0f) {
            auto* waves = GetWorld().GetSubsystem<WaveSubsystem>();
            waves->enemies_alive--;
            waves->OnEnemyKilled.Broadcast({.enemy = GetEntity(), .position = transform->position});
            GetWorld().DestroyEntity(GetEntity());
        }
    }

    // ─── Strategic: 10 Hz — waypoint navigation ─────────────────────────
    /// Evaluate waypoint progress and compute next move direction.
    /// Waypoint checks don't need per-frame precision.
    UZ_SCHEDULED(PathUpdate, 10.0f);
    void PathUpdate(float dt) {
        constexpr int kCount = sizeof(kWaypoints) / sizeof(kWaypoints[0]);

        if (enemy->path_index >= kCount) {
            GetWorld().GetSubsystem<WaveSubsystem>()->enemies_alive--;
            GetWorld().DestroyEntity(GetEntity());
            return;
        }

        Vec3 dir = kWaypoints[enemy->path_index] - transform->position;
        if (dir.Length() < 0.3f) {
            enemy->path_index++;
        }

        // Cache direction for OnTick to apply every frame (smooth movement)
        if (enemy->path_index < kCount) {
            move_dir_ = (kWaypoints[enemy->path_index] - transform->position).Normalized();
        } else {
            move_dir_ = Vec3::Zero();
        }
    }
};

class TurretAI : public uz::Behavior {
    UZ_BEHAVIOR(TurretAI);

    // Cached references — no Get<T>() per frame
    UZ_REF(uz::TransformComponent, transform);
    UZ_REF(Turret, turret);

    // Cached assets — loaded once, reused for every bullet spawn
    UZ_ASSET(uz::Mesh, "meshes/sphere.uzmesh", bullet_mesh);
    UZ_ASSET(uz::Material, "materials/yellow_emissive.uzmat", bullet_mat);

    uz::Entity target_ = uz::Entity::Invalid();

public:
    /// OnTick (every frame): smooth turret rotation toward current target.
    /// Cheap — just interpolate rotation. No scanning, no spawning.
    void OnTick(float dt) override {
        if (target_ == uz::Entity::Invalid()) return;
        if (!GetWorld().IsValid(target_)) { target_ = uz::Entity::Invalid(); return; }

        auto& ep = GetWorld().Get<uz::TransformComponent>(target_).position;
        Quat desired = Quat::LookAt((ep - transform->position).Normalized(), Vec3::Up());
        transform->rotation = Quat::Slerp(transform->rotation, desired, dt * 8.0f);
    }

    // ─── Reactive: 10 Hz — scan for targets + fire ──────────────────────
    /// Scan enemies in range, pick nearest, fire if cooldown ready.
    /// 10Hz is plenty for turret targeting — no need for per-frame scans.
    UZ_SCHEDULED(TargetAndFire, 10.0f);
    void TargetAndFire(float dt) {
        turret->cooldown -= dt;

        // Scan for nearest enemy in range
        target_ = uz::Entity::Invalid();
        float nearest_dist = turret->range;

        GetWorld().ForEach<Enemy, uz::TransformComponent>(
            [&](uz::Entity e, Enemy&, uz::TransformComponent& et) {
                float d = (et.position - transform->position).Length();
                if (d < nearest_dist) { nearest_dist = d; target_ = e; }
            }
        );

        // Fire if target found and cooldown ready
        if (target_ != uz::Entity::Invalid() && turret->cooldown <= 0.0f) {
            auto bullet = GetWorld().CreateEntity("Bullet");
            GetWorld().Add<uz::TransformComponent>(bullet, {
                .position = transform->position + Vec3::Up() * 0.5f,
                .scale = {0.1f, 0.1f, 0.1f}
            });
            GetWorld().Add<uz::MeshRendererComponent>(bullet, {
                .mesh = bullet_mesh,      // UZ_ASSET — already loaded, zero cost
                .material = bullet_mat
            });
            GetWorld().Add<Bullet>(bullet, {.target = target_});
            GetWorld().Add<BulletLogic>(bullet);
            turret->cooldown = 1.0f / turret->fire_rate;
        }
    }
};

class BulletLogic : public uz::Behavior {
    UZ_BEHAVIOR(BulletLogic);

public:
    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();
        auto& bullet = Get<Bullet>();

        if (!GetWorld().IsValid(bullet.target)) {
            GetWorld().DestroyEntity(GetEntity());
            return;
        }

        auto& tp = GetWorld().Get<uz::TransformComponent>(bullet.target).position;
        Vec3 dir = tp - transform.position;

        if (dir.Length() < 0.3f) {
            GetWorld().Get<Enemy>(bullet.target).health -= 10.0f;
            GetWorld().DestroyEntity(GetEntity());
        } else {
            transform.position += dir.Normalized() * bullet.speed * dt;
        }
    }
};
```

---

### 15.5 Key Patterns Visible in the Examples

**What the developer writes:** Only Behaviors, Components, and Subsystems. Zero bootstrap.

**What the scene file defines:** Entity layout, component data, which behaviors are attached, which subsystems to activate.

**What the engine handles automatically:**
- Window creation, RHI init, renderer setup (from .uzproject)
- Scene loading and entity deserialization (from .uzscene)
- Behavior instantiation by name via ClassRegistry
- Lifecycle dispatch (OnBeginPlay → OnTick → OnEndPlay)
- Render loop (collect MeshRenderers, build draw calls, present)

### 15.6 API Coverage Matrix

| System | Pong | FPS | Flappy | Turret |
|---|---|---|---|---|
| Scene-driven bootstrap (.uzscene) | ✓ | ✓ | ✓ | ✓ |
| UZ_BEHAVIOR / UZ_COMPONENT macros | ✓ | ✓ | ✓ | ✓ |
| Behavior.Get\<T\>() shorthand | ✓ | ✓ | ✓ | ✓ |
| TransformComponent | ✓ | ✓ | ✓ | ✓ |
| MeshRendererComponent | ✓ | ✓ | ✓ | ✓ |
| CameraComponent | ✓ | ✓ | ✓ | ✓ |
| LightComponent | ✓ | ✓ | ✓ | ✓ |
| Behavior (OnTick) | ✓ | ✓ | ✓ | ✓ |
| Behavior (OnBeginPlay) | ✓ | ✓ | | |
| Custom components (data) | ✓ | ✓ | | ✓ |
| ForEach query | ✓ | ✓ | | ✓ |
| Entity.IsValid() | | | | ✓ |
| Input (keyboard) | ✓ | ✓ | ✓ | |
| Input (mouse + capture) | | ✓ | | |
| Runtime entity spawning | | ✓ | ✓ | ✓ |
| Deferred DestroyEntity | | ✓ | ✓ | ✓ |
| WorldSubsystem | ✓ | | ✓ | ✓ |
| Subsystem.CanTick() | | | ✓ | ✓ |
| MulticastDelegate events | | | | ✓ |
| TickGroup selection | ✓ | | | |
| UZ_SCHEDULED (independent frequency) | | | | ✓ |
| UZ_TICK_PARALLEL | | | | ✓ |
| Game state machine | | | ✓ | |
| Scene transitions (LoadScene) | | | | |
| UZ_REF (cached component access) | | | | ✓ |
| UZ_ASSET (cached resource) | | | | ✓ |
| UZ_ENTITY_REF (cross-entity ref) | | | | |
| UZ_OBSERVABLE (reactive fields) | | | | |
| UZ_LOG | ✓ | | ✓ | ✓ |

---

## 16. Reference & Reactivity System

### 16.1 The Problem

In Unity, calling `GetComponent<T>()` every frame inside `Update()` is a common anti-pattern — it's a type-based lookup each call. In Unreal, the best practice is to cache references in `BeginPlay` using `UPROPERTY()`. But without engine support, developers must manually write boilerplate: store pointers, null-check, handle invalidation.

Similarly, cross-entity data access (`GetWorld().Get<TransformComponent>(other_entity).position`) is verbose and repeated constantly. And polling for changes (`if (health != last_health_)`) wastes CPU when most values change rarely.

### 16.2 UZ_REF — Cached Component Access

`UZ_REF` declares a cached reference to a component on the **owning entity**. Resolved automatically during `OnBeginPlay`, accessed as a direct pointer with O(1) array lookup and generation safety check.

```cpp
class EnemyWalker : public uz::Behavior {
    UZ_BEHAVIOR(EnemyWalker);

    // Cached references — resolved once at BeginPlay, ~1ns access thereafter
    UZ_REF(uz::TransformComponent, transform);
    UZ_REF(Enemy, enemy);
    UZ_REF(Health, health);

    Vec3 move_dir_ = {};

public:
    void OnTick(float dt) override {
        // Direct access — no Get<T>() lookup per frame
        transform->position += move_dir_ * enemy->speed * dt;
    }
};
```

**Implementation:** `UZ_REF` stores entity handle + component type ID. Access dereferences via `world.components[type_id][entity.index]` — a single array lookup (~1ns). The generation check catches stale references (component removed or entity destroyed).

**Invariant:** If the component is removed while a `UZ_REF` exists, access returns nullptr (safe) and logs a warning in dev builds.

### 16.3 UZ_ASSET — Cached Resource Handle

`UZ_ASSET` declares a resource that loads once during init and stays cached. Eliminates repeated `Application::GetResources().Load<T>()` calls.

```cpp
class WaveSubsystem : public uz::WorldSubsystem {
    UZ_SUBSYSTEM(WaveSubsystem);

    // Loaded once at subsystem init — zero cost per spawn
    UZ_ASSET(uz::Mesh, "meshes/sphere.uzmesh", enemy_mesh);
    UZ_ASSET(uz::Material, "materials/red.uzmat", enemy_mat);
    UZ_ASSET(uz::Mesh, "meshes/sphere.uzmesh", bullet_mesh);
    UZ_ASSET(uz::Material, "materials/yellow_emissive.uzmat", bullet_mat);

    void SpawnEnemy() {
        auto e = GetWorld().CreateEntity("Enemy");
        GetWorld().Add<uz::MeshRendererComponent>(e, {
            .mesh = enemy_mesh,      // already loaded, just copy handle
            .material = enemy_mat
        });
        // ...
    }
};
```

**Hot-reload:** When the asset is reloaded by the developer layer, the `ResourceHandle` inside `UZ_ASSET` automatically points to the new version. No callback registration needed — it's built into `ResourceHandle`.

### 16.4 UZ_ENTITY_REF — Cross-Entity Reference

`UZ_ENTITY_REF` caches a reference to **another entity** by name or tag. Resolved at BeginPlay via world query, validated each access via generation check.

```cpp
class EnemyAI : public uz::Behavior {
    UZ_BEHAVIOR(EnemyAI);

    // Find entity named "Player" at BeginPlay, cache handle
    UZ_ENTITY_REF("Player", player);

    // Cache a component on the referenced entity
    UZ_ENTITY_COMPONENT(player, uz::TransformComponent, player_transform);

public:
    void OnTick(float dt) override {
        if (!player.IsValid()) return;  // player destroyed? skip

        auto& transform = Get<uz::TransformComponent>();
        Vec3 to_player = player_transform->position - transform.position;
        // Chase player...
    }
};
```

**Resolution strategies:**
- `UZ_ENTITY_REF("name", var)` — find by entity name (debug builds only, slow)
- `UZ_ENTITY_REF_TAG(PlayerTag, var)` — find first entity with component type (fast, O(1) via sparse set)
- `UZ_ENTITY_REF_NEAREST(EnemyTag, var)` — find nearest entity with tag (spatial query)

### 16.5 UZ_SYNC — Read-Only Cross-Entity Data Cache

`UZ_SYNC` copies a field from another entity's component once per frame, before tick groups run. Useful for frequently-read cross-entity data like player position.

```cpp
class EnemyAI : public uz::Behavior {
    UZ_BEHAVIOR(EnemyAI);

    UZ_ENTITY_REF_TAG(PlayerTag, player);

    // Synced once per frame before tick groups — zero lookup cost during OnTick
    UZ_SYNC(player, uz::TransformComponent, position, player_pos);  // Vec3
    UZ_SYNC(player, Health, current, player_health);                 // float

public:
    void OnTick(float dt) override {
        // player_pos and player_health are plain member variables, updated automatically
        Vec3 to_player = player_pos - Get<uz::TransformComponent>().position;
        if (player_health <= 0.0f) {
            // Player dead, stop chasing
        }
    }
};
```

**When to use:** Data that changes every frame (position, velocity) where you need to read it from many entities. 200 enemies reading `player_pos` = 200 cheap copies (memcpy of Vec3) vs 200 world lookups + generation checks.

**When NOT to use:** Data that changes rarely — use `UZ_OBSERVABLE` + `Observe<>` instead.

### 16.6 UZ_OBSERVABLE — Reactive Field Change Detection

`UZ_OBSERVABLE` marks a component field for change detection. When the value changes, registered observers are notified. This replaces per-frame polling with event-driven updates.

```cpp
/// Health component with observable 'current' field.
/// Writing to 'current' via Set() dispatches change events.
struct Health {
    float max = 100.0f;
    UZ_OBSERVABLE(float, current, 100.0f);  // generates getter, setter, and change delegate
    UZ_COMPONENT(Health);
};
```

**Observer registration from behaviors:**

```cpp
class HealthBar : public uz::Behavior {
    UZ_BEHAVIOR(HealthBar);

    UZ_REF(Health, health);
    UZ_REF(HealthBarUI, ui);

public:
    void OnBeginPlay() override {
        // Callback only fires when health.current CHANGES — not every frame
        Observe(health, &Health::current, [this](float old_val, float new_val) {
            ui->fill_percent = new_val / health->max;

            if (new_val < old_val)
                ui->FlashRed();  // damage feedback

            if (new_val <= 0.0f)
                GetWorld().DestroyEntity(GetEntity());
        });
    }

    // No OnTick needed! Zero per-frame cost.
};
```

**Performance comparison:**
- **Without reactivity:** 500 entities × HealthBar polling `if (health != last_)` = 500 branches/frame
- **With reactivity:** 0 checks/frame + N callbacks only when damage happens (e.g., 3 hits/frame = 3 callbacks)

**Cost:** Each `UZ_OBSERVABLE` field adds ~8 bytes (delegate list pointer) to the component. The setter compares old vs new value before dispatching — a single branch per write.

### 16.7 When to Use What

| Macro | Purpose | Access Cost | Memory | Best For |
|---|---|---|---|---|
| `UZ_REF(T, name)` | Cache own-entity component | ~1ns (array lookup) | 8 bytes (handle) | Replace Get<T>() in OnTick |
| `UZ_ASSET(T, path, name)` | Cache loaded resource | 0 (already loaded) | 16 bytes (handle) | Replace Load<T>() in spawn code |
| `UZ_ENTITY_REF(q, name)` | Cache other-entity handle | ~1ns (generation check) | 8 bytes (handle) | Player ref, target ref |
| `UZ_SYNC(src, C, field, name)` | Read-only copy per frame | 0 (pre-copied) | sizeof(field) | Position, velocity (changes every frame) |
| `UZ_OBSERVABLE(T, name, default)` | Reactive field with change events | ~1 branch per write | ~8 bytes (delegate ptr) | Health, score, state (changes rarely) |

**Rules of thumb:**
- Field changes **every frame** (position, velocity) → `UZ_REF` or `UZ_SYNC`
- Field changes **rarely** (health, score, game state) → `UZ_OBSERVABLE` + `Observe<>`
- Field **never changes** at runtime (mesh, material) → `UZ_ASSET`
- Need reference to **own entity's component** → `UZ_REF`
- Need reference to **another entity** → `UZ_ENTITY_REF`

### 16.8 Anti-Patterns — What NOT to Do

**1. Don't make position/rotation observable:**
```cpp
// ❌ WRONG — position changes 60x/sec, delegate dispatch overhead > poll cost
struct TransformComponent {
    UZ_OBSERVABLE(Vec3, position, {});  // fires 60 events/sec per entity!
};

// ✅ CORRECT — just read it directly
UZ_REF(uz::TransformComponent, transform);
transform->position;  // ~1ns
```

**2. Don't create bidirectional sync:**
```cpp
// ❌ WRONG — who has authority? race condition
UZ_SYNC(player, Health, current, player_hp);
// then writing back: player_hp = 50;  ← writes to LOCAL copy, not player's Health

// ✅ CORRECT — read via SYNC, write via world access
auto& hp = GetWorld().Get<Health>(player.GetEntity());
hp.SetCurrent(50.0f);  // goes through UZ_OBSERVABLE setter
```

**3. Don't cascade observers:**
```cpp
// ❌ WRONG — A observes B, B observes C, C change cascades to A in same frame
// This is React.js, not a game engine. Leads to infinite loops and frame spikes.

// ✅ CORRECT — limit to 1 level of observation. If you need A→B→C, use events.
```

### 16.9 MVP vs Full Implementation

**MVP (Phase 2):**
- `UZ_REF` — cached component access with generation check
- `UZ_ASSET` — cached resource handles

**Phase 2 — Cross-Entity:**
- `UZ_ENTITY_REF` — name/tag-based entity lookup
- `UZ_ENTITY_COMPONENT` — component on referenced entity
- `UZ_SYNC` — per-frame field copy

**Phase 3 — Reactivity:**
- `UZ_OBSERVABLE` — setter with change detection
- `Observe<>` — callback registration
- Observer auto-cleanup on entity/behavior destruction

---

## 17. GameInstance — Cross-Scene Persistence

### 17.1 The Problem

`WorldSubsystem` dies with the `World`. When you call `LoadScene("level2")`, all entities, components, and subsystems are destroyed. Where do you store the player's total score, inventory, settings, or online session data?

In Unreal, `UGameInstance` solves this — it persists from boot to shutdown. In Unity, developers hack around it with `DontDestroyOnLoad` singletons. In Godot, Autoload nodes serve a similar purpose. UzEngine needs a first-class solution.

### 17.2 Design

```cpp
/// Persists across scene transitions. One per application.
/// Created at startup (Phase 0), destroyed at shutdown.
/// Game developers subclass this and register it in .uzproject config.
///
/// # Lifecycle
/// - `OnInit()` — after engine init, before first scene load
/// - `OnSceneLoaded(path)` — after each scene finishes loading
/// - `OnSceneUnloaded(path)` — before a scene is destroyed (on transition)
/// - `OnShutdown()` — application closing
///
/// # Access
/// - `GameInstance::Get()` — returns base reference from anywhere
/// - `GameInstance::GetAs<T>()` — returns typed reference (game subclass)
///
/// # Thread Safety
/// GameInstance is accessed from the main thread only. Scene transitions
/// are synchronous in the MVP — no concurrent access.
class GameInstance {
public:
    virtual ~GameInstance() = default;

    virtual void OnInit() {}
    virtual void OnSceneLoaded(const char* path) {}
    virtual void OnSceneUnloaded(const char* path) {}
    virtual void OnShutdown() {}

    /// Access the singleton instance.
    static GameInstance& Get();

    /// Typed access — game code uses their subclass.
    template<typename T>
    static T& GetAs();
};
```

### 17.3 Project Registration

The game's `GameInstance` subclass is declared in `.uzproject`:

```json
// game.uzproject
{
    "game_instance": "MyGameInstance",
    "startup_scene": "scenes/main_menu.uzscene"
}
```

The engine creates the `GameInstance` via `ClassRegistry` before loading the startup scene.

### 17.4 Usage Example

```cpp
class MyGameInstance : public uz::GameInstance {
    UZ_GAME_INSTANCE(MyGameInstance);

public:
    int total_score = 0;
    int current_level = 1;
    PlayerProfile profile;
    SettingsData settings;

    void OnInit() override {
        profile = LoadProfile("save/profile.dat");
        settings = LoadSettings("save/settings.json");
    }

    void OnSceneLoaded(const char* path) override {
        UZ_LOG(Game, Info, "Scene loaded: path='{}'", path);
    }

    void OnShutdown() override {
        SaveProfile("save/profile.dat", profile);
        SaveSettings("save/settings.json", settings);
    }
};

// Access from any behavior or subsystem:
auto& gi = uz::GameInstance::GetAs<MyGameInstance>();
gi.total_score += 100;
```

### 17.5 Comparison

| Engine | Mechanism | Weight |
|---|---|---|
| Unreal 5 | `UGameInstance` (UObject, GC, reflection) | Heavy |
| Unity | No equivalent (manual `DontDestroyOnLoad` singletons) | Hack |
| Godot | Autoload nodes | Medium |
| **UzEngine** | `GameInstance` (lightweight, static access, no GC) | **Light** |

### 17.6 Implementation Phase

**MVP Phase 2** — required for scene transitions and save/load.

---

## 18. Player Controller & Possession

### 18.1 The Problem

Without a controller abstraction, input is coupled directly to the character entity. When the player dies and respawns, all input state is lost. When the player enters a vehicle, you need to duplicate or redirect input logic. In multiplayer, mapping inputs to specific pawns becomes ad-hoc.

Unreal's `APlayerController` solves this with the possess/unpossess pattern — the controller persists and can be attached to different pawns. Unity and Godot have no equivalent; developers must build it manually.

### 18.2 Design

```cpp
/// Intermediary between human input and the controlled entity (Pawn).
/// Persists across pawn changes (death/respawn, vehicle enter/exit).
/// One PlayerController per local player (splitscreen = multiple).
///
/// # Ownership
/// - Input is always routed through the controller, never the pawn directly.
/// - The controller decides what input means; the pawn executes movement.
///
/// # Lifecycle
/// - Created in the scene (or spawned at runtime).
/// - Possesses its initial pawn via scene config or OnBeginPlay.
/// - Survives pawn destruction — can possess a new pawn later.
class PlayerController : public uz::Behavior {
    UZ_BEHAVIOR(PlayerController);

public:
    /// The entity this controller currently possesses.
    uz::Entity GetPawn() const;

    /// Transfer control to a different entity.
    /// Calls OnUnpossess() on old pawn, OnPossess() on new pawn.
    void Possess(uz::Entity pawn);

    /// Release the current pawn without possessing a new one.
    void Unpossess();

    /// Override to set up input bindings. Called on BeginPlay and on every Possess().
    /// Add mapping contexts, bind action callbacks here.
    /// Previous bindings are automatically cleared on Unpossess().
    virtual void OnSetupInput(uz::InputComponent& input) {}
};

/// Any entity that can be possessed by a PlayerController.
/// Marker behavior — attach to entities that accept possession.
class Pawn : public uz::Behavior {
    UZ_BEHAVIOR(Pawn);

public:
    /// Returns the controller currently possessing this pawn, or nullptr.
    PlayerController* GetController() const;

    /// True if a PlayerController is possessing this pawn.
    bool IsPlayerControlled() const;

    /// Called when a controller possesses this pawn.
    virtual void OnPossess(PlayerController& controller) {}

    /// Called when the controller releases this pawn.
    virtual void OnUnpossess() {}
};
```

### 18.3 Usage Example — On-Foot + Mount

The key pattern: `OnSetupInput()` is called on every `Possess()`. The controller checks what type of pawn it is possessing and sets up the appropriate mapping context. When the player dismounts, `Unpossess()` clears all bindings, then `Possess(foot_pawn)` calls `OnSetupInput()` again with the on-foot context.

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// Actions (shared, global)
// ─────────────────────────────────────────────────────────────────────────────
inline InputAction IA_Move     { "IA_Move",     InputValueType::Axis2D };
inline InputAction IA_Jump     { "IA_Jump",     InputValueType::Boolean };
inline InputAction IA_Attack   { "IA_Attack",   InputValueType::Boolean };
inline InputAction IA_Interact { "IA_Interact", InputValueType::Boolean };
inline InputAction IA_Dismount { "IA_Dismount", InputValueType::Boolean };
inline InputAction IA_Steer    { "IA_Steer",    InputValueType::Axis2D };
inline InputAction IA_Gallop   { "IA_Gallop",   InputValueType::Boolean };

// ─────────────────────────────────────────────────────────────────────────────
// Mapping contexts — different key layouts for different modes
// ─────────────────────────────────────────────────────────────────────────────
inline InputMappingContext IMC_OnFoot = { "IMC_OnFoot", {
    { &IA_Move,     Key::W, {new Negate({false,true,false}), new SwizzleAxis(YXZ)}, {} },
    { &IA_Move,     Key::S, {new SwizzleAxis(YXZ)}, {} },
    { &IA_Move,     Key::A, {new Negate()}, {} },
    { &IA_Move,     Key::D, {}, {} },
    { &IA_Jump,     Key::Space, {}, {new TriggerPressed()} },
    { &IA_Attack,   Key::MouseLeft, {}, {} },
    { &IA_Interact, Key::E, {}, {new TriggerPressed()} },
}};

inline InputMappingContext IMC_Mount = { "IMC_Mount", {
    { &IA_Steer,    Key::W, {new Negate({false,true,false}), new SwizzleAxis(YXZ)}, {} },
    { &IA_Steer,    Key::S, {new SwizzleAxis(YXZ)}, {} },
    { &IA_Steer,    Key::A, {new Negate()}, {} },
    { &IA_Steer,    Key::D, {}, {} },
    { &IA_Gallop,   Key::Space, {}, {} },          // hold to gallop (default Down trigger)
    { &IA_Attack,   Key::MouseLeft, {}, {} },       // same key, different pawn executes it
    { &IA_Dismount, Key::E, {}, {new TriggerPressed()} },  // E = dismount instead of interact
}};

// ─────────────────────────────────────────────────────────────────────────────
// Player Controller — persists across pawn changes
// ─────────────────────────────────────────────────────────────────────────────
class MyPlayerController : public uz::PlayerController {
    UZ_BEHAVIOR(MyPlayerController);

    uz::Entity foot_pawn_;   // saved reference for dismounting back

public:
    /// Called on every Possess(). Previous bindings are auto-cleared.
    void OnSetupInput(uz::InputComponent& input) override {
        auto& pawn_world = GetWorld();
        auto pawn = GetPawn();

        if (pawn_world.Has<MountPawn>(pawn)) {
            // ── Mount mode ──────────────────────────────────────────
            auto& sub = pawn_world.GetSubsystem<InputSubsystem>();
            sub.AddMappingContext(&IMC_Mount, 0);

            input.BindAction(&IA_Steer, ETriggerEvent::Triggered,
                [this](const InputActionInstance& ctx) {
                    Vec2 dir = ctx.value.AsAxis2D();
                    GetWorld().Get<MountMovement>(GetPawn()).Steer(dir);
                });

            input.BindAction(&IA_Gallop, ETriggerEvent::Triggered,
                [this](const InputActionInstance& ctx) {
                    GetWorld().Get<MountMovement>(GetPawn()).Gallop();
                });

            input.BindAction(&IA_Dismount, ETriggerEvent::Triggered,
                [this](const InputActionInstance& ctx) {
                    Dismount();
                });

        } else {
            // ── On-foot mode ────────────────────────────────────────
            auto& sub = pawn_world.GetSubsystem<InputSubsystem>();
            sub.AddMappingContext(&IMC_OnFoot, 0);

            input.BindAction(&IA_Move, ETriggerEvent::Triggered,
                [this](const InputActionInstance& ctx) {
                    Vec2 move = ctx.value.AsAxis2D();
                    GetWorld().Get<CharacterMovementComponent>(GetPawn()).AddInput({move.x, 0, move.y});
                });

            input.BindAction(&IA_Jump, ETriggerEvent::Triggered,
                [this](const InputActionInstance& ctx) {
                    GetWorld().Get<CharacterMovementComponent>(GetPawn()).Jump();
                });

            input.BindAction(&IA_Interact, ETriggerEvent::Triggered,
                [this](const InputActionInstance& ctx) {
                    TryInteract();
                });
        }

        // Attack works in both modes — same key, pawn-specific execution
        input.BindAction(&IA_Attack, ETriggerEvent::Triggered,
            [this](const InputActionInstance& ctx) {
                if (auto* combat = GetWorld().TryGet<CombatComponent>(GetPawn())) {
                    combat->Attack();
                }
            });
    }

    void Mount(uz::Entity mount_entity) {
        foot_pawn_ = GetPawn();
        Unpossess();                    // clears on-foot bindings + context
        Possess(mount_entity);          // triggers OnSetupInput() with mount pawn
    }

    void Dismount() {
        Unpossess();                    // clears mount bindings + context
        Possess(foot_pawn_);            // triggers OnSetupInput() with foot pawn
    }
};
```

### 18.3.1 Possess/Unpossess Flow

```
Player presses E near horse
    → TryInteract() detects MountPawn nearby
    → controller.Mount(horse_entity)
        → Unpossess()
            ← clears all InputComponent bindings
            ← RemoveMappingContext(IMC_OnFoot)
            ← calls pawn.OnUnpossess()
        → Possess(horse_entity)
            ← calls horse.OnPossess(controller)
            ← calls OnSetupInput(input)  ← now sets up IMC_Mount + mount bindings
            ← player is now steering the horse

Player presses E while mounted
    → Dismount()
        → Unpossess()
            ← clears mount bindings + IMC_Mount
        → Possess(foot_pawn_)
            ← calls OnSetupInput(input)  ← now sets up IMC_OnFoot + foot bindings
            ← player walks again
```

### 18.4 Scene Configuration

```json
// fps.uzscene
{
    "entities": [
        {
            "name": "PlayerController_0",
            "components": {
                "MyPlayerController": { "initial_pawn": "Player" }
            }
        },
        {
            "name": "Player",
            "components": {
                "Transform": { "position": [0, 1.7, 0] },
                "CharacterPawn": { "move_speed": 5.0 },
                "Health": { "max": 100, "current": 100 }
            }
        }
    ]
}
```

### 18.5 Comparison

| Engine | Mechanism | Notes |
|---|---|---|
| Unreal 5 | `APlayerController` (AActor, heavy) | Full-featured, possess/unpossess |
| Unity | No equivalent | Manual implementation required |
| Godot | No equivalent | Manual implementation required |
| **UzEngine** | `PlayerController` (Behavior, lightweight) | **Possess/unpossess, input routing** |

### 18.6 Implementation Phase

**Phase 3** — requires input system (Enhanced Input) first.

---

## 19. Property System & Reflection

### 19.1 The Problem

Game engines need runtime type information for: editor inspectors (display and edit fields), serialization (save/load scenes), networking (replicate state), and data binding (UI ↔ game state). In Unreal, the heavy `UObject` + `UPROPERTY()` system handles all of this. In Unity, C# reflection + `[SerializeField]` serves the same role.

UzEngine does **not** have a universal base object (no `UObject` equivalent). Instead, reflection is opt-in via macros — only types that need it pay for it.

### 19.2 Design — No Universal Base Object

```
// What UObject gives you, and how UzEngine achieves it without a universal base:
// ┌───────────────────┬──────────────────────────────────────────┐
// │ UObject Feature   │ UzEngine Equivalent                      │
// ├───────────────────┼──────────────────────────────────────────┤
// │ Reflection        │ ClassRegistry + UZ_REFLECT macro         │
// │ Serialization     │ UZ_COMPONENT auto-serialization          │
// │ GC                │ Entity generation check (no GC needed)   │
// │ CDO               │ .uzscene default values                  │
// │ Property system   │ UZ_PROPERTY macro (field enumeration)    │
// │ Networking        │ UZ_REPLICATED macro (future)             │
// │ Editor metadata   │ UZ_PROPERTY annotations                  │
// └───────────────────┴──────────────────────────────────────────┘
```

### 19.3 UZ_PROPERTY — Field Declaration

```cpp
/// Component with reflected properties.
/// UZ_PROPERTY generates: field storage, name string, default value,
/// tooltip, type info, and enables iteration over all properties at runtime.
struct Enemy {
    UZ_COMPONENT(Enemy);

    UZ_PROPERTY(float, speed, 2.0f, "Movement speed in m/s");
    UZ_PROPERTY(float, health, 30.0f, "Current health points");
    UZ_PROPERTY(int, path_index, 0, "Current waypoint index");
};
```

### 19.4 Runtime Property Iteration

```cpp
// Editor inspector, serialization, and networking all use this:
uz::ClassRegistry::ForEachProperty<Enemy>([](const uz::PropertyInfo& prop) {
    // prop.name     = "speed"
    // prop.type     = uz::PropertyType::Float
    // prop.offset   = offsetof(Enemy, speed)
    // prop.default_value = 2.0f
    // prop.tooltip  = "Movement speed in m/s"
});
```

### 19.5 PropertyInfo Structure

```cpp
/// Describes a single reflected property on a component or behavior.
struct PropertyInfo {
    const char* name;           /// Field name (e.g., "speed")
    PropertyType type;          /// Float, Int, Bool, Vec3, String, EntityRef, Asset, Enum
    size_t offset;              /// Byte offset from struct start (for direct memory access)
    size_t size;                /// sizeof(field)
    const char* tooltip;        /// Human-readable description (shown in editor)
    const void* default_value;  /// Pointer to default value (for reset-to-default)

    /// Category for editor grouping (optional).
    const char* category = "";

    /// Range constraints for numeric types (optional).
    float min = -FLT_MAX;
    float max = FLT_MAX;

    /// Flags: read-only, hidden, replicated, save-game, etc.
    PropertyFlags flags = PropertyFlags::None;
};

enum class PropertyFlags : uint32_t {
    None        = 0,
    ReadOnly    = 1 << 0,   /// Cannot be edited in inspector
    Hidden      = 1 << 1,   /// Not shown in inspector
    Replicated  = 1 << 2,   /// Sent over network (future)
    SaveGame    = 1 << 3,   /// Included in save files
    Transient   = 1 << 4,   /// Not serialized to .uzscene
};
```

### 19.6 Advanced Property Types

```cpp
struct Weapon {
    UZ_COMPONENT(Weapon);

    // Basic types
    UZ_PROPERTY(float, damage, 25.0f, "Damage per hit");
    UZ_PROPERTY(int, ammo, 30, "Current ammo count");
    UZ_PROPERTY(bool, automatic, false, "Full-auto fire mode");

    // Math types
    UZ_PROPERTY(Vec3, muzzle_offset, Vec3(0, 0, 1), "Local-space muzzle position");
    UZ_PROPERTY(Color, tracer_color, Color::Yellow(), "Bullet tracer color");

    // Enum types (auto-generates dropdown in editor)
    UZ_PROPERTY(WeaponType, type, WeaponType::Rifle, "Weapon category");

    // Asset references (shows asset picker in editor)
    UZ_PROPERTY(uz::AssetRef<uz::Mesh>, mesh, {}, "Weapon mesh");
    UZ_PROPERTY(uz::AssetRef<uz::Sound>, fire_sound, {}, "Fire sound effect");

    // Entity references (shows entity picker in editor)
    UZ_PROPERTY(uz::EntityRef, owner, {}, "Entity that owns this weapon");

    // Range-constrained (editor shows slider)
    UZ_PROPERTY_RANGE(float, spread, 0.05f, 0.0f, 1.0f, "Bullet spread angle");
};
```

### 19.7 Comparison

| Engine | Mechanism | Weight | GC Required |
|---|---|---|---|
| Unreal 5 | `UPROPERTY()` on `UObject` (mandatory base) | Heavy | Yes |
| Unity | `[SerializeField]` (C# reflection) | Medium | Yes (C# GC) |
| Godot | `@export` + Variant system | Medium | RefCounted |
| **UzEngine** | `UZ_PROPERTY()` (opt-in macro, no base class) | **Light** | **No** |

### 19.8 Implementation Phase

**MVP Phase 2** — required for editor inspector and scene serialization.

---

## 20. Data Tables — Data-Driven Design

### 20.1 The Problem

Hardcoding `float damage = 25.0f` in C++ code is terrible for game balancing. Every number change requires recompilation. Designers want to tweak weapon stats, enemy waves, XP curves, and drop rates without touching code.

In Unreal, `UDataTable` provides spreadsheet-like tables with typed rows, editable in the editor or imported from CSV. Unity uses `ScriptableObject` (manual, no table format). Godot uses `Resource` files.

### 20.2 Design

```cpp
/// A typed table of rows, loaded from .uzdata (JSON) files.
/// Each row has a string key and typed columns defined by the RowType struct.
/// Hot-reloadable — designers edit JSON, values update in real-time.
///
/// # File Format
/// `.uzdata` files are JSON with a `row_type` header and a `rows` object.
/// The row type must have `UZ_DATA_ROW` and `UZ_PROPERTY` declarations.
///
/// # Access
/// - `Find(key)` — O(1) hash lookup, returns nullptr if not found
/// - `ForEach(fn)` — iterate all rows
/// - `Count()` — number of rows
///
/// # Hot-Reload
/// When the `.uzdata` file changes on disk, the table is re-parsed and
/// all existing `DataTable<T>` handles automatically see the new data.
template<typename RowType>
class DataTable {
public:
    /// Find a row by string key. Returns nullptr if not found.
    const RowType* Find(const char* key) const;

    /// Iterate all rows with their keys.
    void ForEach(std::function<void(const char* key, const RowType&)> fn) const;

    /// Number of rows in the table.
    int Count() const;
};
```

### 20.3 Row Definition

```cpp
/// C++ row struct — defines the columns and their types.
/// UZ_DATA_ROW registers it with ClassRegistry for JSON deserialization.
struct WeaponStats {
    UZ_DATA_ROW(WeaponStats);

    UZ_PROPERTY(float, damage, 10.0f, "Damage per hit");
    UZ_PROPERTY(float, fire_rate, 1.0f, "Shots per second");
    UZ_PROPERTY(int, magazine, 10, "Ammo capacity");
    UZ_PROPERTY(float, reload_time, 2.0f, "Reload duration in seconds");
};
```

### 20.4 Data File Format (.uzdata)

```json
// data/weapons.uzdata
{
    "row_type": "WeaponStats",
    "rows": {
        "pistol":  { "damage": 15,  "fire_rate": 5.0, "magazine": 12, "reload_time": 1.5 },
        "shotgun": { "damage": 80,  "fire_rate": 1.2, "magazine": 6,  "reload_time": 2.5 },
        "rifle":   { "damage": 25,  "fire_rate": 10,  "magazine": 30, "reload_time": 2.0 },
        "sniper":  { "damage": 150, "fire_rate": 0.8, "magazine": 5,  "reload_time": 3.0 },
        "rocket":  { "damage": 200, "fire_rate": 0.5, "magazine": 1,  "reload_time": 4.0 }
    }
}
```

### 20.5 Usage in Behaviors

```cpp
class WeaponSystem : public uz::Behavior {
    UZ_BEHAVIOR(WeaponSystem);

    // Loaded once at init — hot-reloadable
    UZ_ASSET(uz::DataTable<WeaponStats>, "data/weapons.uzdata", weapon_table);

    std::string current_weapon = "pistol";

public:
    void Fire() {
        auto* stats = weapon_table->Find(current_weapon.c_str());
        if (!stats) return;

        // Data-driven values — designer changes JSON, values hot-reload
        SpawnProjectile(stats->damage);
        cooldown_ = 1.0f / stats->fire_rate;
    }
};
```

### 20.6 Common Use Cases

**Enemy wave configuration:**
```json
// data/enemy_waves.uzdata
{
    "row_type": "WaveConfig",
    "rows": {
        "wave_1":  { "enemy_count": 5,  "speed": 2.0, "health": 30,  "spawn_interval": 0.8 },
        "wave_2":  { "enemy_count": 8,  "speed": 2.3, "health": 45,  "spawn_interval": 0.7 },
        "wave_3":  { "enemy_count": 12, "speed": 2.6, "health": 60,  "spawn_interval": 0.6 },
        "wave_10": { "enemy_count": 30, "speed": 4.0, "health": 150, "spawn_interval": 0.3 }
    }
}
```

**XP level curve:**
```json
// data/xp_curve.uzdata
{
    "row_type": "LevelXP",
    "rows": {
        "level_1":  { "xp_required": 0 },
        "level_2":  { "xp_required": 100 },
        "level_3":  { "xp_required": 300 },
        "level_4":  { "xp_required": 600 },
        "level_5":  { "xp_required": 1000 }
    }
}
```

### 20.7 Comparison

| Engine | Mechanism | Editor Integration | Hot-Reload |
|---|---|---|---|
| Unreal 5 | `UDataTable` (row struct + CSV/JSON) | Full editor | Yes |
| Unity | `ScriptableObject` (manual, no table) | Inspector only | No |
| Godot | `Resource` (JSON/custom format) | Limited | Partial |
| **UzEngine** | `DataTable<T>` (JSON `.uzdata`, typed rows) | **Editor + JSON** | **Yes** |

### 20.8 Implementation Phase

**Phase 3** — requires Property System (§19) for JSON deserialization of row types.

---

## 21. Input System — Enhanced Input

> **Reference**: UE5 Enhanced Input Plugin (clean-room). Adapted for UzEngine's C++ component model.
> **Goal**: Value-typed, modifier/trigger-driven input with context stacking, runtime rebinding, and zero-boilerplate binding in Behaviors.

### 21.1 The Problem

Traditional input systems map keys directly to callbacks (`if (IsKeyPressed(Key::Space)) Jump()`). This breaks down quickly:

- **No value types** — a joystick axis and a button press use completely different code paths
- **No composability** — dead zones, smoothing, negation require per-action if-chains
- **No context switching** — UI vs gameplay vs vehicle all fight over the same key bindings
- **No runtime rebinding** — key mappings hardcoded in C++, unmodifiable by players
- **No chords** — Shift+E requires manual state tracking

### 21.2 Architecture Overview

```
OS Key Event (WM_KEYDOWN, XInput, etc.)
    │
    ▼
Platform Layer (platform/windows/) → stores raw KeyState per key
    │
    ▼
InputSystem::Tick() — per player, per frame:
    │
    │  For each ActionKeyMapping (sorted by context priority):
    │    1. Read raw key value
    │    2. Apply mapping-level Modifiers (DeadZone, Negate, Scalar...)
    │    3. Evaluate mapping-level Triggers (Down, Hold, Tap...)
    │    4. Accumulate into InputActionInstance (highest abs value or cumulative)
    │
    │  For each InputActionInstance (post-accumulation):
    │    5. Apply action-level Modifiers
    │    6. Evaluate action-level Triggers
    │    7. Compute ETriggerEvent from state transition (None→Ongoing = Started, etc.)
    │
    │  Walk InputComponent binding stack:
    │    8. For each binding where action.trigger_event matches → fire callback
    │
    ▼
Game Code receives (InputActionInstance&) with typed value + event
```

**Two-pass design** (critical): mapping-level modifiers/triggers run first per-key, then action-level modifiers/triggers run on the accumulated value. A `Negate` on a mapping negates only that key's contribution; a `Negate` on the action negates the combined input from all keys.

### 21.3 InputValue — Unified Value Type

All input flows through a single value type. No branching on "is this a button or an axis?"

```cpp
/// The type tag describes which components of the Vec3 are meaningful.
/// The data is always Vec3 — modifiers operate on 3 floats without branching.
enum class InputValueType : uint8_t {
    Boolean,  // value.x is 0.0 or 1.0, y/z = 0
    Axis1D,   // value.x is the scalar, y/z = 0
    Axis2D,   // value.x + value.y active, z = 0
    Axis3D,   // all components active
};

/// Unified input value. 16 bytes, no heap, no virtuals.
/// Modifiers receive and return this — they never need to know the semantic type.
struct InputValue {
    Vec3           value = {};
    InputValueType type  = InputValueType::Boolean;

    bool  AsBool()   const;   // magnitude² >= 0.25
    float AsAxis1D() const;   // value.x
    Vec2  AsAxis2D() const;   // {value.x, value.y}
    Vec3  AsAxis3D() const;   // value

    float MagnitudeSq() const; // type-aware: bool→x², 2D→x²+y², 3D→full
    bool  IsNonZero(float tolerance = 0.01f) const;

    InputValue& operator+=(const InputValue& rhs); // promotes type, adds vector
    InputValue& operator*=(float scalar);
};
```

### 21.4 Input Modifiers — Pure Function Chain

A modifier transforms a value. Modifiers chain: output of one feeds input of the next. Some are stateless (Negate, Scalar), some carry per-instance state (SmoothDelta, ResponseCurve).

```cpp
/// Base class for all input modifiers.
/// Override Modify() to transform the input value.
/// Modifiers are instanced per-player (state like SmoothDelta::old_value is per-player).
class InputModifier {
public:
    virtual ~InputModifier() = default;

    /// Transform the input value. Called once per frame per mapping or action.
    /// @param value  Output of the previous modifier (or raw key value for the first).
    /// @param dt     Frame delta time (non-dilated).
    /// @returns      Modified value. Type tag must be preserved unless explicitly promoted.
    virtual InputValue Modify(InputValue value, float dt) = 0;
};
```

**Built-in modifiers:**

| Modifier | Description | Stateful? |
|----------|-------------|-----------|
| `DeadZone` | Axial or radial dead zone with lower/upper threshold | No |
| `Scalar` | Per-axis multiply (`Vec3 scale`) | No |
| `Negate` | Flip sign on selected axes | No |
| `SwizzleAxis` | Remap axes (e.g. swap X↔Y for different stick conventions) | No |
| `SmoothDelta` | Frame-to-frame smoothing (stores previous value) | **Yes** |
| `ResponseCurve` | Exponential/custom curve remapping | No |
| `FOVScaling` | Scale sensitivity by current camera FOV | No |

**Chain execution:**

```cpp
/// Apply a chain of modifiers sequentially. Returns the final value.
InputValue ApplyModifiers(Span<InputModifier*> chain, InputValue value, float dt) {
    for (auto* mod : chain) {
        value = mod->Modify(value, dt);
    }
    return value;
}
```

### 21.5 Input Triggers — State Machine

A trigger evaluates whether an action should fire. Triggers return a **state** (`None`, `Ongoing`, `Triggered`), not an **event**. The event (`Started`, `Completed`, `Canceled`) is derived by the system by comparing previous state to current state.

```cpp
/// What role this trigger plays in the voting system.
enum class TriggerType : uint8_t {
    Explicit,  // At least one Explicit must be Triggered for the action to fire
    Implicit,  // ALL Implicits must be Triggered (used for chords)
    Blocker,   // If ANY Blocker is Triggered, the action is suppressed
};

/// Current state of a trigger (NOT the event — see §21.6).
enum class TriggerState : uint8_t {
    None,       // not active
    Ongoing,    // in progress (e.g. hold timer counting)
    Triggered,  // condition met
};

/// Base class for all input triggers.
class InputTrigger {
public:
    virtual ~InputTrigger() = default;

    float actuation_threshold = 0.5f;

    /// What type of trigger this is (for voting).
    virtual TriggerType GetType() const { return TriggerType::Explicit; }

    /// Evaluate the trigger this frame. Returns the current state.
    /// @param value  The modified input value (after modifier chain).
    /// @param dt     Frame delta time.
    virtual TriggerState Update(InputValue value, float dt) = 0;

protected:
    InputValue last_value;  // auto-updated by the system after Update()

    /// Helper: is the value above the actuation threshold?
    bool IsActuated(const InputValue& v) const {
        return v.MagnitudeSq() >= actuation_threshold * actuation_threshold;
    }
};
```

**Built-in triggers:**

| Trigger | Fires when | Type |
|---------|-----------|------|
| `Down` | Value is non-zero (default if no triggers specified) | Explicit |
| `Pressed` | Transition from zero → non-zero (single frame) | Explicit |
| `Released` | Transition from non-zero → zero (single frame) | Explicit |
| `Hold` | Held for N seconds continuously | Explicit |
| `HoldAndRelease` | Held for N seconds, fires on release | Explicit |
| `Tap` | Pressed and released within N seconds | Explicit |
| `Pulse` | Fires repeatedly every N seconds while held | Explicit |
| `ChordAction` | Another action is currently Triggered | **Implicit** |
| `ChordBlocker` | Blocks the action while a chord key is active | **Blocker** |

**Trigger voting** (when multiple triggers on one mapping/action):

- No triggers defined → fire if value is non-zero (implicit `Down`).
- Explicit only → at least one Explicit must be `Triggered`.
- Implicit present → ALL Implicits must be `Triggered` (AND logic).
- Any Blocker `Triggered` → action is suppressed regardless.

### 21.6 Trigger Events — State Transition Table

Triggers return `TriggerState`. The system converts state transitions into `ETriggerEvent` — this is what game code binds to.

```cpp
/// The public events that game code can bind to.
enum class ETriggerEvent : uint8_t {
    None       = 0,
    Started    = 1 << 0,  // None/Triggered → Ongoing (or instant None → Triggered)
    Ongoing    = 1 << 1,  // Ongoing → Ongoing (still in progress)
    Triggered  = 1 << 2,  // any → Triggered (condition met)
    Completed  = 1 << 3,  // Triggered → None (cleanly finished)
    Canceled   = 1 << 4,  // Ongoing → None (interrupted before Triggered)
};
```

**Full state transition table:**

```
Last State  →  New State   →  Event
─────────────────────────────────────────
None        →  Ongoing     →  Started
None        →  Triggered   →  Started + Triggered (both fire)
Ongoing     →  Ongoing     →  Ongoing
Ongoing     →  Triggered   →  Triggered
Ongoing     →  None        →  Canceled
Triggered   →  Triggered   →  Triggered
Triggered   →  Ongoing     →  Ongoing
Triggered   →  None        →  Completed
```

The `None → Triggered` case fires **both** `Started` and `Triggered` callbacks in the same frame. This handles single-frame triggers (like `Pressed`) that actuate and resolve immediately.

### 21.7 InputAction — Definition vs Instance

An **InputAction** is a named, typed definition — authored once, shared across all players. The **InputActionInstance** is the per-player runtime state cloned from the definition.

```cpp
/// Static definition of a logical input action.
/// Define in code or load from .uzinput config file.
/// Shared — never holds per-player state.
struct InputAction {
    const char*    name;           // unique ID, e.g. "IA_Jump", "IA_Move"
    InputValueType value_type;     // Boolean for jump, Axis2D for movement, etc.

    /// How values from multiple keys mapping to the same action combine.
    /// - HighestAbsValue: take the key with the largest absolute value (default)
    /// - Cumulative: sum all key values (for WASD → Axis2D cancellation)
    enum class Accumulation { HighestAbsValue, Cumulative };
    Accumulation accumulation = Accumulation::HighestAbsValue;

    /// Action-level modifiers/triggers (applied AFTER mapping-level, on accumulated value).
    Span<InputModifier*> modifiers;
    Span<InputTrigger*>  triggers;

    bool consume_input     = true;   // prevent lower-priority contexts from seeing this key
    bool trigger_when_paused = false;
};

/// Per-player runtime state for an action. Created during context rebuild.
/// Holds per-instance copies of triggers/modifiers (so HeldDuration etc. are per-player).
struct InputActionInstance {
    const InputAction* source;

    InputValue    value;                   // accumulated this frame
    ETriggerEvent trigger_event;           // the event fired this frame
    TriggerState  last_trigger_state;      // from previous frame

    float elapsed_processed_time = 0.0f;  // time in Started/Ongoing/Triggered
    float elapsed_triggered_time = 0.0f;  // time in Triggered specifically

    // Per-instance clones (state like HeldDuration is per-player, not shared)
    std::vector<std::unique_ptr<InputModifier>> modifiers;
    std::vector<std::unique_ptr<InputTrigger>>  triggers;
};
```

### 21.8 Mapping Context — Priority-Based Context Stack

An **InputMappingContext** groups key→action bindings into a named "mode" (Gameplay, UI, Vehicle, Spectator). Multiple contexts can be active simultaneously with different priorities.

```cpp
/// One physical key → one logical action, with per-mapping modifiers and triggers.
struct ActionKeyMapping {
    const InputAction* action;
    KeyCode            key;
    std::vector<std::unique_ptr<InputModifier>> modifiers;  // mapping-level (run first)
    std::vector<std::unique_ptr<InputTrigger>>  triggers;   // mapping-level

    bool should_ignore = false;  // key was down during context switch — skip until released
};

/// A named collection of key mappings representing one input "mode."
/// Authored as a data asset (.uzinput) or defined in code.
struct InputMappingContext {
    const char*                    name;       // e.g. "IMC_Gameplay", "IMC_UI"
    std::vector<ActionKeyMapping>  mappings;
};
```

**Context stack operations:**

```cpp
/// Add a context at the given priority. Higher priority = processed first.
/// Deferred to end-of-frame unless force_immediately is true.
///
/// @param priority                      Integer priority (higher wins).
/// @param ignore_pressed_until_release  If true, keys currently held are skipped
///                                      until released. Prevents "instant trigger"
///                                      when swapping contexts.
void AddMappingContext(const InputMappingContext* context, int32_t priority,
                       bool ignore_pressed_until_release = true);

/// Remove a context. Reference-counted — if added twice, must be removed twice.
void RemoveMappingContext(const InputMappingContext* context);
```

**Typical context setup:**

```cpp
// In a gameplay Behavior:
void OnBeginPlay() override {
    auto& input = GetWorld().GetSubsystem<InputSubsystem>();
    input.AddMappingContext(&IMC_Gameplay, /*priority=*/ 0);
}

// When opening a menu:
void OpenMenu() {
    auto& input = GetWorld().GetSubsystem<InputSubsystem>();
    input.AddMappingContext(&IMC_UI, /*priority=*/ 10);  // higher = processed first
    // IMC_UI consumes movement keys → gameplay context doesn't see them
}

// When closing the menu:
void CloseMenu() {
    auto& input = GetWorld().GetSubsystem<InputSubsystem>();
    input.RemoveMappingContext(&IMC_UI);
    // Gameplay context resumes, ignore_pressed_until_release prevents stale triggers
}
```

**Context rebuild** (end-of-frame, only when contexts changed):
1. Sort active contexts by priority (descending).
2. Flatten all mappings into a single array.
3. Re-order: chord sources first, then chorded actions, then everything else.
4. Mark currently-held keys as `should_ignore` on new mappings (if `ignore_pressed_until_release`).
5. Clone modifiers/triggers per-player into fresh `InputActionInstance` objects.

### 21.9 Binding in Game Code

Input is always routed through the **PlayerController** (§18), never through a standalone Behavior. When the controller possesses a new pawn, `OnSetupInput()` is called — this is where mapping contexts are added and actions are bound. On `Unpossess()`, all bindings and contexts are automatically cleared.

This makes input **interchangeable by possession**: same player, different pawn = different keys, different actions, different behavior. See §18.3 for the full mount/dismount example.

```cpp
/// The PlayerController owns all input binding.
/// OnSetupInput() is called on every Possess() — bind actions here.
/// Previous bindings are auto-cleared on Unpossess().
class FPSController : public uz::PlayerController {
    UZ_BEHAVIOR(FPSController);

public:
    void OnSetupInput(uz::InputComponent& input) override {
        auto& sub = GetWorld().GetSubsystem<InputSubsystem>();
        sub.AddMappingContext(&IMC_OnFoot, 0);

        input.BindAction(&IA_Move, ETriggerEvent::Triggered,
            [this](const InputActionInstance& ctx) {
                Vec2 move = ctx.value.AsAxis2D();
                GetWorld().Get<CharacterMovementComponent>(GetPawn()).AddInput({move.x, 0, move.y});
            });

        input.BindAction(&IA_Jump, ETriggerEvent::Triggered,
            [this](const InputActionInstance& ctx) {
                GetWorld().Get<CharacterMovementComponent>(GetPawn()).Jump();
            });

        input.BindAction(&IA_Attack, ETriggerEvent::Triggered,
            [this](const InputActionInstance& ctx) {
                GetWorld().Get<CombatComponent>(GetPawn()).Attack();
            });
    }
};
```

**Why PlayerController, not Behavior?**

| Scenario | With Behavior (wrong) | With PlayerController (correct) |
|----------|----------------------|-------------------------------|
| Player dies + respawns | Input bindings lost, must re-bind | Controller persists, `Possess(new_pawn)` re-triggers `OnSetupInput()` |
| Player mounts horse | Must manually swap bindings, error-prone | `Possess(horse)` → auto-clears foot bindings, `OnSetupInput()` sets up mount bindings |
| Player enters vehicle | Duplicate input logic in vehicle Behavior | Controller checks pawn type, binds vehicle context |
| UI menu opens | Behavior doesn't know about context priority | Controller adds `IMC_UI` at higher priority, gameplay keys consumed |

### 21.10 Chord Actions

Chords (Shift+E, Ctrl+Shift+S) are implemented entirely as triggers — no special engine code path.

```cpp
// IA_Interact = E (normal)
// IA_PickUp = Shift+E (chord)

// Define a ChordAction trigger on IA_PickUp that requires IA_Shift to be active:
InputAction IA_Shift { "IA_Shift", InputValueType::Boolean };

InputMappingContext IMC_Gameplay = {
    "IMC_Gameplay",
    {
        // Shift key → IA_Shift (always evaluates, consumed by chord system)
        { &IA_Shift, Key::LeftShift, {}, {} },

        // E → IA_Interact (normal, but auto-blocked when Shift is held)
        { &IA_Interact, Key::E, {}, {new TriggerPressed()} },

        // Shift+E → IA_PickUp (ChordAction makes IA_Shift an Implicit requirement)
        { &IA_PickUp, Key::E, {}, {new TriggerPressed(), new ChordAction(&IA_Shift)} },
    }
};
```

**How it works internally:**
1. During context rebuild, the system detects `ChordAction(&IA_Shift)` on `IA_PickUp`.
2. It injects a `ChordBlocker(&IA_Shift)` on `IA_Interact` automatically — so plain E is blocked while Shift is held.
3. Evaluation order is sorted: `IA_Shift` first (chord source), then `IA_PickUp` (chorded), then `IA_Interact` (blocked).
4. When Shift is held + E pressed: `IA_Shift` = Triggered → `ChordAction` Implicit is satisfied → `IA_PickUp` fires. `ChordBlocker` on `IA_Interact` suppresses it.
5. When only E pressed: `IA_Shift` = None → `ChordAction` Implicit fails → `IA_PickUp` doesn't fire. `ChordBlocker` not active → `IA_Interact` fires.

### 21.11 Runtime Rebinding

Players can remap keys at runtime. Rebindings are saved per-player and persist across sessions.

```cpp
/// Rebind a specific mapping in a context. The new key replaces the old one.
/// Persisted to player settings (JSON) automatically.
void InputSubsystem::RebindKey(
    const InputMappingContext* context,
    const InputAction*         action,
    KeyCode                    old_key,
    KeyCode                    new_key);

/// Get current binding for an action (for UI display).
/// Returns all keys mapped to this action across all active contexts.
std::vector<KeyCode> InputSubsystem::GetBoundKeys(const InputAction* action) const;

/// Reset all rebindings to default.
void InputSubsystem::ResetToDefaults(const InputMappingContext* context);
```

**Rebinding storage** (`.uzinput` sidecar per player profile):

```json
{
    "rebindings": {
        "IMC_Gameplay": {
            "IA_Jump": { "Space": "GamepadFaceBottom" },
            "IA_Fire": { "MouseLeft": "GamepadRightTrigger" }
        }
    }
}
```

### 21.12 Input Config File Format

Mapping contexts can be defined in JSON (`.uzinput`) instead of code — enabling designers and LLMs to author/modify input mappings without recompilation.

```json
{
    "name": "IMC_Gameplay",
    "mappings": [
        {
            "action": "IA_Move",
            "key": "W",
            "modifiers": [
                { "type": "Negate", "axes": [false, true, false] },
                { "type": "SwizzleAxis", "order": "YXZ" }
            ],
            "triggers": []
        },
        {
            "action": "IA_Move",
            "key": "Gamepad_LeftStick",
            "modifiers": [
                { "type": "DeadZone", "lower": 0.2, "upper": 1.0, "mode": "Radial" }
            ],
            "triggers": []
        },
        {
            "action": "IA_Jump",
            "key": "Space",
            "modifiers": [],
            "triggers": [{ "type": "Pressed" }]
        },
        {
            "action": "IA_Fire",
            "key": "MouseLeft",
            "modifiers": [],
            "triggers": [{ "type": "Hold", "threshold": 0.0 }]
        }
    ]
}
```

### 21.13 Directory Structure

```
engine/input/
├── input_value.h                ← InputValue struct + operators
├── input_action.h               ← InputAction (definition) + InputActionInstance (per-player)
├── input_component.h/.cpp       ← InputComponent (ActorComponent) + BindAction API
├── input_subsystem.h/.cpp       ← InputSubsystem (WorldSubsystem) + context stack + tick
├── mapping_context.h            ← InputMappingContext + ActionKeyMapping
├── modifier.h                   ← InputModifier base class + ApplyModifiers()
├── trigger.h                    ← InputTrigger base class + TriggerState + voting
├── trigger_event.h              ← ETriggerEvent enum + state transition table
├── key_codes.h                  ← KeyCode enum (keyboard, mouse, gamepad)
├── modifiers/                   ← built-in modifier implementations
│   ├── deadzone.h
│   ├── scalar.h
│   ├── negate.h
│   ├── swizzle_axis.h
│   ├── smooth_delta.h
│   └── response_curve.h
└── triggers/                    ← built-in trigger implementations
    ├── trigger_down.h
    ├── trigger_pressed.h
    ├── trigger_released.h
    ├── trigger_hold.h
    ├── trigger_tap.h
    ├── trigger_pulse.h
    └── trigger_chord.h
```

### 21.14 Comparison

| Feature | Legacy (UE4/Unity) | UE5 Enhanced Input | **UzEngine** |
|---------|--------------------|--------------------|--------------|
| Value types | bool/float separate | Unified `FInputActionValue` | Unified `InputValue` (Vec3 + type tag) |
| Modifiers | Hardcoded per-action | Chainable `UInputModifier` | Chainable `InputModifier` (virtual) |
| Triggers | Pressed/Released only | `UInputTrigger` state machine | `InputTrigger` state machine |
| Context switching | Manual enable/disable | Priority stack + `bConsumeInput` | Priority stack + `consume_input` |
| Chords | Manual state tracking | `ChordAction` trigger (Implicit) | `ChordAction` trigger (Implicit) |
| Rebinding | Platform-specific | `UPlayerMappableKeySettings` | `RebindKey()` + `.uzinput` JSON |
| Config format | C++ only | Data assets (UObject) | **JSON `.uzinput`** (LLM-friendly) |

### 21.15 Implementation Phase

**Phase 2** — requires platform/ input capture (§12) and WorldSubsystem (§6). InputComponent is an ActorComponent (§4.2).

---

## 22. Scene Serialization — `.uzscene` & `.uzprefab`

> **Goal**: Define the file format for scenes and prefabs — how entities, components, hierarchy, and cross-references are serialized.
> **Principles**: JSON everywhere (LLM-friendly), Property System (§19) resolves component types, hierarchy expressed inline.

### 22.1 The Problem

The engine needs a scene format that:
- Is **human-readable and LLM-editable** — no binary blobs, no custom syntax
- Supports **component hierarchy** — SceneComponents with parent/child attachment
- Supports **prefab instancing** with overrides — designers place 50 enemies without duplicating data
- Supports **entity cross-references** — a PlayerController references its initial pawn
- Is **hot-reloadable** — edit JSON, see changes in-editor without restart
- Resolves component types via the **Property System** (§19) — no hardcoded deserialization

### 22.2 Format: `.uzscene`

```json
{
    "format": "uzscene",
    "version": 1,

    "settings": {
        "subsystems": ["PhysicsWorld", "NavigationServer"],
        "gravity": [0, -981, 0],
        "ambient_light": [0.1, 0.1, 0.15]
    },

    "entities": [
        {
            "id": "player",
            "components": {
                "SceneComponent": { "position": [0, 1.7, 0] },
                "SkeletalMeshComponent": {
                    "parent": "SceneComponent",
                    "mesh": "content/meshes/character.uzmesh",
                    "material": "content/materials/player.uzmat"
                },
                "CapsuleCollision": {
                    "parent": "SceneComponent",
                    "radius": 0.4,
                    "half_height": 0.9
                },
                "CharacterBody": {
                    "parent": "CapsuleCollision"
                },
                "CameraComponent": {
                    "parent": "SceneComponent",
                    "position": [0, 0.6, 0],
                    "fov": 90,
                    "is_active": true
                },
                "CharacterMovementComponent": {
                    "max_speed": 600,
                    "jump_velocity": 420
                },
                "FPSController": {
                    "initial_pawn": "player"
                }
            }
        },
        {
            "id": "enemy_01",
            "prefab": "content/prefabs/grunt.uzprefab",
            "overrides": {
                "SceneComponent": { "position": [10, 0, 5] },
                "Health": { "max": 150 }
            }
        },
        {
            "id": "main_light",
            "components": {
                "SceneComponent": { "rotation": [-45, 30, 0] },
                "DirectionalLightComponent": {
                    "parent": "SceneComponent",
                    "intensity": 2.0,
                    "color": [1.0, 0.95, 0.9],
                    "cast_shadows": true
                }
            }
        }
    ]
}
```

### 22.3 Design Rules

#### Component Hierarchy via `parent` Field

SceneComponents declare their parent attachment inline. The first `SceneComponent` without a `parent` field is the entity root. This avoids deeply nested JSON and mirrors how UE5 organizes component attachment — components attached to each other, not entities nested inside entities.

```json
"components": {
    "SceneComponent": { "position": [0, 0, 0] },
    "StaticMeshComponent": { "parent": "SceneComponent", "mesh": "..." },
    "CapsuleCollision": { "parent": "SceneComponent", "radius": 0.5 },
    "CharacterBody": { "parent": "CapsuleCollision" }
}
```

Produces the hierarchy:
```
SceneComponent (root)
├── StaticMeshComponent
└── CapsuleCollision
    └── CharacterBody
```

#### Multiple Components of the Same Type

When an entity needs two components of the same type (e.g., two collision shapes), use a suffix after `#`:

```json
"components": {
    "SceneComponent": {},
    "BoxCollision#body": { "parent": "SceneComponent", "half_extents": [0.5, 1.0, 0.3] },
    "BoxCollision#head": { "parent": "SceneComponent", "half_extents": [0.3, 0.3, 0.3], "position": [0, 1.2, 0] },
    "RigidBody": { "parent": "BoxCollision#body" }
}
```

The part before `#` is the component type (resolved via Property System). The part after `#` is the instance name (used for `parent` references and debugging). If no `#` suffix, the instance name equals the type name.

#### ActorComponents vs SceneComponents

ActorComponents have no `parent` field — they are flat, non-spatial:

```json
"CharacterMovementComponent": { "max_speed": 600 },
"FPSController": { "initial_pawn": "player" },
"Health": { "max": 100 }
```

SceneComponents have an optional `parent` field and inherit transform from their parent:

```json
"CameraComponent": { "parent": "SceneComponent", "fov": 90 }
```

The SceneLoader distinguishes them by checking the type's inheritance chain in the Property System registry.

#### Entity References by ID

Cross-entity references use the `id` string:

```json
{ "id": "player", "components": { ... } },
{ "id": "controller", "components": {
    "FPSController": { "initial_pawn": "player" }
}}
```

Resolved at load-time by the SceneLoader. The Property System marks `UZ_PROPERTY` fields with `meta(entity_ref)` so the loader knows to resolve them:

```cpp
class FPSController : public PlayerController {
    UZ_PROPERTY(meta(entity_ref))
    uz::Entity initial_pawn;
};
```

In shipping builds, string IDs are stripped and replaced by integer indices for faster lookup.

#### Transform Fields

SceneComponents carry their transform as direct fields — there is no separate `TransformComponent`. Transform IS the SceneComponent:

```json
"SceneComponent": { "position": [0, 1.7, 0], "rotation": [0, 90, 0], "scale": [1, 1, 1] }
```

- `position`: `Vec3`, world or local (local if has parent). Default `[0, 0, 0]`.
- `rotation`: `Vec3` Euler angles in degrees (pitch, yaw, roll). Default `[0, 0, 0]`.
- `scale`: `Vec3`. Default `[1, 1, 1]`.

Omitted fields use defaults. A bare `"SceneComponent": {}` is identity transform at origin.

### 22.4 Format: `.uzprefab`

A prefab is a reusable entity template. Same structure as a scene entity, but with `"format": "uzprefab"` and a single `entity` (no `id` — the ID comes from whoever instantiates it).

```json
{
    "format": "uzprefab",
    "version": 1,

    "entity": {
        "components": {
            "SceneComponent": {},
            "StaticMeshComponent": {
                "parent": "SceneComponent",
                "mesh": "content/meshes/crate.uzmesh",
                "material": "content/materials/wood.uzmat"
            },
            "BoxCollision": {
                "parent": "SceneComponent",
                "half_extents": [0.5, 0.5, 0.5]
            },
            "RigidBody": { "parent": "BoxCollision", "mass": 10.0 },
            "Health": { "max": 50 }
        }
    }
}
```

#### Prefab Instancing with Overrides

In a `.uzscene`, reference a prefab and override specific fields:

```json
{
    "id": "crate_01",
    "prefab": "content/prefabs/crate.uzprefab",
    "overrides": {
        "SceneComponent": { "position": [5, 0, 3] },
        "RigidBody": { "mass": 25.0 },
        "Health": { "max": 200 }
    }
}
```

**Override rules:**
- Only listed fields are overridden — unlisted fields keep prefab defaults.
- Component types not in the prefab are **added** (extend the prefab).
- A component set to `null` is **removed** (strip from this instance).
- Overrides are shallow-merged per component (field-level, not deep).

#### Nested Prefabs

A prefab can reference another prefab:

```json
{
    "format": "uzprefab",
    "version": 1,

    "entity": {
        "prefab": "content/prefabs/base_enemy.uzprefab",
        "overrides": {
            "SkeletalMeshComponent": { "mesh": "content/meshes/goblin.uzmesh" },
            "Health": { "max": 80 },
            "AIController": { "behavior_tree": "content/ai/goblin_bt.uzbt" }
        }
    }
}
```

Resolution is recursive: load base prefab → apply overrides → if base also references a prefab, resolve that first. Max depth: 8 (fail with error if exceeded — prevents cycles).

### 22.5 Runtime Spawning from Prefab

Game code can instantiate prefabs at runtime:

```cpp
// Spawn a single prefab instance
uz::Entity crate = GetWorld().SpawnPrefab("content/prefabs/crate.uzprefab", {
    .position = {5, 2, 0},
    .rotation = Quat::Identity(),
});

// Spawn with component overrides
uz::Entity boss = GetWorld().SpawnPrefab("content/prefabs/grunt.uzprefab", {
    .position = {0, 0, 20},
    .overrides = {
        { "Health", { {"max", 500} } },
        { "SceneComponent", { {"scale", {2, 2, 2}} } },
    }
});
```

### 22.6 Scene Loading Pipeline

```
.uzscene file (JSON on disk)
    │
    ▼
SceneLoader::Load(path)
    │
    ├── 1. Parse JSON (rapidjson / simdjson)
    ├── 2. Validate format + version
    ├── 3. Apply scene settings (subsystems, gravity, etc.)
    │
    ├── 4. For each entity:
    │   ├── If "prefab" → load prefab recursively, apply overrides
    │   ├── For each component:
    │   │   ├── Resolve type name via Property System registry
    │   │   ├── Allocate component in World
    │   │   ├── Deserialize JSON fields → UZ_PROPERTY fields
    │   │   └── If SceneComponent with "parent" → attach to parent
    │   └── Register entity in World
    │
    ├── 5. Resolve entity references (string ID → Entity handle)
    │
    ├── 6. Call OnRegister() on all components
    ├── 7. Call OnInitialize() on all components
    └── 8. Call OnBeginPlay() on all components
```

**Ordering guarantee** (from §4.3): ALL components call OnInitialize before ANY component calls OnBeginPlay.

### 22.7 Scene Saving

```cpp
/// Serialize the current World state back to JSON.
/// Only saves entities marked as persistent (editor-placed or explicitly flagged).
/// Runtime-spawned entities are excluded unless flagged with UZ_SAVE.
void SceneSaver::Save(const World& world, const std::string& path);
```

The saver walks all entities, serializes their components via the Property System (§19), and writes valid `.uzscene` JSON. Round-trip guarantee: `Load(Save(world))` produces an identical world state.

### 22.8 Hot-Reload

When a `.uzscene` or `.uzprefab` file changes on disk (detected by FileWatcher):

1. **In editor mode**: reload the scene — destroy all entities, re-load from updated JSON.
2. **Prefab instances**: if a `.uzprefab` changes, all instances in the current scene are re-created with updated base + their overrides re-applied.
3. **No hot-reload in shipping builds** — scenes are baked into `.uzpak`.

### 22.9 Comparison

| Feature | Unreal 5 | Unity | Godot | **UzEngine** |
|---------|----------|-------|-------|--------------|
| Format | Binary `.umap` (UAsset) | Binary YAML `.unity` | Text `.tscn` (custom syntax) | **JSON `.uzscene`** |
| Human-readable | No | Barely | Yes | **Yes** |
| LLM-editable | No | No | Partially | **Yes (pure JSON)** |
| Prefabs | Blueprint (heavy) | `.prefab` (nested) | `.tscn` (PackedScene) | **`.uzprefab` (JSON + overrides)** |
| Nested prefabs | Yes (complex) | Yes | Yes (via instance) | **Yes (recursive, max depth 8)** |
| Entity references | FObjectProperty (GUIDs) | fileID (integer) | NodePath (string) | **String ID → Entity handle** |
| Hot-reload | Editor only | Editor only | Yes | **Yes (editor mode)** |

### 22.10 Implementation Phase

**Phase 2** — requires Property System (§19) for component type resolution and field deserialization. SceneLoader lives in `engine/scene/` (§12).

---

## 23. Render World & Sync

> **Goal**: Define how the game world (entities, components) communicates with the rendering pipeline. The two worlds are **parallel and decoupled** — game code describes what to render, the Render World decides how.
> **References**: UE5 `FScene` + `FPrimitiveSceneProxy` (clean-room), Godot `RenderingServer` + RID pattern.

### 23.1 The Problem

Game code manipulates entities and components — position, mesh, material, visibility. The renderer needs a **GPU-optimized representation** — vertex buffers, pipeline states, draw call batches, culling structures. If game code touches render data directly:
- Thread safety is impossible (game tick and render submission fight over the same data)
- Cache layout is wrong (components are entity-centric, rendering needs material-centric sorting)
- Culling requires spatial structures that game code doesn't maintain

### 23.2 Architecture — Dual World Model

```
Game Thread                              Render Thread (future — single-thread MVP)
───────────────────────────              ──────────────────────────────────────────
World                                    RenderWorld
  │                                        │
  │ PrimitiveComponent (game data)         │ RenderProxy (GPU-ready data)
  │   - ResourceHandle<Mesh>               │   - GPU vertex/index buffers
  │   - ResourceHandle<Material>           │   - PSO handle
  │   - visible, cast_shadows              │   - world matrix (copied from sync)
  │   - render_layer bitmask               │   - material param block (copied)
  │   - param_overrides                    │   - bounding sphere (for culling)
  │                                        │
  │ CameraComponent                        │ ViewState
  │   - fov, near, far, is_active          │   - view/proj matrices
  │   - layer_mask                         │   - frustum planes (derived)
  │                                        │   - HZB from previous frame
  │                                        │
  │◄──── Sync Point ─────────────────────►│
  │   (end of game tick, before render)    │
  │   1. Added primitives → CreateProxy    │
  │   2. Dirty transforms → SetTransform   │
  │   3. Changed props → SetMaterial/etc   │
  │   4. Removed primitives → DestroyProxy │
  │   5. Active camera → SetViewState      │
  │                                        │
  │                                        ▼
  │                                    Visibility Pipeline (§23.6)
  │                                        │
  │                                        ▼
  │                                    Draw Call Generation
  │                                        │
  │                                        ▼
  │                                    RHI Submission (DX12/Vulkan/Metal)
```

### 23.3 RenderProxy — Render-Side Primitive

A `RenderProxy` is the render-side representation of a `PrimitiveComponent`. It is a **POD struct** in a dense `HandleMap` — no vtable, no heap allocation per-proxy, cache-coherent iteration.

```cpp
/// Opaque handle to a render proxy. Game code holds this but never dereferences it.
/// Generation-checked — stale handles are safely detected.
struct RenderProxyHandle { uint32_t id = INVALID_ID; };

/// Type tag for proxy-specific data.
enum class RenderProxyType : uint8_t {
    StaticMesh,
    SkeletalMesh,
    DirectionalLight,
    PointLight,
    SpotLight,
    Decal,
    ParticleSystem,
};

/// Render-side representation of a PrimitiveComponent.
/// Owned exclusively by RenderWorld. Game thread writes only during sync.
/// POD struct — no virtuals, no inheritance. Stored in HandleMap<RenderProxy> (dense array).
///
/// # Memory Layout
/// All proxies are contiguous in memory. The visibility pass iterates linearly —
/// cache-friendly for scenes with 10k+ proxies.
struct RenderProxy {
    // ─── Identity ──────────────────────────────
    RenderProxyHandle handle;
    RenderProxyType   type;

    // ─── Transform ─────────────────────────────
    Mat4   world_matrix;
    Sphere bounding_sphere;              // world-space, updated on transform change

    // ─── Visibility ────────────────────────────
    bool     visible;
    bool     cast_shadows;
    uint32_t render_layer;               // bitmask, matched against camera layer mask

    // ─── Material ──────────────────────────────
    ResourceHandle<Material> material;
    MaterialParamBlock       param_overrides;  // per-instance (color tints, texture swaps)

    // ─── LOD ───────────────────────────────────
    uint8_t  current_lod;                // selected by LOD system or virtual geometry
    float    screen_size;                // cached from last frame (for LOD selection)

    // ─── Geometry (type-specific) ──────────────
    union {
        struct {
            ResourceHandle<Mesh> mesh;
            uint32_t instance_batch_key;   // hash of mesh+material — same key = auto-instance
        } static_mesh;

        struct {
            ResourceHandle<Mesh> mesh;
            JointPalette* joint_palette;   // pointer to skinning matrices (updated per-frame)
        } skeletal_mesh;

        struct {
            float intensity;
            Color color;
            float range;                   // 0 for directional
            float inner_angle;             // spot only
            float outer_angle;             // spot only
            bool  shadow_enabled;
        } light;
    };
};
```

**Why POD instead of virtual (like UE5)?**

UE5's `FPrimitiveSceneProxy` has ~50 virtual methods and per-proxy heap allocation. For 10k proxies, that's 10k vtable pointer chases during visibility. Our `RenderProxy` is a flat struct in a dense array — the CPU prefetcher handles it. The type-specific behavior lives in the draw call generator (switch on `type`), not in per-proxy virtual dispatch.

### 23.4 RenderWorld — The Render Container

```cpp
/// Render-side parallel to World. Owns all RenderProxies.
/// The ONLY code that writes to this is RenderWorldSubsystem::Sync().
/// The renderer reads from it during visibility + draw call generation.
class RenderWorld {
public:
    // ─── Proxy Lifecycle (called during sync) ──────────
    RenderProxyHandle CreateProxy(const RenderProxyDesc& desc);
    void              DestroyProxy(RenderProxyHandle handle);

    // ─── Proxy Updates (called during sync) ────────────
    void SetTransform(RenderProxyHandle h, const Mat4& world_matrix, const Sphere& bounds);
    void SetVisible(RenderProxyHandle h, bool visible);
    void SetMaterial(RenderProxyHandle h, ResourceHandle<Material> material);
    void SetParamOverrides(RenderProxyHandle h, const MaterialParamBlock& params);
    void SetRenderLayer(RenderProxyHandle h, uint32_t layer_mask);
    void SetCastShadows(RenderProxyHandle h, bool cast);
    void SetJointPalette(RenderProxyHandle h, const JointPalette& palette);

    // ─── View State (called during sync) ───────────────
    void SetActiveView(const ViewState& view);

    // ─── Rendering Pipeline ────────────────────────────

    /// Run the full visibility pipeline: frustum cull → GPU occlusion → LOD select.
    /// Populates internal visible_proxies list.
    void CullVisibility();

    /// Generate sorted draw calls from visible proxies.
    /// Opaque: sorted by PSO → material → mesh (minimize state changes).
    /// Transparent: sorted back-to-front.
    void GenerateDrawCalls(DrawCallList& opaque, DrawCallList& transparent);

    /// Gather shadow-casting proxies for a specific light.
    void GatherShadowCasters(RenderProxyHandle light, DrawCallList& out);

    // ─── Queries (read-only, for debug/editor) ─────────
    uint32_t GetProxyCount() const;
    uint32_t GetVisibleCount() const;
    uint32_t GetDrawCallCount() const;

private:
    HandleMap<RenderProxy> proxies_;         // dense array, generation-checked
    ViewState              active_view_;
    Frustum                frustum_;         // derived from view_proj
    std::vector<RenderProxyHandle> visible_; // populated by CullVisibility
};
```

### 23.5 Sync Point — The Bridge

```cpp
/// WorldSubsystem that bridges Game World → Render World.
/// Runs automatically at the end of every game tick, before rendering.
/// This is the ONLY place where game data touches render data.
class RenderWorldSubsystem : public WorldSubsystem {
public:
    /// Sync all PrimitiveComponents and CameraComponents to RenderWorld.
    void Sync() {
        auto& world = GetWorld();
        auto& rw = render_world_;

        // ── 1. NEW primitives → create proxies ──────────────
        for (auto [entity, prim] : world.Added<PrimitiveComponent>()) {
            prim.proxy_ = rw.CreateProxy(BuildDesc(prim));
            auto& scene = world.Get<SceneComponent>(entity);
            rw.SetTransform(prim.proxy_, scene.GetWorldMatrix(), prim.ComputeWorldBounds(scene));
        }

        // ── 2. DIRTY transforms → update proxy matrices ─────
        for (auto [entity, prim, scene] : world.View<PrimitiveComponent, SceneComponent>()) {
            if (scene.IsDirty()) {
                rw.SetTransform(prim.proxy_, scene.GetWorldMatrix(), prim.ComputeWorldBounds(scene));
            }
        }

        // ── 3. CHANGED properties → update proxy state ──────
        for (auto [entity, prim] : world.Changed<PrimitiveComponent>()) {
            rw.SetVisible(prim.proxy_, prim.visible);
            rw.SetMaterial(prim.proxy_, prim.material);
            rw.SetParamOverrides(prim.proxy_, prim.param_overrides);
            rw.SetRenderLayer(prim.proxy_, prim.render_layer);
            rw.SetCastShadows(prim.proxy_, prim.cast_shadows);
        }

        // ── 4. REMOVED primitives → destroy proxies ─────────
        for (auto [entity, prim] : world.Removed<PrimitiveComponent>()) {
            rw.DestroyProxy(prim.proxy_);
        }

        // ── 5. ACTIVE CAMERA → update view state ────────────
        for (auto [entity, cam, scene] : world.View<CameraComponent, SceneComponent>()) {
            if (cam.is_active) {
                rw.SetActiveView({
                    .view_matrix = Mat4::Inverse(scene.GetWorldMatrix()),
                    .proj_matrix = cam.GetProjectionMatrix(),
                    .position    = scene.GetWorldPosition(),
                    .near_plane  = cam.near_plane,
                    .far_plane   = cam.far_plane,
                    .layer_mask  = cam.layer_mask,
                });
            }
        }
    }

private:
    RenderWorld render_world_;
};
```

**Key invariant**: After `Sync()` completes, the `RenderWorld` is a complete, self-contained snapshot. The renderer can read it without any reference back to the game `World`. This is what makes the future dual-thread split trivial — just double-buffer the `RenderWorld`.

### 23.6 Visibility Pipeline — Three-Phase Culling

```
All Proxies (N)
    │
    ▼
Phase 1: Frustum Cull (CPU)
    │  Test bounding_sphere vs 6 frustum planes (SIMD, 4 spheres at a time)
    │  Rejects ~60-80% of proxies
    │  Cost: ~0.1ms for 10k proxies
    │
    ▼
Frustum-Visible Proxies (~2k-4k)
    │
    ▼
Phase 2: GPU Occlusion Cull (Compute Shader)
    │  Input: AABB list + HZB depth pyramid from previous frame
    │  Per-instance: project AABB to screen rect, sample HZB at appropriate mip
    │  If max_depth < HZB_depth → occluded, reject
    │  Output: indirect draw args (only visible instances survive)
    │  Rejects ~20-50% of remaining
    │  Cost: ~0.05ms GPU for 4k instances
    │
    ▼
Visible Proxies (~1k-3k)
    │
    ▼
Phase 3: LOD Selection
    │  screen_size = bounding_sphere.radius / distance_to_camera
    │  Select LOD level from mesh LOD chain
    │  Virtual geometry (§23.8) overrides this for Nanite-enabled meshes
    │
    ▼
Final Visible Set (with LOD)
    │
    ▼
Sort & Batch
    ├── Opaque: sort by PSO → material → mesh → distance (front-to-back)
    ├── Transparent: sort by distance (back-to-front)
    └── Shadow: per-light frustum cull → sort by mesh
```

### 23.7 GPU Instance Culling — Indirect Draw Pipeline

For scenes with thousands of instances (vegetation, rocks, buildings), CPU-side draw call submission becomes the bottleneck. GPU instance culling moves the per-instance visibility decision to a compute shader, producing `DrawIndexedInstancedIndirect` arguments directly on the GPU.

```
CPU (per frame, once):
    1. Upload instance buffer: [world_matrix, bounds, mesh_id, material_id] × N instances
    2. Upload HZB depth pyramid (from previous frame's depth buffer)
    3. Dispatch CullInstances compute shader

GPU Compute (CullInstances.hlsl):
    [numthreads(64, 1, 1)]
    void CullInstances(uint3 id : SV_DispatchThreadID) {
        Instance inst = instances[id.x];

        // Frustum test (6 planes vs AABB)
        if (!FrustumTestAABB(frustum_planes, inst.aabb)) return;

        // HZB occlusion test
        float2 screen_min, screen_max;
        ProjectAABB(inst.aabb, view_proj, screen_min, screen_max);
        float mip = log2(max(screen_max - screen_min) * hzb_size);
        float hzb_depth = hzb.SampleLevel(sampler, (screen_min + screen_max) * 0.5, mip);
        float inst_depth = ProjectZ(inst.aabb.near_z, view_proj);
        if (inst_depth > hzb_depth) return;  // fully occluded

        // Survived — append to indirect draw args
        uint batch_idx = inst.batch_key;  // hash(mesh + material)
        uint offset;
        InterlockedAdd(draw_args[batch_idx].InstanceCount, 1, offset);
        visible_instances[batch_idx][offset] = id.x;
    }

GPU Draw:
    For each batch (unique mesh + material combo):
        DrawIndexedInstancedIndirect(args_buffer, batch_idx * stride)
        // GPU reads instance transforms from visible_instances buffer
```

**Benefits:**
- Zero CPU-side per-instance work after the initial upload
- Draw call count = number of unique mesh+material combos (typically 50-200), not number of instances (10k+)
- HZB occlusion is free — it reuses the depth pyramid already built for post-processing

**When it kicks in:** Automatically for `StaticMeshComponent` with `instance_batch_key`. The batch key is `hash(mesh_handle, material_handle)` — identical mesh+material combos share the same indirect draw. Game code does nothing special; the `RenderWorld` detects batchable proxies during `GenerateDrawCalls()`.

### 23.8 Virtual Geometry — Nanite-Style Mesh Streaming (Phase 4+)

For extremely dense scenes (photogrammetry, film-quality assets), traditional LOD breaks down — artists can't author 5 LOD levels per asset, and pop-in is visible. UE5's Nanite solves this with GPU-driven mesh streaming at cluster granularity.

UzEngine's virtual geometry system follows the same principles, adapted for our architecture:

#### Concept

```
Source Mesh (millions of triangles)
    │
    ▼
Offline: Build Cluster Hierarchy (uzcook)
    │  Split mesh into clusters (~128 triangles each)
    │  Build BVH over clusters
    │  Generate DAG of cluster groups at multiple detail levels
    │  Store as .uzvmesh (virtual mesh format)
    │
    ▼
Runtime: GPU-Driven Cluster Selection
    │  Per-frame compute pass:
    │    1. Traverse cluster BVH on GPU
    │    2. For each cluster: screen-space error test
    │       If projected error < 1 pixel → this detail level is sufficient
    │    3. Surviving clusters → append to draw buffer
    │    4. Software rasterizer for small triangles (< 2x2 pixels)
    │       Hardware rasterizer for larger triangles
    │
    ▼
No LOD pops, no artist-authored LODs, triangle count adapts continuously
```

#### Integration with RenderWorld

```cpp
/// Virtual geometry mesh. Replaces traditional LOD chain.
/// The mesh is a hierarchy of clusters — the GPU selects which clusters to render
/// based on screen-space error, every frame.
struct VirtualMeshComponent : public PrimitiveComponent {
    ResourceHandle<VirtualMesh> virtual_mesh;  // .uzvmesh asset

    /// Error threshold in pixels. Lower = higher quality, more triangles.
    /// Default 1.0 = Nanite default. 0.5 = 2x quality. 2.0 = half quality.
    float pixel_error_threshold = 1.0f;

    /// Force a specific detail level (for debugging). -1 = automatic.
    int32_t force_detail_level = -1;
};

/// RenderProxy extension for virtual geometry.
/// Instead of a single mesh + LOD index, holds a cluster hierarchy handle.
/// The visibility pipeline skips traditional LOD selection for these proxies
/// and instead dispatches the cluster selection compute pass.
struct VirtualMeshProxyData {
    ResourceHandle<VirtualMesh> virtual_mesh;
    float    pixel_error_threshold;
    uint32_t cluster_bvh_offset;     // offset into global cluster buffer
    uint32_t cluster_count;          // total clusters in hierarchy
};
```

#### Visibility Pipeline with Virtual Geometry

```
Phase 1: Frustum Cull (CPU) — same as §23.6
    │
    ▼
Phase 2: Split by type
    ├── Traditional proxies → GPU Instance Culling (§23.7)
    └── Virtual mesh proxies → Cluster Selection Pass
                                   │
                                   ▼
                               GPU Compute: TraverseClusters
                                   │  Walk BVH per virtual mesh instance
                                   │  Screen-space error test per cluster group
                                   │  Expand to individual clusters
                                   │  Append surviving clusters to draw buffer
                                   │
                                   ▼
                               GPU Compute: ClassifyClusters
                                   │  Small triangles (< 2x2 px) → software raster queue
                                   │  Large triangles → hardware raster queue
                                   │
                                   ▼
                               Rasterize
                                   ├── Software rasterizer (compute) → visibility buffer
                                   └── Hardware rasterizer (draw) → visibility buffer
                                   │
                                   ▼
                               Material Pass
                                   │  Read visibility buffer
                                   │  Shade each pixel with its material
                                   │  Output to GBuffer
```

#### File Format: `.uzvmesh`

```json
{
    "format": "uzvmesh",
    "version": 1,
    "source_mesh": "content/meshes/statue.uzmesh",
    "cluster_count": 4096,
    "cluster_group_levels": 6,
    "total_triangles": 2300000,
    "bvh_depth": 8,
    "data": "content/meshes/statue.uzvmesh.bin"   // binary blob: clusters + BVH + vertex data
}
```

#### What Ships vs What's Phase 4

| Feature | MVP (Phase 1-2) | Phase 3 | Phase 4+ |
|---------|-----------------|---------|----------|
| RenderProxy in HandleMap | ✅ | ✅ | ✅ |
| Frustum culling (CPU) | ✅ | ✅ | ✅ |
| Traditional LOD | ✅ | ✅ | ✅ (fallback for non-virtual meshes) |
| GPU instance culling | — | ✅ | ✅ |
| HZB occlusion | — | ✅ | ✅ |
| Virtual geometry (cluster BVH) | — | — | ✅ |
| Software rasterizer | — | — | ✅ |
| Visibility buffer | — | — | ✅ |

### 23.9 Material Parameter Override

Game code can change material properties per-instance without creating new Material assets. Changes are picked up at the next sync point automatically.

```cpp
auto& mesh = GetWorld().Get<StaticMeshComponent>(GetPawn());

// Per-instance color override (doesn't affect other entities using same material)
mesh.SetParameter("base_color", Vec4(1, 0, 0, 1));

// Texture swap
mesh.SetParameter("albedo_map", GetResources().Load<Texture>("content/textures/damage.uztexture"));

// Swap entire material
mesh.material = GetResources().Load<Material>("content/materials/burning.uzmat");

// Dissolve effect (animate a scalar parameter)
void OnTick(float dt) override {
    dissolve_progress += dt * 0.5f;
    auto& mesh = GetWorld().Get<StaticMeshComponent>(GetEntity());
    mesh.SetParameter("dissolve_amount", dissolve_progress);
}
```

Internally, `SetParameter()` writes to a `MaterialParamBlock` on the component. The sync point copies it to the proxy. The renderer binds it as a per-instance constant buffer alongside the material's shared constant buffer.

### 23.10 Render Layers

Bitmask-based visibility filtering. A proxy is visible to a camera only if `(proxy.render_layer & camera.layer_mask) != 0`.

```cpp
namespace RenderLayer {
    constexpr uint32_t Default     = 1 << 0;   // normal world geometry
    constexpr uint32_t UI3D        = 1 << 1;   // 3D HUD (health bars, nameplates)
    constexpr uint32_t Minimap     = 1 << 2;   // only visible to minimap camera
    constexpr uint32_t FirstPerson = 1 << 3;   // first-person arms, weapon
    constexpr uint32_t Portal      = 1 << 4;   // portal rendering targets
    constexpr uint32_t DebugDraw   = 1 << 5;   // debug lines, wireframes (stripped in shipping)
}

// Main camera
camera.layer_mask = RenderLayer::Default | RenderLayer::FirstPerson;

// Minimap camera (top-down, no FP arms)
minimap_cam.layer_mask = RenderLayer::Default | RenderLayer::Minimap;

// Portal camera (renders into a texture)
portal_cam.layer_mask = RenderLayer::Default | RenderLayer::Portal;
```

### 23.11 Thread Model — MVP to Full

The sync point API is identical in single-thread and dual-thread modes. The only difference is where the buffer lives.

```
MVP (Phase 1-2): Single-Thread
────────────────────────────────────────────────────
│ Game Tick │ Sync │ Cull │ Draw │ Present │
────────────────────────────────────────────────────
All on main thread. Simple, debuggable.
Sync writes directly to RenderWorld — no copy needed.

Phase 3: Dual-Thread (Double-Buffered)
────────────────────────────────────────────────────
Game Thread: │ Tick N │ Sync N→buf │ Tick N+1 │ Sync N+1→buf │
Render Thread:            │ Read buf │ Cull │ Draw │ Present │ Read buf │ ...
────────────────────────────────────────────────────
Sync writes to a staging buffer. Render thread swaps and reads.
Game thread never stalls (except if render is 2+ frames behind → frame pacing).

Phase 4: Triple-Buffered + Job System
────────────────────────────────────────────────────
Game Thread: │ Tick │ Sync │ Tick │ Sync │
Render Thread:       │ Cull+Draw N-1 │ Cull+Draw N │
Worker Pool:    │ AnimUpdate │ PhysicsSync │ ParticleUpdate │
────────────────────────────────────────────────────
```

**What game code sees**: Nothing changes. `PrimitiveComponent`, `SetParameter()`, `CameraComponent.is_active` — all the same API. The engine handles double-buffering internally.

### 23.12 Frame Pipeline — Full Picture

```
Frame N:
    │
    ├── 1. Input Tick (§21)
    │      OS events → modifier chains → trigger evaluation → callbacks
    │
    ├── 2. Game Tick (§8)
    │      PrePhysics → Physics → PostPhysics → PreRender tick groups
    │      Behaviors run, components update, events fire
    │
    ├── 3. Transform Propagation
    │      Dirty SceneComponents → recompute world matrices (parent→child)
    │
    ├── 4. Sync Point (§23.5)
    │      PrimitiveComponent → RenderProxy (create/update/destroy)
    │      CameraComponent → ViewState
    │
    ├── 5. Visibility (§23.6)
    │      Frustum cull (CPU) → GPU occlusion cull → LOD select
    │      Virtual geometry: cluster BVH traversal → cluster selection
    │
    ├── 6. Shadow Pass
    │      Per shadow-casting light: frustum cull → shadow map render
    │
    ├── 7. GBuffer Pass (Deferred)
    │      Opaque geometry → Albedo, Normal, Metallic/Roughness, Emissive, Depth
    │      Virtual geometry → visibility buffer → material shading → GBuffer
    │
    ├── 8. Lighting Pass
    │      Screen-space: read GBuffer + shadow maps → lit result
    │      GI: Lumen-style software RT / light probes / DXR (§ARCHITECTURE)
    │
    ├── 9. Transparent Pass (Forward)
    │      Back-to-front sorted, forward shading
    │
    ├── 10. Post-Processing (§ARCHITECTURE)
    │       TAA → SSAO → SSR → Bloom → DoF → Motion Blur → Tonemap → DLSS/FSR
    │
    └── 11. Present
            Swapchain present → start Frame N+1
```

### 23.13 Directory Structure

```
engine/
├── render_world/                           ← this section's code
│   ├── render_world.h/.cpp                 ← RenderWorld container + CullVisibility + GenerateDrawCalls
│   ├── render_proxy.h                      ← RenderProxy struct + RenderProxyHandle + RenderProxyDesc
│   ├── render_world_subsystem.h/.cpp       ← WorldSubsystem: Sync()
│   ├── view_state.h                        ← ViewState, Frustum
│   ├── render_layers.h                     ← RenderLayer constants
│   ├── material_param_block.h/.cpp         ← per-instance parameter storage
│   ├── gpu_culling/                        ← GPU instance culling pipeline
│   │   ├── gpu_cull_pipeline.h/.cpp        ← dispatch + buffer management
│   │   ├── hzb_generator.h/.cpp            ← depth pyramid from previous frame
│   │   └── shaders/
│   │       ├── cull_instances.hlsl
│   │       └── generate_hzb.hlsl
│   └── virtual_geometry/                   ← Phase 4+: Nanite-style pipeline
│       ├── virtual_mesh.h                  ← VirtualMesh asset + VirtualMeshComponent
│       ├── cluster_bvh.h/.cpp              ← cluster hierarchy traversal
│       ├── software_rasterizer.h/.cpp      ← compute-based triangle rasterizer
│       ├── visibility_buffer.h/.cpp        ← triangle ID + instance ID buffer
│       └── shaders/
│           ├── traverse_clusters.hlsl
│           ├── classify_clusters.hlsl
│           ├── software_raster.hlsl
│           └── material_shade.hlsl
```

### 23.14 Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Render proxy | `FPrimitiveSceneProxy` (vtable, heap) | `RID` → `RenderingServer` | Internal | **POD struct in HandleMap (cache-coherent)** |
| Sync mechanism | `ENQUEUE_RENDER_COMMAND` (lambda queue) | Scattered `RS::instance_set_*()` | `BatchRendererGroup` | **Single sync point (subsystem)** |
| CPU culling | Frustum | CPU HZB | Umbra (baked) | **Frustum (SIMD)** |
| GPU culling | GPU instance culling (indirect) | None | None (SRP) | **GPU instance culling (indirect)** |
| Occlusion | GPU HZB | CPU HZB | Umbra (baked) | **GPU HZB (no bake, fully dynamic)** |
| Virtual geometry | Nanite (cluster BVH + sw raster) | None | None | **Phase 4+ (cluster BVH + sw raster)** |
| Material override | Dynamic Material Instance | `set_instance_shader_parameter` | `MaterialPropertyBlock` | **`SetParameter()` → MaterialParamBlock** |
| Render layers | Bitmask (32 layers) | Bitmask (20 layers) | Bitmask (32 layers) | **Bitmask (32 layers)** |
| Thread model | Separate render thread | Threaded RenderingServer | Render thread | **Single-thread MVP → dual-thread Phase 3** |
| Draw call batching | GPU instancing + Nanite | MultiMesh | SRP Batcher | **GPU instancing + virtual geometry** |

### 23.15 Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 1 (MVP)** | RenderWorld + RenderProxy + Sync + CPU frustum cull + basic draw calls. Single-thread. |
| **Phase 2** | Material param override + render layers + LOD selection. |
| **Phase 3** | GPU instance culling + HZB occlusion + dual-thread render. |
| **Phase 4+** | Virtual geometry (cluster BVH, software rasterizer, visibility buffer). |

---

## 24. Physics World — Jolt Integration

> **Goal**: Define the gameplay-facing physics API — collision channels, queries, callbacks, forces, constraints, fixed timestep, character movement, and physical materials.
> **Backend**: Jolt Physics (MIT, deterministic, validated by Godot at scale).
> **References**: UE5 Chaos Physics public API (clean-room), Godot `PhysicsServer3D` + Jolt integration.

### 24.1 The Problem

Without a physics abstraction, game code must:
- Manually check AABB overlaps for trigger volumes
- Implement its own raycast against triangle meshes
- Track collision pairs frame-to-frame for begin/end events
- Hardcode timestep logic that drifts under variable frame rates
- Implement character ground detection, step-up, and wall sliding from scratch

### 24.2 Collision Channels & Response Matrix

Every physics body has two properties: an **object channel** (what it IS) and a **response table** (how it reacts to every other channel). Collision between two bodies uses the **minimum** of both responses — if either says Ignore, the pair is ignored.

```cpp
/// 32 collision channels. First 14 are engine-reserved, 14-31 are game-defined.
enum class CollisionChannel : uint8_t {
    WorldStatic   = 0,    // immovable geometry (floors, walls)
    WorldDynamic  = 1,    // movable physics objects (crates, barrels)
    Pawn          = 2,    // characters
    Visibility    = 3,    // trace-only (line-of-sight checks, no physics)
    Camera        = 4,    // camera collision traces
    PhysicsBody   = 5,    // ragdolls, cloth
    Projectile    = 6,    // bullets, arrows
    Trigger       = 7,    // overlap-only volumes
    // 8-13: engine reserved
    Game0         = 14,   // project-defined
    // ...
    Game17        = 31,
};

/// How a body responds to a specific channel.
enum class CollisionResponse : uint8_t {
    Ignore  = 0,   // no event, no block, no query hit
    Overlap = 1,   // fires OnOverlapBegin/End callbacks, no physical blocking
    Block   = 2,   // physically blocked, fires OnHit callback
};

/// Per-body response table. 32 bytes — one response per channel.
struct CollisionResponseContainer {
    CollisionResponse responses[32];

    /// Preset helpers
    static CollisionResponseContainer BlockAll();
    static CollisionResponseContainer OverlapAll();
    static CollisionResponseContainer IgnoreAll();

    void Set(CollisionChannel channel, CollisionResponse response);
    CollisionResponse Get(CollisionChannel channel) const;
};
```

**Collision enabled modes** — orthogonal to the response table:

```cpp
/// Controls whether a body participates in queries, simulation, or both.
enum class CollisionEnabled : uint8_t {
    NoCollision,       // invisible to everything
    QueryOnly,         // raycasts/sweeps hit it, no rigid body simulation
    PhysicsOnly,       // simulates, but invisible to raycasts
    QueryAndPhysics,   // full (default for dynamic bodies)
    ProbeOnly,         // gets contact callbacks but no physical response
};
```

**Why three responses (Ignore/Overlap/Block)?** Without this, trigger volumes require a separate object type. With it, any body can be a trigger for some channels and a blocker for others simultaneously. A door can Block Pawns but Overlap Projectiles (bullets pass through, player can't).

### 24.3 Physics Queries — Raycast, Sweep, Overlap

All queries go through `PhysicsWorld` (a WorldSubsystem). Three query shapes × two result modes (single/multi) × filter by channel.

#### HitResult

```cpp
/// Complete result of a physics query. Contains both swept-shape and surface contact info.
struct HitResult {
    bool  blocking_hit;        // false = overlap only
    bool  start_penetrating;   // trace started inside geometry
    float time;                // 0..1 along the trace segment
    float distance;            // world units from start

    /// Where the swept shape CENTER ends up at contact.
    /// For a sphere sweep, this is the sphere center at hit time.
    Vec3  location;

    /// Actual surface contact point.
    /// For VFX spawning (sparks, decals) — always use this, not location.
    Vec3  impact_point;

    /// Normal of the swept shape surface (inward-facing for spheres).
    Vec3  normal;

    /// Normal of the HIT surface (outward-facing from geometry).
    /// Use this for floor angle checks, wall sliding, and surface alignment.
    Vec3  impact_normal;

    Vec3  trace_start;
    Vec3  trace_end;
    float penetration_depth;   // if start_penetrating, distance to depenetrate

    Entity          entity;           // which game entity was hit
    PhysBodyHandle  body;             // Jolt body ID
    PhysMaterialId  physical_material;// surface type for footsteps/decals
    uint32_t        face_index;       // triangle index (complex trace only)
};
```

**Critical distinction**: `location` vs `impact_point`. Character movement uses `impact_normal` for floor walkability. VFX uses `impact_point` for spark placement. Never conflate them.

#### Query API

```cpp
/// Query parameters — per-query filtering.
struct QueryParams {
    bool trace_complex = false;         // hit triangle mesh, not simplified hull
    bool find_initial_overlaps = true;  // detect if start is already inside
    bool return_physical_material = false;

    /// Entities to skip (inline, no heap allocation).
    Entity ignore_entities[8];
    uint32_t ignore_entity_count = 0;
};

class PhysicsWorld : public WorldSubsystem {
public:
    // ─── Line Trace ────────────────────────────────────────
    /// Cast a ray. Returns true if a blocking hit was found.
    bool LineTraceSingle(HitResult& out, Vec3 start, Vec3 end,
                         CollisionChannel channel,
                         const QueryParams& params = {}) const;

    /// Cast a ray. Returns all hits (blocking + overlapping), sorted by distance.
    int32_t LineTraceMulti(HitResult* out, int32_t max_results,
                           Vec3 start, Vec3 end,
                           CollisionChannel channel,
                           const QueryParams& params = {}) const;

    // ─── Sweep (Swept Shape) ───────────────────────────────
    /// Sweep a shape (sphere, box, capsule) along a line. Returns first blocking hit.
    bool SweepSingle(HitResult& out, Vec3 start, Vec3 end, Quat rotation,
                     const CollisionShape& shape, CollisionChannel channel,
                     const QueryParams& params = {}) const;

    int32_t SweepMulti(HitResult* out, int32_t max_results,
                       Vec3 start, Vec3 end, Quat rotation,
                       const CollisionShape& shape, CollisionChannel channel,
                       const QueryParams& params = {}) const;

    // ─── Overlap (Static Test) ─────────────────────────────
    /// Test if a shape at a position overlaps anything. Returns overlap count.
    int32_t OverlapMulti(HitResult* out, int32_t max_results,
                         Vec3 position, Quat rotation,
                         const CollisionShape& shape, CollisionChannel channel,
                         const QueryParams& params = {}) const;

    /// Quick boolean test — any overlap at all?
    bool OverlapAny(Vec3 position, Quat rotation,
                    const CollisionShape& shape, CollisionChannel channel,
                    const QueryParams& params = {}) const;
};

/// Shape for sweeps and overlaps.
struct CollisionShape {
    enum Type { Sphere, Box, Capsule };
    Type type;
    union {
        struct { float radius; } sphere;
        struct { Vec3 half_extents; } box;
        struct { float radius; float half_height; } capsule;
    };

    static CollisionShape MakeSphere(float radius);
    static CollisionShape MakeBox(Vec3 half_extents);
    static CollisionShape MakeCapsule(float radius, float half_height);
};
```

#### Usage Example

```cpp
// Raycast from camera for aiming
HitResult hit;
Vec3 start = camera.GetWorldPosition();
Vec3 end = start + camera.GetForward() * 1000.0f;

if (GetWorld().GetSubsystem<PhysicsWorld>().LineTraceSingle(hit, start, end, CollisionChannel::Visibility)) {
    // hit.impact_point = where the crosshair is pointing
    // hit.impact_normal = surface normal for decal alignment
    // hit.entity = what we're looking at
    // hit.physical_material = surface type for hit sound selection
    SpawnDecal(hit.impact_point, hit.impact_normal);
}

// Overlap sphere for area damage
HitResult overlaps[32];
auto shape = CollisionShape::MakeSphere(5.0f);
int32_t count = physics.OverlapMulti(overlaps, 32, explosion_pos, Quat::Identity(),
                                      shape, CollisionChannel::Pawn);
for (int32_t i = 0; i < count; ++i) {
    float dist = Vec3::Distance(explosion_pos, overlaps[i].impact_point);
    float damage = Lerp(100.0f, 10.0f, dist / 5.0f);
    ApplyDamage(overlaps[i].entity, damage);
}
```

### 24.4 Collision Events & Callbacks

Three event types, matching the three collision responses:

```cpp
/// Fired when physics simulation produces a blocking contact.
/// Only fires if the body has notify_hit = true.
struct HitEvent {
    Entity    self_entity;
    Entity    other_entity;
    Vec3      normal_impulse;   // direction × magnitude of the impact
    HitResult hit;              // full contact info
};

/// Fired when a new overlap begins (Overlap response, not Block).
/// Only fires if both bodies have generate_overlaps = true.
struct OverlapBeginEvent {
    Entity    self_entity;
    Entity    other_entity;
    bool      from_sweep;       // detected by movement sweep vs physics step
    HitResult sweep_result;     // valid only if from_sweep
};

/// Fired when an existing overlap ends.
struct OverlapEndEvent {
    Entity self_entity;
    Entity other_entity;
};
```

**Registration in Behaviors:**

```cpp
class ExplosiveBarrel : public Behavior {
    void OnBeginPlay() override {
        auto& body = Get<RigidBody>();
        body.notify_hit = true;
        body.OnHit.Bind(this, &ExplosiveBarrel::OnHit);
    }

    void OnHit(const HitEvent& event) {
        if (event.normal_impulse.Length() > 500.0f) {
            Explode();
        }
    }
};

class TriggerZone : public Behavior {
    void OnBeginPlay() override {
        auto& trigger = Get<TriggerVolume>();
        trigger.OnOverlapBegin.Bind(this, &TriggerZone::OnEnter);
        trigger.OnOverlapEnd.Bind(this, &TriggerZone::OnExit);
    }

    void OnEnter(const OverlapBeginEvent& event) {
        if (GetWorld().Has<PlayerTag>(event.other_entity)) {
            OpenDoor();
        }
    }

    void OnExit(const OverlapEndEvent& event) {
        CloseDoor();
    }
};
```

### 24.5 Force & Impulse API

Two categories: **force** (continuous, applied per physics tick) and **impulse** (instantaneous, applied once). Each has a mass-dependent and mass-independent variant.

```cpp
class RigidBody : public PhysicsBody {
public:
    // ─── Continuous Forces (call from OnPhysicsTick) ───────
    /// Apply force in Newtons. Affected by mass (heavier = less acceleration).
    void AddForce(Vec3 force);
    /// Apply acceleration directly. Mass-independent (jump = same height regardless of mass).
    void AddAcceleration(Vec3 accel);
    /// Apply force at a world position. Generates torque if off-center.
    void AddForceAtPosition(Vec3 force, Vec3 world_pos);
    /// Apply torque in Newton-meters.
    void AddTorque(Vec3 torque);

    // ─── Instantaneous Impulses (call once per event) ──────
    /// Apply impulse in kg·m/s. Affected by mass.
    void AddImpulse(Vec3 impulse);
    /// Apply velocity change directly. Mass-independent.
    void AddVelocityChange(Vec3 delta_velocity);
    /// Apply impulse at a world position. Generates angular impulse if off-center.
    void AddImpulseAtPosition(Vec3 impulse, Vec3 world_pos);
    /// Apply angular impulse in rad/s.
    void AddAngularImpulse(Vec3 angular_impulse);

    // ─── Direct Velocity Control (use sparingly) ───────────
    void SetLinearVelocity(Vec3 velocity, bool add_to_current = false);
    void SetAngularVelocity(Vec3 angular_vel, bool add_to_current = false);
    Vec3 GetLinearVelocity() const;
    Vec3 GetAngularVelocity() const;
    Vec3 GetVelocityAtPoint(Vec3 world_point) const;

    // ─── Sleep Control ─────────────────────────────────────
    void WakeUp();
    void PutToSleep();
    bool IsSleeping() const;
};
```

**Why mass-independent variants?** A jump should always reach the same height regardless of character mass. Without `AddAcceleration()` / `AddVelocityChange()`, game code must divide by mass — introducing bugs when mass changes at runtime (power-ups, carrying objects).

### 24.6 Physical Material

Per-surface properties for friction, bounciness, density, and game-side surface type routing.

```cpp
/// Physical material — assigned per collision shape.
/// Controls how surfaces interact and provides a game-side surface type tag
/// for footstep sounds, decals, and particle effects.
struct PhysicalMaterial {
    float friction          = 0.5f;
    float static_friction   = 0.5f;
    float restitution       = 0.3f;   // bounciness 0..1
    float density           = 1.0f;   // g/cm³, used to compute mass from shape volume

    /// How friction/restitution combines when two materials meet.
    /// If materials disagree, the more conservative mode wins.
    enum class CombineMode { Average, Min, Max, Multiply };
    CombineMode friction_combine    = CombineMode::Average;
    CombineMode restitution_combine = CombineMode::Average;

    /// Game-side surface type tag (0-31). Returned in HitResult.physical_material.
    /// Use for: footstep sounds, bullet decals, tire friction, particle effects.
    /// Project defines the mapping (0=Default, 1=Concrete, 2=Metal, 3=Wood, etc.)
    uint8_t surface_type = 0;
};
```

**Surface type is the correct hook for game-side routing.** Don't use collision channel or entity type for footstep/decal selection — a stone wall and stone floor are different objects on different channels but share the same physical material.

### 24.7 Physics Body Properties

```cpp
/// Per-body simulation configuration.
/// Set on RigidBody, CharacterBody, or StaticBody components.
struct PhysicsBodyDesc {
    CollisionChannel          object_channel = CollisionChannel::WorldDynamic;
    CollisionResponseContainer response      = CollisionResponseContainer::BlockAll();
    CollisionEnabled          collision_mode = CollisionEnabled::QueryAndPhysics;

    bool  simulate_physics    = true;
    bool  enable_gravity      = true;
    bool  use_ccd             = false;   // continuous collision detection (fast objects)
    bool  notify_hit          = false;   // enables OnHit callbacks
    bool  generate_overlaps   = false;   // enables OnOverlapBegin/End callbacks

    float linear_damping      = 0.01f;
    float angular_damping     = 0.05f;
    float max_angular_velocity = 100.0f; // rad/s cap
    float mass_override       = 0.0f;   // 0 = compute from shape + material density
    float mass_scale          = 1.0f;
    Vec3  center_of_mass_offset = {};

    bool lock_linear_x  = false, lock_linear_y  = false, lock_linear_z  = false;
    bool lock_angular_x = false, lock_angular_y = false, lock_angular_z = false;

    float sleep_threshold_multiplier = 1.0f;
};
```

### 24.8 Constraints (Joints)

Per-axis Free/Limited/Locked model — any joint type is expressible without a type enum.

```cpp
/// Constraint between two bodies. Per-axis motion control.
/// A hinge = all translations locked, 1 rotation free.
/// A ball socket = all translations locked, all rotations free.
/// A slider = 1 translation free, all rotations locked.
struct ConstraintDesc {
    Entity body_a, body_b;
    Vec3   pivot_a, pivot_b;    // local-space pivot points
    Quat   frame_a, frame_b;    // local-space orientation

    enum Motion : uint8_t { Free, Limited, Locked };

    // Linear axes
    Motion linear_x = Locked, linear_y = Locked, linear_z = Locked;
    float  linear_limit = 0.0f;   // used when Limited

    // Angular: swing (Y,Z cone) + twist (X axis)
    Motion swing1 = Free, swing2 = Free, twist = Free;
    float  swing1_limit_deg = 0.0f, swing2_limit_deg = 0.0f, twist_limit_deg = 0.0f;

    // Break threshold (0 = unbreakable)
    float break_force  = 0.0f;
    float break_torque = 0.0f;

    // Motor (optional)
    struct Drive {
        bool  enabled = false;
        float stiffness = 0.0f;
        float damping = 0.0f;
        float max_force = FLT_MAX;
    };
    Drive linear_drive;
    Drive angular_drive;
    Vec3  linear_target_position;
    Vec3  linear_target_velocity;
    Quat  angular_target_orientation;
    Vec3  angular_target_velocity;
};

/// Convenience constructors
ConstraintDesc ConstraintDesc::MakeHinge(Entity a, Entity b, Vec3 pivot, Vec3 axis);
ConstraintDesc ConstraintDesc::MakeBallSocket(Entity a, Entity b, Vec3 pivot);
ConstraintDesc ConstraintDesc::MakeSlider(Entity a, Entity b, Vec3 pivot, Vec3 slide_axis);
ConstraintDesc ConstraintDesc::MakeFixed(Entity a, Entity b);
```

### 24.9 Fixed Timestep & Async Physics Tick

Physics runs at a fixed rate (default 120Hz), independent of the variable game frame rate. Game code hooks into the physics tick for deterministic force application.

```cpp
/// Behavior callback for fixed-rate physics tick.
/// Called at the physics substep rate (e.g., 120Hz), not the game tick rate.
/// Apply forces and read physics state here for frame-rate-independent behavior.
class Behavior {
    // ... existing methods ...
    virtual void OnPhysicsTick(float fixed_dt) {}  // called at fixed rate (e.g., 1/120s)
};
```

**Timing model:**

```
Game Frame (variable, e.g., 16ms at 60 FPS):
    │
    ├── Game Tick (variable dt)
    │
    ├── Physics Step (may run 0, 1, or 2+ substeps to catch up)
    │   ├── Substep 1 (fixed_dt = 1/120s)
    │   │   ├── OnPhysicsTick() callbacks
    │   │   └── Jolt simulation step
    │   ├── Substep 2 (if time accumulated)
    │   │   ├── OnPhysicsTick() callbacks
    │   │   └── Jolt simulation step
    │   └── ... (max substeps capped to prevent spiral of death)
    │
    └── Transform Interpolation
        └── Visual transform = lerp(prev_physics, curr_physics, alpha)
            where alpha = accumulated_time / fixed_dt
```

**Transform interpolation** prevents visual jitter when physics runs at a different rate than rendering. The game world stores two snapshots (previous + current physics transform). The renderer interpolates between them using the fractional time remaining after the last substep.

### 24.10 Character Movement — Query-Based, Not Simulated

Characters do NOT use rigid body simulation. The character capsule is `CollisionEnabled::QueryOnly` — it participates in raycasts/sweeps but has no physics body. All motion is driven by game code via sweeps. This prevents rigid body instabilities (tumbling on slopes, jittering on edges).

```cpp
class CharacterMovementComponent : public ActorComponent {
public:
    // ─── Configuration ─────────────────────────────────────
    float max_walk_speed       = 600.0f;   // cm/s
    float max_fall_speed       = 2000.0f;  // cm/s terminal velocity
    float acceleration         = 2048.0f;  // cm/s²
    float braking_deceleration = 2048.0f;
    float ground_friction      = 8.0f;
    float gravity              = -981.0f;  // cm/s²
    float jump_velocity        = 420.0f;
    float max_step_height      = 45.0f;   // cm
    float walkable_floor_angle = 44.0f;   // degrees — steeper = slide

    // ─── State ─────────────────────────────────────────────
    Vec3  velocity;
    bool  is_grounded = false;
    bool  is_falling  = false;

    // ─── Inputs (set by PlayerController) ──────────────────
    void AddInput(Vec3 world_direction);
    void Jump();

    // ─── Queries used internally ───────────────────────────

    /// Sweep capsule downward to detect walkable ground.
    /// Returns floor info: distance, normal, walkable flag.
    struct FloorResult {
        bool      walkable;         // normal angle < walkable_floor_angle
        bool      from_line_trace;  // very close to floor (sweep can't detect)
        float     floor_distance;
        HitResult hit;
    };
    FloorResult FindFloor(Vec3 position) const;

    /// Attempt to step over an obstacle. Sweeps up → forward → down.
    /// Returns true if the step succeeded and updates position.
    bool StepUp(Vec3& position, Vec3 move_delta, const HitResult& wall_hit) const;

    /// Slide remaining velocity along a surface after hitting a wall/ramp.
    Vec3 SlideAlongSurface(Vec3 velocity, float dt, Vec3 surface_normal) const;
};
```

**Why not rigid body?**
- Rigid body characters tumble unpredictably on slight geometry variations
- Step-up logic requires custom sweeps regardless
- Query-only capsule is cheaper (not in simulation BVH)
- Deterministic: same input = same position, no solver jitter

### 24.11 Physics Debug Draw

```cpp
/// Debug visualization for physics shapes, contacts, and queries.
/// Only active in dev/editor builds — stripped in shipping.
class PhysicsWorld : public WorldSubsystem {
    // ... queries above ...

    // ─── Debug ─────────────────────────────────────────────
    void SetDebugDrawEnabled(bool enabled);

    /// Draw options (bitmask)
    enum DebugDraw : uint32_t {
        Shapes        = 1 << 0,   // collision shapes (wireframe)
        Contacts      = 1 << 1,   // active contact points
        Constraints   = 1 << 2,   // joint visualizations
        BoundingBoxes = 1 << 3,   // AABBs
        CenterOfMass  = 1 << 4,   // CoM markers
        Velocities    = 1 << 5,   // velocity vectors
        SleepState    = 1 << 6,   // color sleeping bodies differently
    };
    void SetDebugDrawFlags(uint32_t flags);
};
```

### 24.12 Directory Structure

```
engine/components/physics/                   ← component definitions (§4.2)
│   ├── collision_object.h
│   ├── physics_body.h
│   ├── rigid_body.h/.cpp
│   ├── character_body.h/.cpp
│   ├── static_body.h
│   ├── collision_shape.h/.cpp
│   ├── trigger_volume.h
│   └── joints/
│       └── ...

engine/subsystem/
│   └── physics_world.h/.cpp                 ← PhysicsWorld subsystem (queries, step, debug)

engine/physics/                              ← physics internals
├── collision_channel.h                      ← CollisionChannel enum + CollisionResponseContainer
├── collision_shape.h                        ← CollisionShape (sphere/box/capsule)
├── hit_result.h                             ← HitResult struct
├── query_params.h                           ← QueryParams
├── physics_events.h                         ← HitEvent, OverlapBeginEvent, OverlapEndEvent
├── physical_material.h                      ← PhysicalMaterial + SurfaceType
├── constraint.h                             ← ConstraintDesc + convenience constructors
├── physics_body_desc.h                      ← PhysicsBodyDesc
└── jolt/                                    ← Jolt integration internals (not public API)
    ├── jolt_physics_world.cpp               ← JPH::PhysicsSystem wrapper
    ├── jolt_contact_listener.cpp            ← hit/overlap event dispatch
    ├── jolt_layer_mapping.cpp               ← CollisionChannel → Jolt ObjectLayer
    └── jolt_debug_renderer.cpp              ← Jolt debug draw → engine debug draw
```

### 24.13 Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Backend | Chaos (proprietary) | Jolt | PhysX / Havok | **Jolt (MIT, deterministic)** |
| Collision model | Channel × Response (Ignore/Overlap/Block) | Layer × Mask (bitmask) | Layer × Layer matrix | **Channel × Response (UE5 pattern)** |
| Query API | `LineTraceSingle/Multi` | `PhysicsServer3D::intersect_ray` | `Physics.Raycast` | **`LineTraceSingle/Multi`, `Sweep`, `Overlap`** |
| Hit result | `FHitResult` (Location + ImpactPoint split) | Dictionary | `RaycastHit` | **`HitResult` (Location + ImpactPoint split)** |
| Callbacks | `OnComponentHit/BeginOverlap/EndOverlap` | `body_entered/exited` signals | `OnCollisionEnter/Exit` | **`OnHit`, `OnOverlapBegin/End`** |
| Force API | `AddForce` + `bAccelChange` flag | `apply_force/impulse` | `AddForce/AddTorque` | **`AddForce` / `AddAcceleration` (separate methods)** |
| Character | Kinematic sweep (QueryOnly capsule) | `CharacterBody3D.move_and_slide()` | `CharacterController.Move()` | **Kinematic sweep (QueryOnly capsule)** |
| Fixed timestep | Async physics tick (120Hz) | Fixed 60Hz | `FixedUpdate` (50Hz) | **Fixed 120Hz + interpolation** |
| Constraints | `FConstraintInstance` (6DOF) | `Joint3D` subtypes | `Joint` components | **Per-axis Free/Limited/Locked (6DOF)** |
| Physical material | `UPhysicalMaterial` + SurfaceType | `PhysicsMaterial` | `PhysicMaterial` | **`PhysicalMaterial` + SurfaceType (32 slots)** |
| Deterministic | No (Chaos) | Yes (Jolt) | No (PhysX) / Yes (Havok) | **Yes (Jolt)** |

### 24.14 Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | PhysicsWorld subsystem, collision channels, line trace, sweep, overlap, HitResult. Static + dynamic bodies. |
| **Phase 3** | Callbacks (OnHit, OnOverlapBegin/End), constraints, physical materials, character movement integration. |
| **Phase 4** | Async physics tick (120Hz), transform interpolation, ragdoll, cloth. |

---

## 25. Audio System — miniaudio + Submix Hierarchy

> **Goal**: Define the gameplay-facing audio API — play/stop/fade, 3D spatialization, bus hierarchy, concurrency, attenuation, and audio zones.
> **Backend**: miniaudio (public domain, cross-platform device abstraction) + custom submix mixer.
> **References**: UE5 AudioMixer + Submix hierarchy (clean-room), Godot AudioServer bus system.

### 25.1 The Problem

Without an audio abstraction, game code must:
- Manage raw PCM buffers and device output manually
- Implement distance attenuation, spatialization, and air absorption per-sound
- Track how many gunshot sounds are playing simultaneously (voice budget)
- Hardcode volume levels with no bus routing (can't mute "all SFX" in one call)
- Implement crossfade logic for music transitions

### 25.2 Architecture Overview

```
Game Code
    │
    ├── Fire-and-forget: uz::Audio::PlayAt(sound, position)
    │   └── Creates internal voice, auto-destroys on completion
    │
    ├── Persistent: AudioSourceComponent (attached to entity)
    │   └── Play, FadeIn, FadeOut, Stop, SetVolume, SetPitch
    │
    ▼
Audio Engine (runs on dedicated audio thread)
    │
    ├── Voice Pool (max ~64 simultaneous voices)
    │   ├── Active voices → mixing
    │   ├── Virtual voices → tracked silently (resume when slot opens)
    │   └── Concurrency Manager → decides which voices live/die
    │
    ├── Submix Bus Tree
    │   ├── Master
    │   │   ├── SFX → effects: compressor
    │   │   ├── Music → effects: none (or crossfade)
    │   │   ├── Voice → effects: ducking
    │   │   └── Ambient → effects: reverb
    │   └── Each bus: volume, mute, effect chain
    │
    ├── Spatialization (per-voice)
    │   ├── Distance attenuation (inverse square / logarithmic / linear)
    │   ├── HRTF panning (optional plugin)
    │   ├── Air absorption LPF
    │   └── Occlusion (physics raycast → LPF + volume reduction)
    │
    └── miniaudio Device Output
```

### 25.3 Audio Assets

```cpp
/// Base audio asset — the playable contract. Can be a raw wave or a sound cue.
/// Game code always uses ResourceHandle<SoundAsset>, never raw PCM.
class SoundAsset {
public:
    float  base_volume   = 1.0f;
    float  base_pitch    = 1.0f;
    float  priority      = 50.0f;      // 0-100, for voice eviction
    float  duration      = 0.0f;       // seconds, computed at import

    ResourceHandle<Attenuation> attenuation;    // spatial falloff (null = 2D)
    ResourceHandle<Concurrency> concurrency;    // voice budget rules (null = unlimited)

    /// Which bus this sound routes to by default.
    StringView default_bus = "SFX";
};

/// Raw audio data — decoded or streaming PCM.
class SoundWave : public SoundAsset {
    // Backed by ma_decoder or ma_audio_buffer in miniaudio.
    // Supports: WAV, MP3, OGG, FLAC via miniaudio's built-in decoders.
    enum class DecompressionType { InMemory, Streaming };
    DecompressionType decompression = DecompressionType::InMemory;
};

/// Behavioral audio descriptor — picks from a set of waves with randomization,
/// volume/pitch variation, and looping. JSON-authored (.uzsound).
class SoundCue : public SoundAsset {
    /// Playback mode for selecting from the wave list.
    enum class Mode { Random, RoundRobin, Shuffle };
    Mode mode = Mode::Random;

    std::vector<ResourceHandle<SoundWave>> waves;

    float volume_variation_min = 1.0f;   // random range applied per play
    float volume_variation_max = 1.0f;
    float pitch_variation_min  = 1.0f;
    float pitch_variation_max  = 1.0f;
    bool  looping = false;
};
```

**`.uzsound` format (SoundCue):**

```json
{
    "format": "uzsound",
    "version": 1,
    "mode": "random",
    "waves": [
        "content/audio/footstep_concrete_01.wav",
        "content/audio/footstep_concrete_02.wav",
        "content/audio/footstep_concrete_03.wav"
    ],
    "volume_variation": [0.9, 1.1],
    "pitch_variation": [0.95, 1.05],
    "looping": false,
    "default_bus": "SFX",
    "attenuation": "content/audio/attenuation_footstep.uzatten",
    "concurrency": "content/audio/concurrency_footstep.uzconcurrency"
}
```

### 25.4 Fire-and-Forget API

For one-shot sounds that game code doesn't need to control after triggering.

```cpp
namespace uz::Audio {
    /// Play a 2D sound (non-spatialized). UI clicks, menu sounds.
    void Play2D(ResourceHandle<SoundAsset> sound,
                float volume = 1.0f, float pitch = 1.0f);

    /// Play a 3D sound at a fixed world position. Explosions, impacts.
    /// The sound does NOT follow any entity — it stays at the position.
    void PlayAt(World& world, ResourceHandle<SoundAsset> sound, Vec3 position,
                float volume = 1.0f, float pitch = 1.0f);

    /// Play a 3D sound attached to an entity. Follows the entity's transform.
    /// Auto-stops when the entity is destroyed (if stop_on_destroy = true).
    void PlayAttached(World& world, ResourceHandle<SoundAsset> sound, Entity entity,
                      float volume = 1.0f, float pitch = 1.0f,
                      bool stop_on_destroy = true);
}
```

**No handle returned.** Game code cannot stop, fade, or mutate these sounds. If you need control, use `AudioSourceComponent` (§25.5).

### 25.5 AudioSourceComponent — Persistent Emitter

```cpp
/// 3D positional audio emitter. Attached to an entity, follows its transform.
/// Use for: looping sounds (engine hum, torch crackle), music sources, ambient emitters.
class AudioSourceComponent : public SceneComponent {
public:
    ResourceHandle<SoundAsset> sound;
    bool auto_play = false;         // start playing on BeginPlay
    bool stop_on_destroy = true;    // stop when entity is destroyed

    // ─── Playback Control ──────────────────────────────────
    void Play(float start_time = 0.0f);
    void Stop();
    void FadeIn(float duration, float target_volume = 1.0f, FadeCurve curve = FadeCurve::Linear);
    void FadeOut(float duration, float target_volume = 0.0f, FadeCurve curve = FadeCurve::Linear);
    void Pause();
    void Resume();

    // ─── Runtime Parameters ────────────────────────────────
    void SetVolume(float linear);           // 0..1+
    void SetPitch(float scale);             // 0.5 = octave down, 2.0 = octave up
    void SetLowPassFilter(float cutoff_hz); // 0 = disabled
    void SetHighPassFilter(float cutoff_hz);

    // ─── Routing ───────────────────────────────────────────
    void SetBus(StringView bus_name);                        // route to a different bus
    void SetBusSend(StringView bus_name, float send_level);  // parallel send (e.g., reverb)

    // ─── State ─────────────────────────────────────────────
    enum class PlayState { Stopped, Playing, Paused, FadingIn, FadingOut };
    PlayState GetPlayState() const;
    bool IsPlaying() const;
    float GetPlaybackPosition() const;  // seconds

    // ─── Callbacks ─────────────────────────────────────────
    MulticastDelegate<void()> OnFinished;   // fires when sound completes (non-looping)
    MulticastDelegate<void(float)> OnPlaybackPercent;  // 0..1 progress
};

/// Fade curve types for FadeIn/FadeOut.
enum class FadeCurve : uint8_t {
    Linear,         // constant rate
    Logarithmic,    // fast start, slow end
    SCurve,         // smooth acceleration/deceleration
    EqualPower,     // perceptually correct crossfade (sin/cos)
};
```

### 25.6 AudioListenerComponent

```cpp
/// Defines the listener position for 3D spatialization.
/// Usually attached to the camera entity. Only one active listener per world.
class AudioListenerComponent : public SceneComponent {
public:
    bool is_active = false;   // only one active listener — set true on the main camera
};
```

The audio engine reads the active listener's world position and orientation each frame to compute per-voice spatialization.

### 25.7 Submix Bus Tree

Hierarchical mixing with per-bus volume, mute, and effect chain. Every sound routes to a bus. Every bus feeds its parent. Master is the root.

```cpp
/// A mixing bus in the submix hierarchy. Created at startup from project config.
class AudioBus {
public:
    StringView name;

    void SetVolume(float linear);   // 0..1+
    void SetMuted(bool muted);      // mutes this bus and all children
    float GetVolume() const;
    bool IsMuted() const;

    /// Add a DSP effect to this bus's chain (reverb, compressor, EQ).
    void AddEffect(AudioEffect* effect);
    void RemoveEffect(AudioEffect* effect);

    /// Reroute this bus to a different parent at runtime.
    /// Use for: underwater → reroute SFX through a lowpass bus.
    void SetParent(AudioBus* new_parent);
};
```

**Default bus topology** (configured in `.uzproject`):

```
Master (output to device)
├── SFX                    ← gunshots, footsteps, impacts
│   └── Weapons            ← sub-bus for weapon-specific mixing
├── Music                  ← background music, stingers
├── Voice                  ← dialogue, VO
│   └── Dialogue           ← sub-bus for subtitle-synced VO
└── Ambient                ← environment loops, wind, rain
```

**Usage:**

```cpp
// Mute all music (pause menu)
auto& audio = GetWorld().GetSubsystem<AudioSubsystem>();
audio.GetBus("Music")->SetMuted(true);

// Reduce SFX volume to 50%
audio.GetBus("SFX")->SetVolume(0.5f);

// Add reverb to Ambient bus
auto* reverb = audio.CreateEffect<ReverbEffect>(ReverbPreset::Cathedral);
audio.GetBus("Ambient")->AddEffect(reverb);
```

### 25.8 Concurrency — Voice Budget Management

Controls how many instances of a sound can play simultaneously and what happens when the limit is exceeded.

```cpp
/// Voice eviction strategy when max_count is reached.
enum class ConcurrencyRule : uint8_t {
    PreventNew,             // reject the new sound
    StopOldest,             // kill the longest-running voice
    StopFarthest,           // kill the most distant voice
    StopFarthestThenOldest, // kill most distant; if tied, kill oldest (default)
    StopLowestPriority,     // kill the lowest SoundAsset::priority
    StopQuietest,           // kill the quietest (post-attenuation volume)
};

/// Concurrency rules for a sound class. Shared across assets.
struct ConcurrencySettings {
    int32_t         max_count = 8;
    bool            limit_to_owner = false;  // per-entity vs global limit
    ConcurrencyRule rule = ConcurrencyRule::StopFarthestThenOldest;
    float           retrigger_time = 0.0f;   // min seconds between plays
    float           voice_steal_fade = 0.05f;// fade out before killing (prevents pops)
};
```

**Example:** Footsteps use `StopOldest, max_count=4, limit_to_owner=true` — each character can play at most 4 footstep sounds, oldest is cut first. Globally, 50 NPCs can each have their 4 footsteps without stomping each other.

**Voice virtualization:** When the global voice pool (~64 voices) is exhausted, low-priority voices are **virtualized** — the engine tracks their elapsed time silently. When a voice slot opens, the sound resumes at the correct playback position. This prevents audible pops from hard-cutting distant ambient loops.

### 25.9 Attenuation — 3D Spatial Falloff

```cpp
/// Reusable attenuation settings. Shared across sounds (e.g., all footsteps use the same).
struct AttenuationSettings {
    // ─── Distance Volume Falloff ───────────────────────────
    bool  enable_distance = true;
    float min_distance = 100.0f;     // full volume within this radius (cm)
    float max_distance = 5000.0f;    // silence beyond this radius
    enum class FalloffModel { Linear, InverseSquare, Logarithmic };
    FalloffModel falloff = FalloffModel::InverseSquare;

    // ─── Spatialization ────────────────────────────────────
    bool  enable_spatialization = true;
    float non_spatialized_radius = 50.0f;  // blend to 2D within this radius
                                            // (prevents comb filtering on close sources)

    // ─── Air Absorption (distance-based LPF) ──────────────
    bool  enable_air_absorption = false;
    float lpf_min_distance = 500.0f;
    float lpf_max_distance = 5000.0f;
    float lpf_cutoff_near  = 20000.0f; // Hz at min distance
    float lpf_cutoff_far   = 2000.0f;  // Hz at max distance

    // ─── Occlusion (physics raycast) ───────────────────────
    bool  enable_occlusion = false;
    float occlusion_check_interval = 0.1f;   // seconds between raycasts
    float occlusion_lpf_cutoff = 1000.0f;    // Hz when fully occluded
    float occlusion_volume_scale = 0.3f;     // volume when fully occluded
    float occlusion_interp_time = 0.2f;      // smooth transition in/out

    // ─── Reverb Send (distance-based) ──────────────────────
    bool  enable_reverb_send = false;
    float reverb_send_near = 0.0f;   // at min_distance
    float reverb_send_far  = 0.6f;   // at max_distance
};
```

**Occlusion** uses a physics raycast (via PhysicsWorld §24) from the sound source to the listener. If the ray is blocked, LPF and volume attenuation are applied with smooth interpolation. This models sound traveling through walls.

### 25.10 Audio Zones — World-Space DSP

Volume-based DSP modification. When the listener enters a zone, audio processing changes.

```cpp
/// World-space audio zone. When the listener is inside, modifies audio globally.
/// Use for: cave reverb, underwater muffling, indoor/outdoor transitions.
class AudioZone : public SceneComponent {
public:
    /// How outdoor sounds are heard from inside this zone.
    float exterior_volume = 0.3f;       // attenuate outdoor sounds
    float exterior_lpf_cutoff = 1500.0f; // muffle outdoor sounds

    /// How indoor sounds are heard from outside this zone.
    float interior_volume = 0.5f;
    float interior_lpf_cutoff = 2000.0f;

    /// Transition time when entering/exiting the zone.
    float crossfade_time = 0.5f;

    /// Optional: override a bus's effect chain while listener is inside.
    /// e.g., swap SFX bus to route through a cave reverb.
    StringView bus_override_target;                   // "SFX"
    std::vector<AudioEffect*> bus_override_effects;   // cave reverb chain
};
```

### 25.11 Music System

Music has unique requirements: crossfades, stingers (short one-shot overlays), and layered stems.

```cpp
namespace uz::Music {
    /// Play a music track. Crossfades from any currently playing music.
    void Play(ResourceHandle<SoundAsset> music, float crossfade_duration = 1.0f);

    /// Stop all music with fade out.
    void Stop(float fade_duration = 1.0f);

    /// Play a stinger (short one-shot) on top of current music.
    /// Ducks the music bus by duck_amount for the stinger's duration.
    void PlayStinger(ResourceHandle<SoundAsset> stinger, float duck_amount = 0.3f);

    /// Set music intensity (0..1). For layered music systems where
    /// stems fade in/out based on gameplay state.
    void SetIntensity(float intensity);
}
```

### 25.12 Thread Model

Audio runs on a dedicated thread — never blocks the game thread.

```
Game Thread                          Audio Thread
───────────────────                  ────────────────────────────
AudioSourceComponent.Play()          │
    │ enqueue command ──────────────►│ Voice allocation
                                     │ Concurrency check
AudioSourceComponent.SetVolume()     │ Mix active voices
    │ enqueue command ──────────────►│ Apply attenuation per voice
                                     │ Apply bus effects
                                     │ Sum to master
                                     │ Output to miniaudio device
```

Parameter changes from the game thread are **command-queued** and applied between mix frames. This avoids data races without locks on the hot path.

### 25.13 Directory Structure

```
engine/components/audio/                     ← component definitions (§4.2)
│   ├── audio_source_component.h/.cpp
│   └── audio_listener_component.h

engine/subsystem/
│   └── audio_subsystem.h/.cpp               ← AudioSubsystem (bus tree, voice pool, tick)

engine/audio/                                ← audio internals
├── audio_bus.h/.cpp                         ← AudioBus + effect chain
├── audio_voice.h/.cpp                       ← internal voice (wraps ma_sound)
├── audio_concurrency.h/.cpp                 ← ConcurrencySettings + voice eviction
├── audio_attenuation.h/.cpp                 ← AttenuationSettings + distance calc
├── audio_zone.h/.cpp                        ← AudioZone (world-space DSP)
├── audio_music.h/.cpp                       ← Music namespace (crossfade, stingers, stems)
├── audio_effect.h                           ← AudioEffect base (reverb, compressor, EQ)
├── sound_asset.h                            ← SoundAsset, SoundWave, SoundCue
├── fade_curve.h                             ← FadeCurve enum + interpolation
└── miniaudio/                               ← miniaudio integration internals
    ├── ma_engine_wrapper.cpp                ← ma_engine lifecycle
    ├── ma_bus_node.cpp                      ← custom ma_node for bus mixing
    └── ma_spatial_processor.cpp             ← 3D attenuation + HRTF + occlusion
```

### 25.14 Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Backend | Custom AudioMixer | Custom AudioServer | FMOD-based | **miniaudio (public domain)** |
| Bus system | USoundSubmix hierarchy | Flat indexed buses | AudioMixer groups | **Named bus tree (submix hierarchy)** |
| Concurrency | USoundConcurrency (6 rules) | `max_polyphony` (oldest only) | None built-in | **ConcurrencySettings (6 rules + per-owner)** |
| Attenuation | FSoundAttenuationSettings (asset) | Per-node params | Per-AudioSource | **AttenuationSettings (shared asset)** |
| Occlusion | Physics trace + LPF | None built-in | None built-in | **Physics raycast + LPF + volume** |
| Voice virtualization | Yes (track + resume) | No | No | **Yes (track + resume)** |
| Fire-and-forget | `PlaySoundAtLocation` | `AudioStreamPlayer.play()` | `AudioSource.PlayOneShot()` | **`Audio::PlayAt()` / `Audio::Play2D()`** |
| Music | Manual crossfade | Manual | Manual | **`Music::Play()` with auto-crossfade + stingers** |
| Audio zones | AAudioVolume | None built-in | Reverb zones | **AudioZone (interior/exterior + bus override)** |
| Thread model | Separate audio thread | Separate audio thread | FMOD thread | **Dedicated audio thread** |
| Spatialization | HRTF plugin support | Basic panning | Basic panning | **HRTF optional + distance + air absorption** |

### 25.15 Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | AudioSubsystem, AudioSourceComponent, Play2D/PlayAt, bus tree (Master/SFX/Music/Voice), basic distance attenuation. |
| **Phase 3** | Concurrency manager, voice virtualization, SoundCue (.uzsound), air absorption LPF, occlusion via physics raycast. |
| **Phase 4** | AudioZone, music system (crossfade/stingers/stems), HRTF spatialization, reverb effects. |
