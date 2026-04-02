<!-- doc-meta
type: analysis
subsystem: vfx
feature: niagara-parity-gap
tags: [particles, vfx, gpu-rendering, simulation]
created: 2026-03-30
updated: 2026-03-30
status: draft
-->

# Niagara VFX Missing Features — Priority Ranking

UzEngine currently implements the core GPU particle simulation pipeline but lacks several major feature systems from UE5's Niagara. This analysis ranks the TOP 15 missing features by **visual impact × feasibility** based on UE5 5.6 source code analysis.

## Executive Summary

- **Total Niagara Data Interfaces in UE5**: 40+ (audio, landscape, skeletal mesh, render targets, vector fields, sparse volume textures, etc.)
- **Renderers in UE5**: Sprite (✓ done), Mesh, Ribbon/Trail (✓ done), Light, Decal, Component, Volume
- **System Features**: Neighbor Grid (particle-particle interaction), Async GPU Trace (GPU raycasts), Sparse Volume Texture sampling, Render Target 2D/3D I/O, Audio Spectrum, Skeletal Mesh sampling, Landscape sampling, Camera data

---

## TOP 15 MISSING FEATURES

| # | Feature | One-Line Description | Est. Hours | Impact | Feasibility | Priority |
|---|---------|----------------------|-----------|--------|-------------|----------|
| **1** | **Mesh Renderer** | Render particles as instanced 3D meshes with LOD, per-particle scale/rotation, mesh index binding | 40–50 | ★★★★★ | ★★★★☆ | **P0** |
| **2** | **Light Renderer** | Per-particle dynamic lights with radius, falloff, inverse-squared attenuation, brightness modulation | 35–45 | ★★★★★ | ★★★★☆ | **P0** |
| **3** | **Decal Renderer** | Per-particle screen-space decals with orientation, size, fade, sort order, normal mapping | 30–40 | ★★★★☆ | ★★★★☆ | **P0** |
| **4** | **Skeletal Mesh DI** | Spawn particles from bone positions/surfaces, weighted sampling, UV mapping | 50–70 | ★★★★☆ | ★★★☆☆ | **P1** |
| **5** | **Audio Spectrum DI** | Constant-Q FFT analysis of audio channels, normalized band sampling (0–1 range) | 35–50 | ★★★★☆ | ★★★☆☆ | **P1** |
| **6** | **Neighbor Grid 3D** | Spatial hashing for particle-to-particle queries (collision avoidance, flocking) | 40–60 | ★★★★☆ | ★★★☆☆ | **P1** |
| **7** | **Vector Field DI** | Sample 3D vector fields (smoke, flow simulation) for force application | 20–30 | ★★★☆☆ | ★★★★★ | **P1** |
| **8** | **Landscape DI** | Spawn/project particles onto landscape surfaces, height queries | 30–45 | ★★★☆☆ | ★★★☆☆ | **P1** |
| **9** | **Render Target 2D/3D DI** | GPU-to-GPU async I/O (particles write to texture, or read from texture for spawning) | 40–50 | ★★★★☆ | ★★★☆☆ | **P1** |
| **10** | **Async GPU Trace DI** | Deferred GPU raycasts against scene (ray-terrain, ray-mesh collision) | 35–50 | ★★★☆☆ | ★★☆☆☆ | **P2** |
| **11** | **Sparse Volume Texture DI** | Sample sparse 3D volume textures (weather, fog, procedural density) | 25–40 | ★★★☆☆ | ★★★☆☆ | **P2** |
| **12** | **Dynamic Material Parameters** | Bind per-particle vec4 to material dynamic params (glow, dissolve, custom vertex deformation) | 20–30 | ★★★★☆ | ★★★★★ | **P1** |
| **13** | **Camera Data DI + Distance Sort** | Camera position/FOV, distance-based particle sorting for translucency | 20–30 | ★★★☆☆ | ★★★★★ | **P2** |
| **14** | **Component Renderer** | Spawn arbitrary UObject components as particles (custom visual behavior) | 45–70 | ★★★☆☆ | ★★☆☆☆ | **P2** |
| **15** | **Collision Query DI** | GPU-resident collision detection against world primitives (scene captures, static meshes) | 40–60 | ★★☆☆☆ | ★★☆☆☆ | **P3** |

---

## DETAILED ANALYSIS

### P0 TIER (Highest Priority)

#### **1. Mesh Renderer** (40–50 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraMeshRendererProperties.h`

**What it does**:
- Renders each particle as an instanced 3D mesh instead of sprite
- Per-particle scale (XYZ), rotation (quaternion), mesh index selection
- Facing modes: Default (particle rotation), Velocity-aligned, Camera-facing
- LOD control per-particle (screen size, distance) or fixed LOD level
- Pivot offset in world/local/mesh space
- Material remapping per mesh

**Why it matters**:
- Enables complex particle effects (explosions with chunks, mechanical effects, swarms of objects)
- Visual fidelity jump from 2D sprites to 3D geometry
- Already have RHI infrastructure (instancing, indirect draw)

**Implementation plan**:
1. Add `MeshIndices`, `Scale`, `Rotation` (quaternion) particle attributes
2. Create `MeshRendererPass` compute shader (cull + instance data generation)
3. Bind mesh SRV/material texture arrays
4. Execute indirect draw with instance data
5. Shader permutations for facing modes (reuse billboard_lit logic)

**Estimated breakdown**:
- GPU memory layout + attribute binding: 8h
- Compute cull + instance gen: 12h
- Shader permutations: 8h
- Rendering pass integration: 12h
- Testing: 10h

---

#### **2. Light Renderer** (35–45 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraLightRendererProperties.h`

**What it does**:
- Each particle becomes a point light (no geometry rendered)
- Radius, intensity, color (from particle color)
- Falloff: inverse-squared (physically correct) or custom exponent
- Alpha → brightness multiplier
- Optional: shadow casting, translucency affect, MegaLights integration (UE5.6+)

**Why it matters**:
- Massive visual impact for magic effects, explosions, fire, neon
- Particle lights are cheaper than static lights, enable dynamic gameplay feedback
- Already have lighting CB infrastructure

**Implementation plan**:
1. Create `light_descriptor` buffer (position, radius, intensity, color)
2. CPU: fill light descriptors from particle data
3. Integrate with deferred lighting pass (read light buffer, accumulate)
4. Shader: inverse-squared falloff + optional shadow raymarch
5. Clipping lights outside camera frustum

**Estimated breakdown**:
- Light buffer creation + CPU gathering: 8h
- Deferred lighting integration: 12h
- Inverse-squared falloff shader: 6h
- Testing + optimization: 10h
- Shadow support (optional): 5h+

---

#### **3. Decal Renderer** (30–40 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraDecalRendererProperties.h`

**What it does**:
- Each particle becomes a screen-space decal (orientation, size, fade)
- Decal parameters: rotation (quaternion), size (XYZ), fade (0–1)
- Render to G-buffer (normal, basecolor, roughness, etc.) or deferred decal pass
- Sort order control (layer stacking)
- Visible flag per-particle

**Why it matters**:
- Enables splat effects, bullet holes, scorch marks, blood splatters
- Cheap compared to mesh geometry
- Standard in AAA games (Unreal, Frostbite, etc.)

**Implementation plan**:
1. Create decal descriptor buffer (position, orientation, size, fade, params)
2. CPU: gather decal data from particles
3. Decal geometry shader or VS expansion to full-screen quad per decal
4. Decal shader: sample source material, apply to deferred targets
5. Optional depth-fade to avoid hard edges on surfaces

**Estimated breakdown**:
- Descriptor creation + gathering: 6h
- Geometry/quad setup: 8h
- Decal shader + blending: 10h
- Integration with deferred + testing: 8h

---

### P1 TIER (Medium-High Priority)

#### **4. Skeletal Mesh Data Interface** (50–70 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceSkeletalMesh.h`

**What it does**:
- Spawn particles from skeletal mesh bones (weighted random or nearest bone)
- Spawn from surface (triangles weighted by area)
- Bone position/velocity queries during simulation
- Pre-skinned vertex positions (CPU → GPU via structured buffer)
- Connectivity info (bone parents, hierarchy)

**Why it matters**:
- Character-based effects (blood spray from wounds, armor particle trails)
- Complex character-scale animations (spell effects on limbs, equipment trails)

**Implementation plan**:
1. SkeletalMeshComponent integration → bone transforms buffer
2. Bone matrix buffer SRV (world transform per bone)
3. Spawn compute: sample bone positions, random offset
4. Surface spawn: load triangle data, barycentric sampling
5. Query functions in simulation: bone position, velocity lookup

**Estimated breakdown**:
- SkeletalMeshComponent binding: 12h
- Bone/surface spawn compute: 15h
- Connectivity/query functions: 12h
- Shader integration: 12h
- Testing + rigging examples: 15h

---

#### **5. Audio Spectrum Data Interface** (35–50 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceAudioSpectrum.h`

**What it does**:
- Real-time FFT (Constant-Q transform) of audio input
- Band sampling: queries return normalized amplitude (0–1) at frequency bands
- Frequency range configurable (e.g., 20Hz–20kHz)
- Optional: per-channel (L/R stereo)
- Noise floor threshold (ignore silence)

**Why it matters**:
- Music-reactive VFX (beat pulses, frequency-driven fountains, EQ visualizers)
- Very high-impact effect visually, relatively isolated feature

**Implementation plan**:
1. Audio submix callback registration (capture game audio)
2. CPU FFT (Opus codec has CQT, or build FFT via DSP library)
3. Fill spectrum buffer (SRV of float4 per band)
4. Shader function: `GetSpectrumValue(normalized_freq_0_to_1) → float amplitude`
5. Particle spawn/update: sample spectrum, modulate velocity/scale

**Estimated breakdown**:
- Audio submix registration: 8h
- FFT/CQT computation (or DSP lib integration): 12h
- Buffer management + syncing: 8h
- Shader binding + functions: 6h
- Testing with audio assets: 5h

---

#### **6. Neighbor Grid 3D Data Interface** (40–60 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceNeighborGrid3D.h`

**What it does**:
- Spatial hash grid (user-configurable cell size)
- Each cell: list of particle indices (up to max neighbors per cell)
- GPU compute: bin particles, query neighbors
- Query function: `GetNeighbors(particle_position) → particle_id_list`
- Use cases: flocking, collision avoidance, N-body forces

**Why it matters**:
- Enables flocking, schooling, particle-particle repulsion effects
- Architectural requirement for advanced sims (springs, constraints)

**Implementation plan**:
1. Pre-compute grid dimensions from world bounds
2. Compute pass 1: clear grid + atomic count neighbors per cell
3. Compute pass 2: bin particle indices into grid cells
4. GPU query function in simulation: read neighbor list from grid
5. Force application: iterate neighbors, apply repulsion/attraction

**Estimated breakdown**:
- Grid allocation + CB setup: 8h
- Compute binning passes: 12h
- Atomic operations + synchronization: 8h
- Query functions in simulation: 12h
- Testing + tuning: 8h

---

#### **7. Vector Field Data Interface** (20–30 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceVectorField.h`

**What it does**:
- Sample a 3D vector field texture (velocity field, flow field)
- Tiling modes (X/Y/Z) for seamless wrapping
- Trilinear filtering native to GPU sampling
- Query: `SampleVectorField(world_position) → velocity`

**Why it matters**:
- Cheap force without expensive compute (pre-baked smoke sim, flow fields)
- Seamlessly integrates with wind/drift simulation

**Implementation plan**:
1. Load/create 3D texture (volume texture in content)
2. Sampler binding (trilinear, wrap/clamp configurable)
3. Shader: simple `Texture3D.Sample(position_normalized)`
4. Apply result as acceleration force each frame

**Estimated breakdown**:
- Texture binding + sampler setup: 6h
- Shader functions: 6h
- World-to-UV transform logic: 6h
- Testing: 4h

---

#### **8. Landscape Data Interface** (30–45 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceLandscape.h`

**What it does**:
- Query landscape height at XY position
- Material info (physical material for effects)
- Spawn particles on landscape surface
- Project particles onto landscape (character footsteps)

**Why it matters**:
- Environment-aware effects (grass impact clouds, dust trails, rain splashes)
- Required for terrain-scale VFX (earthquake debris, dust storms)

**Implementation plan**:
1. Landscape component integration (query heightfield)
2. GPU heightfield texture binding (LOD0 heightmap as R32F)
3. Heightmap UV lookup from world position
4. Spawn: offset particles upward from landscape surface
5. Queries: height, normal, material ID per position

**Estimated breakdown**:
- Landscape component API: 8h
- Heightmap texture binding: 8h
- World-to-heightmap UV: 6h
- Shader functions + normals: 8h
- Testing: 6h

---

#### **9. Render Target 2D/3D Data Interface** (40–50 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceRenderTarget2D.h`

**What it does**:
- GPU particles write to render target (2D or 3D texture)
- Or read from render target as simulation input
- Example: particles paint on a canvas, or simulate based on a texture
- Mip generation (optional)

**Why it matters**:
- Feedback loops (particles modify environment, environment affects particles)
- Texture-based painting effects (wet paint, fire spread on maps)
- Complex simulation feedback

**Implementation plan**:
1. Render target allocation (2D/3D, configurable format)
2. UAV binding for particle compute writes
3. SRV binding for particle compute reads
4. Sync after compute (resource barriers)
5. Optional: mip generation pass

**Estimated breakdown**:
- RT allocation + format handling: 10h
- UAV/SRV binding + barriers: 8h
- Write compute logic: 12h
- Read compute logic: 8h
- Testing: 6h

---

#### **10. Dynamic Material Parameters** (20–30 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Internal/Stateless/Modules/NiagaraStatelessModule_DynamicMaterialParameters.h`

**What it does**:
- Per-particle `DynamicParameter0–3` (4D vectors, user-defined semantics)
- Bind to material dynamic parameters in shader
- Examples: glow intensity, dissolve amount, custom vertex deformation, UV scroll
- Runtime-modifiable per-particle

**Why it matters**:
- Multiplicative effect on material complexity without permutations
- Enables material-based animation (dissolve in/out, glow pulsing, etc.)

**Implementation plan**:
1. Particle attribute: `DynamicParam0–3` (float4 each)
2. Constant buffer: pass to PSO/material
3. Material shader: read from CB (auto-bound by name convention)
4. Example: multiply base color by `DynamicParam0.x`

**Estimated breakdown**:
- Particle attribute addition: 4h
- CB layout + binding: 6h
- Material integration: 6h
- Example materials + testing: 8h

---

### P2 TIER (Medium Priority)

#### **11. Camera Data Interface** (20–30 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceCamera.h`

**What it does**:
- Query camera position, rotation, FOV from simulation
- Distance-based particle sorting (for translucent particles)
- Optional: screen-space size queries (project to screen)

**Why it matters**:
- Sorting is critical for translucent effects (trails, smoke, fire)
- Already have camera data in frame CB; this just exposes it

**Implementation plan**:
1. Expose camera CB fields as DI functions
2. Distance sort: compute per-particle distance, write sort key
3. Bitonic sort pass (already have sorting for indices)
4. Reorder particles by sort key

**Estimated breakdown**:
- Camera exposure functions: 6h
- Distance compute: 6h
- Sort integration: 8h
- Testing: 4h

---

#### **12. Async GPU Trace Data Interface** (35–50 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceAsyncGpuTrace.h`

**What it does**:
- GPU raycasts against scene geometry (deferred one frame)
- Traces from particle position in a direction
- Returns: hit distance, hit normal, hit material
- Used for collision response (bounce, stick, kill)

**Why it matters**:
- GPU-side collision without CPU readback stalls
- Required for advanced collision (ray-terrain, ray-mesh)
- High-performance alternative to physics queries

**Implementation plan**:
1. Ray descriptor buffer (origin, direction, max distance per particle)
2. Hit result buffer (distance, normal, material_id)
3. Async compute pass: raycasts via DXR or scene capture
4. Delayed response: apply results next frame
5. Optional retry logic if ray hits backface

**Estimated breakdown**:
- Ray buffer setup: 8h
- DXR raycasting (or SDF-based alternative): 15h
- Hit result processing: 8h
- Delayed response handling: 6h
- Testing: 8h

**Note**: Requires either DXR support or conservative SDF-based ray approximation.

---

#### **13. Sparse Volume Texture Data Interface** (25–40 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceSparseVolumeTexture.h`

**What it does**:
- Sample sparse 3D volume texture (memory-efficient)
- Virtual texture pages accessed via page table
- Example: procedural density fields, weather clouds, baked smoke sim

**Why it matters**:
- Complex volumetric data without prohibitive VRAM
- Enables baked volumetric effects (cheaper than real-time compute)

**Implementation plan**:
1. Load sparse volume texture asset (PhysicalTileDataA/B, PageTable)
2. Bind page table + physical texture array
3. Shader: virtual address → physical address lookup
4. Sample virtual texture in simulation

**Estimated breakdown**:
- Sparse texture asset loading: 8h
- Page table + physical texture binding: 10h
- Virtual address shader logic: 8h
- Testing: 6h

---

#### **14. Component Renderer** (45–70 hours)
**Reference**: `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraComponentRendererProperties.h`

**What it does**:
- Spawn arbitrary UObject components as particles
- Bind particle attributes to component properties (rotation, scale, material param)
- Example: miniature flame effect instances, UI elements, audio cues

**Why it matters**:
- Ultimate flexibility for custom visual/logic effects
- Enables gameplay integration (particle = entity)

**Implementation plan**:
1. Component pool pre-allocation
2. Spawn: pull from pool, attach to world
3. Per-frame: update component transform/properties from particle data
4. Death: return to pool
5. Property binding system (name-based reflection)

**Estimated breakdown**:
- Component pool: 12h
- Spawn/death logic: 12h
- Transform/property binding: 15h
- Testing + performance tuning: 15h

---

### P3 TIER (Lower Priority)

#### **15. Collision Query Data Interface** (40–60 hours)
**Reference**: Generic collision DI (not shown, but standard in Niagara)

**What it does**:
- Query collision geometry in world (static meshes, primitives)
- Ray-cast, sphere-cast, line trace
- Returns: hit/no-hit, distance, normal, material

**Why it matters**:
- General-purpose collision for gameplay effects
- Less critical than async GPU trace (which is GPU-async)

**Implementation plan**:
1. Physics query abstraction (wrap physics engine API)
2. Buffer-based batch queries (array of rays/spheres)
3. Query execution (CPU or GPU deferred)
4. Result buffer filled with hit data

**Estimated breakdown**:
- Physics query API binding: 15h
- Batch query infrastructure: 12h
- Result gathering: 8h
- Testing: 10h

---

## IMPLEMENTATION ROADMAP

### Phase 4a (Immediate, Weeks 1–2)
1. **Mesh Renderer** — foundational for visual variety
2. **Light Renderer** — highest visual impact for code investment
3. **Dynamic Material Parameters** — multiplies material expressiveness

**Estimated effort**: 85–95 hours (2.5 weeks FT)

### Phase 4b (Weeks 3–4)
4. **Vector Field DI** — quick win, enables flow-based effects
5. **Camera DI** — needed for sorting translucent effects
6. **Landscape DI** — environment awareness

**Estimated effort**: 70–105 hours (2–3 weeks FT)

### Phase 4c (Weeks 5–8)
7. **Decal Renderer** — surface effects
8. **Audio Spectrum DI** — music-reactive effects
9. **Skeletal Mesh DI** — character-scale effects
10. **Neighbor Grid 3D** — advanced simulation

**Estimated effort**: 155–225 hours (4–6 weeks FT)

### Phase 4d (Optional, Weeks 9+)
11. **Render Target DI** — feedback loops
12. **Async GPU Trace** — high-performance collision
13. **Sparse Volume Texture** — baked volumetric
14. **Component Renderer** — ultimate flexibility
15. **Collision Query** — general-purpose collision

**Total unfinished**: ~250–350 hours (6–10 weeks FT depending on DXR availability)

---

## KEY ARCHITECTURAL NOTES

### Data Interface Pattern in UE5
Each DI is:
1. **CPU-side**: `UNiagaraDataInterface*` (property store, validation)
2. **GPU-side**: Proxy class + shader parameter binding
3. **Shader**: Generated HLSL functions + CB parameters
4. **Per-instance data**: GT (game thread) ↔ RT (render thread) sync via `PerInstanceData`

**UzEngine adaptation**:
- Use compute shader modules for each DI (spawn, update, query)
- Bind DI data as SRVs/UAVs in compute CBs
- HLSL-side module composition (DIs are includes, not inheritance)

### Renderer Types in UE5
Each renderer type is:
1. **CPU**: Gather particle data into dynamic buffers
2. **GPU**: Indirect draw + culling compute pass
3. **Shader**: Render pass-specific shaders (VS/PS or CS)

**UzEngine adaptation**:
- Follow existing billboard_renderer pattern
- One compute pass per renderer type (cull + instance data)
- Multi-render-target output for meshes/decals

### Performance Considerations
- **Mesh Renderer**: O(num_particles × num_meshes) in worst case; mitigate with LOD + frustum cull
- **Light Renderer**: Deferred lighting → O(num_lights × num_pixels); cap light count
- **Neighbor Grid**: O(num_particles) binning + O(neighbors_per_query); tunable cell size
- **Async GPU Trace**: DXR cost ~0.5–2 ms per 1M rays depending on scene complexity

---

## REFERENCES

### Source Code Files
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraMeshRendererProperties.h` (lines 23–100)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraLightRendererProperties.h` (lines 15–100)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraDecalRendererProperties.h` (lines 18–70)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceSkeletalMesh.h`
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceAudioSpectrum.h`
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceNeighborGrid3D.h`
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceVectorField.h`
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceLandscape.h`
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceRenderTarget2D.h`
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceAsyncGpuTrace.h`
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceSparseVolumeTexture.h`

### UE5 Version
- Analyzed: **UE5.6 source code**
- Architecture: GPU-driven compute simulation, deferred rendering integration, stateless + stateful pipelines
