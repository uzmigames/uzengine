## 1. Collision Module

- [x] 1.1 Create `runtime/engine/vfx/modules/collision_module.h/.cpp` — CollisionModule: config (response enum: Kill/Bounce/Slide, bounce_factor float, depth_buffer_collision bool). GetShaderCode() returns HLSL: project particle to screen space, sample depth buffer, compare. If particle behind depth → collision at surface. Kill: age=lifespan. Bounce: reflect velocity off surface normal (estimated from depth gradient), multiply by bounce_factor. Slide: project velocity onto surface plane.
- [x] 1.2 Create `runtime/shaders/vfx/vfx_collision.hlsli` — GPU collision utilities: ScreenSpaceRaycast(pos, vel, depth_texture, view_proj) → hit bool + surface_normal estimate. EstimateSurfaceNormal(depth_texture, screen_uv) using depth gradient (sample 4 neighbors). ReflectVelocity(vel, normal, factor). ProjectOntoPlane(vel, normal).
- [x] 1.3 Integrate depth buffer access — ParticleSimulationPass receives scene depth texture SRV for collision module. Render graph declares depth as read dependency.

## 2. Light Render Module

- [x] 2.1 Create `runtime/engine/vfx/modules/light_render_module.h/.cpp` — LightRenderModule: config (radius float, intensity float, intensity_over_life curve, cast_shadow bool, max_lights uint32 default 64). Does NOT contribute HLSL to compute shader. Instead, after compute: CPU reads back alive particle positions (via readback buffer, capped at max_lights), creates temporary point lights for the light system. Readback is async — 1 frame latency acceptable.
- [x] 2.2 Create GPU→CPU readback for light particles — Small readback buffer (max_lights × sizeof(float3+float4) = max_lights × 28 bytes). Compute shader writes top-N closest-to-camera particles' positions and colors. CPU reads previous frame's buffer (no stall). Creates transient PointLight entries in light system.
- [x] 2.3 Integrate with light system — VFXSystem::UpdateLights() adds per-particle lights to the clustered light list. Lights inherit particle color × intensity_over_life curve. Radius and intensity from module config.

## 3. Mesh Render Module

- [x] 3.1 Create `runtime/engine/vfx/modules/mesh_render_module.h/.cpp` — MeshRenderModule: config (mesh handle, material handle, mesh_scale Vec3, cast_shadow bool, rotation_mode enum: None/VelocityAligned/Random). GetShaderCode() writes per-particle instance transform (position + rotation + scale) to a structured buffer. Rendering uses GPU instancing: DrawIndexedInstanced with mesh geometry + instance buffer of particle transforms.
- [x] 3.2 Create `runtime/shaders/vfx/particle_mesh_instance_cs.hlsl` — Compute shader: for each alive particle, compute instance transform matrix from position, rotation (based on rotation_mode), and mesh_scale. Write to RWStructuredBuffer<float4x4> instance_transforms. Also write instance count to indirect args.
- [x] 3.3 Create `runtime/engine/vfx/passes/particle_mesh_render_pass.h/.cpp` — ParticleMeshRenderPass: render graph pass. Binds mesh vertex/index buffers + instance transform buffer. DrawIndexedInstancedIndirect using particle indirect args. Supports opaque and transparent mesh particles (separate passes).

## 4. LOD System

- [x] 4.1 Create `runtime/engine/vfx/lod/vfx_lod.h/.cpp` — VFXLodLevel enum: Full (100%), Reduced (50%), Minimal (25%), Culled (0%). VFXLodSettings per emitter: lod_distances[3] (e.g., {50, 100, 200} meters for Full→Reduced→Minimal→Culled transitions). VFXLodEvaluator: ComputeLodLevel(emitter_pos, camera_pos, lod_settings) → VFXLodLevel.
- [x] 4.2 Create screen-size culling — VFXScreenCuller: compute emitter bounding sphere screen coverage in pixels. If coverage < min_screen_pixels (default 4), return Culled. Uses perspective projection formula: screen_pixels = (radius / distance) * screen_height * 0.5.
- [x] 4.3 Integrate LOD with spawn rate — ParticleEmitter::CalculateSpawnCount(dt) multiplies base rate by LOD factor: Full=1.0, Reduced=0.5, Minimal=0.25, Culled=0.0. LOD evaluated once per frame per emitter.

## 5. Significance Manager

- [x] 5.1 Create `runtime/engine/vfx/budget/vfx_significance_manager.h/.cpp` — VFXSignificanceManager: ranks all active emitters. Significance score = emitter.priority × distance_score × visibility_score. distance_score = 1.0 / (1.0 + distance * 0.01). visibility_score = 1.0 if frustum visible, 0.1 if not. Sort emitters by score descending.
- [x] 5.2 Integrate significance with budget — When total particle demand exceeds budget, iterate emitters from lowest significance. Reduce their spawn rate by LOD factor or disable entirely until demand fits budget. Log culled emitters via UZ_LOG(VFX, Verbose).

## 6. Budget Enforcement

- [x] 6.1 Enhance ParticleBudget with quality levels — QualityLevel enum: Low (125K), Medium (250K), High (500K), Ultra (1M). SetQualityLevel(level) adjusts max_total_particles. Read from project config (.uzproj) VFX settings.
- [x] 6.2 Create budget statistics — VFXBudgetStats: total_budget, particles_used, particles_available, num_active_emitters, num_culled_emitters, budget_utilization_percent. Exposed for debug UI / profiling overlay.

## 7. Module Registration

- [x] 7.1 Register advanced modules — Add "collision", "light_render", "mesh_render" to RegisterBuiltinModules(). Update ModuleRegistry.

## 8. Testing

- [x] 8.1 Unit tests for CollisionModule — config parsing, shader code generation, collision response types
- [x] 8.2 Unit tests for LightRenderModule — config, max_lights capping, readback buffer sizing
- [x] 8.3 Unit tests for MeshRenderModule — config, instance transform generation, rotation modes
- [x] 8.4 Unit tests for VFXLodEvaluator — distance thresholds, screen-size culling formula, LOD level transitions
- [x] 8.5 Unit tests for VFXSignificanceManager — scoring formula, sort order, budget-driven culling
- [x] 8.6 Unit tests for ParticleBudget quality levels — Low/Medium/High/Ultra limits, quality transitions

## 9. Demo

- [x] 9.1 Create collision demo — Particles rain down, bounce off floor and walls with bounce_factor=0.6. Some particles kill on contact (blood splatter).
- [x] 9.2 Create mesh particle demo — Explosion spawns 3D debris chunks (instanced mesh), fall with gravity and rotation.
- [x] 9.3 Create LOD demo — Camera dolly from close to far. Show particle count changing with distance. LOD level indicator overlay.
- [x] 9.4 Create budget stress test — 50 emitters active simultaneously, budget=100K. Show significance manager culling low-priority distant emitters.

## 10. Documentation

- [x] 10.1 Document collision module (depth-buffer raycasting, response types)
- [x] 10.2 Document LOD system configuration and distance thresholds
- [x] 10.3 Document budget system and significance manager algorithm
- [x] 10.4 Document mesh particle rendering pipeline
- [x] 10.5 Update docs/engine/25-vfx/ with complete system reference

## 11. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 11.1 Update or create documentation covering the implementation
- [ ] 11.2 Write tests covering the new behavior
- [ ] 11.3 Run tests and confirm they pass
