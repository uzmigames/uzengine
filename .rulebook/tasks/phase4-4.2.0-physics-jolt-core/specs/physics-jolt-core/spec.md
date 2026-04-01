# Physics Jolt Core — Specification

## ADDED Requirements

### Requirement: PhysicsWorld Lifecycle
The physics system SHALL provide a PhysicsWorld that owns the Jolt simulation and steps at a fixed 120Hz rate independent of frame rate.

#### Scenario: World Creation
Given the engine is initializing
When PhysicsWorld::Init(gravity={0,0,-981}) is called
Then JPH::PhysicsSystem SHALL be created with max_bodies=65536, max_body_pairs=65536, max_contact_constraints=16384
And JPH::TempAllocatorImpl SHALL be allocated with 16MB
And JPH::JobSystemThreadPool SHALL use (hardware_threads - 1) worker threads
And gravity SHALL be Z-down (UzEngine Z-up convention)

#### Scenario: Fixed Timestep Step
Given PhysicsWorld with accumulator=0
When Step(dt=0.033) is called (33ms frame, ~30fps)
Then the accumulator SHALL add dt (total=0.033)
And the physics system SHALL step 3 times at 1/120s = 0.00833s each
And the accumulator SHALL retain remainder=0.033 - 3*0.00833 = 0.008s
And max substeps per frame SHALL be capped at 4 to prevent spiral of death

#### Scenario: World Shutdown
Given PhysicsWorld with active bodies
When PhysicsWorld::Shutdown() is called
Then all bodies SHALL be removed from JPH::PhysicsSystem
And JPH::TempAllocator SHALL be freed
And JPH::Factory::sInstance SHALL be destroyed

### Requirement: CRigidBody Component
The physics system SHALL provide a CRigidBody ECS component that maps an entity to a Jolt physics body.

#### Scenario: Dynamic Body Creation
Given an entity with CRigidBody(body_type=Dynamic, mass=2.0) and CColliderBox(half_extents={1,1,1})
When the physics system initializes the entity
Then a JPH::Body SHALL be created with MotionType::Dynamic
And mass SHALL be 2.0 kg
And the body SHALL be added to the MOVING broadphase layer

#### Scenario: Static Body
Given an entity with CRigidBody(body_type=Static) and CColliderBox
When the physics system initializes the entity
Then a JPH::Body SHALL be created with MotionType::Static
And the body SHALL be added to the NON_MOVING broadphase layer
And the body SHALL NOT be affected by gravity or forces

#### Scenario: Apply Impulse
Given a dynamic CRigidBody at rest
When ApplyImpulse({0, 0, 500}) is called
Then the body's linear velocity SHALL change immediately
And the magnitude SHALL be impulse / mass

#### Scenario: Transform Sync (Physics → ECS)
Given a dynamic body that has moved during Step()
When BodySync runs after Step()
Then TransformComponent.position SHALL match Jolt body position (cm units)
And TransformComponent.rotation SHALL match Jolt body rotation

#### Scenario: Transform Sync (ECS → Kinematic)
Given a kinematic body with CRigidBody(body_type=Kinematic)
When TransformComponent is modified by game code
Then BodySync SHALL call JPH::BodyInterface::MoveKinematic() with the new transform
And static/dynamic bodies SHALL NOT be synced ECS→Jolt

### Requirement: Collider Components
The physics system SHALL provide multiple collider types that create Jolt collision shapes.

#### Scenario: Box Collider
Given CColliderBox(half_extents={2,1,0.5}, offset={0,0,1})
When ShapeFactory::BuildShape() is called
Then a JPH::BoxShape SHALL be created with half-extents (2,1,0.5)
And the shape SHALL be wrapped in JPH::OffsetCenterOfMassShape with offset (0,0,1)

#### Scenario: Compound Shape
Given an entity with CColliderBox(half_extents={1,1,1}) AND CColliderSphere(radius=0.5, offset={0,0,2})
When ShapeFactory::BuildShape() is called
Then a JPH::StaticCompoundShape SHALL be created
And it SHALL contain both sub-shapes with their respective offsets

#### Scenario: Mesh Collider (Convex)
Given CColliderMesh(mesh_path="box.uzmesh", convex=true)
When ShapeFactory::BuildShape() is called
Then vertices SHALL be loaded from the .uzmesh asset
And JPH::ConvexHullShapeSettings SHALL be used to create the shape

#### Scenario: Mesh Collider (Triangle Mesh — Static Only)
Given CColliderMesh(mesh_path="terrain.uzmesh", convex=false) on a Dynamic body
When ShapeFactory::BuildShape() is called
Then the system SHALL log an error: "Triangle mesh collider only valid on Static bodies"
And the shape SHALL NOT be created

### Requirement: Collision Layers
The physics system SHALL support configurable collision layers with an interaction matrix.

#### Scenario: Default Layer Matrix
Given the default layer configuration
When two bodies on layer Default(0) and Static(1) are checked
Then they SHALL collide (Default ↔ Static is enabled by default)

#### Scenario: Non-Interacting Layers
Given layer Debris(7) configured to NOT interact with Trigger(4)
When a Debris body overlaps a Trigger volume
Then NO TriggerEvent SHALL be generated

### Requirement: Collision Events
The physics system SHALL dispatch collision and trigger events via the EventBus.

#### Scenario: Collision Begin
Given two dynamic bodies approaching each other
When they first make contact during Step()
Then a CollisionEvent SHALL be queued with is_begin=true
And contact_point SHALL be the world-space contact location
And contact_normal SHALL point from body_a toward body_b
And impulse SHALL be the contact impulse magnitude

#### Scenario: Trigger Enter/Exit
Given a dynamic body entering a trigger volume
When overlap is first detected
Then a TriggerEvent SHALL be queued with type=Enter
When the body leaves the trigger volume
Then a TriggerEvent SHALL be queued with type=Exit

### Requirement: Scene Serialization
All physics components SHALL be serializable to/from .uzscene JSON format.

#### Scenario: Save CRigidBody
Given an entity with CRigidBody(body_type=Dynamic, mass=5.0, friction=0.8)
When the scene is saved to JSON
Then the output SHALL contain: { "rigid_body": { "body_type": "dynamic", "mass": 5.0, "friction": 0.8, ... } }

#### Scenario: Load Scene with Physics
Given a .uzscene file with physics components
When the scene is loaded
Then CRigidBody and collider components SHALL be deserialized
And bodies SHALL be created in PhysicsWorld automatically
