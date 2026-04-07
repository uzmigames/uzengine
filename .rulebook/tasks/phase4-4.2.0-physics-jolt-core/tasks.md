## 1. Jolt Integration & PhysicsWorld

- [ ] 1.1 Add Jolt Physics to `scripts/setup.ps1` — Clone-Repo call for Jolt v5.x from `jrouwe/JoltPhysics`. Add to `runtime/thirdparty/` via setup script (NEVER manual clone). Update `runtime/build.zig` to link Jolt static library with include paths.
- [ ] 1.2 Create `runtime/engine/physics/physics_world.h` — PhysicsWorld class: owns `JPH::PhysicsSystem`, `JPH::TempAllocatorImpl` (16MB), `JPH::JobSystemThreadPool` (N-1 threads). Init(gravity=Vec3{0,0,-981}), Shutdown(), Step(float dt). Z-up coordinate system. Max 65536 bodies, 65536 body pairs, 16384 contact constraints.
- [ ] 1.3 Create `runtime/engine/physics/physics_world.cpp` — Implementation: `JPH::RegisterDefaultAllocator()`, `JPH::Factory::sInstance` init, register all Jolt types. Create `BPLayerInterfaceImpl` (2 layers: NON_MOVING=0, MOVING=1), `ObjectVsBroadPhaseLayerFilterImpl`, `ObjectLayerPairFilterImpl`. Fixed timestep accumulator: 120Hz (8.33ms), max 4 substeps to prevent spiral of death.
- [ ] 1.4 Create `runtime/engine/physics/physics_layers.h` — Collision layer definitions: enum PhysicsLayer { Default=0, Static=1, Dynamic=2, Kinematic=3, Trigger=4, Character=5, Projectile=6, Debris=7 }. 16-layer interaction matrix configurable via `.uzproj`. Default: Static↔Dynamic, Dynamic↔Dynamic, Character↔Static, Character↔Dynamic, Character↔Trigger.
- [ ] 1.5 Create `runtime/engine/physics/physics_subsystem.h/.cpp` — PhysicsSubsystem (WorldSubsystem): owns PhysicsWorld, registers in SubsystemRegistry with TickGroup::Physics. OnTick(dt) calls PhysicsWorld::Step(). After step, runs BodySync to write Jolt positions back to ECS.

## 2. Rigid Body Component

- [ ] 2.1 Create `runtime/engine/physics/components/rigid_body.h` — CRigidBody component: body_type (Static/Dynamic/Kinematic), mass (1.0), friction (0.5), restitution (0.3), linear_damping (0.05), angular_damping (0.05), gravity_scale (1.0), is_ccd (false), lock_rotation_x/y/z. Holds JPH::BodyID.
- [ ] 2.2 Create `runtime/engine/physics/components/rigid_body.cpp` — CreateBody(PhysicsWorld&, shape, transform), DestroyBody(PhysicsWorld&), ApplyForce(Vec3), ApplyImpulse(Vec3), ApplyTorque(Vec3), SetLinearVelocity(Vec3), GetLinearVelocity().
- [ ] 2.3 Create `runtime/engine/physics/body_sync.h/.cpp` — BodySync: after Step(), dynamic bodies → write Jolt pos/rot to TransformComponent. Kinematic bodies → read TransformComponent, call MoveKinematic(). Skip static bodies.

## 3. Collider Components

- [ ] 3.1 Create `runtime/engine/physics/components/collider_box.h/.cpp` — CColliderBox: half_extents ({0.5,0.5,0.5}), offset ({0,0,0}), is_trigger (false). Creates JPH::BoxShape.
- [ ] 3.2 Create `runtime/engine/physics/components/collider_sphere.h/.cpp` — CColliderSphere: radius (0.5), offset, is_trigger. Creates JPH::SphereShape.
- [ ] 3.3 Create `runtime/engine/physics/components/collider_capsule.h/.cpp` — CColliderCapsule: radius (0.3), half_height (0.5), offset, is_trigger. Z-axis oriented. Creates JPH::CapsuleShape.
- [ ] 3.4 Create `runtime/engine/physics/components/collider_mesh.h/.cpp` — CColliderMesh: mesh_path (string), convex (true). Convex → JPH::ConvexHullShape. Non-convex → JPH::MeshShape (static only, error if dynamic).
- [ ] 3.5 Create `runtime/engine/physics/shape_factory.h/.cpp` — ShapeFactory::BuildShape(entity, registry) → JPH::ShapeRefC. Multiple colliders → StaticCompoundShape. Shape caching by hash.

## 4. Collision Events

- [ ] 4.1 Create `runtime/engine/physics/collision_listener.h/.cpp` — ContactListener (JPH::ContactListener): OnContactAdded/Persisted/Removed. Queues CollisionEvent { entity_a, entity_b, contact_point, contact_normal, impulse, is_begin, is_end }. Thread-safe queue.
- [ ] 4.2 Create `runtime/engine/physics/trigger_listener.h/.cpp` — TriggerListener: tracks overlap pairs. Generates TriggerEvent { trigger_entity, other_entity, type: Enter/Stay/Exit } via set-difference between frames.
- [ ] 4.3 Wire events to EventBus — PhysicsSubsystem::PostStep() drains queues, fires EventBus<CollisionEvent> and EventBus<TriggerEvent>.

## 5. Scene Serialization

- [ ] 5.1 Add CRigidBody to ComponentSerializerRegistry — JSON: body_type, mass, friction, restitution, linear_damping, angular_damping, gravity_scale, is_ccd, lock_rotation_x/y/z.
- [ ] 5.2 Add collider serialization — CColliderBox/Sphere/Capsule/Mesh JSON. Multiple colliders per entity as JSON array.
- [ ] 5.3 Wire deserialization → PhysicsWorld body creation on scene load.

## 6. Testing

- [ ] 6.1 Unit tests for PhysicsWorld — create/destroy, step with gravity, timestep accumulator.
- [ ] 6.2 Unit tests for CRigidBody — create body, apply force, velocity after step, kinematic move.
- [ ] 6.3 Unit tests for colliders — box/sphere/capsule/mesh shape creation, compound shape.
- [ ] 6.4 Unit tests for collision events — two dynamic bodies collide, verify CollisionEvent.
- [ ] 6.5 Unit tests for trigger events — body enters/exits trigger, verify Enter/Exit.
- [ ] 6.6 Integration test: 60 frames with gravity, verify dynamic body falls along Z-axis.

## 7. Demo

- [ ] 7.1 Create `demos/physics_basic/` — ground plane (static box), falling cubes (dynamic), ramps. Headless + screenshot.
- [ ] 7.2 Add physics debug wireframe overlay — collider shapes as wireframe via debug draw.

## 8. Documentation

- [ ] 8.1 Update `docs/engine/16-physics/` with implementation details and API reference.
- [ ] 8.2 Document `.uzscene` physics component JSON format.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
