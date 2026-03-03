## 1. RadialFalloff Enum and API Declaration

- [ ] 1.1 Define `enum class RadialFalloff { Linear, InverseSquare, Constant }` in `src/physics/physics_world.h`
- [ ] 1.2 Add `void apply_radial_impulse(Vec3 origin, float radius, float strength, RadialFalloff falloff)` declaration to `PhysicsWorld` in `src/physics/physics_world.h`
- [ ] 1.3 Add `void apply_radial_force(Vec3 origin, float radius, float strength, RadialFalloff falloff, uint32_t duration_ms)` declaration to `PhysicsWorld` in `src/physics/physics_world.h`

## 2. Broad-Phase Sphere Query Implementation

- [ ] 2.1 Implement `PhysicsWorld::apply_radial_impulse()` in `src/physics/physics_world.cpp` using `JPH::BroadPhaseQuery` to collect all dynamic body IDs within the sphere of `radius` centered at `origin`
- [ ] 2.2 For each collected body, compute impulse direction as `(body_center - origin).normalized()`; skip bodies where `(body_center - origin).length() == 0`
- [ ] 2.3 Compute falloff magnitude: `Linear` = `strength * (1.0 - dist/radius)`, `InverseSquare` = `strength / (dist*dist + 1e-4)`, `Constant` = `strength`
- [ ] 2.4 Call `body.AddImpulse(direction * magnitude)` for each body in the query result
- [ ] 2.5 Implement `apply_radial_force()` as a time-limited variant: store active radial force descriptors in a `std::vector<RadialForceDescriptor>` updated in `PhysicsWorld::step()`; apply per-step force and remove descriptors whose elapsed time exceeds `duration_ms`

## 3. Lua API

- [ ] 3.1 Add `ctx.physics:explosion(pos, radius, force, falloff)` binding in `src/scripting/lua_physics.cpp`; `falloff` parameter defaults to `"linear"` when omitted
- [ ] 3.2 Parse `falloff` string to `RadialFalloff` enum: "linear" → `Linear`, "inverse_square" → `InverseSquare`, "constant" → `Constant`; raise a Lua error for unrecognized strings
- [ ] 3.3 Accept `pos` as a Lua table `{x, y, z}` or a Vec3 userdata

## 4. Debug Visualization

- [ ] 4.1 When `r_debug_physics` cvar is enabled, draw a wire sphere at `origin` with `radius` lasting exactly one frame after `apply_radial_impulse()` is called
- [ ] 4.2 Use the existing DX12 debug draw primitive API for the wire sphere; no new rendering infrastructure needed

## 5. IPC MCP Tool

- [ ] 5.1 Register IPC handler `physics.apply_radial_impulse` in `src/ipc/ipc_handlers.cpp` accepting JSON `{ "origin": [x,y,z], "radius": f, "strength": f, "falloff": "linear"|"inverse_square"|"constant" }`
- [ ] 5.2 Call `PhysicsWorld::apply_radial_impulse()` from the IPC handler and return `{ "ok": true, "bodies_affected": N }` in the response

## 6. Testing

- [ ] 6.1 Create `tests/physics/test_radial_force.cpp` testing `apply_radial_impulse()` with a single body at distance 0.5*radius: verify non-zero impulse applied in the outward direction
- [ ] 6.2 Test body at exactly `radius` distance receives near-zero linear impulse (boundary condition)
- [ ] 6.3 Test body beyond `radius` receives no impulse
- [ ] 6.4 Test `Linear` falloff: body at 0 distance receives `strength`; body at 0.5*radius receives approximately 0.5*`strength`
- [ ] 6.5 Test `Constant` falloff: all bodies within radius receive exactly `strength` regardless of distance
- [ ] 6.6 Test `InverseSquare` falloff: closer body receives stronger impulse than farther body within same radius
- [ ] 6.7 Test `apply_radial_force()` with `duration_ms = 100`: verify force is applied for 2+ physics steps and removed after the duration elapses
- [ ] 6.8 Test degenerate case where origin coincides with body center (distance = 0): no NaN, no crash, impulse is zero or along a stable fallback direction
- [ ] 6.9 Test Lua `ctx.physics:explosion()` with unknown falloff string raises a Lua error
- [ ] 6.10 Test IPC handler `physics.apply_radial_impulse` returns `bodies_affected` count matching the number of bodies in range
- [ ] 6.11 Achieve >= 95% line and branch coverage across `physics_world.cpp` radial impulse/force paths and `lua_physics.cpp` explosion binding

## 7. Documentation

- [ ] 7.1 Update `docs/specs/physics-system.md` documenting `apply_radial_impulse()`, `apply_radial_force()`, `RadialFalloff` enum values, and Lua `ctx.physics:explosion()` API
- [ ] 7.2 Update `docs/specs/debug-profiling.md` noting the `r_debug_physics` wire-sphere visualization for radial impulses
