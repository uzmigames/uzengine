# Rigid Bodies, Forces & Constraints

> **Status**: Planned (Phase 2–3)
> **Source**: `engine/components/physics/rigid_body.h`, `engine/physics/constraint.h`

## Collision Events & Callbacks

Three event types, matching the three collision responses:

```cpp
/// Fired when physics simulation produces a blocking contact.
struct HitEvent {
    Entity    self_entity;
    Entity    other_entity;
    Vec3      normal_impulse;   // direction × magnitude of the impact
    HitResult hit;              // full contact info
};

/// Fired when a new overlap begins (Overlap response, not Block).
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

### Registration in Behaviors

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

    void OnExit(const OverlapEndEvent& event) { CloseDoor(); }
};
```

## Force & Impulse API

Two categories: **force** (continuous, applied per physics tick) and **impulse** (instantaneous, applied once). Each has a mass-dependent and mass-independent variant.

```cpp
class RigidBody : public PhysicsBody {
public:
    // ─── Continuous Forces (call from OnPhysicsTick) ───────
    void AddForce(Vec3 force);                         // Newtons, affected by mass
    void AddAcceleration(Vec3 accel);                  // mass-independent
    void AddForceAtPosition(Vec3 force, Vec3 world_pos); // generates torque
    void AddTorque(Vec3 torque);                       // Newton-meters

    // ─── Instantaneous Impulses (call once per event) ──────
    void AddImpulse(Vec3 impulse);                     // kg·m/s, affected by mass
    void AddVelocityChange(Vec3 delta_velocity);       // mass-independent
    void AddImpulseAtPosition(Vec3 impulse, Vec3 world_pos);
    void AddAngularImpulse(Vec3 angular_impulse);      // rad/s

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

**Why mass-independent variants?** A jump should always reach the same height regardless of character mass. Without `AddAcceleration()` / `AddVelocityChange()`, game code must divide by mass — introducing bugs when mass changes at runtime.

## Physical Material

```cpp
/// Physical material — assigned per collision shape.
struct PhysicalMaterial {
    float friction          = 0.5f;
    float static_friction   = 0.5f;
    float restitution       = 0.3f;   // bounciness 0..1
    float density           = 1.0f;   // g/cm³, mass from shape volume

    enum class CombineMode { Average, Min, Max, Multiply };
    CombineMode friction_combine    = CombineMode::Average;
    CombineMode restitution_combine = CombineMode::Average;

    /// Game-side surface type tag (0-31). Returned in HitResult.physical_material.
    /// Use for: footstep sounds, bullet decals, tire friction, particle effects.
    uint8_t surface_type = 0;
};
```

**Surface type is the correct hook for game-side routing.** Don't use collision channel or entity type for footstep/decal selection — a stone wall and stone floor share the same physical material.

## Physics Body Properties

```cpp
struct PhysicsBodyDesc {
    CollisionChannel          object_channel = CollisionChannel::WorldDynamic;
    CollisionResponseContainer response      = CollisionResponseContainer::BlockAll();
    CollisionEnabled          collision_mode = CollisionEnabled::QueryAndPhysics;

    bool  simulate_physics    = true;
    bool  enable_gravity      = true;
    bool  use_ccd             = false;   // continuous collision (fast objects)
    bool  notify_hit          = false;
    bool  generate_overlaps   = false;

    float linear_damping      = 0.01f;
    float angular_damping     = 0.05f;
    float max_angular_velocity = 100.0f;
    float mass_override       = 0.0f;   // 0 = compute from shape + density
    float mass_scale          = 1.0f;
    Vec3  center_of_mass_offset = {};

    bool lock_linear_x = false, lock_linear_y = false, lock_linear_z = false;
    bool lock_angular_x = false, lock_angular_y = false, lock_angular_z = false;
};
```

## Constraints (Joints)

Per-axis Free/Limited/Locked model — any joint type is expressible without a type enum.

```cpp
struct ConstraintDesc {
    Entity body_a, body_b;
    Vec3   pivot_a, pivot_b;
    Quat   frame_a, frame_b;

    enum Motion : uint8_t { Free, Limited, Locked };

    Motion linear_x = Locked, linear_y = Locked, linear_z = Locked;
    float  linear_limit = 0.0f;

    Motion swing1 = Free, swing2 = Free, twist = Free;
    float  swing1_limit_deg = 0.0f, swing2_limit_deg = 0.0f, twist_limit_deg = 0.0f;

    float break_force  = 0.0f;   // 0 = unbreakable
    float break_torque = 0.0f;

    struct Drive {
        bool  enabled = false;
        float stiffness = 0.0f;
        float damping = 0.0f;
        float max_force = FLT_MAX;
    };
    Drive linear_drive, angular_drive;
    Vec3  linear_target_position, linear_target_velocity;
    Quat  angular_target_orientation;
    Vec3  angular_target_velocity;
};

/// Convenience constructors
ConstraintDesc ConstraintDesc::MakeHinge(Entity a, Entity b, Vec3 pivot, Vec3 axis);
ConstraintDesc ConstraintDesc::MakeBallSocket(Entity a, Entity b, Vec3 pivot);
ConstraintDesc ConstraintDesc::MakeSlider(Entity a, Entity b, Vec3 pivot, Vec3 slide_axis);
ConstraintDesc ConstraintDesc::MakeFixed(Entity a, Entity b);
```
