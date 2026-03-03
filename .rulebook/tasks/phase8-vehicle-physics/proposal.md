# Proposal: phase3-vehicle-physics

## Why

Racing games, open-world traversal, and military games require vehicle physics: cars, trucks, motorcycles, boats. Hand-rolling vehicle physics in Lua is impractical — correct wheel suspension, drivetrain simulation, and tire friction require the kind of numerical integration that Jolt Physics provides natively via `JPH::VehicleConstraint`. Exposing this as `CVehicle` and `CWheelCollider` ECS components gives designers configurable vehicle handling parameters while Lua scripts drive controls (throttle, brake, steer).

## What Changes

- **`ecs/components.h`** — `CVehicle` (body, drivetrain config), `CWheelCollider` (per-wheel suspension, friction)
- **`src/engine/physics/vehicle_system.h/cpp`** — `VehicleSystem` ECS system: creates `JPH::VehicleConstraint`, drives simulation
- **`src/engine/scripting/lua_binding.cpp`** — `ctx.vehicle:set_throttle(0–1)`, `ctx.vehicle:set_brake(0–1)`, `ctx.vehicle:set_steer(angle)`, `ctx.vehicle:get_speed() -> float`
- **Tests** — vehicle creation, throttle/brake/steer response, wheel suspension, collision

## Impact

- Affected specs: `docs/specs/physics-system.md` (vehicle section)
- Dependencies: `phase3-physics-system` (Jolt Physics, `CRigidBody`)
- Breaking change: NO — additive; existing physics unaffected
- User benefit: arcade to simulation vehicle handling without physics code; AI can tune vehicle parameters in data
