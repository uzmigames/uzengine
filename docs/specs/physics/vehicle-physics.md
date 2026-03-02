# Vehicle Physics System Spec

> **Status**: Planned (Phase 4)
> **Library**: Jolt Physics (VehicleConstraint)
> **Owner**: `game.dll` (C++ vehicle simulation)
> **Lua API**: Available Phase 4+ via `ctx.vehicle` and `ctx.input` for steering/throttle

---

## Overview

The Vehicle Physics System provides realistic vehicle simulation for cars, trucks, motorcycles, and other wheeled vehicles. Built on Jolt Physics' `VehicleConstraint`, it handles multi-wheel dynamics, suspension, drivetrain simulation, steering geometry, aerodynamics, and terrain interaction. All vehicle control is exposed to Lua scripts for gameplay logic.

**Key principles**:
- Single `VehicleManager` singleton in `game.dll` managing all active vehicles
- Each vehicle is an ECS entity with `CVehicle` component + individual wheel colliders
- Vehicles are integrated with the fixed-step physics loop (60 Hz)
- Steering uses Ackermann geometry for realistic turning behavior
- Braking, traction, and weight transfer are physically simulated
- IPC debug visualization shows wheel contact points, suspension compression, and force vectors

---

## Architecture

### Vehicle Component Structure

```cpp
// Core vehicle component (one per vehicle entity)
struct CVehicle {
    // Vehicle identity
    StringId            vehicle_type;          // "car", "truck", "motorcycle", etc.

    // Mass & geometry
    float               mass;                  // kg
    Vec3                center_of_mass;        // local offset from node origin
    float               wheelbase;             // distance from front to rear axle (m)
    float               track_width;           // distance between left and right wheels (m)

    // Jolt integration
    JPH::VehicleConstraint* jolt_vehicle;    // Jolt's vehicle simulation
    std::vector<uint32_t>   wheel_entity_ids; // Child entities for each wheel

    // State (read-only; updated each physics frame)
    float               speed;                 // m/s (forward speed along vehicle axis)
    Vec3                velocity;              // world-space linear velocity
    float               angular_velocity_y;    // rotation around vertical axis (rad/s)
    bool                is_grounded;           // true if any wheel touches ground

    // Control inputs (set by Lua each frame)
    float               throttle;              // 0.0–1.0 (0 = no throttle, 1 = full)
    float               brake_force;           // 0.0–1.0 (0 = no braking, 1 = max)
    float               handbrake_force;       // 0.0–1.0 (independent from foot brake)
    float               steering_angle;        // radians (positive = left, negative = right)
};

// Wheel collider component (one per wheel)
struct CWheelCollider {
    // Wheel geometry
    float               radius;                // wheel radius (m)
    float               width;                 // tire width (m)

    // Suspension
    float               suspension_travel;    // max compression distance (m)
    float               spring_stiffness;     // N/m
    float               spring_damping;       // N·s/m
    float               rest_length;          // natural length of suspension

    // Tire friction
    float               lateral_friction;     // grip in lateral direction
    float               longitudinal_friction; // grip in forward/backward direction

    // Physics state (read-only; updated each frame)
    bool                is_touching_ground;   // current contact state
    Vec3                contact_point;        // world-space ground contact position
    Vec3                contact_normal;       // surface normal at contact
    float               suspension_compression; // 0.0–1.0 (0 = full extension, 1 = fully compressed)
    float               wheel_angular_velocity; // rad/s (spin speed)
    float               slip_ratio;           // longitudinal slip (0 = no slip, >1 = spinning/locked)
    float               slip_angle;           // radians (lateral slip angle)
};

// Drivetrain configuration
struct Drivetrain {
    enum class Mode { FWD, RWD, AWD };
    Mode                mode;                 // Front/Rear/All-Wheel-Drive

    float               torque;               // max engine torque (N·m)
    float               max_rpm;              // engine redline (rpm)
    float               inertia;              // engine inertia (kg·m²)

    // Gear ratios (index 0 is reverse, 1+ are forward gears)
    std::vector<float>  gear_ratios;          // e.g., {-3.5, 3.2, 2.0, 1.5, 1.0}
    float               differential_ratio;   // final drive (e.g., 3.73)

    // Torque curve: at given RPM, output torque = interpolate(rpm, torque_curve)
    std::vector<std::pair<float, float>> torque_curve; // (rpm, torque) points

    // Brake system
    float               brake_torque_front;   // max brake torque on front axle (N·m)
    float               brake_torque_rear;    // max brake torque on rear axle (N·m)
    float               brake_bias;           // 0.0–1.0 (0 = all rear, 1 = all front)
};

// Steering configuration
struct SteeringConfig {
    float               max_steer_angle;      // radians (±π/6 = ±30° typical)
    float               steer_rate;           // radians/second (how fast steering response)
    float               center_return_rate;   // radians/second (auto-center speed)

    // Ackermann correction (reduces understeer/oversteer)
    bool                use_ackermann;        // true for cars, false for tank treads
    float               ackermann_correction; // 0.0–1.0 (0 = no correction, 1 = perfect Ackermann)
};

// Aerodynamics configuration
struct Aerodynamics {
    float               drag_coefficient;     // Cd (0.25–0.5 typical for cars)
    float               frontal_area;         // m² (reference area for drag)

    float               downforce_coefficient; // CL (negative = downforce)
    float               downforce_area;      // m² (reference area)

    // Wind resistance: force = (0.5 * air_density * speed²) * coeff * area
    float               air_density;          // kg/m³ (default 1.225)
};
```

### Vehicle Manager

```cpp
struct VehicleManager {
    JPH::PhysicsSystem* physics_system;       // Reference to main physics world
    Arena*              arena;

    // Active vehicles indexed by entity ID
    std::unordered_map<uint32_t, CVehicle*> vehicles;

    // Step vehicles at physics framerate (60 Hz)
    void step(float dt);

    // Create vehicle with config
    uint32_t create_vehicle(const VehicleConfig& config);

    // Set control inputs (called from Lua each frame)
    void set_vehicle_input(uint32_t entity_id, float throttle, float brake,
                          float handbrake, float steering);

    // Query vehicle state (read-only)
    const CVehicle* get_vehicle(uint32_t entity_id) const;
};
```

### Integration with ECS

Vehicles are created as scene nodes with special properties:

```ini
[node id=100 type=Node3D name="Car" parent=1]
position = [0, 0, 0]
rotation = [0, 0, 0]
vehicle = "sedan"                    ; references a .uzvehicle config
vehicle_mass = 1500.0                ; kg (overrides config if set)
vehicle_center_of_mass = [0, -0.3, 0] ; local offset
```

Each wheel is a child node:

```ini
[node id=101 type=Node3D name="WheelFL" parent=100]
position = [0.8, 0.4, 1.2]            ; local to car
wheel_radius = 0.35
wheel_width = 0.2
suspension_travel = 0.2
suspension_stiffness = 25000.0         ; N/m
suspension_damping = 2000.0            ; N·s/m
```

---

## Configuration Files (`.uzvehicle`)

Vehicles are defined in INI-like format, stored in `res://vehicles/*.uzvehicle`.

### Example: Sports Car

```ini
[Vehicle]
name = sports_car
vehicle_type = car
mass = 1300
wheelbase = 2.6
track_width = 1.6
center_of_mass = [0, -0.35, 0]

[Wheels]
; All 4 wheels identical for this example
radius = 0.335
width = 0.22
suspension_travel = 0.15
spring_stiffness = 30000
spring_damping = 2500
lateral_friction = 1.2
longitudinal_friction = 1.1

[Drivetrain]
mode = RWD                            ; Rear-wheel drive
torque = 400.0                        ; N·m peak
max_rpm = 7000
gear_ratios = [-3.5, 3.5, 2.1, 1.4, 1.0, 0.8]
differential_ratio = 3.73
brake_torque_front = 5000
brake_torque_rear = 3000
brake_bias = 0.65                     ; 65% front, 35% rear

; Torque curve (rpm → torque)
[Drivetrain.TorqueCurve]
0 = 0
1000 = 200
2000 = 350
4000 = 400
5000 = 380
7000 = 200                            ; drops at redline

[Steering]
max_steer_angle = 0.52                ; ~30°
steer_rate = 3.0                      ; radians/second
center_return_rate = 2.0
use_ackermann = true
ackermann_correction = 0.8

[Aerodynamics]
drag_coefficient = 0.31
frontal_area = 2.2
downforce_coefficient = -0.15
downforce_area = 2.2
```

### Example: Heavy Truck

```ini
[Vehicle]
name = cargo_truck
vehicle_type = truck
mass = 8000
wheelbase = 3.5
track_width = 2.0
center_of_mass = [0, 0.0, 0.5]       ; higher COM, shifted back for trailer

[Wheels]
radius = 0.45
width = 0.35
suspension_travel = 0.25
spring_stiffness = 60000
spring_damping = 5000
lateral_friction = 0.9
longitudinal_friction = 1.0

[Drivetrain]
mode = RWD
torque = 1200.0                       ; lots of torque for weight
max_rpm = 2500                        ; lower RPM = more low-end power
gear_ratios = [-4.0, 4.5, 3.2, 2.4, 1.8, 1.2, 1.0]
differential_ratio = 4.88
brake_torque_front = 10000
brake_torque_rear = 8000
brake_bias = 0.5

[Drivetrain.TorqueCurve]
0 = 0
500 = 400
1000 = 900
1500 = 1200
2000 = 1100
2500 = 800

[Steering]
max_steer_angle = 0.35                ; ~20° (less agile)
steer_rate = 1.5                      ; slower response
center_return_rate = 1.0
use_ackermann = false                 ; trucks often don't use Ackermann
ackermann_correction = 0.3

[Aerodynamics]
drag_coefficient = 0.6                ; high drag (boxy)
frontal_area = 5.0                    ; large frontal area
downforce_coefficient = 0.0           ; no downforce
downforce_area = 5.0
```

---

## Jolt Physics Integration

### VehicleConstraint Setup

Jolt's `VehicleConstraint` manages multi-wheel vehicle dynamics. The engine wraps it with our ECS layer:

```cpp
void create_jolt_vehicle(CVehicle& vehicle, const VehicleConfig& cfg) {
    // Create vehicle constraint
    JPH::VehicleConstraintSettings constraint_settings;
    constraint_settings.mDriveConfig = /* ... drivetrain setup ... */;
    constraint_settings.mWheels.resize(4);  // 4 wheels for car

    for (int i = 0; i < 4; ++i) {
        JPH::WheelSettings& wheel = constraint_settings.mWheels[i];
        wheel.mRadius = cfg.wheels[i].radius;
        wheel.mWidth = cfg.wheels[i].width;
        wheel.mSuspensionSpring.mFrequency = /* compute from stiffness/damping */;
        wheel.mSuspensionSpring.mDamping = cfg.wheels[i].spring_damping;
        wheel.mLongitudinalFriction = cfg.wheels[i].longitudinal_friction;
        wheel.mLateralFriction = cfg.wheels[i].lateral_friction;
    }

    vehicle.jolt_vehicle = constraint_settings.Create(vehicle_body);
}
```

### Wheel Contact Detection

Jolt provides per-wheel contact information:

```cpp
void update_wheel_contacts(CVehicle& vehicle) {
    JPH::VehicleConstraint* constraint = vehicle.jolt_vehicle;

    for (size_t i = 0; i < vehicle.wheel_entity_ids.size(); ++i) {
        const JPH::WheelSettings& wheel = constraint->GetWheelSettings(i);
        const JPH::Vehicle::WheelState& wheel_state = constraint->GetWheelState(i);

        CWheelCollider& wheel_collider = get_wheel_collider(vehicle.wheel_entity_ids[i]);
        wheel_collider.is_touching_ground = wheel_state.mContactNode != nullptr;
        wheel_collider.contact_point = wheel_state.mContactPoint;
        wheel_collider.contact_normal = wheel_state.mContactNormal;
        wheel_collider.suspension_compression = wheel_state.mSuspensionLength / wheel.mSuspensionTravel;
        wheel_collider.slip_ratio = wheel_state.mLongitudinalSlip;
        wheel_collider.slip_angle = wheel_state.mLateralSlip;
        wheel_collider.wheel_angular_velocity = wheel_state.mAngularVelocity;
    }
}
```

### Force Application

Each physics frame, the vehicle applies forces based on control inputs:

```cpp
void apply_vehicle_forces(CVehicle& vehicle, float dt) {
    // Engine torque from throttle + current gear
    float engine_torque = calculate_engine_torque(vehicle.throttle);

    // Apply to driven wheels (depends on drivetrain mode)
    apply_drivetrain_torque(vehicle, engine_torque);

    // Brake torque (foot brake + handbrake, with bias)
    float front_brake = (vehicle.brake_force * vehicle.drivetrain.brake_bias);
    float rear_brake = (vehicle.brake_force * (1.0f - vehicle.drivetrain.brake_bias));
    apply_brake_torque(vehicle, 0, front_brake);  // Front wheels
    apply_brake_torque(vehicle, 1, rear_brake);   // Rear wheels

    // Handbrake (independent, usually rear)
    apply_brake_torque(vehicle, 1, vehicle.handbrake_force);

    // Aerodynamic drag & downforce
    float speed = vehicle.velocity.length();
    float drag_force = 0.5f * vehicle.aero.air_density * speed * speed * vehicle.aero.drag_coefficient * vehicle.aero.frontal_area;
    vehicle_body->AddForce(-vehicle.velocity.normalized() * drag_force);

    float downforce = 0.5f * vehicle.aero.air_density * speed * speed * vehicle.aero.downforce_coefficient * vehicle.aero.downforce_area;
    if (downforce < 0) {  // negative = downforce
        vehicle_body->AddForce(Vec3(0, downforce, 0));
    }
}
```

---

## Lua Vehicle API

All vehicle control is exposed via the `ctx.vehicle` namespace (injected by `game.dll`).

### Control Functions

#### `ctx.vehicle:accelerate(entity, throttle) → nil`

Set throttle (0–1). Call every frame from `on_update`.

```lua
function M:on_update(ctx, dt)
    local throttle = ctx.input:axis("Throttle")  -- -1 to +1 (negative = reverse)
    local throttle_clamped = math.max(0, throttle)  -- 0 to 1
    ctx.vehicle:accelerate(self, throttle_clamped)
end
```

#### `ctx.vehicle:brake(entity, force) → nil`

Set foot brake force (0–1).

```lua
if ctx.input:pressed("Brake") then
    ctx.vehicle:brake(self, 1.0)  -- full brake
else
    ctx.vehicle:brake(self, 0.0)
end
```

#### `ctx.vehicle:handbrake(entity, force) → nil`

Set handbrake force (0–1). Independent from foot brake.

```lua
if ctx.input:pressed("Handbrake") then
    ctx.vehicle:handbrake(self, 1.0)
else
    ctx.vehicle:handbrake(self, 0.0)
end
```

#### `ctx.vehicle:steer(entity, angle) → nil`

Set steering angle (radians). Positive = left, negative = right. The system clamps to `max_steer_angle` and applies `steer_rate` smoothing.

```lua
function M:on_update(ctx, dt)
    local steering_input = ctx.input:axis("Horizontal")  -- -1 to +1
    local target_angle = steering_input * self.max_steer_angle
    ctx.vehicle:steer(self, target_angle)
end
```

#### `ctx.vehicle:shift_gear(entity, gear_index) → bool`

Manually shift to a specific gear index (0 = reverse, 1+ = forward gears).

```lua
if ctx.input:pressed("GearUp") then
    self.current_gear = self.current_gear + 1
    ctx.vehicle:shift_gear(self, self.current_gear)
end
```

#### `ctx.vehicle:get_speed(entity) → float`

Return forward speed (m/s).

```lua
local speed = ctx.vehicle:get_speed(self)
if speed > 30.0 then
    self:activate_speed_warning(ctx)
end
```

#### `ctx.vehicle:get_wheel_contact(entity, wheel_index) → contact_table | nil`

Return contact info for a specific wheel (0–3 for 4-wheel vehicle). Returns `nil` if wheel not touching ground.

```lua
local contact = ctx.vehicle:get_wheel_contact(self, 0)  -- front-left wheel
if contact then
    ctx:log("FL wheel on: " .. contact.surface_type)
    ctx:log("Slip ratio: " .. contact.slip_ratio)
end
```

**`contact` table fields**:

| Field | Type | Description |
|-------|------|-------------|
| `point` | `Vec3` | World-space contact position |
| `normal` | `Vec3` | Surface normal |
| `slip_ratio` | `float` | Longitudinal slip (0 = no slip, >1 = spinning) |
| `slip_angle` | `float` | Lateral slip angle (radians) |
| `suspension_compression` | `float` | 0–1 (0 = full extension, 1 = fully compressed) |
| `is_ground` | `bool` | True if touching solid ground |
| `surface_type` | `string` | Physics material name (e.g., "concrete", "ice", "mud") |

#### `ctx.vehicle:get_suspension_compression(entity, wheel_index) → float`

Return suspension compression for a wheel (0–1).

```lua
for i = 0, 3 do
    local compression = ctx.vehicle:get_suspension_compression(self, i)
    -- Update visual suspension (animated suspension springs)
end
```

#### `ctx.vehicle:is_grounded(entity) → bool`

Return true if any wheel is touching ground.

```lua
if not ctx.vehicle:is_grounded(self) then
    self.airtime = self.airtime + dt
end
```

#### `ctx.vehicle:set_center_of_mass(entity, offset) → nil`

Adjust center of mass (local-space offset) at runtime. Useful for weight transfer simulation.

```lua
-- Shift COM forward when braking (for visual effect)
if brake_force > 0.5 then
    ctx.vehicle:set_center_of_mass(self, Vec3(0, -0.3, 0.2))
else
    ctx.vehicle:set_center_of_mass(self, Vec3(0, -0.3, 0))
end
```

---

## Vehicle Events

Vehicles dispatch events that scripts can subscribe to:

### `VehicleEvent` — ECS Event

```cpp
struct VehicleEvent {
    NodeHandle vehicle;         // The vehicle entity
    enum class Type {
        EngineStart,           // Engine started
        EngineStop,            // Engine stopped
        GearChanged,           // Shifted gears
        WheelSlip,             // Wheel exceeded slip threshold
        Collision,             // Vehicle hit something hard
        GroundLost,            // Left the ground
        GroundRegained,        // Touched ground again
    };
    Type       type;
    float      magnitude;      // Speed, slip amount, impact force, etc.
};
```

### Lua Callbacks

Vehicles with Lua scripts can define these methods to react to events:

#### `M:on_vehicle_slip(ctx, wheel_index, slip_ratio)`

Called when a wheel's longitudinal slip exceeds a threshold (e.g., 0.2).

```lua
function M:on_vehicle_slip(ctx, wheel_index, slip_ratio)
    if wheel_index < 2 then
        ctx:log("Front wheels slipping: " .. slip_ratio)
        self:play_tire_squeal(ctx, wheel_index)
    end
end
```

#### `M:on_vehicle_collision(ctx, other, impact_force)`

Called when vehicle collides with another object.

```lua
function M:on_vehicle_collision(ctx, other, impact_force)
    if impact_force > 100.0 then
        self:apply_damage(ctx, math.floor(impact_force / 50.0))
    end
end
```

---

## `.uzscene` Syntax

### Full Vehicle Example

```ini
[node id=100 type=Node3D name="PlayerCar" parent=1]
position = [0, 2, 0]
rotation = [0, 0, 0]
vehicle = res://vehicles/sports_car.uzvehicle
vehicle_mass = 1300.0

; Front-left wheel
[node id=101 type=Node3D name="WheelFL" parent=100]
position = [0.8, 0.35, 1.2]
wheel_radius = 0.335
wheel_width = 0.22
wheel_index = 0
suspension_travel = 0.15
suspension_stiffness = 30000
suspension_damping = 2500
lateral_friction = 1.2
longitudinal_friction = 1.1

; Front-right wheel
[node id=102 type=Node3D name="WheelFR" parent=100]
position = [-0.8, 0.35, 1.2]
wheel_radius = 0.335
wheel_width = 0.22
wheel_index = 1
suspension_travel = 0.15
suspension_stiffness = 30000
suspension_damping = 2500
lateral_friction = 1.2
longitudinal_friction = 1.1

; Rear-left wheel
[node id=103 type=Node3D name="WheelRL" parent=100]
position = [0.8, 0.35, -1.4]
wheel_radius = 0.335
wheel_width = 0.22
wheel_index = 2
suspension_travel = 0.15
suspension_stiffness = 28000
suspension_damping = 2500
lateral_friction = 1.2
longitudinal_friction = 1.1

; Rear-right wheel
[node id=104 type=Node3D name="WheelRR" parent=100]
position = [-0.8, 0.35, -1.4]
wheel_radius = 0.335
wheel_width = 0.22
wheel_index = 3
suspension_travel = 0.15
suspension_stiffness = 28000
suspension_damping = 2500
lateral_friction = 1.2
longitudinal_friction = 1.1

; Visual mesh for car body
[node id=105 type=Node3D name="Mesh" parent=100]
mesh = res://meshes/sports_car_body.uzmesh

; Script for player control
[node id=106 type=Node3D name="Script" parent=100]
script = res://scripts/car_controller.lua
```

### Minimal Vehicle

```ini
[node id=50 type=Node3D name="SimpleCart" parent=1]
vehicle = res://vehicles/cart.uzvehicle  ; auto-creates 4 wheels

[node id=51 type=Node3D name="Mesh" parent=50]
mesh = res://meshes/cart.uzmesh

[node id=52 type=Node3D name="Script" parent=50]
script = res://scripts/simple_cart_drive.lua
```

---

## Physics Debug Draw

### In Editor Mode

When physics debug draw is enabled, wheels and suspension are visualized:

```cpp
// game.dll — per-frame debug rendering
if (UZ_EDITOR) {
    for (auto& [entity_id, vehicle] : vehicle_manager.vehicles) {
        // Draw vehicle frame
        draw_box(vehicle->get_bounds(), {0.2f, 1.0f, 0.2f});

        // Draw wheels
        for (size_t i = 0; i < vehicle->wheels.size(); ++i) {
            const CWheelCollider& wheel = vehicle->wheels[i];
            Vec3 wheel_pos = vehicle->get_wheel_world_position(i);
            draw_sphere(wheel_pos, wheel.radius, {1.0f, 1.0f, 0.0f});

            // Draw suspension line (from attachment point to contact)
            Vec3 attachment = vehicle->get_suspension_attachment(i);
            draw_line(attachment, wheel_pos, {0.5f, 0.5f, 1.0f});

            // Draw suspension compression indicator
            float compression_pct = wheel.suspension_compression * 100.0f;
            draw_text(wheel_pos, "Compression: " + std::to_string((int)compression_pct) + "%");

            // Draw slip indicators
            if (wheel.slip_ratio > 0.2f) {
                draw_line(wheel_pos, wheel_pos + wheel.contact_normal * 0.5f, {1.0f, 0.0f, 0.0f});
            }
        }
    }
}
```

### IPC Commands

| Command | Args | Returns | Purpose |
|---------|------|---------|---------|
| `vehicle.debug_draw` | `{enable: bool}` | `{}` | Enable/disable vehicle visualization |
| `vehicle.list_vehicles` | — | `[{name, speed, gear, wheels: [...]}]` | List all active vehicles |
| `vehicle.get_vehicle_state` | `{entity_id: int}` | `{speed, steering, throttle, brake, ...}` | Inspect vehicle state |

---

## Common Patterns

### Basic Car Controller

```lua
local M = {}

function M:on_start(ctx)
    self.throttle = 0
    self.steering = 0
    self.brake = 0
    self.handbrake = 0
    self.max_steer_angle = 0.52
end

function M:on_update(ctx, dt)
    -- Read input
    self.throttle = math.max(0, ctx.input:axis("Throttle"))
    self.steering = ctx.input:axis("Horizontal") * self.max_steer_angle

    if ctx.input:pressed("Brake") then
        self.brake = 1.0
    else
        self.brake = 0.0
    end

    if ctx.input:pressed("Handbrake") then
        self.handbrake = 1.0
    else
        self.handbrake = 0.0
    end

    -- Apply to vehicle
    ctx.vehicle:accelerate(self, self.throttle)
    ctx.vehicle:steer(self, self.steering)
    ctx.vehicle:brake(self, self.brake)
    ctx.vehicle:handbrake(self, self.handbrake)

    -- Debug display
    local speed = ctx.vehicle:get_speed(self)
    ctx:log(string.format("Speed: %.1f m/s", speed))
end

return M
```

### Advanced: Drift & Powerslide

```lua
local M = {}

function M:on_start(ctx)
    self.is_drifting = false
    self.drift_timer = 0
    self.drift_threshold = 0.5  -- slip ratio threshold
    self.drift_duration = 0.2   -- minimum drift time for points
end

function M:on_update(ctx, dt)
    local steering = ctx.input:axis("Horizontal") * 0.52
    local throttle = ctx.input:axis("Throttle")

    ctx.vehicle:accelerate(self, math.max(0, throttle))
    ctx.vehicle:steer(self, steering)

    -- Check rear wheels for slip
    local rear_left_slip = ctx.vehicle:get_wheel_contact(self, 2)
    local rear_right_slip = ctx.vehicle:get_wheel_contact(self, 3)

    if rear_left_slip and rear_right_slip then
        local avg_slip = (rear_left_slip.slip_ratio + rear_right_slip.slip_ratio) / 2.0

        if avg_slip > self.drift_threshold then
            if not self.is_drifting then
                self.is_drifting = true
                self.drift_timer = 0
                self:play_drift_sound(ctx)
            end
            self.drift_timer = self.drift_timer + dt
        else
            if self.is_drifting and self.drift_timer > self.drift_duration then
                self:award_drift_points(ctx, self.drift_timer)
            end
            self.is_drifting = false
        end
    end
end

return M
```

---

## Performance Considerations

### Vehicle Count

- **Physics-only (no visuals)**: 20–50 vehicles @ 60 Hz
- **With visual wheels**: 10–20 vehicles @ 60 Hz
- **With ragdoll damage**: 5–10 vehicles @ 60 Hz

Constraint solving is O(n × wheels), so vehicles with fewer wheels (e.g., tricycles) are cheaper.

### Optimization Tips

1. **Reduce wheel count**: Use 2 wheels (motorcycle/bike) instead of 4 when possible
2. **Disable collision for off-road vehicles**: If vehicle is far from player, disable physics
3. **Use simpler suspension**: Spring + damping only, no secondary springs
4. **Batch vehicles**: Group vehicles by drivetrain type to amortize torque curve calculations

### LOD System

Vehicles beyond `vehicle_lod_distance` (default: 100m, configurable in `.uzproj`) use simplified suspension and reduced friction recalculation.

---

## Phase Implementation Details

### Phase 4 — Vehicle Physics (this phase)

**Deliverables**:
1. `CVehicle` and `CWheelCollider` ECS components
2. `VehicleManager` singleton in `game.dll`
3. `.uzvehicle` INI config format
4. Jolt `VehicleConstraint` integration
5. Steering (Ackermann geometry), drivetrain (FWD/RWD/AWD), brakes
6. Lua vehicle API: accelerate, brake, handbrake, steer, shift_gear, get_speed, get_wheel_contact
7. Vehicle events and Lua callbacks (on_vehicle_slip, on_vehicle_collision)
8. Physics debug visualization of wheels, suspension, slip indicators
9. IPC commands for editor vehicle inspection

**Out of scope (Phase 5+)**:
- Damage system (deformation, tire punctures)
- Transmission modeling (dynamic gear selection)
- Advanced aerodynamics (wings, spoilers, yaw stability)
- Off-road terrain interaction (mud, sand, water splash)
- Vehicle setup tuning (tuning shop gameplay)

---

## Related Documentation

- [specs/physics-system.md](physics-system.md) — Base physics system, rigid bodies, colliders
- [specs/input-system.md](input-system.md) — Input actions and mappings for vehicle control
- [specs/animation-system.md](animation-system.md) — Animated suspension, wheel rotation, body lean
- [guides/physics-advanced-guide.md](../guides/physics-advanced-guide.md) — Practical physics setup and debugging
- [engine/design-decisions.md](../engine/design-decisions.md) — Why Jolt + custom VehicleConstraint wrapper

---

## C++ Source Structure

```
src/engine/physics/
├── vehicle_system.h/cpp       — VehicleManager, CVehicle, CWheelCollider
├── vehicle_constraint.h/cpp   — Jolt VehicleConstraint wrapper
├── vehicle_dynamics.h/cpp     — Force application, torque curves, Ackermann steering
├── vehicle_config.h/cpp       — .uzvehicle parser, config loading
└── vehicle_debug_draw.h/cpp   — Debug visualization

src/engine/ecs/components.h    — CVehicle, CWheelCollider component definitions

src/engine/scripting/          — Lua bindings
└── lua_vehicle_binding.h/cpp  — ctx.vehicle namespace

game/
└── vehicle_callbacks.cpp       — on_vehicle_* Lua method dispatch
```

---

## Integration with Other Systems

### Animation System

Wheel rotation is driven by Jolt wheel angular velocity:

```lua
-- Wheel visuals updated in bone transform
wheel_bone.rotation.z = wheel_angular_velocity * dt
```

Suspension travel animates suspension spring bones.

### Audio System

Tire slip triggers squeal SFX; engine pitch varies with RPM.

### Collision System

Vehicles participate in normal collision events. Vehicle-specific `VehicleEvent` is fired separately.
