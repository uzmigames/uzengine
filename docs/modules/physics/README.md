# Module: Physics

**Location:** `src/game/physics/`
**Output:** Part of `game.dll`
**Language:** C++23
**Role:** Jolt Physics rigid body simulation, collision detection and response, character control.

---

## Responsibility

The Physics module wraps the Jolt Physics engine into the game runtime. It manages all physics simulation state, collision detection, character movement, and physics queries. Because physics state is tightly coupled to game logic, it lives in game.dll and resets cleanly with each hot-reload.

Key responsibilities:
- **Rigid body simulation**: Static, Dynamic, Kinematic body modes
- **Collision shapes**: Box, Sphere, Capsule, Cylinder, Convex mesh, Heightfield
- **Collision detection and events**: ContactListener queues collisions → EventBus dispatch to Lua
- **Character controller**: Movement, jumping, ground detection
- **Physics queries**: Raycast, linecast, sphere/box overlap
- **Debug visualization**: Jolt debug shapes → IPC → VSCode overlay in editor mode

---

## Why in game.dll (Not uz_engine.lib)

Physics lives in game.dll for critical architectural reasons:

1. **State lifetime tied to game logic**: Physics state (body positions, velocities, forces) must reset when game.dll reloads. Keeping physics in a separate DLL would require serialization and complex state transfer.
2. **Jolt dependency isolation**: Jolt Physics links only here, reducing uz_engine.lib complexity.
3. **Hot-reload protocol**: on_save_state serializes physics bodies; on_load_state rebuilds them from saved data.
4. **Driven via GameContext**: game.dll owns the physics world; engine never calls Jolt directly.

---

## Source Structure

```
src/game/physics/
├── physics_world.h/cpp           — JPH::PhysicsSystem wrapper, fixed-timestep update, TempAllocator
├── physics_body_sync.h/cpp       — Bidirectional sync: CRigidBody (ECS) ↔ JPH::Body (simulation)
├── physics_queries.h/cpp         — raycast, linecast, overlap_sphere, overlap_box, distance queries
├── contact_listener.h/cpp        — JPH::ContactListener → thread-safe collision queue → EventBus
├── character_controller.h/cpp    — CCharacterController → JPH::CharacterVirtual (capsule controller)
└── physics_debug_draw.h/cpp      — JPH::DebugRenderer → IPC line primitives (#ifdef UZ_EDITOR)
```

---

## ECS Components Owned

The Physics module adds these components to the ECS:

| Component | Role |
|-----------|------|
| `CRigidBody` | Rigid body handle + mass + collision layer + linear/angular velocity |
| `CColliderBox` | Box collider primitive (half-extents) |
| `CColliderSphere` | Sphere collider (radius) |
| `CColliderCapsule` | Capsule collider (radius + height) |
| `CColliderCylinder` | Cylinder collider (radius + height) |
| `CColliderMesh` | Convex mesh collider (vertex/index buffer) |
| `CTrigger` | Non-physical trigger volume (collision events only) |
| `CCharacterController` | Character capsule controller with ground state |

---

## Fixed-Timestep Loop

Physics must use a fixed timestep to ensure determinism. The physics_step accumulates elapsed time and updates at exactly 60 Hz:

```cpp
constexpr float PHYSICS_DT = 1.f / 60.f;  // 16.67ms per frame
static float accumulator = 0.f;

void physics_step(float frame_dt) {
    accumulator += frame_dt;
    while (accumulator >= PHYSICS_DT) {
        // Update Jolt Physics
        physics_system.Update(PHYSICS_DT, collision_steps, temp_alloc, job_system);

        // Sync transforms: Jolt bodies → CTransform
        physics_body_sync.sync_from_jolt();

        accumulator -= PHYSICS_DT;
    }
}
```

This ensures physics always steps at 60 Hz regardless of frame rate.

---

## Collision Layer System

Jolt Physics collision layers are configured at scene load. Each layer is a 16-bit identifier:

```
Layer 0  = Default        (all collide with all)
Layer 1  = Static         (fixed geometry)
Layer 2  = Dynamic        (movable objects)
Layer 3  = Character      (player/NPC controller)
Layer 4  = Trigger        (non-physical volumes)
Layer 5  = Ragdoll        (post-mortem physics)
...
Layer 15 = Custom         (game-specific groups)
```

The **BroadPhaseLayerInterface** defines which layers collide:

```cpp
// Example: characters collide with static and dynamic, not other characters
BroadPhaseLayerInterface::ShouldCollide(Layer::Character, Layer::Character) → false
BroadPhaseLayerInterface::ShouldCollide(Layer::Character, Layer::Static) → true
```

Configured in `.uzproj` or `scene.uzscene`:
```toml
[physics]
layers = [
    { name = "Default",    id = 0 },
    { name = "Static",     id = 1 },
    { name = "Dynamic",    id = 2 },
    { name = "Character",  id = 3 },
]

collision_matrix = [
    [0, 1, 1, 1],   # Default collides with all
    [1, 1, 1, 1],   # Static collides with all
    [1, 1, 1, 1],   # Dynamic collides with all
    [1, 1, 1, 0],   # Character collides with all except other characters
]
```

---

## Debug Draw

In editor mode, Jolt's debug shapes are rendered as wireframe in the 3D viewport:

```cpp
#ifdef UZ_EDITOR
class PhysicsDebugDraw : public JPH::DebugRenderer {
    void DrawLine(JPH::RVec3 inFrom, JPH::RVec3 inTo, JPH::ColorArg inColor) override {
        // Queue line primitive via IPC
        ipc_queue_line(inFrom, inTo, inColor);
    }
    void DrawTriangle(JPH::RVec3 inV1, JPH::RVec3 inV2, JPH::RVec3 inV3, ...) override {
        // Queue triangle via IPC
        ipc_queue_triangle(inV1, inV2, inV3);
    }
};

// In game loop:
physics_system.DrawBodies(..., debug_renderer);
// → IPC sends all primitives → VSCode overlay renders wireframe bodies
#endif
```

Useful for debugging: seeing collision shapes, contact points, constraint chains.

---

## Physics Queries

The module provides non-destructive queries:

```cpp
// Raycast from point A to point B
struct RaycastResult {
    float distance;
    Vec3 position;
    Vec3 normal;
    int hit_layer;
    Handle<CRigidBody> body;
};

RaycastResult raycast(Vec3 from, Vec3 to, int mask = ~0);

// Sphere overlap — find all bodies near a position
std::vector<RaycastResult> overlap_sphere(Vec3 center, float radius, int mask = ~0);

// Box overlap
std::vector<RaycastResult> overlap_box(Vec3 center, Vec3 half_extents, int mask = ~0);

// Distance query — nearest point between two shapes
float distance_query(Handle<CRigidBody> a, Handle<CRigidBody> b, Vec3& point_a, Vec3& point_b);
```

---

## Collision Events

Collisions are detected asynchronously in the physics update and dispatched via EventBus:

```cpp
// ContactListener queues collisions during physics step
class ContactListener : public JPH::ContactListener {
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, ...) override {
        // Push to thread-safe queue
        collision_queue.enqueue({a, b, contact_point, contact_normal});
    }
};

// After physics step, drain queue:
void drain_collision_events() {
    while (auto evt = collision_queue.try_dequeue()) {
        // Emit EventBus<OnCollision>
        event_bus.emit<OnCollision>({
            .body_a = evt.a,
            .body_b = evt.b,
            .position = evt.contact_point,
            .normal = evt.contact_normal,
            .impact_velocity = evt.impact_velocity,
        });
    }
}

// Lua subscribers:
node:on_event("collision", function(evt)
    print("Collided at", evt.position, "with impact", evt.impact_velocity)
end)
```

---

## Character Controller

The character controller is a specialized capsule-shaped controller used for player/NPC movement:

```cpp
class CCharacterController {
    JPH::Ref<JPH::CharacterVirtual> m_controller;

    void move(Vec3 velocity_intent, float dt) {
        // CharacterVirtual handles:
        // - Gravity
        // - Ground detection
        // - Slope climbing
        // - Step height limits
        // - Collision response
        m_controller->SetLinearVelocity(velocity_intent);
        m_controller->Update(dt, ...);

        // Sync back to CTransform
        node->transform.position = m_controller->GetPosition();
    }

    void jump(float force) {
        if (is_grounded()) {
            Vec3 vel = m_controller->GetLinearVelocity();
            vel.y = force;
            m_controller->SetLinearVelocity(vel);
        }
    }

    bool is_grounded() const {
        return m_controller->IsSupported();
    }
};
```

---

## Performance Optimization Tips

1. **Use Static bodies for non-moving geometry** (buildings, terrain) — faster broad phase
2. **Primitive shapes over mesh colliders** — Box, Sphere, Capsule are cheaper
3. **Limit collision layers** — Only groups that need to collide
4. **Raycasts vs overlaps** — Raycasts faster for single hits; overlaps for multiple
5. **Batch physics queries** — Group queries together, not spread across frame

---

## Phase Roadmap

**Phase 3**:
- Core rigid bodies (Static, Dynamic, Kinematic)
- Basic colliders (Box, Sphere, Capsule)
- Collision layers and matrix
- Character controller
- Physics queries (raycast, overlap)
- Collision events via EventBus
- Lua physics API (body creation, properties, queries)
- Debug visualization in editor

**Phase 3.5**:
- Ragdoll system (constraints, motors, active ragdoll)
- Breakable constraints

**Phase 4**:
- Soft body physics
- Vehicle physics (wheels, suspension)
- Cloth simulation
- Water simulation
- Advanced destruction

---

## Related Docs

- [Game Module](../game/README.md) — Game loop that calls physics_step
- [Physics System Spec](../../specs/physics-system.md) — Full design, Lua API, IPC handlers
- [Event Bus Spec](../../specs/event-bus.md) — Event dispatch to Lua scripts
- [Guides: Physics Guide](../../guides/physics-guide.md) — Practical physics patterns
