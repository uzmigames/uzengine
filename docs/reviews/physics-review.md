# Physics System -- AAA Gap Analysis

> **Author**: Physics Engineer
> **Date**: 2026-03-02
> **Scope**: Critical physics features missing from UzEngine's current roadmap (Phases 1-5)
> **Baseline Documents Reviewed**:
> - `docs/specs/physics-system.md` (Phase 3 spec)
> - `docs/specs/navmesh.md` (Phase 4 spec)
> - `docs/specs/skeletal-mesh.md` (Phase 3 spec)
> - `docs/specs/engine-vision.md` (architecture overview)
> - `docs/ROADMAP.md` (full roadmap, Phases 0-5)
> - `docs/specs/terrain-system.md` (Phase 4 spec)
> - `docs/specs/particle-system.md` (Phase 3 spec)

---

## Executive Summary

The current physics-system.md spec covers a solid foundation: Jolt integration, rigid body ECS components, collision layers, character controller, raycasts, triggers, and ragdoll. This is sufficient for an indie-tier physics pipeline. However, for AAA-competitive capability -- the kind of physics behavior shipped in Unreal Engine 5, Unity 6, or Godot 4.4 -- there are **16 critical gaps** spanning simulation fidelity, advanced constraint systems, performance infrastructure, and specialized domains like vehicles and destruction.

This review is ordered by impact priority: features that block the largest number of gameplay genres are listed first.

---

## Gap 1: Physics Materials System

### Why Critical

Every AAA engine (Unreal's Physical Materials, Unity's Physic Material assets, Godot's PhysicsMaterial resource) treats friction and restitution as **shared, reusable assets** rather than per-body inline values. The current spec embeds `friction` and `restitution` directly on `CRigidBody`, meaning:

- You cannot define "Ice" once and assign it to 200 floor tiles.
- Changing the friction of "Wood" requires editing every wooden object individually.
- There is no surface-type-based audio or particle response (impact on metal vs. glass vs. mud).
- Combined friction/restitution rules (min, max, multiply, average) are not configurable.

### Technical Implementation

```cpp
// Physics material asset, loaded from .uzphysmat (INI-like, AI-friendly)
struct PhysicsMaterial {
    StringId        id;
    float           static_friction   = 0.5f;   // Coulomb static friction
    float           dynamic_friction  = 0.5f;   // Coulomb kinetic friction
    float           restitution       = 0.3f;   // Coefficient of restitution
    float           density           = 1000.f;  // kg/m^3, used for mass-from-shape
    float           softness          = 0.0f;    // 0=rigid, 1=deformable (future soft body)
    StringId        surface_type;                // "metal", "wood", "ice" -- for audio/VFX lookup
    // Jolt: maps to JPH::PhysicsMaterial subclass
};

// Combination rules (configured in .uzproj [physics.material_combine])
enum class CombineMode : uint8_t { Average, Min, Max, Multiply };
```

**File format** (`.uzphysmat`):
```ini
[uzphysmat version=1 name="Ice"]
static_friction  = 0.05
dynamic_friction = 0.03
restitution      = 0.1
density          = 917.0
surface_type     = "ice"
```

**Jolt integration**: Subclass `JPH::PhysicsMaterial` and override `GetFriction()` / `GetRestitution()`. Register materials with `PhysicsSystem` at init. Each `CRigidBody` stores a `StringId material` instead of raw float values. On contact, Jolt calls `CombineFriction()` / `CombineRestitution()` which uses the configured combine mode.

**Surface-type dispatch**: On collision, `ContactListener::OnContactAdded` reads both materials' `surface_type` fields, looks up the interaction table (e.g., `metal_on_wood` -> spark particle + clang sound), and emits an `ImpactEvent` through `EventBus`:

```cpp
struct ImpactEvent {
    NodeHandle   node_a, node_b;
    Vec3         point, normal;
    float        impulse;
    StringId     surface_a, surface_b;  // for audio/VFX lookup
};
```

### Impact if Absent

- No surface-type-based sound effects (a hallmark of every AAA game since Half-Life 2).
- Manual friction/restitution management across thousands of objects.
- No combined friction rules -- physically incorrect when rubber meets ice.

### Complexity: **S** (1-3 days)

### Dependencies: Physics System (Phase 3 core)

---

## Gap 2: Breakable Joints and Constraints

### Why Critical

The current spec mentions ragdoll constraints (`SwingTwistConstraint`, `HingeConstraint` in `.uzphys`), but there is **no general-purpose constraint system** and no concept of **breakable joints**. Unreal Engine exposes 12+ constraint types (hinge, slider, ball-and-socket, distance, cone twist, gear, rack-and-pinion, pulley, weld). Unity has `ConfigurableJoint` with 6 DOF limits plus break force/torque.

Without breakable joints, you cannot build:
- Doors that rip off hinges under explosion force.
- Bridges that collapse when overloaded.
- Chain links that snap.
- Destructible structures with structural integrity simulation.

### Technical Implementation

```cpp
// Generic constraint component -- one per constraint, placed on the "child" node
struct CConstraint {
    NodeHandle      body_a;              // connected body (or scene root for world anchor)
    NodeHandle      body_b;              // this node's body
    ConstraintType  type;                // hinge, slider, ball_socket, distance, 6dof, weld
    Vec3            anchor_a;            // local-space anchor on body_a
    Vec3            anchor_b;            // local-space anchor on body_b
    Vec3            axis;                // primary axis (hinge, slider)

    // Limits
    float           lower_limit;         // type-dependent (angle for hinge, distance for slider)
    float           upper_limit;
    float           twist_lower;         // for 6DOF
    float           twist_upper;

    // Motor
    bool            motor_enabled;
    float           motor_target;        // target position or velocity
    float           motor_max_force;

    // Break threshold
    float           break_force   = 0.f; // 0 = unbreakable. Newtons.
    float           break_torque  = 0.f; // 0 = unbreakable. Newton-meters.

    // Runtime
    JPH::Constraint* jolt_constraint = nullptr;
    bool             is_broken = false;
};
```

**Break detection**: In the physics step callback, after `PhysicsSystem::Update()`, iterate all active constraints. Jolt exposes `Constraint::GetTotalLambda()` which gives the constraint force magnitude. Compare against `break_force`; if exceeded, call `PhysicsSystem::RemoveConstraint()` and emit a `ConstraintBreakEvent`:

```cpp
struct ConstraintBreakEvent {
    NodeHandle  body_a, body_b;
    Vec3        break_point;
    float       force_at_break;
};
```

**Jolt mapping**:
- `hinge` -> `JPH::HingeConstraint`
- `slider` -> `JPH::SliderConstraint`
- `ball_socket` -> `JPH::PointConstraint`
- `distance` -> `JPH::DistanceConstraint`
- `6dof` -> `JPH::SixDOFConstraint`
- `weld` -> `JPH::FixedConstraint`

**Scene format**:
```ini
[node id=20 type=Node3D name="DoorHinge" parent=1]
constraint_type     = "hinge"
constraint_body_a   = "DoorFrame"       ; resolved to NodeHandle at load
constraint_axis     = [0, 1, 0]
constraint_lower    = -90.0
constraint_upper    = 0.0
constraint_break_force = 5000.0         ; breaks at 5000 N
```

### Impact if Absent

- No mechanical simulations (doors, levers, drawbridges, elevators).
- No destructible environment physics (a AAA staple since Red Faction).
- Ragdoll-only constraints are severely limiting for gameplay.

### Complexity: **L** (2-3 weeks)

### Dependencies: Physics System (Phase 3 core)

---

## Gap 3: Destructible Meshes / Fracture System

### Why Critical

Unreal Engine 5 has Chaos Destruction (Voronoi fracture, damage thresholds, hierarchical fracture). Unity has third-party solutions (RayFire, Destructible Environments). Godot 4 has basic `SoftBody3D` but lacks fracture. Destruction is a cornerstone of action games, FPS, racing, and building sims.

The current UzEngine roadmap has **zero mention** of mesh fracture or destruction.

### Technical Implementation

**Offline fracture (cook-time)**:

```cpp
// Pre-fracture tool: takes a mesh and produces a FracturedMesh asset
struct FracturedMesh {
    struct Fragment {
        MeshData        geometry;        // sub-mesh after fracture
        Vec3            center_of_mass;
        float           volume;          // for mass calculation
        float           surface_area;    // for damage propagation
        std::vector<int> neighbors;      // adjacency graph
    };
    std::vector<Fragment> fragments;
    float                 total_mass;
};
```

Use **Voronoi fracture** via a cell decomposition library (e.g., voro++ MIT license, or custom implementation using Jolt's convex hull decomposition). The fracture is computed at cook time and stored as `.uzfracture`:

```ini
[uzfracture version=1 name="Crate_Fractured"]
source_mesh      = "res://meshes/crate.glb"
fragment_count   = 24
seed             = 42
fracture_mode    = voronoi            ; voronoi | planar | radial
; Fragments stored as binary sub-meshes alongside this file
```

**Runtime destruction flow**:

1. Intact mesh renders normally with a `CDestructible` component.
2. When accumulated damage (from `CollisionEvent.collision_impulse` or explicit damage API) exceeds a threshold, the `DestructionSystem` replaces the intact node with N fragment nodes.
3. Each fragment gets a `CRigidBody` (mass from volume * density via `PhysicsMaterial`).
4. Fragments are connected by `CConstraint` (weld type, with break force). As forces propagate, fragments separate.
5. Small fragments auto-despawn after a configurable timeout to manage body count.

```cpp
struct CDestructible {
    StringId    fracture_asset;         // .uzfracture
    float       health = 100.0f;        // total damage before full fracture
    float       accumulated_damage = 0.f;
    float       fragment_despawn_time = 10.f; // seconds before cleanup
    bool        fractured = false;
    uint8_t     max_cascade_depth = 2;  // recursive fracture depth
};
```

**Hierarchical fracture**: Large structures (buildings) use a damage graph. Destroying a load-bearing fragment propagates stress to neighbors. If a neighbor's constraints all break, it also becomes free. This models structural collapse without simulating every fragment every frame.

### Impact if Absent

- No destructible environments -- a major expectation in action, FPS, and racing games.
- No building collapse, breakable walls, or shattering glass.
- Severely limits environmental interactivity.

### Complexity: **XL** (4+ weeks)

### Dependencies: Physics System, Breakable Joints, Physics Materials, Asset Pipeline (cook-time fracture tool)

---

## Gap 4: Vehicle Physics

### Why Critical

Unreal has `WheeledVehiclePawn` (Chaos Vehicle), Unity has `WheelCollider`, Godot has `VehicleBody3D`. Vehicle physics is required for racing games, open-world games, military sims, and any game with driveable vehicles. The current roadmap lists "Vehicle physics" as "Phase 5+" -- effectively unscheduled and undocumented.

### Technical Implementation

Jolt Physics 5.x includes `JPH::VehicleConstraint` with `JPH::WheeledVehicleController` and `JPH::TrackedVehicleController`. These should be the primary integration target.

```cpp
struct CVehicle {
    // Configuration
    float           mass = 1500.0f;           // kg
    Vec3            center_of_mass_offset;    // lower for stability
    float           max_engine_torque = 500.f; // Nm
    float           max_brake_torque  = 1500.f;
    float           max_steering_angle = 35.f; // degrees
    bool            is_tracked = false;        // wheeled vs tracked

    // Per-wheel data (up to 8 wheels)
    struct WheelConfig {
        Vec3    position;                 // local-space suspension top
        float   suspension_length = 0.5f; // meters
        float   suspension_stiffness = 15000.f;
        float   suspension_damping   = 1500.f;
        float   wheel_radius = 0.35f;
        float   wheel_width  = 0.25f;
        float   lateral_friction = 1.0f;
        float   longitudinal_friction = 1.0f;
        bool    is_driven = true;         // receives engine torque
        bool    is_steered = true;
    };
    std::array<WheelConfig, 8> wheels;
    uint8_t wheel_count = 4;

    // Transmission
    std::array<float, 8> gear_ratios = {3.5f, 2.3f, 1.5f, 1.0f, 0.8f};
    uint8_t gear_count = 5;
    float   final_drive_ratio = 3.42f;

    // Runtime
    JPH::VehicleConstraint* jolt_vehicle = nullptr;
    int8_t  current_gear = 0;            // 0=neutral, -1=reverse
    float   engine_rpm = 0.f;
    float   speed_kmh  = 0.f;
};
```

**Tire model**: Jolt uses a simplified Pacejka-like tire model. For AAA fidelity, expose the friction curve parameters:

```ini
[uzvehicle version=1 name="SportsCar"]
mass              = 1400
center_of_mass    = [0, -0.3, 0.2]
max_engine_torque = 600
max_brake_torque  = 2000

[wheel.front_left]
position          = [-0.8, 0.0, 1.5]
radius            = 0.33
suspension_length = 0.5
stiffness         = 18000
damping           = 2000
is_driven         = false
is_steered        = true
lateral_friction  = 1.2
longitudinal_friction = 1.1

; ... front_right, rear_left, rear_right ...

[transmission]
gear_ratios       = [3.82, 2.36, 1.52, 1.14, 0.87, 0.74]
final_drive       = 3.42
auto_clutch       = true
```

**Lua API**:
```lua
self:vehicle_set_throttle(1.0)      -- 0..1
self:vehicle_set_brake(0.5)         -- 0..1
self:vehicle_set_steer(-0.3)        -- -1..1
local rpm = self:vehicle_get_rpm()
local gear = self:vehicle_get_gear()
self:vehicle_shift_up()
self:vehicle_shift_down()
```

### Impact if Absent

- No racing games, no open-world vehicles, no military sims.
- Eliminates an entire genre of games from the engine's capability.

### Complexity: **L** (2-3 weeks)

### Dependencies: Physics System, Physics Materials (tire friction surfaces)

---

## Gap 5: Continuous Collision Detection (CCD) -- Configurable and Per-Shape

### Why Critical

The current spec has a `continuous_ccd` boolean on `CRigidBody`, but this is insufficient. AAA engines provide:

1. **Motion quality settings** (Jolt calls this `EMotionQuality::LinearCast`): uses a swept shape test instead of discrete position updates.
2. **Per-shape CCD** (not just per-body): compound shapes may have thin parts that need CCD while the main body does not.
3. **CCD threshold** (maximum allowed penetration before CCD activates): prevents wasting CCD budget on slow objects.
4. **Max linear velocity clamping**: prevents tunneling for extreme velocities.

Jolt supports all of these via `BodyCreationSettings::mMotionQuality` and `BodyCreationSettings::mMaxLinearVelocity`. The current spec does not expose them.

### Technical Implementation

Extend `CRigidBody`:

```cpp
struct CRigidBody {
    // ... existing fields ...

    // CCD configuration (replacing bool continuous_ccd)
    enum class MotionQuality : uint8_t {
        Discrete,       // default -- fastest, can tunnel
        LinearCast      // swept test -- prevents tunneling
    };
    MotionQuality  motion_quality = MotionQuality::Discrete;
    float          max_linear_velocity  = 500.f;   // m/s; clamp to prevent instability
    float          max_angular_velocity = 47.12f;  // rad/s (~7.5 rev/s)

    // Jolt: BodyCreationSettings::mMotionQuality = mq;
    // Jolt: BodyCreationSettings::mMaxLinearVelocity = max_linear_velocity;
};
```

**Scene format update**:
```ini
rigid_body_motion_quality = "linear_cast"   ; "discrete" or "linear_cast"
rigid_body_max_velocity   = 1000.0          ; m/s (for bullets, railgun projectiles)
```

**Global CCD budget** in `.uzproj`:
```toml
[physics]
ccd_max_bodies = 64    ; max bodies using LinearCast per frame (performance budget)
```

### Impact if Absent

- Fast projectiles (bullets, arrows, railgun shots) tunnel through thin walls.
- High-speed vehicles clip through barriers.
- Players can exploit physics tunneling to bypass collision.

### Complexity: **S** (1-3 days)

### Dependencies: Physics System (Phase 3 core)

---

## Gap 6: Physics Queries Batch API (Sweep Tests, Multi-Raycast)

### Why Critical

The current Lua API exposes `raycast`, `linecast`, `overlap_sphere`, and `overlap_box` -- all single-query functions. AAA engines provide:

1. **Shape cast / Sweep test**: Move a shape (sphere, box, capsule) along a direction and find the first hit. Essential for: thick projectiles, character controller ground probing, camera collision, vehicle wheel raycasts.
2. **Batch raycasts**: Fire N rays in a single call, returning N results. Jolt's `CastRay` with `RayCastBodyCollector` already supports this pattern efficiently.
3. **Closest vs. All hits**: Current API only returns first hit. Many use cases need all hits (penetration shots, X-ray vision, multi-target line attacks).
4. **Async queries**: Queue physics queries on a background thread, collect results next frame. For AI systems that issue hundreds of line-of-sight checks per frame.

### Technical Implementation

**C++ query functions**:

```cpp
// Shape cast: sweep a shape along a direction
struct ShapeCastResult {
    bool      hit;
    Vec3      point;
    Vec3      normal;
    NodeHandle node;
    float     fraction;  // 0..1 along the sweep direction
};
ShapeCastResult physics_shape_cast(
    const JPH::Shape* shape,   // sphere, box, capsule
    Vec3 start, Vec3 direction, float max_dist,
    uint16_t layer_mask = 0xFFFF
);

// Multi-hit raycast: returns all bodies hit along the ray
struct RayCastHit {
    Vec3        point;
    Vec3        normal;
    NodeHandle  node;
    float       distance;
};
std::vector<RayCastHit> physics_raycast_all(
    Vec3 origin, Vec3 direction, float max_dist,
    uint16_t layer_mask = 0xFFFF,
    uint16_t max_hits = 32
);

// Batch raycast: N rays in one call (cache-friendly, single broadphase traversal)
struct BatchRayRequest { Vec3 origin; Vec3 direction; float max_dist; uint16_t layer_mask; };
std::vector<RayCastResult> physics_raycast_batch(
    std::span<const BatchRayRequest> requests
);

// Overlap capsule (missing from current spec -- needed for character checks)
std::vector<NodeHandle> physics_overlap_capsule(
    Vec3 center, float radius, float half_height,
    Quat rotation, uint16_t layer_mask = 0xFFFF
);
```

**Lua API additions**:

```lua
-- Shape cast (sweep)
local hit = ctx.world:shape_cast("sphere", 0.5, origin, direction, max_dist, layer_mask)
local hit = ctx.world:shape_cast("box", Vec3(0.5, 0.5, 0.5), origin, direction, max_dist)
local hit = ctx.world:shape_cast("capsule", {radius=0.3, half_height=0.5}, origin, dir, dist)

-- Raycast all (penetrating)
local hits = ctx.world:raycast_all(origin, direction, max_dist, layer_mask)
for _, h in ipairs(hits) do ... end

-- Overlap capsule
local nodes = ctx.world:overlap_capsule(center, radius, half_height, rotation, layer_mask)

-- Batch raycast (returns array of results matching input array)
local results = ctx.world:raycast_batch({
    { origin=pos1, direction=dir1, max_dist=100 },
    { origin=pos2, direction=dir2, max_dist=50 },
})
```

### Impact if Absent

- No thick projectile collision (plasma bolts, cannonballs).
- No penetrating projectile queries (railgun through multiple enemies).
- AI line-of-sight checks are inefficient (single queries, no batching).
- Character controller ground probing is imprecise (raycast instead of capsule cast).
- Camera collision in SpringArm is unreliable (point ray vs. sphere sweep).

### Complexity: **M** (1 week)

### Dependencies: Physics System (Phase 3 core)

---

## Gap 7: Cloth Simulation

### Why Critical

Unreal has Chaos Cloth (formerly APEX Cloth via NVIDIA). Unity has Cloth component. Cloth simulation is used for: character capes, flags, curtains, banners, tent fabric, tablecloths, hair cards. It is visually expected in any AAA game from the last decade.

### Technical Implementation

Jolt Physics 5.x includes `JPH::SoftBodyCreationSettings` with cloth-specific features: `SoftBodySharedSettings` allows defining a particle mesh with distance constraints, bend constraints, and collision with rigid bodies.

**ECS component**:

```cpp
struct CCloth {
    StringId    mesh_id;                // source mesh to simulate
    float       mass_per_vertex = 0.01f;
    float       stiffness       = 0.8f; // 0=loose, 1=rigid
    float       damping         = 0.1f;
    float       drag            = 0.05f;
    float       wind_influence  = 1.0f;
    uint8_t     solver_iterations = 4;  // constraint solve passes per step
    float       collision_margin = 0.01f;

    // Pinned vertices (fixed points, e.g., shoulder attachment)
    std::vector<uint32_t> pinned_vertices;

    // Runtime
    JPH::SoftBodyMotionProperties* jolt_soft_body = nullptr;
};
```

**Simulation pipeline**:

1. At scene load, convert the cloth mesh into a Jolt `SoftBodySharedSettings` particle system: vertices become particles, edges become distance constraints, diagonal edges become bend constraints.
2. Pin vertices that attach to the character skeleton (e.g., cape attaches to shoulder bones).
3. Each physics step, Jolt simulates the particle positions.
4. After simulation, read back particle positions into a dynamic vertex buffer for GPU rendering.
5. Wind force is applied globally via `SoftBodyMotionProperties::AddForce()`.

**Scene format**:
```ini
[node id=30 type=Node3D name="Cape" parent=5]
mesh          = "res://meshes/cape_cloth.glb"
cloth         = true
cloth_stiffness = 0.75
cloth_damping   = 0.1
cloth_pinned    = [0, 1, 2, 3, 4]    ; vertex indices attached to skeleton
cloth_wind      = 1.0
```

**Performance considerations**:
- Limit cloth to max 1024 vertices per cloth instance.
- Max 8 cloth instances per scene (configurable in `.uzproj`).
- LOD: reduce solver iterations at distance, switch to baked animation at far LOD.

### Impact if Absent

- No flowing capes, flags, curtains, hair, or fabric.
- Characters look rigid and lifeless in AAA-quality scenes.
- Major visual downgrade compared to competitor engines.

### Complexity: **L** (2-3 weeks)

### Dependencies: Physics System, Skeletal Mesh (for pinned vertex attachment to bones)

---

## Gap 8: Deterministic Physics for Netcode

### Why Critical

The ROADMAP.md Phase 5 lists "Rollback netcode (GGPO)" as a future feature. Rollback netcode fundamentally requires **deterministic physics** -- given the same inputs and initial state, the simulation must produce bit-identical results across all clients. Without determinism, rollback desynchronizes and the game becomes unplayable.

Jolt Physics is **not deterministic by default** across different platforms or compiler settings. Cross-platform determinism requires:

1. Fixed-point or IEEE-754 strict compliance with identical rounding modes.
2. Identical thread scheduling (Jolt's job system processes bodies in parallel, and floating-point addition is not associative).
3. Identical memory layout and allocation order.

### Technical Implementation

**Phase A -- Same-platform determinism** (achievable):

```cpp
// 1. Single-threaded physics for netcode-critical frames
struct PhysicsWorld {
    // When netcode is active, use single-threaded job system
    JPH::JobSystemSingleThreaded  single_thread_jobs;
    JPH::JobSystemThreadPool      multi_thread_jobs;
    bool use_deterministic_mode = false;

    void Step(float dt) {
        if (use_deterministic_mode) {
            jph->Update(dt, 1, temp_allocator, single_thread_jobs);
        } else {
            jph->Update(dt, 1, temp_allocator, multi_thread_jobs);
        }
    }
};

// 2. Fixed simulation seed and body creation order
// Bodies must be created in the exact same order on all clients
// Use a stable sort by entity ID before body creation

// 3. State serialization for rollback
struct PhysicsSnapshot {
    std::vector<BodyState> body_states;  // position, rotation, velocity, angular_vel
    float                  accumulator;
    uint64_t               step_count;
};

PhysicsSnapshot physics_save_state();
void physics_restore_state(const PhysicsSnapshot& snapshot);
```

**Phase B -- Cross-platform determinism** (hard, requires Jolt modifications):

- Compile Jolt with `/fp:strict` (MSVC) or `-ffp-strict` (GCC/Clang).
- Replace Jolt's internal sorting (which may vary across platforms) with a stable sort.
- Disable SIMD optimizations that differ between SSE and AVX (or ensure identical instruction sets on all clients via runtime CPU feature detection).
- Consider using fixed-point arithmetic for critical paths (position, velocity integration).

**State checksum** for desync detection:
```cpp
uint64_t physics_compute_checksum() {
    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);
    for (auto& body : active_bodies) {
        XXH64_update(state, &body.position, sizeof(Vec3));
        XXH64_update(state, &body.rotation, sizeof(Quat));
        XXH64_update(state, &body.linear_velocity, sizeof(Vec3));
    }
    return XXH64_digest(state);
}
```

### Impact if Absent

- Rollback netcode (Phase 5) will desynchronize immediately.
- Multiplayer games with physics interactions (fighting games, co-op, racing) are impossible.
- Server-authoritative physics require frequent corrections causing jitter.

### Complexity: **XL** (4+ weeks)

### Dependencies: Physics System, Networking (Phase 5)

---

## Gap 9: Jolt Job System Full Utilization and Multi-Threading Strategy

### Why Critical

The current spec shows `PhysicsSystem::Update(PHYSICS_DT, ...)` but does not document: how many threads Jolt uses, how the job system is configured, how it interacts with UzEngine's thread model, or how to avoid contention with the render thread and Lua VM.

Jolt's `JobSystemThreadPool` is designed to use all available cores for broadphase, narrowphase, constraint solving, and island splitting. In AAA, physics often consumes 2-4ms of a 16ms frame budget. Incorrect threading causes frame spikes, priority inversion, or underutilization.

### Technical Implementation

```cpp
// Thread budget configuration in .uzproj
// [physics]
// worker_threads = 0   ; 0 = auto (num_cores - 2, minimum 1)
// max_barriers   = 8   ; Jolt barrier count (default 8)
// temp_allocator_size = 10485760  ; 10 MB TempAllocator per step

struct PhysicsWorld {
    std::unique_ptr<JPH::TempAllocatorImpl>  temp_allocator;
    std::unique_ptr<JPH::JobSystemThreadPool> job_system;

    void init(const PhysicsConfig& cfg) {
        int threads = cfg.worker_threads;
        if (threads == 0) {
            threads = std::max(1, (int)std::thread::hardware_concurrency() - 2);
        }

        temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(
            cfg.temp_allocator_size
        );
        job_system = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            cfg.max_barriers,
            threads
        );
    }
};
```

**Thread affinity**: Pin physics worker threads to specific cores to avoid migration overhead. Reserve core 0 for the main/render thread, core 1 for audio.

**Frame pipeline integration**:
```
Main Thread:      [Input] [Lua on_update] [--- wait for physics ---] [Transform Sync] [Render Submit]
Physics Threads:  [------- Jolt::Update (broadphase, narrowphase, solve, integrate) -------]
Render Thread:    [--- GPU command recording (previous frame) ---]
Audio Thread:     [--- XAudio2 mixing ---]
```

Physics can run in parallel with rendering (current frame physics while GPU processes previous frame), but the main thread must synchronize before transform sync. Document this pipeline explicitly.

**Profiling hooks**: Insert Tracy zones around each Jolt job barrier:
```cpp
class TracyJobSystem : public JPH::JobSystemThreadPool {
    void OnJobStarted(const JPH::Job* job) override {
        ZoneScopedN("Jolt Job");
    }
};
```

### Impact if Absent

- Physics underutilizes available CPU cores, leaving performance on the table.
- Frame spikes from contention between physics, rendering, and scripting threads.
- No visibility into physics thread performance in the profiler.
- Developers cannot tune thread allocation for their hardware targets.

### Complexity: **M** (1 week)

### Dependencies: Physics System (Phase 3 core), Debug & Profiling (Phase 1)

---

## Gap 10: Physics LOD (Simplified Physics at Distance)

### Why Critical

Unreal Engine uses "physics sleeping" (which Jolt already does) plus **simulation distance culling** -- bodies beyond a threshold are frozen or removed from the simulation entirely. Unity has similar behavior via `Rigidbody.sleepThreshold` and scripted activation distances.

The current spec mentions Jolt's built-in sleeping but does not address:

1. **Simulation distance culling**: Bodies far from the camera/player should not simulate even if technically "awake" (e.g., a pile of crates knocked over by an NPC far away).
2. **Shape simplification at distance**: Replace convex mesh colliders with simpler bounding box colliders at distance.
3. **Island management**: Large islands of connected bodies should be split or simplified.
4. **Body budget**: Hard limit on simultaneously active bodies with priority-based activation.

### Technical Implementation

```cpp
struct CPhysicsLOD {
    float   simulation_radius = 100.f;   // meters from player; beyond this, freeze
    float   simplified_radius = 200.f;   // beyond sim_radius but within this: use box collider
    float   despawn_radius    = 500.f;   // beyond this: remove body entirely (for debris)
    bool    always_simulate   = false;   // override for important bodies (player, vehicles)
};

// PhysicsLODSystem runs once per frame (after camera update, before physics step)
class PhysicsLODSystem {
    void update(const Vec3& player_pos, entt::registry& reg) {
        for (auto [e, rb, lod, transform] : reg.view<CRigidBody, CPhysicsLOD, CTransform>().each()) {
            float dist = glm::distance(player_pos, transform.position);

            if (lod.always_simulate) continue;

            if (dist > lod.despawn_radius) {
                // Remove from Jolt entirely
                body_interface.RemoveBody(rb.jolt_body_id);
                rb.jolt_body_id = JPH::BodyID(); // invalidate
            } else if (dist > lod.simplified_radius) {
                // Swap shape to AABB
                simplify_shape(rb);
                body_interface.DeactivateBody(rb.jolt_body_id);
            } else if (dist > lod.simulation_radius) {
                // Deactivate but keep body
                body_interface.DeactivateBody(rb.jolt_body_id);
            } else {
                // Active simulation
                if (!body_interface.IsActive(rb.jolt_body_id)) {
                    body_interface.ActivateBody(rb.jolt_body_id);
                }
            }
        }
    }
};
```

**Body budget system**:
```toml
[physics]
max_active_bodies = 1024     ; hard limit; excess bodies frozen by distance priority
```

When the active body count exceeds the budget, sort all awake bodies by distance from player and freeze the farthest ones.

### Impact if Absent

- Large open-world scenes with thousands of physics objects will drop below 60 FPS.
- Explosion debris persists in simulation indefinitely, consuming CPU.
- No control over physics performance scaling.

### Complexity: **M** (1 week)

### Dependencies: Physics System, Camera System (for player position)

---

## Gap 11: Cable / Rope / Chain Simulation

### Why Critical

Ropes, cables, and chains appear in: grappling hooks, suspension bridges, cranes, wrecking balls, hanging lanterns, rope swings, chain-link barriers, and power lines. Unreal has Cable Component (Verlet integration), Unity has Obi Rope (third-party, widely used). This is a high-visibility feature for environmental storytelling and gameplay.

### Technical Implementation

**Verlet chain** (not Jolt soft body -- more controllable for gameplay):

```cpp
struct CRope {
    uint16_t    segment_count = 16;
    float       segment_length = 0.5f;  // meters per segment
    float       total_length;           // segment_count * segment_length
    float       stiffness = 0.9f;       // constraint compliance (0=loose, 1=rigid)
    float       damping   = 0.05f;
    float       gravity_scale = 1.0f;
    bool        collides_with_world = true;
    float       collision_radius = 0.02f;  // per-segment sphere radius

    // Attachment
    NodeHandle  anchor_start;           // fixed to this node (or world)
    NodeHandle  anchor_end;             // fixed to this node (or free)
    bool        start_fixed = true;
    bool        end_fixed   = false;

    // Runtime (not serialized)
    std::vector<Vec3> positions;        // segment positions
    std::vector<Vec3> prev_positions;   // for Verlet integration
};
```

**Simulation** (runs as a separate ECS system, after physics step):

```cpp
void RopeSystem::update(float dt, entt::registry& reg, PhysicsWorld& physics) {
    for (auto [e, rope] : reg.view<CRope>().each()) {
        // 1. Apply gravity
        for (int i = 0; i < rope.segment_count; ++i) {
            if (is_fixed(rope, i)) continue;
            Vec3 vel = rope.positions[i] - rope.prev_positions[i];
            rope.prev_positions[i] = rope.positions[i];
            rope.positions[i] += vel * (1.f - rope.damping)
                               + Vec3(0, -9.81f * rope.gravity_scale, 0) * dt * dt;
        }

        // 2. Distance constraints (Jakobsen method, N iterations)
        for (int iter = 0; iter < 8; ++iter) {
            for (int i = 0; i < rope.segment_count - 1; ++i) {
                Vec3 delta = rope.positions[i + 1] - rope.positions[i];
                float dist = glm::length(delta);
                float error = dist - rope.segment_length;
                Vec3 correction = glm::normalize(delta) * error * 0.5f * rope.stiffness;
                if (!is_fixed(rope, i))     rope.positions[i]     += correction;
                if (!is_fixed(rope, i + 1)) rope.positions[i + 1] -= correction;
            }
        }

        // 3. Collision with world (sphere cast per segment)
        if (rope.collides_with_world) {
            for (int i = 0; i < rope.segment_count; ++i) {
                auto hit = physics.shape_cast_sphere(
                    rope.collision_radius, rope.prev_positions[i],
                    rope.positions[i] - rope.prev_positions[i],
                    glm::length(rope.positions[i] - rope.prev_positions[i])
                );
                if (hit.hit) {
                    rope.positions[i] = hit.point + hit.normal * rope.collision_radius;
                }
            }
        }
    }
}
```

**Rendering**: Line renderer (tube mesh generated from rope positions) or instanced cylinder segments.

**Scene format**:
```ini
[node id=40 type=Node3D name="HangingRope" parent=1]
rope              = true
rope_segments     = 20
rope_segment_len  = 0.3
rope_stiffness    = 0.85
rope_anchor_start = "CeilingHook"
rope_anchor_end   = "Lantern"
rope_end_fixed    = true
```

### Impact if Absent

- No rope, cable, or chain physics.
- No grappling hooks, suspension bridges, swinging objects.
- Environments feel static.

### Complexity: **M** (1 week)

### Dependencies: Physics System (for collision queries)

---

## Gap 12: Soft Body Simulation

### Why Critical

Soft bodies are used for: jelly, deformable obstacles, pressure-based objects (tires, balloons, cushions), and organic shapes. Unreal 5 has Chaos SoftBody (limited but present). Unity has third-party (Obi SoftBody). Jolt Physics 5.x has built-in soft body support via `JPH::SoftBodyCreationSettings`.

The current spec lists "Soft bodies (Phase 4)" as out-of-scope, but Phase 4 documentation does not mention it at all. It is effectively unplanned.

### Technical Implementation

Jolt's soft body system uses a position-based dynamics (PBD) solver with volume preservation. Integration is relatively straightforward:

```cpp
struct CSoftBody {
    StringId    mesh_id;
    float       compliance         = 0.0001f;  // inverse stiffness
    float       damping            = 0.0f;
    float       pressure           = 1.0f;     // internal pressure (for inflated objects)
    float       friction           = 0.5f;
    float       gravity_scale      = 1.0f;
    uint8_t     solver_iterations  = 8;
    bool        enable_collision   = true;     // collide with rigid bodies

    // Pinned vertices (kinematic vertices driven by animation)
    std::vector<uint32_t> pinned_vertices;

    // Runtime
    JPH::BodyID  jolt_body_id;
    JPH::SoftBodyMotionProperties* motion_props = nullptr;
};
```

**Creation**:
```cpp
JPH::SoftBodySharedSettings* shared = new JPH::SoftBodySharedSettings();
// Add vertices from mesh
for (auto& v : mesh.vertices) {
    JPH::SoftBodySharedSettings::Vertex sv;
    sv.mPosition = ToJolt(v.position);
    sv.mInvMass = 1.0f / vertex_mass;
    shared->mVertices.push_back(sv);
}
// Add edge constraints from mesh edges
for (auto& edge : mesh.edges) {
    shared->mEdgeConstraints.push_back({edge.v0, edge.v1, compliance});
}
// Volume constraint for pressure
shared->mVolumeConstraints.push_back({...});

JPH::SoftBodyCreationSettings creation(shared, position, rotation, layer);
creation.mPressure = comp.pressure;
BodyID id = body_interface.CreateAndAddSoftBody(creation, EActivation::Activate);
```

**Vertex readback for rendering**: Each frame after physics step, read `SoftBodyMotionProperties::GetVertex(i).mPosition` to update the dynamic vertex buffer.

### Impact if Absent

- No deformable objects (cushions, tires, organic blobs).
- Limits physics-based puzzle design.
- "Soft bodies (Phase 4)" is promised but unplanned.

### Complexity: **L** (2-3 weeks)

### Dependencies: Physics System, Renderer (dynamic vertex buffer update)

---

## Gap 13: Buoyancy System

### Why Critical

The terrain spec includes a water plane (`[water]` section). The physics spec has no awareness of water volumes. Without buoyancy, rigid bodies sink through water surfaces with no resistance. AAA games since Half-Life 2 have water physics with buoyancy, drag, and splash effects.

### Technical Implementation

```cpp
struct CWaterVolume {
    float       surface_height;        // world Y of water surface
    float       density = 1000.f;      // water density (kg/m^3)
    float       linear_drag  = 2.0f;   // submerged drag coefficient
    float       angular_drag = 1.0f;
    Vec3        flow_velocity = Vec3(0); // current (river flow)
};

struct CBuoyant {
    float       buoyancy_factor = 1.0f;  // 1.0 = neutrally buoyant at water density
    uint8_t     sample_points = 4;       // number of submersion test points
    float       splash_threshold = 2.0f; // velocity for splash particle
};
```

**Simulation** (per-frame system, after physics step):

```cpp
void BuoyancySystem::update(entt::registry& reg, PhysicsWorld& physics) {
    for (auto [e, rb, buoy, transform] : reg.view<CRigidBody, CBuoyant, CTransform>().each()) {
        // Find overlapping water volumes
        CWaterVolume* water = find_water_at(transform.position);
        if (!water) continue;

        // Calculate submerged fraction (approximate with sample points on AABB)
        AABB bounds = physics.get_body_bounds(rb.jolt_body_id);
        float submerged = calculate_submerged_fraction(bounds, water->surface_height);
        if (submerged <= 0.f) continue;

        // Archimedes' principle: F_buoyancy = rho * g * V_submerged
        float volume = bounds.volume();
        float buoyancy_force = water->density * 9.81f * volume * submerged * buoy.buoyancy_factor;

        Vec3 force_up = Vec3(0, buoyancy_force, 0);
        physics.body_interface().AddForce(rb.jolt_body_id, ToJolt(force_up));

        // Submerged drag
        Vec3 vel = ToVec3(physics.body_interface().GetLinearVelocity(rb.jolt_body_id));
        Vec3 drag = -vel * water->linear_drag * submerged;
        physics.body_interface().AddForce(rb.jolt_body_id, ToJolt(drag));

        // Water current
        Vec3 current_force = water->flow_velocity * water->density * submerged * 0.5f;
        physics.body_interface().AddForce(rb.jolt_body_id, ToJolt(current_force));
    }
}
```

**Integration with terrain water**: The terrain system's `[water]` section provides `height`. The buoyancy system reads this as a global water plane. Box-shaped water volumes allow rivers, pools, and enclosed water.

### Impact if Absent

- Objects sink through water with no physical response.
- No boat/ship physics possible.
- Water surfaces are purely visual -- breaks immersion.
- Terrain system's water feature is half-complete without physics interaction.

### Complexity: **M** (1 week)

### Dependencies: Physics System, Terrain System (water plane height)

---

## Gap 14: Fluid Simulation

### Why Critical

Fluid simulation covers: flowing water, blood pools, lava, pouring liquids, and volumetric fluid effects. Unreal has Niagara Fluid (GPU-based FLIP solver). Unity has third-party (Obi Fluid, Zibra). Fluid is a high-impact visual feature for AAA.

However, real-time fluid simulation is extremely expensive. For an engine at UzEngine's stage, a **particle-based SPH (Smoothed Particle Hydrodynamics) system** is the pragmatic approach.

### Technical Implementation

This can extend the existing particle system (`.uzfx`) rather than being a separate subsystem:

```ini
[uzfx version=1 name="WaterSplash"]
emitter_type      = sphere
sphere_radius     = 0.3
burst_count       = 500
emission_rate     = 0

; Fluid behavior module
[module.fluid]
type              = sph
particle_radius   = 0.05        ; smoothing radius
rest_density      = 1000.0      ; kg/m^3
viscosity         = 0.01
surface_tension   = 0.01
gravity           = [0, -9.81, 0]
collision         = rigid_body   ; particles collide with rigid bodies
max_particles     = 2048

; Rendering
render_mode       = metaball     ; metaball | billboard | mesh
```

**GPU implementation** (compute shader):

1. **Spatial hashing**: Hash particles into grid cells for neighbor queries.
2. **Density estimation**: For each particle, sum contributions from neighbors within smoothing radius.
3. **Pressure force**: Computed from density deviation from rest density (Tait equation).
4. **Viscosity force**: Laplacian of velocity field.
5. **Integration**: Semi-implicit Euler.
6. **Boundary handling**: Particle-rigid body collision via signed distance fields.

**Performance budget**: Limit to 4096 particles per fluid emitter, max 2 fluid emitters per scene. This keeps the compute shader within 2ms at 1080p.

### Impact if Absent

- No flowing liquids, blood effects, or lava.
- Particle system limited to dry visual effects.
- Major visual gap vs. competitor engines.

### Complexity: **XL** (4+ weeks)

### Dependencies: Particle System (Phase 3), Physics System, Compute Shader infrastructure

---

## Gap 15: Physics Debug Profiler Integration

### Why Critical

The current spec has debug draw (wireframes, contact points, velocity arrows) but no **physics performance profiler**. AAA engines show: body count breakdown (active/sleeping/static), broadphase pair count, narrowphase test count, constraint solve time, island count, memory usage, CCD test count, and per-subsystem timing.

Without this, developers cannot diagnose physics performance problems. They see "physics takes 5ms" but not why.

### Technical Implementation

```cpp
struct PhysicsProfileStats {
    // Body statistics
    uint32_t total_bodies;
    uint32_t active_bodies;
    uint32_t sleeping_bodies;
    uint32_t static_bodies;
    uint32_t kinematic_bodies;

    // Collision statistics
    uint32_t broadphase_pairs;
    uint32_t narrowphase_tests;
    uint32_t active_contacts;
    uint32_t ccd_tests;

    // Constraint statistics
    uint32_t active_constraints;
    uint32_t island_count;

    // Timing (microseconds)
    float    broadphase_time_us;
    float    narrowphase_time_us;
    float    solve_time_us;
    float    integrate_time_us;
    float    total_step_time_us;

    // Memory
    size_t   temp_allocator_peak_bytes;
    size_t   body_memory_bytes;
    size_t   constraint_memory_bytes;
};
```

**Collection**: Jolt provides hooks via `PhysicsSystem::GetBodyStats()` and timing can be captured via Tracy zones around each step phase. Alternatively, Jolt's `ProfileMeasurement` callback captures per-phase timings.

**IPC command** (editor integration):
```json
{ "fn": "physics.get_profile", "args": {} }
// Returns PhysicsProfileStats as JSON
```

**VSCode panel**: A "Physics Profiler" webview panel showing:
- Bar chart of step phases (broadphase, narrowphase, solve, integrate).
- Body count pie chart (active/sleeping/static).
- Time series graph of step time over last 300 frames.
- Alert when step time exceeds budget (configurable, default 4ms).

**ImGui overlay** (in-engine debug): Show stats in the debug overlay (F12 toggle), same data as the IPC panel.

### Impact if Absent

- No way to diagnose why physics is slow.
- Developers randomly reduce body count or disable features instead of targeted optimization.
- "Performance: stable 60 FPS with 1000+ bodies" (DoD from ROADMAP) is unverifiable.

### Complexity: **M** (1 week)

### Dependencies: Physics System, Debug & Profiling (Phase 1), Editor Panels (Phase 1)

---

## Gap 16: Physics Regions / Substepping Configuration

### Why Critical

The current spec uses a global 60Hz fixed timestep with a max of 4 substeps. AAA games often need:

1. **Per-region physics settings**: Different gravity, different timestep rate, different collision matrix. Example: a zero-gravity room, an underwater area with reduced gravity, a slow-motion zone.
2. **Configurable substepping per body**: Fast-moving objects (bullets) need more substeps than slow objects (crates).
3. **Variable timestep support**: Some games run physics at 120Hz for fighting games or 30Hz for strategy games.
4. **Time dilation**: Slow motion effects that affect physics but not UI.

### Technical Implementation

**Physics regions (volumes with custom settings)**:

```cpp
struct CPhysicsRegion {
    Vec3        gravity_override;          // region-local gravity
    bool        use_gravity_override = false;
    float       time_scale = 1.0f;         // 0.5 = half speed, 2.0 = double speed
    float       damping_override = -1.f;   // -1 = use body default
    uint16_t    layer_mask_override = 0;   // optional collision matrix override
};
```

When a body enters a physics region (detected via trigger overlap), the `PhysicsRegionSystem` applies the override:

```cpp
void PhysicsRegionSystem::on_trigger_enter(NodeHandle body, NodeHandle region) {
    auto& region_comp = reg.get<CPhysicsRegion>(region);
    auto& rb = reg.get<CRigidBody>(body);

    if (region_comp.use_gravity_override) {
        body_interface.SetGravityFactor(rb.jolt_body_id, 0.f);
        // Apply region gravity manually each step
        per_body_gravity[rb.jolt_body_id] = region_comp.gravity_override;
    }
}
```

**Global time dilation**:
```cpp
struct PhysicsWorld {
    float time_scale = 1.0f;  // global time dilation

    void Step(float dt) {
        float scaled_dt = dt * time_scale;
        accumulator += scaled_dt;
        while (accumulator >= PHYSICS_DT) {
            jph->Update(PHYSICS_DT, ...);
            accumulator -= PHYSICS_DT;
        }
    }
};
```

**Lua API**:
```lua
ctx.physics:set_time_scale(0.2)   -- slow motion
ctx.physics:set_gravity(Vec3(0, -4.9, 0))  -- half gravity
```

**Scene format**:
```ini
[node id=50 type=Node3D name="ZeroGravRoom" parent=1]
collider              = "box"
collider_half_extents = [10, 5, 10]
trigger               = true
physics_region        = true
region_gravity        = [0, 0, 0]
region_time_scale     = 0.5
```

### Impact if Absent

- No gravity zones, no slow motion, no underwater physics.
- All objects share identical physics parameters -- unrealistic.
- Time manipulation mechanics (common in action games) are impossible.

### Complexity: **M** (1 week)

### Dependencies: Physics System, Trigger system (from Phase 3 core)

---

## Summary Table

| # | Feature | Complexity | Phase | Blocks |
|---|---------|-----------|-------|--------|
| 1 | Physics Materials | S | 3 | Surface audio/VFX, realistic friction |
| 2 | Breakable Joints & Constraints | L | 3 | Doors, levers, mechanical objects, destruction |
| 3 | Destructible Meshes / Fracture | XL | 4 | Environmental destruction, breakable walls |
| 4 | Vehicle Physics | L | 4 | Racing games, open-world vehicles |
| 5 | CCD Configuration (per-body) | S | 3 | Bullet tunneling, fast projectiles |
| 6 | Batch Queries & Sweep Tests | M | 3 | Thick projectiles, AI LOD, camera collision |
| 7 | Cloth Simulation | L | 4 | Capes, flags, curtains, fabric |
| 8 | Deterministic Physics | XL | 5 | Rollback netcode, multiplayer |
| 9 | Multi-Threading Strategy | M | 3 | Performance, frame budget |
| 10 | Physics LOD | M | 4 | Open-world performance scaling |
| 11 | Cable / Rope / Chain | M | 3-4 | Grappling hooks, bridges, swinging |
| 12 | Soft Body Simulation | L | 4 | Deformable objects, pressure bodies |
| 13 | Buoyancy System | M | 4 | Water physics, boats |
| 14 | Fluid Simulation (SPH) | XL | 5+ | Flowing liquids, lava, blood |
| 15 | Physics Debug Profiler | M | 3 | Performance diagnosis |
| 16 | Physics Regions / Time Dilation | M | 3-4 | Gravity zones, slow motion, underwater |

---

## Recommended Phase Assignment

### Phase 3 Additions (ship with core physics)

These should be part of the initial Jolt integration to avoid costly refactors later:

1. **Physics Materials** (S) -- foundational; changing from inline values to material assets later requires rewriting every body creation path.
2. **CCD Configuration** (S) -- trivial to expose Jolt's existing `MotionQuality` setting; prevents tunneling bugs from day one.
3. **Batch Queries & Sweep Tests** (M) -- essential for character controller, camera collision (SpringArm), and AI.
4. **Multi-Threading Strategy** (M) -- must be designed at integration time; retrofitting thread config is error-prone.
5. **Physics Debug Profiler** (M) -- without this, performance tuning during Phase 3 development is blind.
6. **Physics Regions / Time Dilation** (M) -- region triggers are already in the spec; adding gravity override is minimal extra work.

### Phase 4 Additions (advanced features)

7. **Breakable Joints & Constraints** (L) -- general constraint system needed before destruction.
8. **Destructible Meshes / Fracture** (XL) -- depends on constraints, materials, asset pipeline.
9. **Vehicle Physics** (L) -- Jolt has built-in vehicle support; integration is well-scoped.
10. **Cloth Simulation** (L) -- depends on skeletal mesh being stable (Phase 3).
11. **Physics LOD** (M) -- needed for open-world terrain scenes in Phase 4.
12. **Cable / Rope / Chain** (M) -- Verlet simulation is self-contained.
13. **Soft Body Simulation** (L) -- Jolt has built-in support; depends on dynamic vertex buffer pipeline.
14. **Buoyancy System** (M) -- needed once terrain water is implemented (Phase 4).

### Phase 5 Additions (shipping multiplayer)

15. **Deterministic Physics** (XL) -- only needed when netcode ships.
16. **Fluid Simulation (SPH)** (XL) -- highest risk, lowest priority; can ship without it.

---

## Total Estimated Effort

| Phase | Features | Effort |
|-------|----------|--------|
| Phase 3 additions | 6 features | ~5-6 weeks |
| Phase 4 additions | 8 features | ~14-18 weeks |
| Phase 5 additions | 2 features | ~8-10 weeks |
| **Total** | **16 features** | **~27-34 weeks** |

This is a significant investment but proportional to the gap between "indie physics" and "AAA physics". The Phase 3 additions (5-6 weeks) are the most critical and should be incorporated into the existing Phase 3 spec immediately to avoid architectural debt.

---

## References

- Jolt Physics Documentation: https://jrouwe.github.io/JoltPhysics/
- Jolt Vehicle Constraint: `JPH::VehicleConstraint`, `JPH::WheeledVehicleController`
- Jolt Soft Body: `JPH::SoftBodyCreationSettings`, `JPH::SoftBodySharedSettings`
- Unreal Chaos Destruction: https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-destruction
- Unity Physics Material: https://docs.unity3d.com/Manual/class-PhysicsMaterial.html
- NVIDIA APEX Cloth (historical): https://developer.nvidia.com/apex
- Recast/Detour: https://github.com/recastnavigation/recastnavigation
- voro++ (Voronoi): https://math.lbl.gov/voro++/
