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

### 4.2 Components

Components are **data structs** registered with the engine. No inheritance required.

```cpp
/// Position, rotation, scale in world or local space.
/// If the entity has a parent, local_transform is relative to parent.
/// world_transform is cached and recomputed when dirty.
struct TransformComponent {
    Vec3 position = {0, 0, 0};
    Quat rotation = Quat::Identity();
    Vec3 scale = {1, 1, 1};
    // --- internal (not set by user) ---
    Mat4 world_matrix;          // cached
    bool world_dirty = true;    // recompute on next access
};

/// References a mesh + material for rendering.
/// The RenderWorld reads this to build draw calls.
struct MeshRendererComponent {
    ResourceHandle<Mesh> mesh;
    ResourceHandle<Material> material;
    bool visible = true;
    bool cast_shadows = true;
};

/// Perspective or orthographic camera.
struct CameraComponent {
    float fov = 60.0f;           // degrees, perspective only
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    bool  is_ortho = false;
    float ortho_size = 10.0f;    // half-height, ortho only
    bool  is_active = false;     // only one active camera per world
};

/// Directional, point, or spot light.
struct LightComponent {
    enum Type { Directional, Point, Spot };
    Type type = Directional;
    Vec3 color = {1, 1, 1};
    float intensity = 1.0f;
    float range = 10.0f;         // point/spot only
    float spot_angle = 45.0f;    // spot only
    bool cast_shadows = true;
};
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
  core/                 ← primitives (no engine deps)
    assertions/
    config/
    containers/
    logging/
    math/
    memory/
    paths/
    random/

  rhi/                  ← GPU abstraction (depends on core/)
    dx12/
    rhi.h
    rhi_backend.h
    rhi_types.h
    rhi_config.h/.cpp

  renderer/             ← render pipeline (depends on core/, rhi/)
    camera/
    mesh/
    shader/
    upload/
    constant_buffers/
    vertex/

  engine/               ← game API (depends on core/, rhi/, renderer/, platform/)
    application/        ← Application, GameLoop, TimeManager
    world/              ← World, Entity, Component storage
    components/         ← built-in components (Transform, MeshRenderer, Light, Camera)
    behavior/           ← Behavior base class
    subsystem/          ← WorldSubsystem base + built-in subsystems
    event/              ← MulticastDelegate, EventQueue, EngineEvents
    tick/               ← TickGroup, TickFunction, TickManager
    plugin/             ← Plugin loader, ModuleDescriptor, C ABI
    resource/           ← ResourceManager, ResourceHandle, ResourceLoader
    scene/              ← SceneLoader, SceneSaver (.uzscene/.uzprefab)
    input/              ← InputAction, InputMap (wraps platform/)

  editor/               ← editor-only (depends on engine/)
    font/
    grid/
    screenshot/
    stats/

  developer/            ← dev-only, stripped in shipping (see DEVELOPER.md)
    profiling/
    debug_draw/
    hot_reload/
    console/

  platform/             ← OS abstraction (depends on core/)
    windows/

  demos/                ← refactored to use engine/ API
  tests/                ← mirrors source structure
  tools/                ← uzcook, uzembed, uzengine
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

    void SpawnEnemy() {
        auto& res = uz::Application::GetResources();
        auto e = GetWorld().CreateEntity("Enemy");
        GetWorld().Add<uz::TransformComponent>(e, {
            .position = {-10, 0.5f, 0}, .scale = {0.5f, 0.5f, 0.5f}
        });
        GetWorld().Add<uz::MeshRendererComponent>(e, {
            .mesh = res.Load<uz::Mesh>("meshes/sphere.uzmesh"),
            .material = res.Load<uz::Material>("materials/red.uzmat")
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

    Vec3 move_dir_ = {};

public:
    /// OnTick (every frame): apply movement + check death.
    /// Cheap — just move along cached direction, check health.
    void OnTick(float dt) override {
        auto& transform = Get<uz::TransformComponent>();
        auto& enemy = Get<Enemy>();

        // Apply cached movement direction (computed by PathUpdate)
        transform.position += move_dir_ * enemy.speed * dt;
        if (move_dir_.LengthSq() > 0.0f)
            transform.rotation = Quat::LookAt(move_dir_, Vec3::Up());

        // Death check — immediate response, not deferred
        if (enemy.health <= 0.0f) {
            auto* waves = GetWorld().GetSubsystem<WaveSubsystem>();
            waves->enemies_alive--;
            waves->OnEnemyKilled.Broadcast({.enemy = GetEntity(), .position = transform.position});
            GetWorld().DestroyEntity(GetEntity());
        }
    }

    // ─── Strategic: 10 Hz — waypoint navigation ─────────────────────────
    /// Evaluate waypoint progress and compute next move direction.
    /// Waypoint checks don't need per-frame precision.
    UZ_SCHEDULED(PathUpdate, 10.0f);
    void PathUpdate(float dt) {
        auto& transform = Get<uz::TransformComponent>();
        auto& enemy = Get<Enemy>();
        constexpr int kCount = sizeof(kWaypoints) / sizeof(kWaypoints[0]);

        if (enemy.path_index >= kCount) {
            GetWorld().GetSubsystem<WaveSubsystem>()->enemies_alive--;
            GetWorld().DestroyEntity(GetEntity());
            return;
        }

        Vec3 dir = kWaypoints[enemy.path_index] - transform.position;
        if (dir.Length() < 0.3f) {
            enemy.path_index++;
        }

        // Cache direction for OnTick to apply every frame (smooth movement)
        if (enemy.path_index < kCount) {
            move_dir_ = (kWaypoints[enemy.path_index] - transform.position).Normalized();
        } else {
            move_dir_ = Vec3::Zero();
        }
    }
};

class TurretAI : public uz::Behavior {
    UZ_BEHAVIOR(TurretAI);

    uz::Entity target_ = uz::Entity::Invalid();

public:
    /// OnTick (every frame): smooth turret rotation toward current target.
    /// Cheap — just interpolate rotation. No scanning, no spawning.
    void OnTick(float dt) override {
        if (target_ == uz::Entity::Invalid()) return;
        if (!GetWorld().IsValid(target_)) { target_ = uz::Entity::Invalid(); return; }

        auto& transform = Get<uz::TransformComponent>();
        auto& ep = GetWorld().Get<uz::TransformComponent>(target_).position;
        Quat desired = Quat::LookAt((ep - transform.position).Normalized(), Vec3::Up());
        transform.rotation = Quat::Slerp(transform.rotation, desired, dt * 8.0f);
    }

    // ─── Reactive: 10 Hz — scan for targets + fire ──────────────────────
    /// Scan enemies in range, pick nearest, fire if cooldown ready.
    /// 10Hz is plenty for turret targeting — no need for per-frame scans.
    UZ_SCHEDULED(TargetAndFire, 10.0f);
    void TargetAndFire(float dt) {
        auto& turret = Get<Turret>();
        auto& transform = Get<uz::TransformComponent>();

        turret.cooldown -= dt;

        // Scan for nearest enemy in range
        target_ = uz::Entity::Invalid();
        float nearest_dist = turret.range;

        GetWorld().ForEach<Enemy, uz::TransformComponent>(
            [&](uz::Entity e, Enemy&, uz::TransformComponent& et) {
                float d = (et.position - transform.position).Length();
                if (d < nearest_dist) { nearest_dist = d; target_ = e; }
            }
        );

        // Fire if target found and cooldown ready
        if (target_ != uz::Entity::Invalid() && turret.cooldown <= 0.0f) {
            auto& res = uz::Application::GetResources();
            auto bullet = GetWorld().CreateEntity("Bullet");
            GetWorld().Add<uz::TransformComponent>(bullet, {
                .position = transform.position + Vec3::Up() * 0.5f,
                .scale = {0.1f, 0.1f, 0.1f}
            });
            GetWorld().Add<uz::MeshRendererComponent>(bullet, {
                .mesh = res.Load<uz::Mesh>("meshes/sphere.uzmesh"),
                .material = res.Load<uz::Material>("materials/yellow_emissive.uzmat")
            });
            GetWorld().Add<Bullet>(bullet, {.target = target_});
            GetWorld().Add<BulletLogic>(bullet);
            turret.cooldown = 1.0f / turret.fire_rate;
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
| UZ_LOG | ✓ | | ✓ | ✓ |
