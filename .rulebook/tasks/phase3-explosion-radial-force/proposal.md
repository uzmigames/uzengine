# Proposal: phase3-explosion-radial-force

## Why
There is no radial impulse or force system in the engine. Explosions, shockwaves, and magnetic repulsion all require applying outward force to every rigid body within a radius, but the current API only allows per-body `add_impulse()` calls. Manual iteration over all bodies is O(N) across the entire simulation without leveraging the Jolt broad phase spatial index, making it both expensive and inconvenient to implement in game code. Without a built-in API, every game that needs explosions must re-implement the same broad-phase sphere query pattern in Lua.

## What Changes
- Add `PhysicsWorld::apply_radial_impulse(Vec3 origin, float radius, float strength, RadialFalloff falloff)` where `RadialFalloff` is an enum: `Linear`, `InverseSquare`, `Constant`
- Internally uses Jolt's `BroadPhaseQuery::CollectTransientContacts` with a sphere shape to gather candidate bodies without iterating all bodies
- For each body in range, compute impulse direction as `(body_center - origin).normalized()` and magnitude as `strength * falloff_factor(distance, radius)`
- Add `PhysicsWorld::apply_radial_force(origin, radius, strength, falloff, duration_ms)` variant that applies a sustained force over N physics steps
- Lua API: `ctx.physics:explosion(pos, radius, force, falloff)` where falloff defaults to `"linear"`
- Debug visualization: draws a wire sphere at origin with radius for one frame when called (respects `r_debug_physics` cvar)
- Expose `apply_radial_impulse` as an IPC MCP tool for scene testing from the editor

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/physics/physics_world.cpp`, `src/physics/physics_world.h`, `src/scripting/lua_physics.cpp`, `src/ipc/ipc_handlers.cpp`
- Breaking change: NO
- User benefit: Explosion, shockwave, and magnetic field effects are a single Lua call with no boilerplate. The broad-phase-accelerated query ensures even large explosions affecting 50+ bodies complete in under 0.1ms.
