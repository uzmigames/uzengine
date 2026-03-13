# Physics World — Jolt Integration

> **Status**: Planned (Phase 2–4)
> **Source**: `engine/physics/`, `engine/subsystem/physics_world.h`
> **Backend**: Jolt Physics (MIT, deterministic, validated by Godot at scale)
> **References**: UE5 Chaos Physics public API (clean-room), Godot `PhysicsServer3D` + Jolt integration

## Purpose

Gameplay-facing physics API — collision channels, queries, callbacks, forces, constraints, fixed timestep, character movement, and physical materials. All game code interacts with physics through `PhysicsWorld` (a WorldSubsystem) and component APIs, never through Jolt directly.

## Collision Channels & Response Matrix

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

    static CollisionResponseContainer BlockAll();
    static CollisionResponseContainer OverlapAll();
    static CollisionResponseContainer IgnoreAll();

    void Set(CollisionChannel channel, CollisionResponse response);
    CollisionResponse Get(CollisionChannel channel) const;
};
```

### Collision Enabled Modes

Orthogonal to the response table — controls whether a body participates in queries, simulation, or both:

```cpp
enum class CollisionEnabled : uint8_t {
    NoCollision,       // invisible to everything
    QueryOnly,         // raycasts/sweeps hit it, no rigid body simulation
    PhysicsOnly,       // simulates, but invisible to raycasts
    QueryAndPhysics,   // full (default for dynamic bodies)
    ProbeOnly,         // gets contact callbacks but no physical response
};
```

**Why three responses (Ignore/Overlap/Block)?** Without this, trigger volumes require a separate object type. With it, any body can be a trigger for some channels and a blocker for others simultaneously. A door can Block Pawns but Overlap Projectiles (bullets pass through, player can't).

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Backend | Chaos (proprietary) | Jolt | PhysX / Havok | **Jolt (MIT, deterministic)** |
| Collision model | Channel × Response | Layer × Mask (bitmask) | Layer × Layer matrix | **Channel × Response (UE5 pattern)** |
| Query API | `LineTraceSingle/Multi` | `PhysicsServer3D::intersect_ray` | `Physics.Raycast` | **`LineTraceSingle/Multi`, `Sweep`, `Overlap`** |
| Hit result | `FHitResult` (Location + ImpactPoint split) | Dictionary | `RaycastHit` | **`HitResult` (Location + ImpactPoint split)** |
| Callbacks | `OnComponentHit/BeginOverlap/EndOverlap` | `body_entered/exited` | `OnCollisionEnter/Exit` | **`OnHit`, `OnOverlapBegin/End`** |
| Force API | `AddForce` + `bAccelChange` flag | `apply_force/impulse` | `AddForce/AddTorque` | **`AddForce` / `AddAcceleration` (separate)** |
| Character | Kinematic sweep (QueryOnly) | `CharacterBody3D.move_and_slide()` | `CharacterController.Move()` | **Kinematic sweep (QueryOnly capsule)** |
| Fixed timestep | Async 120Hz | Fixed 60Hz | `FixedUpdate` (50Hz) | **Fixed 120Hz + interpolation** |
| Deterministic | No (Chaos) | Yes (Jolt) | No (PhysX) / Yes (Havok) | **Yes (Jolt)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | PhysicsWorld subsystem, collision channels, line trace, sweep, overlap, HitResult. Static + dynamic bodies. |
| **Phase 3** | Callbacks (OnHit, OnOverlapBegin/End), constraints, physical materials, character movement. |
| **Phase 4** | Async physics tick (120Hz), transform interpolation, ragdoll, cloth. |

## Rules

1. **NEVER** access Jolt types outside `engine/physics/jolt/` — game code uses `PhysicsWorld` and component APIs only
2. **ALWAYS** use `CollisionChannel` + `CollisionResponse` for filtering — never raw bitmasks
3. **ALWAYS** use `impact_point` for VFX placement, `impact_normal` for floor checks — never confuse `location` with `impact_point`
4. **NEVER** use rigid body simulation for character movement — use `CharacterMovementComponent` with query-only capsule
