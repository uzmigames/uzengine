<!-- doc-meta
  title: VFX System Implementation Roadmap (3.66-3.70)
  author: Claude Code
  date: 2026-03-25
  scope: Prioritized phased roadmap to close architectural gaps
  context: Follow this sequence to reach production-ready VFX
-->

# VFX System Implementation Roadmap (3.66-3.70)

## Roadmap Overview

**Goal**: Bring UzEngine VFX to **production-ready parity** with industry systems in 12-16 weeks.

**Scope**: 18 features across 4 categories; phased by priority + dependency.

**Team Assumption**: 1 senior graphics engineer (can parallelize with other work).

---

## Phase 3.66: Foundation Polish & Sort (Weeks 1-2)

**Focus**: Fix critical alpha blending bug; polish near-complete features.

### 3.66.0: Soft Particles Verification & Fix

**Status**: Likely 95% done; needs integration test.

**Checklist**:
- [ ] Read `particle_billboard_ps.hlsl` lines 70-85
- [ ] Verify `softParticleFadeRange` is passed in CB
- [ ] Verify PS applies `smoothstep(fadeRange, 0, depthDistance)` fade
- [ ] Create test emitter (large 50cm particles)
- [ ] Capture screenshot with camera near particles
- [ ] Verify fade-to-transparent at close range
- [ ] Merge PR

**Ownership**: Can pair with 3.66.1 (1-2 days max).

**Effort**: S (1-2 days)

**Deliverables**:
- `particle_billboard_ps.hlsl` (verified + possibly fixed)
- Demo screenshot showing soft fade
- Unit test (particle depth fade correctness)

---

### 3.66.1: GPU Radix Sort for Particles

**Status**: Not started.

**Dependencies**: None (can start immediately).

**Blockers Fixed**: Alpha blending artifacts in dense particle fields.

**Implementation Steps**:

1. **Study radix sort algorithm** (1 day)
   - Review NVIDIA's GPU radix sort whitepaper
   - Understand 2-pass approach (low bits → high bits)
   - Analyze thread group layout (256 threads, 4 elements per thread)

2. **Implement sort kernel** (2-3 days)
   - `runtime/shaders/vfx/particle_radix_sort_cs.hlsl`
   - Two compute passes (one per 16-bit group)
   - Shared memory for local sort
   - Atomic counters for histogram

3. **Integrate into render graph** (1-2 days)
   - `runtime/engine/vfx/passes/particle_sort_pass.h/cpp`
   - Read alive count from previous frame
   - Generate depth keys (32-bit Z from view-proj)
   - Dispatch two sort passes
   - Update indirect render args

4. **Test & validate** (1 day)
   - Render particles with varying depths
   - Verify back-to-front order (use debug colors)
   - Benchmark: sort time on 50k particles

**Effort**: L (5-7 days)

**Deliverables**:
- `particle_radix_sort_cs.hlsl` (2 passes, well-commented)
- `ParticleSortPass` (render graph integration)
- Demo with 50k overlapping particles + screenshot
- Performance benchmark

**Acceptance Criteria**:
- Sort completes in <1ms for 50k particles
- Alpha blending order correct (verified visually)
- No GPU validation errors

---

### 3.66 Milestone

**When Complete**: Soft particles polished, GPU sort working.

**Visual Impact**:
- Dense particle effects render correctly (no depth-sorting artifacts)
- Particles fade naturally at close range

**Production Readiness**: +1 step toward ship-ready VFX.

---

## Phase 3.67: Core Simulation Capabilities (Weeks 3-5)

**Focus**: Add fundamental forces; enable gameplay integration.

### 3.67.0: Attractor Forces Module

**Status**: Not started.

**Dependencies**: None.

**Implementation Steps**:

1. **Design module interface** (0.5 day)
   - Parameters: position, strength, radius, falloff power
   - CPU callback to update position each frame
   - HLSL snippet for force accumulation

2. **Implement HLSL** (1 day)
   - `runtime/shaders/vfx/attractor_module.hlsli`
   - Distance calculation + falloff function
   - Velocity += acceleration logic

3. **Implement C++ module** (1 day)
   - `runtime/engine/vfx/modules/attractor_module.h/cpp`
   - GetParameters() returns position/strength/radius/power
   - Execute() updates position from parameter store
   - GetShaderCode() returns HLSL snippet

4. **Register module** (0.5 day)
   - Add to ModuleRegistry in `register_builtin_modules.cpp`
   - Add to .uzvfx JSON schema documentation

5. **Test** (1 day)
   - Create test effect with attractor
   - Particles pulled toward point
   - Falloff radius respected
   - Strength scaled correctly

**Effort**: M (4-5 days)

**Deliverables**:
- `attractor_module.h/cpp` + `attractor_module.hlsli`
- Updated `register_builtin_modules.cpp`
- Test effect `.uzvfx` file
- Demo screenshot + video

---

### 3.67.1: Vector Field Sampling Module

**Status**: Not started.

**Dependencies**: Attractor (none; independent).

**Prerequisites**: 3D texture support in RHI (verify in `RHITextureHandle`).

**Implementation Steps**:

1. **Design vector field format** (0.5 day)
   - 3D texture, RGB16F or RG16F_SNORM
   - World-space scale (cm per voxel)
   - Animation (offset into texture each frame)

2. **Implement HLSL** (1 day)
   - Sample 3D texture at particle world position
   - Apply scale, offset, strength multiplier

3. **Implement C++ module** (1.5 days)
   - Load 3D texture from disk (or generate procedural)
   - Parameter: texture handle, scale, strength, offset

4. **Create test texture** (1 day)
   - Generate 32³ 3D texture with Perlin noise velocity field
   - Save as `.dds` (3D texture format)

5. **Test** (1 day)
   - Particles follow wind field
   - Scaling affects particle behavior
   - Performance acceptable (sample per particle acceptable)

**Effort**: M (5-6 days)

**Deliverables**:
- `vector_field_module.h/cpp` + `.hlsli`
- Test vector field texture (32³)
- Updated ModuleRegistry
- Demo effect + screenshot

---

### 3.67.2: Sub-Emitter System (Event-Driven Spawning)

**Status**: Not started.

**Dependencies**: Event system refactor (required).

**Complexity**: High; requires inter-emitter messaging + event queue.

**Implementation Steps**:

1. **Refactor VFX event system** (2 days)
   - `VFXEvent` → `ParticleEventQueue` (ring buffer)
   - Event payload: position, velocity, type, emitter ID
   - Event handler framework

2. **Implement sub-emitter support** (2 days)
   - `SubEmitterDef` struct in `.uzvfx` format
   - Child emitter instantiation on event
   - Velocity inheritance from parent

3. **Implement event handlers** (1.5 days)
   - `SpawnOnDeathModule` (triggered by age == lifespan)
   - `SpawnOnCollisionModule` (if collision detected)
   - `SpawnOnAgeModule` (at specific age threshold)

4. **GPU event queue kernel** (1 day)
   - Compute pass reads alive count + event triggers
   - Appends events to GPU queue
   - CPU reads back event queue

5. **Test** (1 day)
   - Parent effect spawns children on death
   - Child spawn count + velocity inheritance correct
   - Budget respected (spawned children count against total)

**Effort**: L (8-10 days)

**Deliverables**:
- Refactored `vfx_event.h` + event queue system
- `SpawnOnDeathModule` + related modules
- Event handler compute kernel
- Test composite effect (parent → children)
- Demo screenshot

**Milestone**: Players can now create compound effects (explosions with sparks, water splashes with droplets, etc.).

---

### 3.67.3: Advanced Constraint Modules (Pin, Bounds)

**Status**: Not started.

**Dependencies**: None (utility features).

**Implementation Steps**:

1. **Bounds Constraint** (1 day)
   - Clamp particle position to AABB
   - Bounce or kill option
   - Used for containment effects (particles in box)

2. **Pin Constraint** (1 day)
   - Keep particle within distance of anchor point
   - Apply damping on velocity
   - Used for tether effects

3. **Register + test** (0.5 day)

**Effort**: S (2-3 days)

**Deliverables**:
- `pin_constraint_module.h/cpp`
- `bounds_constraint_module.h/cpp`
- Test effects demonstrating each constraint

---

### 3.67 Milestone

**When Complete**: Full simulation force system operational.

**Capabilities**:
- Attractors pull particles
- Vector fields affect particles (wind, vortex)
- Sub-emitters spawn children on parent death
- Constraints pin/bound particles

**Production Readiness**: Simulation matches UE5 Niagara scope (forces only, not P2P).

---

## Phase 3.68: Rendering Enhancements (Weeks 6-9)

**Focus**: Visual quality + artist flexibility.

### 3.68.0: Flipbook / Sprite Animation

**Status**: Not started.

**Dependencies**: None.

**Implementation Steps**:

1. **Design flipbook module** (0.5 day)
   - Parameters: frameCountX, frameCountY, FPS, loop
   - Frame advance based on particle age

2. **Implement HLSL** (0.5 day)
   - Compute frame index from age
   - Offset UV into grid

3. **Implement C++ module** (1 day)
   - `FlipbookAnimationModule`
   - Parameter validation (frame count > 0)

4. **Update sprite render shader** (0.5 day)
   - Apply flipbook UV offset

5. **Test** (1 day)
   - Create 4×4 sprite sheet
   - Particles animate through frames
   - Looping works correctly

**Effort**: M (3-4 days)

**Deliverables**:
- `flipbook_animation_module.h/cpp`
- Updated `sprite_render_module.hlsli`
- Test sprite sheet texture
- Demo effect

---

### 3.68.1: Custom Material / Shader Slot

**Status**: Not started.

**Dependencies**: None.

**Complexity**: Medium (conditional render path).

**Implementation Steps**:

1. **Extend ParticleEmitter** (0.5 day)
   - Add optional material handle
   - Add custom texture override

2. **Extend render pass** (1 day)
   - Check emitter for custom material
   - Branch to custom PSO if present
   - Fallback to default if not

3. **Update Lua API** (0.5 day)
   - Expose material slot to Lua

4. **Test** (1 day)
   - Create custom particle material (with custom PS)
   - Bind to emitter
   - Render with custom shader

**Effort**: L (3-4 days)

**Deliverables**:
- Extended `ParticleEmitter` struct
- Modified `ParticleRenderPass`
- Example custom particle material
- Test demonstration

---

### 3.68.2: Distortion / Refraction Particles

**Status**: Not started.

**Dependencies**: Post-process integration (may not exist yet).

**Complexity**: High (requires distortion buffer in render graph).

**Implementation Steps**:

1. **Add distortion render target** (1 day)
   - Allocate R16F or RG16F texture in render graph
   - Clear to (0.5, 0.5) each frame

2. **Implement distortion render pass** (2 days)
   - Separate from color pass
   - Outputs normal + intensity

3. **Implement distortion module** (1 day)
   - `DistortionParticleModule`
   - Parameter: distortion strength

4. **Integrate post-process** (1.5 days)
   - Modify tonemap/resolve to sample distortion
   - Apply UV offset to final color

5. **Test** (1 day)
   - Create distortion effect (heat haze)
   - Verify offset applied correctly
   - No performance regression

**Effort**: L (6-7 days)

**Deliverables**:
- `ParticleDistortionRenderPass`
- `distortion_particle_module.h/cpp`
- Updated post-process integration
- Demo effect (heat haze)

---

### 3.68.3: Strand / Hair Particle Rendering

**Status**: Not started.

**Dependencies**: None (independent geometry path).

**Implementation Steps**:

1. **Design strand geometry** (0.5 day)
   - Line segment per particle (from position to position + velocity)
   - Expand to quad in VS

2. **Implement shaders** (1 day)
   - Vertex shader: expand line to quad
   - Pixel shader: antialiased line rendering

3. **Implement render pass** (1.5 days)
   - `ParticleStrandRenderPass`
   - Use particle positions + velocities

4. **Test** (1 day)
   - Create strand effect (hair, grass, lines)
   - Visual quality acceptable

**Effort**: M (4-5 days)

**Deliverables**:
- `particle_strand_render_pass.h/cpp`
- Strand VS + PS shaders
- Demo effect (hair, grass)

---

### 3.68 Milestone

**When Complete**: Rich rendering toolkit.

**Capabilities**:
- Animated sprites (flipbook)
- Custom materials for specialized effects
- Distortion particles (heat haze, refraction)
- Strand/hair rendering (lines, grass, rope)

**Artist Impact**: Significant visual variety; assets now match UE5 Niagara scope.

---

## Phase 3.69: Editor & Tools (Weeks 10-14)

**Focus**: Developer iteration speed; asset authoring UX.

### 3.69.0: Curve Editor UI (VSCode Extension)

**Status**: Not started.

**Dependencies**: VSCode extension framework (exists).

**Effort**: M (4-5 days TypeScript)

**Implementation Steps**:

1. **Design curve data model** (0.5 day)
   - Key points: (age, value) pairs
   - Interpolation mode (linear, cubic)

2. **Implement curve renderer** (2 days)
   - HTML5 Canvas + React component
   - Grid background
   - Draw spline between keys
   - Draggable key points

3. **Implement editor logic** (1 day)
   - Add/remove key
   - Interpolation mode selector
   - Save to .uzvfx JSON

4. **Integrate hot-reload** (0.5 day)
   - Send curve data to engine
   - Engine hot-reloads effect

5. **Test** (1 day)
   - Create curves, verify smooth interpolation
   - Hot-reload curves into running effect

**Deliverables**:
- `vscode-uzengine/src/components/curve-graph.tsx`
- Curve editor panel in VSCode
- Integration with hot-reload WebSocket

---

### 3.69.1: Live VFX Preview Viewer

**Status**: Not started.

**Dependencies**: WebSocket handler in engine (needs extension).

**Effort**: M (4-5 days TypeScript + C++)

**Implementation Steps**:

1. **Extend engine WebSocket API** (1 day)
   - Add `vfx.preview` command
   - Render current effect + send screenshot

2. **Implement preview panel** (2 days)
   - VSCode panel showing live screenshot
   - Parameter sliders (float, vec3, etc.)
   - Refresh button

3. **Implement parameter binding** (1 day)
   - Slider changes → WebSocket → engine hot-reload

4. **Test** (1 day)
   - Adjust parameters in UI
   - See effect update live in engine

**Deliverables**:
- `vscode-uzengine/src/panels/vfx-preview-panel.tsx`
- Engine WebSocket handler extension
- Real-time parameter adjustment demo

---

### 3.69.2: Parameter Validation & Diagnostics

**Status**: Not started.

**Dependencies**: VFX schema definition (should exist).

**Effort**: S (2-3 days TypeScript)

**Implementation Steps**:

1. **Define validation rules** (0.5 day)
   - Module type exists in registry
   - Parameter types match definitions
   - Values within min/max

2. **Implement validator** (1 day)
   - Check each module
   - Validate parameters
   - Return diagnostic list

3. **Integrate VSCode diagnostics** (0.5 day)
   - Show squiggles on .uzvfx files
   - Hover for error details

4. **Test** (0.5 day)

**Deliverables**:
- `vscode-uzengine/src/vfx-validator.ts`
- VSCode language feature: diagnostics provider
- Example: invalid .uzvfx with error squiggles

---

### 3.69.3: Visual Graph Editor (Optional / Deferred)

**Status**: Sketch only.

**Dependencies**: Graph rendering library (D3.js, Cytoscape.js).

**Effort**: XL (10+ days TypeScript)

**Note**: **DEFER to Phase 3.70 unless team is large.** JSON editing is functional MVP.

---

### 3.69 Milestone

**When Complete**: Artist-friendly tooling.

**Capabilities**:
- Visual curve authoring (UI curves)
- Live preview of changes (real-time feedback)
- Error diagnostics (invalid assets caught early)
- (Optional) Visual graph editor (full node-based authoring)

---

## Phase 3.70: Advanced Features & Polish (Weeks 15-16+)

**Focus**: Performance; advanced simulation; optional nice-to-haves.

### 3.70.0: Frustum Culling (Performance)

**Effort**: M (3-4 days)

**Benefit**: 10-20% GPU savings on offscreen effects.

**Implementation**:
1. Cull kernel: test particles against frustum planes
2. Append live particles to culled list
3. Render pass reads culled list

---

### 3.70.1: Async Compute (If RHI Ready)

**Effort**: L (depends on RHI)

**Benefit**: 1-2ms GPU latency reduction.

**Prerequisite**: RHI must expose compute queue.

---

### 3.70.2: Visual Graph Editor (If Resources Allow)

**Effort**: XL (10+ days)

**Benefit**: Non-technical artists can author effects.

---

### 3.70 Milestone

**When Complete**: Production-ready, feature-complete VFX system matching UE5 Niagara (minus P2P interaction and volumetric).

---

## Effort Summary

| Phase | Weeks | Features | Effort |
|-------|-------|----------|--------|
| 3.66 | 1-2 | Soft particles, GPU sort | 6-9 days |
| 3.67 | 3-5 | Attractors, vector fields, sub-emitters, constraints | 15-20 days |
| 3.68 | 6-9 | Flipbook, materials, distortion, strands | 16-20 days |
| 3.69 | 10-14 | Curve editor, live preview, validation | 10-12 days |
| 3.70 | 15-16+ | Frustum cull, async compute, graph editor | 5-15 days |
| **TOTAL** | **12-16 weeks** | **18 features** | **52-76 days** |

**Realistic Timeline**: 3-4 months (1 FTE engineer).

---

## Success Criteria

### Phase 3.66 Complete
- [ ] Soft particle fade working perfectly
- [ ] 50k particles render with correct depth order
- [ ] Sort < 1ms on GPU

### Phase 3.67 Complete
- [ ] Particles pulled by attractors (visually correct)
- [ ] Particles follow vector field
- [ ] Sub-emitters spawn children on death
- [ ] All constraint types working

### Phase 3.68 Complete
- [ ] Animated sprites (flipbook) render correctly
- [ ] Custom materials override default shader
- [ ] Distortion particles heat-haze effect visible
- [ ] Strands/hair render as lines

### Phase 3.69 Complete
- [ ] Curve UI editable, hot-reloads to engine
- [ ] Live preview panel shows effect changes in real-time
- [ ] Invalid .uzvfx assets show diagnostic squiggles

### Phase 3.70 Complete
- [ ] Frustum culling reduces GPU work for offscreen effects
- [ ] (Optional) Async compute working if RHI supports it
- [ ] (Optional) Visual graph editor fully functional

---

## Risk Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| GPU sort is complex | P0 feature blocks rendering | Study NVIDIA whitepaper + prototype first |
| Sub-emitters need event refactor | Event system entangled | Design event queue carefully before coding |
| Distortion requires post-process integration | Rendering path modified | Verify post-process exists; if not, defer |
| Visual graph editor scope creep | Timeline overrun | **DEFER to 3.70+ unless needed for MVP** |
| RHI doesn't support async compute | Performance optimization blocked | Fall back to single queue; not critical |

---

## Recommended Priorities (If Forced to Cut)

**If only 6 weeks available** (MVP), implement:
1. ✅ Soft particles (verification only)
2. ✅ GPU sort (critical for alpha blending)
3. ✅ Attractors (fundamental simulation)
4. ✅ Vector fields (environmental effects)
5. ✅ Flipbook (visual variety)
6. ✅ Curve editor UI (authoring UX)

**Defer to later**:
- Sub-emitters (complex, high-reward but can ship without)
- Custom materials (advanced feature)
- Distortion (visual polish)
- Strands (niche use case)
- Graph editor (nice-to-have)

---

## Conclusion

This roadmap brings UzEngine VFX to **production-ready parity** in **12-16 weeks**.

**Key wins**:
- Phase 3.66: Alpha blending correctness (GPU sort)
- Phase 3.67: Gameplay-ready simulation (forces, sub-emitters)
- Phase 3.68: Visual richness (rendering toolkit)
- Phase 3.69: Developer UX (live editing, curve UI)

**Success metric**: Shipped title with complex particle effects (explosions, weather, magic) that feel responsive and visually rich.

