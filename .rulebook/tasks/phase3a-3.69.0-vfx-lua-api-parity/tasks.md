## 1. Fix Existing Bindings

- [x] 1.1 Fix uz.vfx:spawn() graph path bug — Added std::string graphPath to ParticleEmitter. VFXSpawn() now matches emitters by graphPath with closest-position tiebreaker. Returns nil + warning if no match.
- [x] 1.2 Fix uz.vfx:emit_event() — Added optional count (arg 4, default 1) and velocity_scale (arg 5, default 1.0) parameters. Removed hardcoded constants.

## 2. Emitter Configuration Bindings

- [x] 2.1 Add get/set for emitter structural fields — VFXGetEmitterField/VFXSetEmitterField with 14 fields (emission_rate, lifetime, lifetime_variation, spawn_velocity, velocity_variation, spread_angle, gravity, damping, priority, start_delay, max_particles read-only, is_emitting, graph_path, soft_particle_fade_range).
- [x] 2.2 Add uz.vfx:set_parameter_vec4() — Reads {x,y,z,w} table, calls parameters.SetVec4().
- [x] 2.3 Add uz.vfx:get_parameter() — Returns float/Vec3 table/Vec4 table/nil based on stored parameter type.

## 3. Trail System Bindings

- [x] 3.1 Create trail Lua API — trail_lua_bindings.h/.cpp with create_trail, destroy_trail, set_trail_active, clear_trail, get_trail_length, get_trail_point_count. Added TrailSystem::GetComponent(handle) for post-registration component access.
- [x] 3.2 Add trail configuration bindings — set_trail_width, set_trail_color (Vec4 tables), set_trail_lifetime, set_trail_uv_scroll. All 10 functions registered on existing uz.vfx table.

## 4. Decal System Bindings

- [x] 4.1 Create decal Lua API — decal_lua_bindings.h/.cpp with spawn_decal, fade_out_decal, destroy_decal. DecalComponent* encoded as lua_Integer handle. Registry key "uz_decal_manager".
- [x] 4.2 Add decal configuration bindings — set_decal_blend_mode (string→enum: opaque/additive/multiply/screen), set_decal_normal_intensity. All 6 functions on uz.vfx table.
- [x] 4.3 Add decal stats query — get_decal_stats() returns table {active_count, pool_available, atlas_occupancy} from DecalManager.

## 5. LOD & Budget Bindings

- [x] 5.1 Add LOD control — VFXSetLodDistances writes emitter->lodSettings distances, VFXGetLodLevel returns int 0-3, VFXSetLodEnabled toggles emitter->lodEnabled.
- [x] 5.2 Add budget query — VFXGetBudgetStats returns {total, used, available, utilization_pct} from ParticleBudget. VFXGetTotalAlive/VFXGetEmitterCount added.

## 6. Composite Effects Bindings

- [x] 6.1 Add composite Lua API — VFXSpawnComposite loads .uzvfx_composite via VFXComposite::Load, spawns emitters with offsets/delays, returns table of handles. VFXStopComposite iterates table and stops each.

## 7. Testing

- [x] 7.1 Unit tests for spawn() with graph path matching — test_vfx_lua_spawn_emit.cpp: SpawnMatchesByGraphPath, SpawnNoMatch, SpawnClosestPosition, SpawnIgnoresUninitializedEmitters, SpawnWithNamedVec3.
- [x] 7.2 Unit tests for emit_event() with count and velocity_scale — EmitEventDefault, EmitEventWithCount, EmitEventWithCountAndVelocity, EmitEventInvalidHandle.
- [x] 7.3 Unit tests for emitter get/set structural fields — test_vfx_lua_fields_trail.cpp: GetSetEmissionRate, GetSetLifetime, GetSetSpawnVelocity, GetSetGravity, GetSetBoolIsEmitting, GetSetGraphPath, GetSetPriority, MaxParticlesReadOnly, SetParameterVec4, GetParameterFloat.
- [x] 7.4 Unit tests for trail Lua API — CreateAndDestroyTrail, SetTrailActive, ClearTrail, GetTrailLength, SetTrailWidth, SetTrailColor, SetTrailLifetime, SetTrailUvScroll.
- [x] 7.5 Unit tests for decal Lua API — test_vfx_lua_decal_lod_composite.cpp: GetDecalStatsReturnsTable, SpawnDecalWithoutManager, SetDecalBlendModeInvalidHandle, DestroyDecalInvalidHandle.
- [x] 7.6 Unit tests for LOD and budget query bindings — SetLodDistances, GetLodLevel, SetLodEnabled, GetBudgetStats, GetTotalAlive, GetEmitterCount.
- [x] 7.7 Unit tests for composite spawn/stop — SpawnCompositeNonexistentFile, StopCompositeEmptyTable.

## 8. Demo

- [x] 8.1 Create Lua VFX showcase — demos/vfx_showcase/lua_vfx_showcase.lua with full API: particles, trails, decals, composites, LOD, budget stats, parameter animation.
- [x] 8.2 Update explosion_composite demo to use full Lua API — added trails (create_trail, set_trail_color/width/uv_scroll), decals (spawn_decal, set_decal_blend_mode/normal_intensity, fade_out_decal), LOD (set_lod_distances/enabled), budget stats, composite spawn/stop, emitter field config, emit_event with velocity_scale.

## 9. Documentation

- [x] 9.1 Complete Lua VFX API reference — docs/guides/lua-vfx-api-reference.md with all particle, LOD, budget, composite functions documented.
- [x] 9.2 Document trail Lua API with usage examples — docs/guides/lua-vfx-trail-api.md with create/destroy/configure examples.
- [x] 9.3 Document decal Lua API with usage examples — docs/guides/lua-vfx-decal-api.md with spawn/fade/destroy, blend modes, normal intensity, stats query, usage example.

## 10. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 10.1 Update or create documentation covering the implementation
- [ ] 10.2 Write tests covering the new behavior
- [ ] 10.3 Run tests and confirm they pass
