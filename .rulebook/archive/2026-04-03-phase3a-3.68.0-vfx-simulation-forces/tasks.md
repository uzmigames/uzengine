## 1. Attractor Forces

- [x] 1.1 Create runtime/engine/vfx/modules/attractor_module.h/.cpp — AttractorFalloff enum (Linear/InverseSquare/Constant), 5 config fields (position, strength, radius, falloff, killOnArrival), kMinRadius/kKillDistance constexprs, HLSL snippet with 3-mode falloff + kill-on-arrival check.
- [x] 1.2 Create runtime/shaders/vfx/vfx_attractors.hlsli — AttractorParams struct (32 bytes), ComputeAttractorForce() with 3 falloff modes + div-by-zero guards, ShouldKillAtAttractor(), ApplyAttractors() with [unroll(8)] loop over StructuredBuffer. 266 lines.
- [x] 1.3 Support multiple attractors per emitter — AttractorBuffer (attractor_buffer.h/.cpp): Upload-heap StructuredBuffer of 8 AttractorData (32 bytes each), Create/Destroy/AddAttractor/Clear/UploadToGPU. Integrated into AttractorModule with UploadAttractors() method. HLSL snippet updated to loop over structured buffer.

## 2. Vector Fields

- [x] 2.1 Create runtime/engine/vfx/modules/vector_field_module.h/.cpp — 6 config fields (fieldTexture, intensity, tightness, bounds, fieldOffset, timeScale). HLSL snippet: UVW remapping from world-space AABB, decode (dir*0.5+0.5), tightness blend. Bindless Texture3D sampling.
- [x] 2.2 Create runtime/shaders/vfx/vfx_vector_field.hlsli — VectorFieldParams struct (48 bytes), WorldToFieldUVW(), IsInsideField(), SampleVectorField(), SampleAnimatedVectorField() with time-based UVW scrolling via frac(). ~200 lines.
- [x] 2.3 Support animated vector fields — Added timeScale float param (default 0.0) to VectorFieldModule. HLSL snippet scrolls UVW by cbVectorFieldTimeScale * cbTime when non-zero, using frac() for seamless wrapping. Parameter added as 6th GetParameters() entry.

## 3. Sub-Emitters

- [x] 3.1 Refactor VFXEventQueue for inter-emitter events — Added EventPayload struct (position Vec3, velocity Vec3, color Vec4, age float). Extended VFXPendingEvent with std::vector<EventPayload> payloads. Added EmitWithPayloads() to VFXEventQueue. All existing API preserved.
- [x] 3.2 Create runtime/engine/vfx/modules/sub_emitter_module.h/.cpp — SubEmitterModule: config (child_graph path, trigger enum: OnDeath/OnCollision/OnEvent, inherit_velocity float 0-1, inherit_color bool). When trigger fires, emit event with parent particle data to child emitter. HLSL snippet writes SubEmitterFlags buffer for GPU-side trigger detection. CPU Execute() emits EventPayloads with inherited particle data.
- [x] 3.3 GPU→CPU readback for sub-emitter triggers — SubEmitterReadbackBuffer (sub_emitter_readback.h/.cpp): triple-buffered readback with SubEmitterGPUPayload (48 bytes, 16-byte aligned). Flag buffer (UAV per particle), compact CS appends triggered data via InterlockedAdd (capped at 256), IssueCopy() via IRHICommandList, GatherTriggered() maps oldest slot and builds SubEmitterReadback. 1-frame latency.

## 4. Advanced Constraints

- [x] 4.1 Create runtime/engine/vfx/modules/orbit_module.h/.cpp — OrbitModule: center, speed, orbitRadius, axis fields. HLSL snippet: project to orbit plane, compute tangent via cross product, apply orbital velocity + radial correction force. Div-by-zero guards.
- [x] 4.2 Create runtime/engine/vfx/modules/pin_module.h/.cpp — PinModule: pinPosition, springStrength fields. HLSL snippet: Hooke's law spring force (F = k * dist) pulling particles toward pin. 2 parameters.
- [x] 4.3 Create runtime/engine/vfx/modules/distance_constraint_module.h/.cpp — DistanceConstraintModule: constraintOrigin, minDistance, maxDistance, constraintStrength. HLSL: push out below min, pull in above max. 4 parameters.

## 5. Module Registration

- [x] 5.1 Register all new modules — Added "attractor", "vector_field", "sub_emitter", "orbit", "pin", "distance_constraint" to RegisterBuiltinModules() in register_builtin_modules.cpp. Total: 15 built-in modules.

## 6. Testing

- [x] 6.1 Unit tests for AttractorModule — 33 tests: defaults, GetName, GetShaderCode, GetParameters (5 slots), AttractorFalloff enum, kMinRadius/kKillDistance, AttractorData size, AttractorBuffer, custom config.
- [x] 6.2 Unit tests for VectorFieldModule — 45 tests: defaults, GetName, GetShaderCode, GetParameters (6 slots), field_texture encoding, animated field HLSL, tightness blend, bounds safety, custom config.
- [x] 6.3 Unit tests for SubEmitterModule — 39 tests: defaults, GetName, GetShaderCode, GetParameters (4 slots), SubEmitterTrigger enum, Execute paths (OnDeath/OnCollision/OnEvent), inheritVelocity/inheritColor, null queue safety.
- [x] 6.4 Unit tests for OrbitModule, PinModule, DistanceConstraintModule — 56 tests: OrbitModule (20), PinModule (15), DistanceConstraintModule (21). Config, GetName, GetShaderCode, GetParameters, Execute no-op, custom config.
- [x] 6.5 Integration test: sub-emitter spawns children on parent death — 19 tests: OnDeath payloads, velocity inheritance, color inheritance, OnCollision, OnEvent relay, chained sub-emitters, empty readback, null queue, multi-frame accumulation, large readback (256 particles).

## 7. Demo

- [x] 7.1 Create attractor demo — demos/attractor_demo/: headless demo with InverseSquare falloff, kill_on_arrival, validates GetName/GetShaderCode/GetParameters.
- [x] 7.2 Create vector field demo — demos/vector_field_demo/: headless demo with animated wind tunnel (timeScale=1.5), validates 6 params + animated HLSL.
- [x] 7.3 Create sub-emitter demo — demos/sub_emitter_demo/: headless firework cascade (rocket→burst→sparkle), validates 3-stage event chain via SubEmitterReadback.
- [x] 7.4 Create orbit demo — demos/orbit_demo/: headless demo with OrbitModule + PinModule + DistanceConstraintModule, validates all 3 constraint types.

## 8. Documentation

- [x] 8.1 Document attractor force types and configuration — docs/guides/vfx-attractor-forces.md
- [x] 8.2 Document vector field creation and 3D texture format — docs/guides/vfx-vector-field-guide.md
- [x] 8.3 Document sub-emitter system architecture and event flow — docs/guides/vfx-sub-emitter-system.md
- [x] 8.4 Document advanced constraint modules — docs/guides/vfx-constraint-modules.md
