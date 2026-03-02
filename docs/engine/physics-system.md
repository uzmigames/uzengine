# UzEngine — Physics System

> **Audience**: Engine developers and AI agents generating game code
> **Scope**: Jolt Physics integration, body/collider design, collision events, character controller, Lua API
> **Language**: C++23, Lua 5.1

---

## 1. Architecture Overview

The physics system is built on **Jolt Physics**, a modern rigid-body physics engine by Jorrit Roux. It runs on a fixed 60 Hz timestep with accumulator-based integration, decoupled from the variable frame rate.

### 1.1 Design Goals

| Goal | Implementation |
|------|-----------------|
| **Fixed timestep determinism** | 60 Hz physics step; variable rendering FPS |
| **Multiple collider shapes** | Box, sphere, capsule, convex mesh, heightfield (Phase 4) |
| **Efficient queries** | Spatial queries for raycasting, shape casting, overlap tests |
| **Character controller** | CharacterVirtual for player/NPC with ground detection |
| **Collision callbacks** | Event-driven contact begin/end/persist |
| **Ragdoll support** | Skeleton-driven multi-body ragdoll (Phase 3) |
| **Deterministic replay** | Fixed seed, fixed timestep, sorted contact order |

### 1.2 Module Boundary

```
┌───────────────────────────────────────────────────┐
│ game.dll (Hot-Reloadable)                         │
│  - PhysicsWorld (Jolt JPH::PhysicsSystem wrapper) │
│  - Body creation from CRigidBody + CCollider*    │
│  - Contact listener → event queue                 │
│  - Character controller update                    │
│  - Lua physics API (ctx.physics.*)               │
└───────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────┐
│ uz_engine.lib (Stable)                            │
│  - Jolt wrapper types and constants               │
│  - No Jolt linked; only forward decls             │
└───────────────────────────────────────────────────┘
```

**Key Point**: Jolt is linked **only** in `game.dll`. UzEngine.exe (editor/headless) does not link physics.

---

## 2. Jolt Physics Initialization

### 2.1 Startup Sequence

```cpp
// game.dll/physics/physics_world.h
class PhysicsWorld {
public:
    static PhysicsWorld* create(const PhysicsConfig& config);
    static void destroy(PhysicsWorld* world);

    // Body management
    JPH::BodyID create_body(const BodyDef& def);
    void destroy_body(JPH::BodyID body_id);

    // Simulation
    void step(float dt);

    // Queries
    RaycastResult raycast(const Vec3& origin, const Vec3& direction, float max_distance);
    array<OverlapResult> overlap_sphere(const Vec3& center, float radius);

    // Character controller
    void add_character(JPH::BodyID body_id, CharacterDef def);

private:
    JPH::PhysicsSystem* physics_system;
    JPH::BodyInterface* body_interface;
    ContactListener* contact_listener;
    CharacterVirtualManager character_mgr;
};

// game.dll/physics/physics_world.cpp
PhysicsWorld* PhysicsWorld::create(const PhysicsConfig& config) {
    // 1. Register allocator with Jolt
    JPH::RegisterDefaultAllocator();

    // 2. Install default factory
    JPH::Factory::sInstance = new JPH::Factory();

    // 3. Register all shape types
    JPH::RegisterTypes();

    // 4. Create physics system
    PhysicsWorld* world = new PhysicsWorld();
    JPH::PhysicsSystemSettings settings;
    settings.mMaxBodies = config.max_bodies;  // e.g., 1024
    settings.mMaxContactConstraints = config.max_contacts;  // e.g., 10240
    settings.mMaxInMotionBodies = config.max_bodies / 2;

    world->physics_system = new JPH::PhysicsSystem();
    world->physics_system->Init(settings);

    // 5. Install contact listener for collision events
    world->contact_listener = new ContactListener(world);
    world->physics_system->SetContactListener(world->contact_listener);

    // 6. Install body activation listener (optional)
    // world->physics_system->SetBodyActivationListener(...);

    return world;
}

void PhysicsWorld::step(float dt) {
    // Clamp dt to prevent spiral of death
    float clamped_dt = min(dt, 0.033f);  // Max 33 ms per step

    const float PHYSICS_DT = 1.0f / 60.0f;  // 16.67 ms
    static float accumulator = 0;

    accumulator += clamped_dt;
    while (accumulator >= PHYSICS_DT) {
        // Do a physics step
        physics_system->Update(PHYSICS_DT,
            JPH::EPhysicsUpdateType::FullSimulation);

        accumulator -= PHYSICS_DT;

        // Process collision events from this step
        for (auto& contact : pending_contacts) {
            handle_contact(contact);
        }
        pending_contacts.clear();
    }
}
```

### 2.2 Layer and Group Configuration

Collision layers define what can collide with what:

```cpp
// game.dll/physics/physics_layers.h
namespace PhysicsLayers {
    enum Layer : uint8_t {
        Default = 0,
        Player = 1,
        Enemy = 2,
        Projectile = 3,
        Trigger = 4,
        StaticGeometry = 5,
        Kinematic = 6,
        Debris = 7,
        // ... expand as game requires
    };

    // Collision matrix: layer A can collide with layers in row A
    // Loaded from .uzproj [physics.collision_matrix]
    static constexpr bool COLLIDES[8][8] = {
        // Def  Plr  Enm  Proj  Trig  Static  Kin  Debris
        {  1,   1,   1,    1,   1,    1,     1,   1 },  // Default
        {  1,   0,   1,    1,   1,    1,     0,   1 },  // Player (not with Player, Kinematic)
        {  1,   1,   0,    1,   1,    1,     1,   1 },  // Enemy
        {  1,   1,   1,    0,   1,    1,     1,   1 },  // Projectile (not with itself)
        {  1,   1,   1,    1,   1,    1,     1,   1 },  // Trigger (collides all)
        {  1,   1,   1,    1,   1,    0,     0,   0 },  // Static (only kinematic)
        {  1,   0,   1,    1,   1,    0,     0,   0 },  // Kinematic
        {  1,   1,   1,    1,   1,    0,     0,   0 },  // Debris
    };

    bool can_collide(Layer a, Layer b) {
        return COLLIDES[a][b] && COLLIDES[b][a];
    }
}
```

Configuration in `.uzproj`:

```toml
[physics]
gravity = [0, -9.81, 0]
max_bodies = 1024
max_contact_constraints = 10240

[physics.layers]
Default = 0
Player = 1
Enemy = 2
Projectile = 3
Trigger = 4
StaticGeometry = 5
Kinematic = 6
Debris = 7

[physics.collision_matrix]
# Format: "LayerA" = "LayerB1,LayerB2,LayerB3"
Default = "Default,Player,Enemy,Projectile,Trigger,StaticGeometry,Kinematic,Debris"
Player = "Default,Enemy,Projectile,Trigger,StaticGeometry,Debris"
Enemy = "Default,Player,Projectile,Trigger,StaticGeometry,Kinematic,Debris"
# ... etc
```

---

## 3. Body and Collider Creation

### 3.1 ECS Integration Pattern

When a scene loads, for each node with CRigidBody and a CCollider* component:

```cpp
struct CRigidBody {
    JPH::BodyID body_id;
    float mass;
    float restitution;      // Bounce
    float friction;         // Slide/rolling resistance
    bool is_static;         // Infinite mass
    bool is_kinematic;      // Player-controlled, no gravity
    uint8_t collision_layer;
    uint8_t collision_flags; // Bit 0=gravity, Bit 1=can_sleep
};

struct CColliderBox {
    Vec3 half_extents;      // Size from center
    Vec3 offset;            // Local space offset
};

struct CColliderSphere {
    float radius;
    Vec3 offset;
};

struct CColliderCapsule {
    float radius;
    float half_height;      // Height from center
    Vec3 offset;
};

struct CColliderMesh {
    StringId mesh_id;       // For runtime convex hull generation
};

// System: create physics bodies at scene load
void create_physics_bodies(GameContext& ctx) {
    auto view = ctx.world.view<CRigidBody, CTransform>();

    for (auto entity : view) {
        auto [rb, tf] = view.get<CRigidBody, CTransform>(entity);

        // 1. Build collision shape from CCollider component
        JPH::ShapeRefC shape = build_shape(entity, ctx.world);
        if (!shape) {
            log_warn("Entity %u has CRigidBody but no collider", (uint32_t)entity);
            continue;
        }

        // 2. Create body
        JPH::BodyCreationSettings body_settings(
            shape,
            to_jolt(tf.position),
            to_jolt(tf.rotation),
            rb.is_static ? JPH::EMotionType::Static :
            rb.is_kinematic ? JPH::EMotionType::Kinematic :
            JPH::EMotionType::Dynamic,
            rb.collision_layer
        );

        body_settings.mRestitution = rb.restitution;
        body_settings.mFriction = rb.friction;
        body_settings.mLinearDamping = 0.05f;
        body_settings.mAngularDamping = 0.05f;
        body_settings.mGravityFactor = (rb.collision_flags & 0x1) ? 1.0f : 0.0f;
        body_settings.mAllowSleeping = (rb.collision_flags & 0x2) != 0;

        // 3. Add to world
        JPH::BodyID body_id = ctx.physics->create_body(body_settings);
        rb.body_id = body_id;
    }
}

// Helper: build shape from collider components
JPH::ShapeRefC build_shape(entt::entity entity, entt::registry& world) {
    // Try CColliderBox
    if (auto* cb = world.try_get<CColliderBox>(entity)) {
        return JPH::ShapeSettings::CreateShape(
            JPH::BoxShapeSettings(to_jolt(cb->half_extents),
                0.01f)  // Convex radius
        ).Get();
    }

    // Try CColliderSphere
    if (auto* cs = world.try_get<CColliderSphere>(entity)) {
        return JPH::ShapeSettings::CreateShape(
            JPH::SphereShapeSettings(cs->radius)
        ).Get();
    }

    // Try CColliderCapsule
    if (auto* cc = world.try_get<CColliderCapsule>(entity)) {
        return JPH::ShapeSettings::CreateShape(
            JPH::CapsuleShapeSettings(cc->half_height, cc->radius)
        ).Get();
    }

    // Try CColliderMesh (convex hull)
    if (auto* cm = world.try_get<CColliderMesh>(entity)) {
        // Load mesh, extract vertices, create convex hull
        array<Vec3> vertices = extract_vertices(cm->mesh_id);
        return JPH::ShapeSettings::CreateShape(
            JPH::ConvexHullShapeSettings(vertices)
        ).Get();
    }

    return nullptr;
}
```

### 3.2 Multi-Collider Support

A single body can have multiple shapes (compound shape):

```cpp
// Future extension: multi-shape bodies
struct CCompoundCollider {
    array<JPH::ShapeRefC> shapes;
    array<Vec3> offsets;
    array<Quat> rotations;
};

// Build compound shape
JPH::ShapeRefC build_compound_shape(entt::entity entity, entt::registry& world) {
    if (auto* cc = world.try_get<CCompoundCollider>(entity)) {
        JPH::StaticCompoundShapeSettings compound;
        for (size_t i = 0; i < cc->shapes.size(); ++i) {
            compound.AddShape(
                to_jolt(cc->offsets[i]),
                to_jolt(cc->rotations[i]),
                cc->shapes[i]
            );
        }
        return compound.Create().Get();
    }
    return nullptr;
}
```

---

## 4. Transform Synchronization

Each frame, after physics step, body transforms must sync to CTransform:

```cpp
void sync_physics_to_transform(GameContext& ctx) {
    auto view = ctx.world.view<CRigidBody, CTransform>();

    for (auto entity : view) {
        auto [rb, tf] = view.get<CRigidBody, CTransform>(entity);

        // Get body's current transform
        JPH::BodyInterface& bi = ctx.physics->body_interface();
        JPH::RVec3 pos = bi.GetWorldTranslationAsync(rb.body_id);
        JPH::Quat rot = bi.GetWorldRotationAsync(rb.body_id);

        // Update ECS transform
        tf.position = from_jolt(pos);
        tf.rotation = from_jolt(rot);

        // Update scene graph node (if editor-mode only)
        // NodeBase& node = find_node_for_entity(entity);
        // node.position = tf.position;
        // node.rotation = tf.rotation;
    }
}
```

---

## 5. Collision Events

### 5.1 Contact Listener

```cpp
// game.dll/physics/contact_listener.h
class ContactListener : public JPH::ContactListener {
public:
    // Called when two bodies start colliding
    void OnContactAdded(const JPH::Body& in_body1, const JPH::Body& in_body2,
                        const JPH::ContactManifold& in_manifold,
                        JPH::ContactSettings& io_settings) override {
        entt::entity e1 = lookup_entity(in_body1.GetID());
        entt::entity e2 = lookup_entity(in_body2.GetID());
        Vec3 contact_point = from_jolt(in_manifold.mWorldSpaceContactPointsOn1[0]);
        Vec3 normal = from_jolt(in_manifold.mWorldSpaceNormal);
        float penetration = in_manifold.mPenetrationDepth;

        // Queue event (don't call Lua directly from physics thread)
        collision_queue.push({
            e1, e2,
            contact_point,
            normal,
            penetration,
            COLLISION_BEGIN
        });
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& in_shape_pair) override {
        // Queue separation event
    }

    void OnContactPersist(const JPH::Body& in_body1, const JPH::Body& in_body2,
                          const JPH::ContactManifold& in_manifold,
                          JPH::ContactSettings& io_settings) override {
        // Contact is ongoing; used for friction/restitution tweaks
    }

private:
    struct CollisionEvent {
        entt::entity entity1;
        entt::entity entity2;
        Vec3 contact_point;
        Vec3 normal;
        float penetration;
        uint8_t event_type;  // COLLISION_BEGIN, COLLISION_PERSIST, COLLISION_END
    };

    ring_buffer<CollisionEvent> collision_queue;
};
```

### 5.2 Event Draining

On the game thread, after physics step, drain collision events and emit them:

```cpp
void drain_collision_events(GameContext& ctx) {
    CollisionEvent ev;
    while (ctx.physics->collision_queue.try_pop(ev)) {
        // Emit to event bus (for C++ subscribers)
        ctx.event_bus.emit<CollisionEvent>(ev);

        // Dispatch to Lua scripts
        if (auto* script1 = ctx.world.try_get<CScriptRef>(ev.entity1)) {
            dispatch_lua_callback(ctx.lua, ev.entity1, *script1, "on_collision",
                ev.entity2, ev.contact_point, ev.normal);
        }
        if (auto* script2 = ctx.world.try_get<CScriptRef>(ev.entity2)) {
            dispatch_lua_callback(ctx.lua, ev.entity2, *script2, "on_collision",
                ev.entity1, ev.contact_point, ev.normal);
        }
    }
}

// Lua callback signature:
// function MyScript:on_collision(other_entity, contact_point, contact_normal)
//     print("Collided with", other_entity)
// end
```

### 5.3 Trigger Volumes

Triggers are bodies with no physics response but fire enter/exit events:

```cpp
struct CTrigger {
    JPH::BodyID body_id;
    // No mass, friction, restitution
};

// Query trigger overlaps per-frame:
void update_triggers(GameContext& ctx) {
    auto view = ctx.world.view<CTrigger, CTransform>();

    for (auto entity : view) {
        auto [trigger, tf] = view.get<CTrigger, CTransform>(entity);

        // Spherecast around trigger body
        array<JPH::BodyID> overlapping = ctx.physics->get_overlapping_bodies(
            trigger.body_id);

        // Compare with last frame's overlaps
        for (auto body_id : overlapping) {
            if (was_not_overlapping_last_frame(trigger.body_id, body_id)) {
                // Emit trigger enter
                ctx.event_bus.emit<TriggerEnterEvent>({
                    entity,
                    lookup_entity(body_id)
                });
            }
        }
    }
}
```

---

## 6. Character Controller

### 6.1 Design

The character controller uses Jolt's `CharacterVirtual`:

```cpp
struct CCharacterController {
    JPH::CharacterVirtual* controller;
    float ground_speed;         // Move speed in units/sec
    float jump_impulse;         // Upward velocity when jumping
    float max_slope_angle;      // Max slope character can walk on (45° typical)
    bool is_grounded;
    Vec3 ground_velocity;       // Platform velocity (moving ground)
};

// System: update character controllers
void update_character_controllers(GameContext& ctx, float dt) {
    auto view = ctx.world.view<CCharacterController, CTransform, CScriptRef>();

    for (auto entity : view) {
        auto [cc, tf, script] = view.get<CCharacterController, CTransform, CScriptRef>(entity);

        // 1. Get desired movement from script (or input)
        Vec3 desired_move = call_lua_on_move(ctx.lua, entity, script);

        // 2. Add gravity
        Vec3 velocity = cc->controller->GetLinearVelocity();
        if (!cc->is_grounded) {
            velocity.y -= 9.81f * dt;  // Apply gravity
        }

        // 3. Apply movement
        cc->controller->SetLinearVelocity(desired_move);

        // 4. Update (this moves the character and collides)
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        cc->controller->ExtendedUpdate(dt,
            ctx.physics->get_gravity(),
            *ctx.physics->get_body_interface(),
            *ctx.physics->get_physics_system()->GetBroadPhaseQuery(),
            *ctx.physics->get_physics_system()->GetNarrowPhaseQuery());

        // 5. Sync transform
        tf.position = from_jolt(cc->controller->GetPosition());

        // 6. Update grounded state
        cc->is_grounded = cc->controller->IsGrounded();
    }
}

// Lua API: player script
function PlayerScript:on_update(delta_time)
    local move_dir = Vec3(0, 0, 0)
    if ctx.input.held("move_forward") then
        move_dir.z = 1
    end
    if ctx.input.held("move_back") then
        move_dir.z = -1
    end

    self:move(move_dir * 5.0)  -- 5 units/sec

    if ctx.input.pressed("jump") and self:is_grounded() then
        self:jump(10.0)  -- 10 units upward
    end
end

function PlayerScript:move(velocity)
    -- Forwarded to C++; sets desired velocity
end

function PlayerScript:jump(impulse)
    -- Sets upward velocity component
end

function PlayerScript:is_grounded()
    return self.grounded
end
```

### 6.2 Common Operations

```cpp
// Check if grounded (useful for jump validation)
bool is_grounded = cc->controller->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;

// Get velocity
Vec3 vel = from_jolt(cc->controller->GetLinearVelocity());

// Apply external impulse (explosion knockback)
cc->controller->AddLinearVelocity(to_jolt(impulse));

// Set position directly (teleport)
cc->controller->SetPosition(to_jolt(new_pos));

// Get position
Vec3 pos = from_jolt(cc->controller->GetPosition());
```

---

## 7. Queries

### 7.1 Raycasting

```cpp
// Cast a ray and get first hit
RaycastResult raycast(const Vec3& origin, const Vec3& direction, float max_distance) {
    JPH::RRayCast ray(to_jolt(origin), to_jolt(direction) * max_distance);
    JPH::RayCastResult result;

    JPH::RayCastSettings settings;
    JPH::BodyFilter filter;  // Can customize which layers to query

    bool hit = ctx.physics->get_physics_system()->GetNarrowPhaseQuery()
        .CastRay(ray, settings, result, filter);

    if (hit) {
        return {
            true,
            from_jolt(ray.mOrigin + ray.mDirection * result.mFraction),
            lookup_entity(result.mBodyID),
            result.mFraction
        };
    }
    return { false };
}
```

### 7.2 Shape Casting

```cpp
// Sweep a sphere along a line
ShapeCastResult sweep_sphere(const Vec3& center, float radius,
                             const Vec3& direction, float max_distance) {
    JPH::ShapeSettings::ShapeCast sc(
        JPH::SphereShapeSettings(radius),
        JPH::Vec3(1, 1, 1),  // Scale
        to_jolt(Vec3(0, 0, 0))  // Relative center
    );

    JPH::RShapeCast shape_cast(sc, to_jolt(center), to_jolt(direction) * max_distance);
    JPH::ShapeCastResult result;

    JPH::ShapeCastSettings settings;
    bool hit = ctx.physics->get_physics_system()->GetNarrowPhaseQuery()
        .CastShape(shape_cast, settings, result);

    // ... process result ...
}
```

### 7.3 Overlap Queries

```cpp
// Get all bodies overlapping a sphere
array<OverlapResult> overlap_sphere(const Vec3& center, float radius) {
    JPH::AllHitCollisionCollector<JPH::SphereShapeCollector> collector;
    JPH::SphereShapeSettings sphere_settings(radius);
    JPH::Shape* sphere_shape = sphere_settings.Create().Get();

    ctx.physics->get_physics_system()->GetNarrowPhaseQuery().CollectTransforms(
        JPH::ShapeFilter(),
        JPH::Vec3(0, 0, 0),  // Query origin (will use body positions)
        JPH::ShapeCastSettings(),
        collector
    );

    array<OverlapResult> results;
    // ... convert hits to OverlapResult ...
    return results;
}
```

---

## 8. Lua Physics API

```lua
-- Rigid body control
self:add_force(force_vector, application_point)
self:add_impulse(impulse_vector, application_point)
self:set_velocity(velocity_vector)
self:get_velocity()

-- Kinematic bodies (player-controlled)
self:set_position(new_pos)
self:get_position()

-- Queries
local hit = ctx.physics.raycast(origin, direction, max_distance)
if hit then
    print("Hit entity:", hit.entity, "at", hit.position)
end

local overlaps = ctx.physics.overlap_sphere(center, radius)
for _, overlap in ipairs(overlaps) do
    print("Overlapping:", overlap.entity)
end

-- Character controller
local character = self:get_character_controller()
character:move(velocity)
character:jump(impulse)
character:is_grounded()
```

---

## 9. Determinism and Replay

For replay/networking (Phase 5):

1. **Fixed timestep** — Always 60 Hz, no variable dt
2. **Fixed random seed** — Deterministic shuffling of bodies
3. **Sorted contact order** — Physics engine steps in consistent order
4. **Input determinism** — Same input sequence → same physics output

```cpp
// Initialize physics with fixed seed
JPH::PhysicsSystemSettings settings;
settings.mDeterministicSimulation = true;
physics_system->Init(settings);
```

---

## 10. Performance Characteristics

| Aspect | Metric | Notes |
|--------|--------|-------|
| **Physics step (60 Hz)** | ~1–3 ms | Varies with body/contact count |
| **Raycast** | ~0.1 ms per ray | Spatial acceleration via BVH |
| **Overlap query** | ~0.5 ms for 100 bodies | Depends on radius and scene density |
| **Body creation** | ~0.5 ms per body | Including shape compilation |
| **Memory per body** | ~1 KB | Includes shape, constraints, cache |

---

## 11. Related Documentation

- [Physics System Specification](../specs/physics-system.md) — Full Jolt API, configuration, layer matrix
- [ECS and Scene System](ecs-and-scene.md) — CRigidBody, CCollider components, entity lifecycle
- [Animation System](../../specs/animation-system.md) — Physics-driven ragdoll (Phase 3)
- [Scripting System](scripting-system.md) — ctx.physics bindings, Lua collision callbacks

---

## 12. Key Takeaways

1. **Jolt is the physics engine** — Modern, deterministic, supports complex shapes
2. **Fixed 60 Hz timestep** — Decoupled from rendering frame rate via accumulator
3. **Layer collision matrix** — Centralized control of what collides with what
4. **Events are queued** — Not called directly from physics thread; drained per-frame
5. **Character controller is special** — Uses CharacterVirtual, not rigid body physics
6. **Queries are efficient** — Raycast, overlap, shape-cast with spatial acceleration
7. **Graceful fallback** — Missing physics body is logged; no crash
