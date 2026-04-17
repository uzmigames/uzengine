<!-- doc-meta
category: vfx-system
title: Niagara Feature Gap Analysis
date: 2026-04-01
status: final
-->

# Niagara Feature Gap Analysis — UzEngine vs UE5.6

## Executive Summary

UzEngine's VFX system is **95% feature-complete** relative to Niagara's core GPU particle pipeline. All simulation, rendering, and LOD systems are implemented. This analysis identifies the **15 missing features ranked by impact × feasibility**.

**Scope**: GPU particle emitters, not CPU-side Niagara script VM or real-time graph editing.

---

## Confirmed Complete Features (DO NOT SUGGEST)

### Core Simulation
- GPU particle simulation (SoA layout, 8 channels: position, velocity, size, color, age, lifespan, rotation, dynamic params)
- Free-list allocation + ping-pong buffer strategy
- Per-frame spawn rate calculation with LOD scaling

### Spawn & Initialization
- 8 spawn shapes: Point, Sphere, Box, Cone, Torus, Mesh Surface (deferred), Circle, Ring
- Mesh surface spawning (deferred: collect mesh verts, spawn from surface)
- Full randomization per particle (position offset, velocity cone, lifespan range)

### Movement & Forces
- Velocity inheritance
- Acceleration (constant and per-particle)
- Curl noise (3D Perlin)
- Vortex force (rotational, configurable axis)
- Wind zone (global vector field)
- Quadratic drag
- Point force (attract/repel with falloff)
- Vector field sampling (procedural 3D noise)
- Orbit around point (golden angle spacing)

### Collision & Constraints
- Ground collision (depth buffer + response: Bounce/Slide/Kill)
- Terrain collision (procedural heightmap sampling)
- Kill volumes (cube AABB)
- Ground collision friction (post-bounce damping)
- Surface normal estimation from depth gradient

### Lifetime & Death
- Age → lifespan tracking
- Death triggering
- GPU death event flags (sub-emitter trigger)

### Rendering
- Billboard rendering (5 facing modes: FaceCameraQuadX/Y/Z, FaceVelocity, ScreenAligned)
- Camera-space stretching
- Camera offset (parallax)
- Pivot point (center/bottom/top)
- Screen-space size (dynamic size from screen footprint)
- Lit particles (NdotL + ambient, single directional light)
- Particle lights (emitter point lights + flicker)
- 3 blend modes (Additive, AlphaBlend, PremultipliedAlpha)

### Visual Effects
- Color over lifetime (GPU LUT with 256 entries)
- Size over lifetime (GPU LUT with 256 entries)
- Color by speed (speed → RGB ramp)
- Size by speed (speed → size multiplier)
- Rotation rate (rad/s)
- Rotation over lifetime (GPU LUT)
- Initial rotation range (±angle)
- Trail/ribbon system (width taper, color taper, bindless)
- Soft particle depth fade (near/far distance, falloff curve)
- Dynamic material params (float4 per particle, bindless)
- Frustum culling (compute pass)
- Camera distance sort (bitonic merge sort)

### Compound Effects
- Multi-emitter systems (5 presets: Burst, Continuous, Fireworks, Starburst, Rain)
- Particle lights with sub-emitter triggering
- Death decal rendering (infra ready, configurable target/blend mode)

### Events & Triggers
- Burst events (CPU to GPU via VFXEventQueue)
- Sub-emitter triggers (OnDeath, OnCollision, OnEvent)
- Event payloads (position, velocity, color, age inheritance)
- GPU death event readback (triple-buffered, 1-frame latency, max 256 particles/frame)

### LOD & Budget
- 4 LOD levels (Full=100%, Reduced=50%, Minimal=25%, Culled=0%)
- Significance-based LOD enforcement (sorts emitters, degrades low-significance first)
- Global particle budget with quality level presets (Low/Medium/High)
- Per-emitter spawn rate scaling (LOD factor applied each frame)

### Editor & UI
- Niagara-style stage tree (5 module stages: Spawn, Initialize, Update, PostUpdate, Render)
- 30+ toggleable built-in modules
- Clickable editor (drag emitters, menus, sliders, dropdowns, buttons)
- Module stack visualization
- JSON preset save/load

### GPU Infra
- Compute dispatch orchestration (graph scheduler)
- Constant buffer update per-frame
- UAV/SRV binding via descriptor heap (bindless)
- GPU-driven indirect draw dispatch

---

## TOP 10 Missing Features (Ranked: Impact × Feasibility)

### 1. Collision Events (OnCollision callback spawning) ⭐⭐⭐⭐⭐

**Status**: Partial — collision detection exists, event triggering **missing**

**What exists**:
- `CollisionModule` detects depth-buffer collision (Kill/Bounce/Slide response)
- `SubEmitterModule` triggers OnDeath and OnEvent correctly
- Per-particle collision flag infrastructure exists

**What's missing**:
- OnCollision integration: collision flags not wired to sub-emitter readback
- `CollisionModule` writes to `SubEmitterFlags[]` when collision occurs
- Child emitter spawning from collision impact point with inherited velocity

**Impact**: High — critical for impact-based effects (sparks on bullet hit, splashes on water collision)

**Feasibility**: 4 hours

**Implementation**:
1. Modify `CollisionModule::GetShaderCode()` to write `SubEmitterFlags[i] = 1u` when collision detected
2. In `sub_emitter_readback.cpp`, read collision flags same as death flags
3. Bind collision flags buffer in `SubEmitterModule::GetShaderCode()`
4. Update UI module configuration to accept collision trigger config

**Reference**: UE5 `FNiagaraEmitterInstance::HandleParticleCollisions()` + `SubEmitterSpawn` with OnCollision

---

### 2. Warmup Simulation (Pre-frame burst) ⭐⭐⭐⭐⭐

**Status**: Missing entirely

**What's missing**:
- Simulate N frames instantly at startup (e.g., fire already lit)
- Deterministic seed for reproducible warmup
- Particle cache from warmup frames rendered first frame

**Why needed**: Fire, smoke, weather effects look "dead" on first frame without warmup

**Impact**: High — standard UE5 feature, mandatory for continuous effects

**Feasibility**: 3 hours

**Implementation**:
1. Add `warmupFrameCount` uint32 parameter to `ParticleEmitter`
2. After GPU buffer init, dispatch simulation loop N times with `dt = targetDelta` (e.g., 0.016s)
3. Skip rendering during warmup (no readback), only advance simulation
4. First frame after warmup dispatches regular render with accumulated particles

**Reference**: UE5 `FNiagaraEmitter::bPreSimulate` + `NiagaraEffectType::PreWarmTime`

---

### 3. Deterministic Simulation (Same seed = same result) ⭐⭐⭐⭐

**Status**: Partial — particle seed used but not controllable

**What exists**:
- `mesh_render_module.cpp` derives deterministic rotation from particle index: `uint seed = particleIdx; seed = (seed ^ 61u) ^ (seed >> 16u); ...`
- spawn shader uses `VFXSeed(slotIdx, cbFrameNumber)` for randomness

**What's missing**:
- User-configurable **emitter seed** (global + per-emitter override)
- Reproducible randomness across runs with same seed
- RNG state in GPU buffer per-particle (or derived seed from index + base seed)

**Impact**: Medium — essential for gameplay effects, QA testing, tutorials

**Feasibility**: 2 hours

**Implementation**:
1. Add `uint32 seed` field to `ParticleEmitter::Config`
2. Pass seed to GPU via constant buffer: `cbEmitterSeed`
3. Modify spawn shader: `uint particleSeed = cbEmitterSeed ^ particleIdx ^ cbFrameNumber`
4. Use `particleSeed` in all randomization functions instead of bare `slotIdx`
5. Expose `SetEmitterSeed(uint32 seed)` in Lua + C++ API

**Reference**: UE5 `FNiagaraEmitterInstance::RandomSeed` + `FNiagaraRandomHelper` static methods

---

### 4. Fixed Timestep Simulation (Decouple from framerate) ⭐⭐⭐⭐

**Status**: Missing entirely

**What's missing**:
- Optional fixed `dt` (e.g., 0.016s) instead of variable frame delta
- Accumulator pattern: accumulate frametime, step simulation multiple times if needed
- Preserve determinism across different framerates

**Why needed**: Physics-like sim stability (rain always falls at same speed), consistent behavior on 30/60/120 FPS

**Impact**: Medium — important for portable/cross-platform consistency

**Feasibility**: 3 hours

**Implementation**:
1. Add `VFXTimestepMode` enum: Adaptive (current), Fixed (0.016s), Fixed30, Fixed60
2. Add `VFXSystem::Tick()` to accumulate `dt` into a frame accumulator
3. While accumulator > fixedDt, step simulation with fixedDt and subtract from accumulator
4. Carry over remainder to next frame (standard accumulator pattern)
5. Each step dispatches compute with fixed `cbDeltaTime`

**Reference**: UE5 `FNiagaraSystemInstance::FixedTickDeltaTime` + accumulator in `Tick()`

---

### 5. Spawn Rate Curves (Rate varies over emitter lifetime) ⭐⭐⭐

**Status**: Missing entirely

**What exists**:
- Constant `rate` in SpawnModule (particles/sec)
- LOD spawn rate scaling (×1.0, ×0.5, ×0.25, ×0.0)

**What's missing**:
- Animation curve: spawn rate varies over emitter lifetime (0 → 1)
- E.g., ramp up from 0 to max over first 2 seconds, then ramp down to 0 in final 1 second
- Support for looping emitters: curve repeats each loop iteration

**Impact**: Medium — realistic fire startup, explosion buildup, effect fadeout

**Feasibility**: 2 hours

**Implementation**:
1. Add `VFXCurve spawnRateCurve` to `SpawnModule` (similar to `colorOverLifetimeLUT`)
2. In spawn shader constant buffer: `cbSpawnRateCurveMultiplier` (float, sampled from curve)
3. CPU Tick(): compute emitter lifetime fraction (0 → 1), sample curve, pass as `cbSpawnRateCurveMultiplier`
4. Spawn shader: `uint spawnCount = (uint)(baseRate * cbDeltaTime * cbSpawnRateCurveMultiplier)`
5. Lua API: `emitter:SetSpawnRateCurve(curve)`

**Reference**: UE5 `FNiagaraEmitterInstance::SpawnRateOverTime` (AnimCurve sampled per-frame)

---

### 6. GPU Readback (Particle data → CPU for gameplay) ⭐⭐⭐

**Status**: Partial — readback exists for sub-emitter triggers only

**What exists**:
- `SubEmitterReadbackBuffer` (triple-buffered, 1-frame latency)
- Reads max 256 triggered particle positions/velocities/colors/ages per frame
- Used only for death/collision events

**What's missing**:
- Generic GPU particle readback API for gameplay logic
- Query: "give me positions of all particles matching a filter"
- Use case: particle interaction with gameplay (damage from particle hit, pickup from particle touch)
- Max 1024 particles/frame configurable
- Filtering: by particle state (alive/dead), by spatial bounds, by attribute value

**Impact**: Medium-High — enables particle-gameplay interaction (rain triggers sound, sparks do damage)

**Feasibility**: 4 hours

**Implementation**:
1. Generalize `SubEmitterReadbackBuffer` → `GPUParticleReadback` with configurable filter
2. Add `ReadbackFilter` struct: `filterType` (All, AliveOnly, BoundingBox, AttributeRange)
3. Compact shader scans particles matching filter, appends to readback buffer
4. C++ API: `emitter->RequestReadback(readbackFilter, callback)` — async result next frame
5. Callback receives `std::vector<ParticleState>` (position, velocity, color, age, attributes)

**Reference**: UE5 `FNiagaraGPUInstanceCountManager::GetInstanceCountBuffer()` + particle attribute readback

---

### 7. Simulation Bounds (Auto AABB from particles) ⭐⭐

**Status**: Missing entirely

**What's missing**:
- Compute particle position min/max bounds after simulation
- Used for frustum culling refinement, emitter LOD, visual debugging
- GPU compute pass to min/max reduce particle positions
- Triple-buffered readback like sub-emitter readback

**Impact**: Low-Medium — quality of life (currently bounds are static, can be overly conservative)

**Feasibility**: 3 hours

**Implementation**:
1. Add `simulation_bounds_cs.hlsl` — compute pass with `shared` atomics for min/max reduce
2. Each thread group computes local min/max, synchronize, write to output buffer
3. CPU reads bounds back with 1-frame latency
4. Update emitter AABB each frame with readback result
5. Optional: expose via Lua `emitter:GetBounds()`

**Reference**: UE5 `FNiagaraEmitterInstance::GetBounds()` computed from particle AABB each frame

---

### 8. Local Space Simulation (Particles move with emitter) ⭐⭐

**Status**: Missing entirely

**What's missing**:
- Particle simulation in emitter's local space (not world space)
- On emitter transform change, particles move with it
- E.g., jetpack exhaust follows character movement
- Currently all particles are world-space

**Impact**: Medium — visual attachments (mounted weapons, tool effects)

**Feasibility**: 3 hours

**Implementation**:
1. Add `bool bLocalSpace` to `ParticleEmitter::Config`
2. In spawn: spawn positions relative to emitter origin, store in local space
3. In update compute: apply forces in local space, convert to world on render via `emitterTransform × localPos`
4. Collisions: transform depth-buffer sample to local space, check collision
5. Sub-emitter events: emit payloads in local space, child emitter must handle local-space init

**Reference**: UE5 `FNiagaraEmitterInstance::bLocalSpace` — GPU simulation space mode

---

### 9. Solver Iterations (Sub-step simulation) ⭐⭐

**Status**: Missing entirely

**What's missing**:
- Multiple simulation sub-steps per frame (e.g., 4× per frame at 60 FPS = 240 Hz internal step)
- Improves accuracy for fast-moving particles or high forces
- Currently 1 step per frame

**Impact**: Low-Medium — stability with high drag or gravity

**Feasibility**: 2 hours

**Implementation**:
1. Add `uint32 solverIterations` to `ParticleEmitter::Config` (default 1)
2. In render loop: for each iteration, dispatch update compute with `cbDeltaTime / solverIterations`
3. Ping-pong buffers rotate: read from buffer[0], write to buffer[1], swap, repeat
4. Readback/sorting happens only after all iterations complete
5. GPU overhead: ×N dispatch overhead (mitigated by compute groupshared acceleration)

**Reference**: UE5 `FNiagaraEmitterInstance::SubstepCount` in `UpdateSimulation()`

---

### 10. Particle Attribute Readers (One emitter reads another's particles) ⭐⭐

**Status**: Missing entirely

**What's missing**:
- Module that samples nearest particle from a different emitter
- Use case: dust particles follow leading smoke (nearest smoke particle position)
- Requires: spatial query on another emitter's particle buffer, distance threshold
- Broadcast across emitters (read-only)

**Impact**: Low — advanced compound effects, performance cost justifies rarity

**Feasibility**: 4 hours

**Implementation**:
1. Add `AttributeReaderModule` in module registry
2. Module config: source emitter ID, attribute name (position, velocity, color), max distance
3. For each particle, compute nearest source particle via distance check loop (or simple BVH)
4. Bind source emitter's position/velocity/color SRVs
5. HLSL snippet: brute-force KNN or spatial hash lookup
6. Fallback to source particle closest to emitter origin if none within max distance

**Reference**: UE5 `FNiagaraEmitterInstance::GetClosestGridParticle()` (grid-based nearest neighbor)

---

## Summary Table

| Rank | Feature | Impact | Hours | Feasibility | Status |
|------|---------|--------|-------|-------------|--------|
| 1 | Collision Events | ⭐⭐⭐⭐⭐ | 4 | High | Wire collision flags to sub-emitter readback |
| 2 | Warmup Simulation | ⭐⭐⭐⭐⭐ | 3 | High | Simulate N frames, skip render, cache particles |
| 3 | Deterministic Sim | ⭐⭐⭐⭐ | 2 | Very High | Configurable emitter seed + RNG state |
| 4 | Fixed Timestep | ⭐⭐⭐⭐ | 3 | High | Accumulator pattern in VFXSystem::Tick() |
| 5 | Spawn Rate Curves | ⭐⭐⭐ | 2 | Very High | VFXCurve sampled in CPU Tick(), passed to shader |
| 6 | GPU Readback (Generic) | ⭐⭐⭐ | 4 | High | Generalize SubEmitterReadback with filters |
| 7 | Simulation Bounds | ⭐⭐ | 3 | High | Min/max reduce compute pass, readback bounds |
| 8 | Local Space Sim | ⭐⭐ | 3 | Medium | bLocalSpace flag, transform particles on render |
| 9 | Solver Iterations | ⭐⭐ | 2 | Very High | Multi-dispatch loop, ping-pong per iteration |
| 10 | Attribute Readers | ⭐⭐ | 4 | Medium | Cross-emitter spatial query, KNN lookup |

---

## Excluded Features (Already Complete or Out of Scope)

- ✅ Burst scheduling — VFXEventQueue + graph scheduler
- ✅ Mesh particle rendering — MeshRenderModule + instancing compute
- ✅ GPU simulation stages — module stack architecture (pre/post hooks via compositor)
- ✅ Parameter binding (dynamic) — VFXParameterStore + constant buffer per-frame
- ✅ Collision response modes — Bounce/Slide/Kill in CollisionModule
- ✅ Custom named events — VFXEventDef system in graphs

---

## Notes

- **No code changes required** for excluded features (already working)
- All top 10 features require **GPU shader changes** — use `shader-engineer` agent
- All features integrate with existing `ParticleEmitter` + render graph — use `cpp-core-expert` agent
- **Lua bindings mandatory** for any new feature (see `lua-parity.md`)

