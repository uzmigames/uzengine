## 1. Prerequisites

- [ ] 1.1 Confirm `phase3-physics-system` is complete — Jolt Physics initialized, `CRigidBody`, `CCollider` exist
- [ ] 1.2 Read `docs/specs/physics-system.md` — understand Jolt body creation, physics step, collision layers
- [ ] 1.3 Read Jolt Physics `VehicleConstraint` documentation — `JPH::VehicleConstraintSettings`, `WheelSettingsWV`, `VehicleController`
- [ ] 1.4 Decide on controller type: `WheeledVehicleController` (car/truck) vs. `MotorcycleController` (bike) — support both via `CVehicle.type` enum

## 2. CVehicle Component

- [ ] 2.1 Add `CVehicle` to `ecs/components.h`
- [ ] 2.2 Fields:
  - `type: VehicleType` enum (`Wheeled`, `Motorcycle`)
  - `mass_kg: float`
  - `max_torque_nm: float`
  - `max_brake_torque_nm: float`
  - `max_steer_angle_deg: float`
  - `transmission: TransmissionSettings` (`auto_gear: bool`, `gear_ratios[]`, `shift_up_rpm`, `shift_down_rpm`)
  - `differential: DifferentialType` (`FWD`/`RWD`/`AWD`)
  - `anti_roll_bar_stiffness: float`
  - `jolt_constraint: JPH::VehicleConstraint*` (runtime, null when not simulating)
- [ ] 2.3 Update `.uzscene` parser — serialize/deserialize `CVehicle` (excluding runtime fields)

## 3. CWheelCollider Component

- [ ] 3.1 Add `CWheelCollider` to `ecs/components.h`
- [ ] 3.2 Fields:
  - `parent_vehicle_entity: entt::entity`
  - `wheel_index: int` (0=FL, 1=FR, 2=RL, 3=RR)
  - `radius_m: float`
  - `width_m: float`
  - `suspension_max_length_m: float`
  - `suspension_spring_stiffness: float`
  - `suspension_spring_damping: float`
  - `lateral_friction: LateralFrictionCurve` — speed vs. friction coefficient
  - `longitudinal_friction: LongitudinalFrictionCurve` — slip ratio vs. friction
  - `driven: bool` — receives engine torque
  - `steered: bool` — receives steer input
  - `braked: bool` — receives brake torque
- [ ] 3.3 Update `.uzscene` parser

## 4. VehicleSystem ECS System

- [ ] 4.1 Create `src/engine/physics/vehicle_system.h/cpp` — `VehicleSystem : IEcsSystem`
- [ ] 4.2 Register with `SystemRegistry` in `PhysicsUpdate` group (before physics step)
- [ ] 4.3 Implement `VehicleSystem::on_register(registry)` — install `on_construct<CVehicle>` observer
- [ ] 4.4 On `CVehicle` construct: create `JPH::VehicleConstraintSettings` from component fields
  - Create `WheelSettingsWV` for each child entity with `CWheelCollider`
  - Create `JPH::WheeledVehicleController` or `JPH::MotorcycleController`
  - Add constraint to `JPH::PhysicsSystem`
  - Store `JPH::VehicleConstraint*` in `CVehicle.jolt_constraint`
- [ ] 4.5 On `CVehicle` destroy: remove constraint from Jolt physics
- [ ] 4.6 Implement `VehicleSystem::tick(registry, dt)`:
  - For each `CVehicle` entity: apply current throttle/brake/steer via `VehicleConstraint::SetDriverInput()`
  - Copy Jolt wheel transform output to `CWheelCollider` child entity `CTransform`
  - Update `CVehicle.current_speed_ms` from Jolt body linear velocity

## 5. Vehicle Input API

- [ ] 5.1 Add per-vehicle input state to `CVehicle`: `throttle: float [0,1]`, `brake: float [0,1]`, `steer: float [-1,1]`, `handbrake: bool`
- [ ] 5.2 Implement `VehicleSystem::set_throttle(entity, value)` — clamps and stores
- [ ] 5.3 Implement `VehicleSystem::set_brake(entity, value)`
- [ ] 5.4 Implement `VehicleSystem::set_steer(entity, normalized_angle)`
- [ ] 5.5 Implement `VehicleSystem::set_handbrake(entity, bool)`

## 6. Lua API

- [ ] 6.1 Implement `ctx.vehicle:set_throttle(value)` — applies to vehicle owned by current entity's `CVehicle`
- [ ] 6.2 Implement `ctx.vehicle:set_brake(value)`
- [ ] 6.3 Implement `ctx.vehicle:set_steer(value)` — -1=full left, 0=straight, 1=full right
- [ ] 6.4 Implement `ctx.vehicle:set_handbrake(bool)`
- [ ] 6.5 Implement `ctx.vehicle:get_speed() -> float` — m/s
- [ ] 6.6 Implement `ctx.vehicle:get_rpm() -> float`
- [ ] 6.7 Implement `ctx.vehicle:get_gear() -> int` — current gear (0=neutral, -1=reverse)
- [ ] 6.8 Implement `ctx.vehicle:is_grounded() -> bool` — true if any wheel has contact

## 7. Wheel Visual Update

- [ ] 7.1 After each Jolt step: copy `VehicleConstraint::GetWheel(i)->GetLocalTransform()` to child entity `CTransform`
- [ ] 7.2 Wheel entity has `CMeshRenderer` for wheel mesh — transform update drives visual rotation
- [ ] 7.3 Suspension compression: move wheel mesh along suspension axis by computed compression amount

## 8. Testing

- [ ] 8.1 Unit test: `CVehicle` construct → `JPH::VehicleConstraint` created with 4 wheel settings
- [ ] 8.2 Unit test: `set_throttle(1.0)` → `VehicleConstraint::SetDriverInput` called with correct values
- [ ] 8.3 Unit test: `CVehicle` destroy → constraint removed from Jolt physics
- [ ] 8.4 Integration test: place car on flat surface, throttle 1.0 → car accelerates forward (velocity increases over 1 second)
- [ ] 8.5 Integration test: throttle then brake 1.0 → car decelerates and stops
- [ ] 8.6 Integration test: steer at speed → car turns (heading changes)
- [ ] 8.7 Integration test: wheel child entities' `CTransform.rotation` matches Jolt wheel rotation
- [ ] 8.8 Integration test: `get_speed()` returns correct value matching Jolt body linear velocity magnitude

## 9. Documentation

- [ ] 9.1 Update `docs/specs/physics-system.md` — "Vehicle Physics" section with `CVehicle`/`CWheelCollider` fields and Lua API
- [ ] 9.2 Create `docs/specs/vehicle-physics.md` — full spec: drivetrain config, suspension curves, Lua control patterns
- [ ] 9.3 Update `docs/guides/physics-guide.md` — "Setting Up a Vehicle" section
