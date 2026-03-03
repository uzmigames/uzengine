# Proposal: phase4-buoyancy-system

## Why
UzEngine has no water physics. Objects placed inside a water volume immediately fall through with no resistance or uplift. Buoyancy is essential for nautical games, flood simulations, swimming gameplay, and any scene with water interactables. The correct model requires integrating displaced water volume against gravity (Archimedes principle), which Jolt Physics does not provide out of the box. Without this system, an entire class of gameplay and environmental interaction is inaccessible.

## What Changes
- `CWaterVolume` ECS component: AABB bounds, water surface Y height, fluid density (kg/m3), wave amplitude and frequency for dynamic surface
- Buoyancy force computation per rigid body overlapping the water volume: compute submerged volume fraction, apply upward force `rho * g * V_submerged`
- Surface normal sampling from a sine-wave or texture-driven wave function so objects rock on animated water
- Linear and angular drag inside fluid: separate drag coefficients from the default Jolt air drag
- Splash event emission via `EventBus` when a rigid body crosses the water surface threshold (enter/exit)
- Lua API: `ctx.physics:is_submerged(entity)` returns submerged fraction 0-1; `on_water_enter` and `on_water_exit` Lua callbacks on `CWaterVolume`
- Multiple overlapping `CWaterVolume` components supported (e.g., pool inside a flooded room)
- Debug draw: water surface plane and per-body buoyancy force arrow in editor physics overlay

## Impact
- Affected specs: `docs/specs/physics-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/event-bus.md`
- Affected code: `src/physics/physics_system.cpp`, `src/physics/components/cwater_volume.h`, `src/scripting/lua_physics_bindings.cpp`, `src/scene/scene_system.cpp`
- Breaking change: NO
- User benefit: Developers can add water volumes to scenes and get realistic floating, sinking, and rocking behavior on all rigid body objects with a single ECS component and no physics code.
