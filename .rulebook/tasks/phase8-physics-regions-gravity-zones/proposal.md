# Proposal: phase3-physics-regions-gravity-zones

## Why
There is no way to define areas with different gravity, such as a space station zero-G corridor, underwater sections with buoyancy drag, or a strong magnet zone that pulls metal objects. Jolt Physics applies a single global gravity vector to all bodies, with no concept of per-region overrides. Game genres including platformers, space games, and puzzle games all require per-region gravity vectors and fluid resistance. Without this system, developers must manually track every body entering a volume and apply compensating forces each tick, which is error-prone and difficult to serialize.

## What Changes
- Add `CPhysicsRegion` ECS component with fields: `gravity` (vec3), `linear_damping` (float), `angular_damping` (float), `priority` (int for nested region ordering), and a `trigger_shape` (box/sphere/capsule size)
- Each `CPhysicsRegion` creates a Jolt sensor body (non-colliding trigger) that fires `OnEnter`/`OnExit` contact events
- Add `PhysicsRegionSystem` that maintains a per-body `std::vector<RegionHandle>` sorted by priority; on overlap change, rebuilds the effective gravity/damping for that body using the highest-priority active region
- Apply per-body overrides in `PhysicsSystem::pre_step()` via `JPH::Body::SetGravityFactor()` and `JPH::MotionProperties::SetLinearDamping()`
- For nested regions the highest `priority` value wins; equal priorities blend linearly by overlap volume (approximated by distance to region center)
- Lua API: `ctx.physics:set_region_gravity(entity, vec3)` to dynamically change a region's gravity at runtime
- Editor visualization: region trigger volume shown as transparent colored box/sphere in scene view; Inspector shows gravity vector widget with direction arrow preview

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/scene-system.md`
- Affected code: `src/physics/physics_system.cpp`, `src/physics/physics_system.h`, `src/physics/physics_region_system.cpp` (new), `src/scene/components.h`, `src/scripting/lua_physics.cpp`
- Breaking change: NO
- User benefit: Zero-G rooms, underwater zones, and gravity-flip puzzles can be placed as simple volume components in the scene without any custom Lua force code. Priority-based nesting handles doorways between gravity zones automatically.
