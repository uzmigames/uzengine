# Character Movement & Fixed Timestep

> **Status**: Planned (Phase 2–4)
> **Source**: `engine/components/physics/character_body.h`

## Fixed Timestep & Async Physics Tick

Physics runs at a fixed rate (default 120Hz), independent of the variable game frame rate.

```cpp
/// Behavior callback for fixed-rate physics tick.
/// Called at the physics substep rate (e.g., 120Hz), not the game tick rate.
class Behavior {
    virtual void OnPhysicsTick(float fixed_dt) {}  // called at fixed rate (e.g., 1/120s)
};
```

### Timing Model

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

## Character Movement — Query-Based, Not Simulated

Characters do NOT use rigid body simulation. The character capsule is `CollisionEnabled::QueryOnly` — it participates in raycasts/sweeps but has no physics body. All motion is driven by game code via sweeps.

```cpp
class CharacterMovementComponent : public ActorComponent {
public:
    // ─── Configuration ─────────────────────────────────────
    float max_walk_speed       = 600.0f;   // cm/s
    float max_fall_speed       = 2000.0f;  // terminal velocity
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
    struct FloorResult {
        bool      walkable;         // normal angle < walkable_floor_angle
        bool      from_line_trace;
        float     floor_distance;
        HitResult hit;
    };
    FloorResult FindFloor(Vec3 position) const;

    /// Attempt to step over an obstacle. Sweeps up → forward → down.
    bool StepUp(Vec3& position, Vec3 move_delta, const HitResult& wall_hit) const;

    /// Slide remaining velocity along a surface after hitting a wall/ramp.
    Vec3 SlideAlongSurface(Vec3 velocity, float dt, Vec3 surface_normal) const;
};
```

### Why Not Rigid Body?

- Rigid body characters tumble unpredictably on slight geometry variations
- Step-up logic requires custom sweeps regardless
- Query-only capsule is cheaper (not in simulation BVH)
- Deterministic: same input = same position, no solver jitter

## Physics Debug Draw

```cpp
class PhysicsWorld : public WorldSubsystem {
    void SetDebugDrawEnabled(bool enabled);

    enum DebugDraw : uint32_t {
        Shapes        = 1 << 0,
        Contacts      = 1 << 1,
        Constraints   = 1 << 2,
        BoundingBoxes = 1 << 3,
        CenterOfMass  = 1 << 4,
        Velocities    = 1 << 5,
        SleepState    = 1 << 6,
    };
    void SetDebugDrawFlags(uint32_t flags);
};
```

## Directory Structure

```
engine/components/physics/
│   ├── collision_object.h
│   ├── physics_body.h
│   ├── rigid_body.h/.cpp
│   ├── character_body.h/.cpp
│   ├── static_body.h
│   ├── collision_shape.h/.cpp
│   ├── trigger_volume.h
│   └── joints/

engine/subsystem/
│   └── physics_world.h/.cpp

engine/physics/
├── collision_channel.h
├── collision_shape.h
├── hit_result.h
├── query_params.h
├── physics_events.h
├── physical_material.h
├── constraint.h
├── physics_body_desc.h
└── jolt/                        ← Jolt internals (not public API)
    ├── jolt_physics_world.cpp
    ├── jolt_contact_listener.cpp
    ├── jolt_layer_mapping.cpp
    └── jolt_debug_renderer.cpp
```
