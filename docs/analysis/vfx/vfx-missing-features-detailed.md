<!-- doc-meta
  title: VFX Missing Features — Detailed Specifications
  author: Claude Code
  date: 2026-03-25
  scope: Deep-dive specs for each of 18 missing features
  context: Quick reference for feature implementation planning
-->

# VFX Missing Features — Detailed Specifications

## TIER 1: Critical Simulation (Effort: 2-3 weeks)

### 1.1: Attractor Forces Module

**What It Does**: Pull particles toward a world-space point with configurable falloff.

**Parameters**:
- `attractorPosition` (Vec3): World position of attractor
- `attractorStrength` (float): Pull force magnitude (m/s²)
- `falloffRadius` (float): Maximum influence distance (cm)
- `falloffPower` (float): Distance falloff exponent (linear=1, quadratic=2, none=0)

**Shader Logic**:
```hlsl
float3 toAttractor = cbModule.attractorPosition - position;
float distSq = dot(toAttractor, toAttractor);
float radius = cbModule.falloffRadius;

if (distSq < radius * radius) {
    float dist = sqrt(distSq);
    float falloff = pow(1.0 - (dist / radius), cbModule.falloffPower);
    velocity += normalize(toAttractor) * cbModule.attractorStrength * falloff * dt;
}
```

**CPU Binding**: Module receives attractor position from component/parameter each frame (no readback needed).

**File Structure**:
- `runtime/engine/vfx/modules/attractor_module.h/cpp`
- `runtime/shaders/vfx/attractor_module.hlsli`

**Estimated Code**: 150 lines C++, 100 lines HLSL

---

### 1.2: Vector Field Sampling Module

**What It Does**: Sample 3D velocity texture (e.g., wind, turbulence field) per particle.

**Parameters**:
- `vectorFieldTexture` (RHITextureHandle): 3D texture sampler
- `vectorFieldScale` (Vec3): World-space scale (cm per texel)
- `vectorFieldStrength` (float): Magnitude multiplier
- `vectorFieldOffset` (Vec3): Texture UV offset (animation)

**Shader Logic**:
```hlsl
float3 samplePos = (position - cbModule.vectorFieldOffset) / cbModule.vectorFieldScale;
float3 fieldVelocity = vectorFieldTexture.SampleLevel(trilinearSampler, samplePos, 0).xyz;
velocity += fieldVelocity * cbModule.vectorFieldStrength * dt;
```

**GPU Texture Format**: RG16F_SNORM or RGB16F (3-component velocity)

**File Structure**:
- `runtime/engine/vfx/modules/vector_field_module.h/cpp`
- `runtime/shaders/vfx/vector_field_module.hlsli`

**Estimated Code**: 200 lines C++, 80 lines HLSL

---

### 1.3: Sub-Emitter System

**What It Does**: Spawn child particles when parent dies, collides, or reaches age threshold.

**Event Types**:
- `OnDeath`: Trigger when age >= lifespan
- `OnCollision`: Trigger when collision detected
- `OnAge`: Trigger when particle age reaches threshold

**Data Flow**:
1. Particle death/event triggers → append event to event queue
2. Event queue read by event handler compute kernel
3. Handler spawns N children from parent death position/velocity

**Structures**:
```cpp
struct ParticleEvent {
    uint32_t parentIndex;      // Which particle triggered
    Vec3 position;             // Death position
    Vec3 velocity;             // Inherited velocity
    uint8_t eventType;         // OnDeath, OnCollision, OnAge
    uint32_t eventDataOffset;  // Custom data (per event type)
};

struct SubEmitterDef {
    std::string childGraphName;    // Asset path to .uzvfx
    uint32_t spawnCountPerEvent;   // Children per death
    float velocityInheritScale;    // 0.5 = inherit 50% of parent velocity
};
```

**GPU Event Queue**: Ring buffer, max 1000 events/frame (configurable).

**File Structure**:
- `runtime/engine/vfx/events/particle_event_queue.h/cpp`
- `runtime/engine/vfx/modules/spawn_on_death_module.h/cpp`
- `runtime/shaders/vfx/event_handler_cs.hlsl`

**Estimated Code**: 400 lines C++, 200 lines HLSL, 100 lines refactor (event system)

---

### 1.4: Advanced Constraint Modules

**Pin Constraint** (Keep particle tethered):
```cpp
// Particle cannot move more than maxDistance from anchorPoint
struct PinConstraintModule : IVFXModule {
    Vec3 anchorPoint = {};
    float maxDistance = 100.0f;  // cm
    float damping = 0.95f;       // velocity reduction
};
```

**String Constraint** (Maintain max distance between particles):
- Requires pairwise distance tracking (sparse, not all-pairs)
- Use spatial hash to find nearby particles

**Bounds Constraint** (Clamp position to AABB):
```cpp
struct BoundsConstraintModule : IVFXModule {
    Vec3 boundsMin = {};
    Vec3 boundsMax = {};
    bool bounce = true;  // or kill
    float bounceCoeff = 0.8f;
};
```

**File Structure**:
- `runtime/engine/vfx/modules/pin_constraint_module.h/cpp`
- `runtime/engine/vfx/modules/bounds_constraint_module.h/cpp`
- `runtime/shaders/vfx/constraints.hlsli`

**Estimated Code**: 200 lines C++ (3 modules), 150 lines HLSL

---

## TIER 2: Rendering Features (Effort: 3-4 weeks)

### 2.1: Soft Particles (Depth Fade) — Polish Pass

**Status**: Likely 90% done; needs verification.

**Verify Checklist**:
- [ ] `ParticleBillboardConstants.softParticleFadeRange` passed to PS
- [ ] Pixel shader reads scene depth via bindless texture
- [ ] Smoothstep fade applied: `alpha *= smoothstep(fadeRange, 0, depthDifference)`
- [ ] Test with various particle sizes (1 cm, 100 cm)
- [ ] Test with camera very close to particles (should fade to 0)

**Shader Integration Point**: `particle_billboard_ps.hlsl` line ~80

**Estimated Code**: 50 lines shader review + fix (if needed)

---

### 2.2: Flipbook/Sprite Sheet Animation

**What It Does**: Advance sprite frame based on particle age or velocity; sample appropriate UV from grid.

**Parameters**:
```cpp
struct FlipbookAnimationModule : IVFXModule {
    uint32_t frameCountX = 4;       // Columns
    uint32_t frameCountY = 4;       // Rows
    float framesPerSecond = 30.0f;  // Playback speed
    bool loopAnimation = true;
};
```

**Shader Logic**:
```hlsl
// Frame index based on age
float frameFloat = (age / lifespan) * (frameCountX * frameCountY);
uint frame = clamp((uint)frameFloat, 0, frameCountX * frameCountY - 1);
uint frameX = frame % frameCountX;
uint frameY = frame / frameCountX;

// Grid UV offset
float2 frameUV = spriteUV / float2(frameCountX, frameCountY);
float2 baseUV = float2(frameX, frameY) / float2(frameCountX, frameCountY);
float2 finalUV = baseUV + frameUV;
```

**GPU Texture Format**: Squarish texture (e.g., 2048x2048 grid of 512x512 frames = 4x4 grid).

**File Structure**:
- `runtime/engine/vfx/modules/flipbook_animation_module.h/cpp`
- `runtime/shaders/vfx/flipbook_animation.hlsli`

**Estimated Code**: 200 lines C++, 100 lines HLSL

---

### 2.3: Distortion/Refraction Particles

**What It Does**: Particles write normal + intensity to distortion buffer; post-process samples it.

**Data Flow**:
1. Distortion render pass: render particles to distortion texture (R16_FLOAT for intensity, or RG16F for normal)
2. Post-process pass: sample distortion texture, apply screen UV offset to scene color

**Shader (PS)**:
```hlsl
// Output distortion intensity (read by post-process)
// Normal + intensity
float4 PSDistortion(PSInput input) : SV_Target {
    float distortionStrength = cbModule.distortionStrength;  // 0.05 = 5% screen offset
    float3 distortionNormal = normalize(worldNormal);
    return float4(distortionNormal * distortionStrength, 1.0);
}
```

**Post-Process Integration**:
```hlsl
// In tonemap/resolve pass
float4 sceneColor = sceneTexture.Sample(linearSampler, uv);
float4 distortion = distortionTexture.Sample(linearSampler, uv);
float2 distortedUV = uv + distortion.xy;
float4 distortedColor = sceneTexture.Sample(linearSampler, distortedUV);
return lerp(sceneColor, distortedColor, distortion.w);
```

**File Structure**:
- `runtime/engine/vfx/passes/particle_distortion_render_pass.h/cpp`
- `runtime/shaders/vfx/particle_distortion_ps.hlsl`
- Modify `tone_mapping_ps.hlsl` to sample distortion buffer

**Estimated Code**: 250 lines C++ (pass + module), 150 lines HLSL

---

### 2.4: Custom Material/Shader Support

**What It Does**: Allow emitters to reference optional material; render pass uses custom shader instead of billboard.

**Design**:
```cpp
struct ParticleEmitter {
    // ... existing fields ...
    RHITextureHandle customMaterialTexture;  // Optional override texture
    RHIMaterialHandle customMaterial;         // Optional material (if materials exist)
    bool useCustomShader = false;
};
```

**Render Path**:
1. If `customMaterial` set: use material's PSO
2. Else if `customMaterialTexture` set: use default billboard with custom texture
3. Else: use default billboard with sprite texture

**File Structure**:
- Extend `ParticleEmitter` with material slot
- Extend `ParticleRenderPass` to query material from emitter
- Update `particle_render_pass.cpp` dispatch logic

**Estimated Code**: 150 lines C++ (mostly conditional logic)

---

### 2.5: Volumetric Particles (Advanced)

**What It Does**: Particles accumulate into 3D voxel grid for volumetric rendering (fog, dust, smoke).

**Architecture**:
1. Froxel grid compute pass: Allocate 3D texture (32×32×32 or 64×64×64)
2. Particle accumulation CS: For each particle, atomically add density to surrounding voxels
3. Lighting pass: Sample voxel grid for shadows/indirect

**Complexity**: Very high; requires:
- Atomic float (or uint with float packing) in 3D texture
- Voxel-to-world space coordinate transform
- Lighting integration (shadows from voxel grid)

**Estimated Code**: 800+ lines (new passes only)

**Recommendation**: **Defer to Phase 3.70+** (lower ROI than other Tier 2 features)

---

### 2.6: Strand/Hair Particle Rendering

**What It Does**: Render particles as line segments (for hair, rope, lightning effects).

**Data Layout**:
- Read positions from standard particle buffer
- Read velocity from standard particle buffer
- Strand width from sizes buffer
- Render quad per particle (2 triangles per particle spanning from position to position+velocity*length)

**Shader Structure**:
```hlsl
// Vertex shader: expand position to quad corners
// Pixel shader: render line segment with antialiasing
```

**File Structure**:
- `runtime/engine/vfx/passes/particle_strand_render_pass.h/cpp`
- `runtime/shaders/vfx/strand_render_vs.hlsl`
- `runtime/shaders/vfx/strand_render_ps.hlsl`

**Estimated Code**: 250 lines C++, 200 lines HLSL

---

## TIER 3: Performance & Optimization (Effort: 2-3 weeks)

### 3.1: GPU Sorting (Radix Sort)

**What It Does**: Sort particle indices by depth (back-to-front) for correct alpha blending.

**Algorithm**: 2-pass radix sort (uint32 depth key)
- Pass 1: Sort by bits [0:7]
- Pass 2: Sort by bits [8:31]

**Integration**:
1. Compute pass generates depth keys (compute Z per particle)
2. Radix sort kernel (2 dispatches)
3. Render pass reads sorted indices buffer

**File Structure**:
- `runtime/engine/vfx/passes/particle_depth_sort_pass.h/cpp`
- `runtime/shaders/vfx/particle_sort_cs.hlsl` (radix sort kernel)

**Estimated Code**: 400 lines C++ (pass + sort kernel), 300 lines HLSL

---

### 3.2: Frustum Culling

**What It Does**: Cull particles outside view frustum; reduce GPU work.

**Algorithm**:
1. For each particle, test AABB against 6 frustum planes
2. Append live particles to culled list via atomic append
3. Render pass reads culled list (indirect args filled by cull kernel)

**File Structure**:
- `runtime/engine/vfx/passes/particle_cull_pass.h/cpp`
- `runtime/shaders/vfx/particle_cull_cs.hlsl`

**Estimated Code**: 200 lines C++, 150 lines HLSL

---

### 3.3: Dynamic LOD (GPU-side Enhancement)

**What It Does**: Use GPU alive count (read last frame) to adjust LOD tier automatically.

**Current State**: `VFXSignificanceManager` exists; extend to use GPU readback.

**Modification**:
```cpp
// Old: CPU-estimated alive count
// New: Read GPU atomic from previous frame (1-frame latency)
uint32_t actualAliveCount = gpuReadbackBuffer[emitterIndex];
if (actualAliveCount > maxForLODTier0) {
    emitter->SetLODTier(1);  // Reduce spawn rate
}
```

**File Structure**:
- `runtime/engine/vfx/budget/vfx_significance_manager.cpp` (modify)
- `runtime/engine/vfx/passes/particle_readback_pass.h/cpp` (new)

**Estimated Code**: 100 lines C++

---

### 3.4: Async Compute (If RHI Supports Compute Queue)

**What It Does**: Dispatch particle sim on separate compute queue; overlap with render.

**Prerequisite**: RHI must support compute queue (`IRHIDevice::CreateComputeQueue()`).

**Design**:
1. Particle sim compute dispatches on compute queue
2. GPU fence signals when sim complete
3. Render pass waits on fence before reading particle data

**File Structure**:
- Extend `ParticleSimulationPass` to use compute queue
- Add fence synchronization

**Estimated Code**: 200 lines C++ (if RHI ready)

---

## TIER 4: Authoring & Tools (Effort: 2-3 weeks)

### 4.1: Curve Editor UI

**What It Does**: Web-based curve editor for age-based particle properties.

**Data Model**:
```json
{
  "type": "curve",
  "keys": [
    {"age": 0.0, "value": 1.0},
    {"age": 0.5, "value": 0.8},
    {"age": 1.0, "value": 0.0}
  ]
}
```

**UI Components**:
- Canvas graph with grid
- Key points (draggable)
- Interpolation mode selector (linear, cubic)
- Copy/paste curve data

**Integration**:
- VSCode extension panel showing curve editor
- On save, update .uzvfx JSON
- Hot-reload curve into running engine

**Technology Stack**: React + HTML5 Canvas or Plotly.js

**File Structure**:
- `vscode-uzengine/src/panels/curve-editor-panel.tsx`
- `vscode-uzengine/src/components/curve-graph.tsx`
- `vscode-uzengine/src/utils/curve-math.ts`

**Estimated Code**: 400 lines TypeScript/React

---

### 4.2: Live Preview Viewer

**What It Does**: VSCode panel showing live particle effect preview (via WebSocket to running engine).

**Data Flow**:
1. VSCode panel emits "preview" command via WebSocket
2. Engine receives, renders current .uzvfx, sends screenshot
3. Panel displays screenshot + parameter sliders
4. User adjusts sliders, engine hot-reloads

**File Structure**:
- `vscode-uzengine/src/panels/vfx-preview-panel.tsx`
- Engine WebSocket handler (expand existing `ws_handlers.cpp`)

**Estimated Code**: 300 lines TypeScript + 150 lines C++

---

### 4.3: Visual Graph Editor (Large Effort)

**What It Does**: Full node-based VFX graph editor in VSCode Web panel.

**Components**:
- Node types (spawn, update, decay, decay, render, events, parameters)
- Connection validation (prevent invalid module chains)
- Serialization to .uzvfx JSON
- Property panels per node

**Challenges**:
- Graph layout + pan/zoom (use D3.js or Cytoscape.js)
- Real-time validation
- Hot-reload feedback

**File Structure**:
- `vscode-uzengine/src/panels/graph-editor-panel.tsx` (main)
- `vscode-uzengine/src/components/graph-node.tsx`
- `vscode-uzengine/src/components/graph-renderer.tsx`
- `vscode-uzengine/src/utils/graph-serializer.ts`

**Estimated Code**: 2000+ lines TypeScript (major component)

**Recommendation**: **Phase 3.69+** (lower priority; JSON editing sufficient for MVP)

---

### 4.4: Parameter Validation & Diagnostics

**What It Does**: Validate .uzvfx files; emit VSCode diagnostics (squiggles, warnings).

**Validation Rules**:
- [ ] All module types registered
- [ ] Parameter types match module definitions
- [ ] Values within min/max ranges
- [ ] Required resources exist (textures, materials)
- [ ] Event handlers reference valid sub-emitters

**Integration**:
```ts
// VSCode extension
const diagnostics = await validateUzvfx(documentPath);
vscode.languages.setDiagnostics(documentUri, diagnostics);
```

**File Structure**:
- `vscode-uzengine/src/vfx-validator.ts`
- `vscode-uzengine/src/language-features.ts` (diagnostics provider)

**Estimated Code**: 200 lines TypeScript

---

## Summary Table

| Feature | Effort | Priority | Blocks | Implementation Complexity |
|---------|--------|----------|--------|--------------------------|
| Attractors | M | P0 | Forces system | Low: standard physics |
| Vector Fields | M | P0 | Advanced environments | Low: texture sampling |
| Sub-Emitters | L | P0 | Gameplay events | High: event queue, messaging |
| Constraints | S | P1 | Physics completeness | Low: constraint math |
| Soft Particles | S | P1 | Visual quality | Low: likely done |
| Flipbook | M | P1 | Visual variety | Low: UV math |
| Distortion | L | P1 | Effects quality | Medium: post-process integration |
| Material Slots | L | P1 | Artist flexibility | Medium: render path branching |
| Volumetric | XL | P2 | Advanced rendering | Very High: voxel grid + lighting |
| GPU Sort | L | P0 | Rendering correctness | High: radix sort kernel |
| Frustum Cull | M | P2 | GPU optimization | Low: plane tests |
| LOD (GPU) | S | P2 | Performance | Low: readback integration |
| Async Compute | L | P2 | GPU latency | Medium: if RHI ready |
| Curve Editor | M | P1 | Authoring UX | Low: Canvas + React |
| Live Preview | M | P1 | Dev iteration | Medium: WebSocket + rendering |
| Graph Editor | XL | P3 | Artist UX | Very High: graph library + serialization |
| Validation | S | P2 | Asset health | Low: rule engine |
| Strands | M | P1 | Effect variety | Low: geometry expansion |

---

## Implementation Dependencies

**Hard Dependencies** (must do first):
1. Soft Particles (likely done; just verify)
2. GPU Sort (fixes alpha blending; unblocks rendering features)

**Soft Dependencies** (can parallelize):
3. Attractors, Vector Fields → Forces system
4. Sub-Emitters → Event refactor
5. Flipbook, Distortion, Material Slots → Rendering enhancements (independent)

**Tier 4** (can defer 1-2 phases):
- Curve Editor, Live Preview, Graph Editor (improve workflow but not critical)
- Validation (asset quality assurance)

---

## Recommended Quick-Win Features (2-week sprint)

If you have **2 weeks before a release deadline**:

1. **Soft Particles** (S) — verify & polish (~1 day)
2. **GPU Sort** (L) — implement radix sort (~4 days)
3. **Flipbook** (M) — sprite animation (~3 days)
4. **Curve Editor** (M) — UI authoring (~2 days)

**Result**: Visual quality improvements + artist iteration speed, ~10 days work.

