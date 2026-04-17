<!-- doc-meta
title: Niagara Missing Features Audit
author: Claude Code
date: 2026-03-30
status: analysis
subsystem: vfx
-->

# Niagara Missing Features Audit for UzEngine

**Analysis Date**: 2026-03-30  
**Source**: Unreal Engine 5.6+ Niagara Stateless Module Library  
**Focus**: High visual impact + practical complexity

## Executive Summary

UzEngine has implemented core VFX systems (spawn, simulate, render). This audit identified **15 missing Niagara modules** ranked by visual impact vs. implementation complexity. The top 5 features are:

1. **Orbit/Rotate Around Point** — particles circle a point/axis (visual: glowing swirls, vortex effects)
2. **Drag/Air Resistance** — velocity-dependent damping (visual: heavy smoke, slower falloff)
3. **Sprite Facing Modes** — 5 camera-facing variants (visual: proper billboarding, pose variety)
4. **Camera Offset** — depth-sorting trick for closeup softness (visual: near-camera blur)
5. **Particle Lights** — each particle emits dynamic light (visual: glowing particles, light interaction)

---

## Features by Priority

### TIER 1: HIGH IMPACT + LOW/MEDIUM COMPLEXITY

#### 1. Orbit / Rotate Around Point
- **Visual Impact**: HIGH — particles swirl/orbit creates compelling vortex, spiral, halo effects
- **What It Does**: Particles orbit around a center point/axis with configurable radius, angular velocity, and initial phase
- **UE5 Source**: `NiagaraStatelessModule_RotateAroundPoint.h` (49 lines)
- **Parameters**:
  - `Rate` (deg/s, default 360) — angular velocity
  - `Radius` (100 cm) — orbit radius
  - `InitialPhase` (0°) — starting angle offset
  - `Center` (vec3, zero) — orbit center position
  - `CenterCoordinateSpace` (Simulation/World)
  - `RotationAxis` (vec3, zero) — axis of rotation
  - `RotationCoordinateSpace` (Simulation/World)
- **Estimated Complexity**: MEDIUM (GPU force application, rotation matrix math)
- **Integration**: Add compute shader module + update shader params

---

#### 2. Drag / Air Resistance
- **Visual Impact**: HIGH — particles slow naturally, heavy smoke/dust effects vs. ballistic
- **What It Does**: Applies velocity-dependent damping (not constant, velocity-proportional)
- **UE5 Source**: `NiagaraStatelessModule_Drag.h` (28 lines)
- **Parameters**:
  - `DragDistribution` (default 1.0) — drag coefficient (0.0 = no drag, 2.0+ = heavy air)
- **Estimated Complexity**: LOW (single scalar multiplication in velocity update)
- **Integration**: CPU-side or GPU compute shader, one-liner update

---

#### 3. Sprite Facing & Alignment (Multi-Mode Billboarding)
- **Visual Impact**: HIGH — proper billboard orientation for different aesthetic needs
- **What It Does**: 5 facing modes + custom alignment vectors
- **UE5 Source**: `NiagaraSpriteVertexFactory.ush` lines 20-37 + `NiagaraStatelessModule_SpriteFacingAndAlignment.h`
- **Facing Modes** (from vertex shader):
  - `FACING_MODE_FACECAMERA` (0) — look at camera origin, preserve camera up
  - `FACING_MODE_FACECAMERAPLANE` (1) — completely parallel to camera plane (flat look)
  - `FACING_MODE_CUSTOMFACINGVECTOR` (2) — face toward `SpriteFacing` vector attribute
  - `FACING_MODE_FACECAMERAPOSITION` (3) — face camera but use fixed up axis (0,0,1), no rotation dependency
  - `FACING_MODE_FACECAMERADISTANCEBLEND` (4) — blend between FACECAMERA and FACECAMERAPOSITION by distance
- **Alignment Modes**:
  - `ALIGNMENT_MODE_UNALIGNED` (0) — sprite rotation only
  - `ALIGNMENT_MODE_VELOCITYALIGNED` (1) — up arrow points along velocity
  - `ALIGNMENT_MODE_CUSTOMALIGNMENT` (2) — up arrow points along `SpriteAlignment` vector
- **Parameters** (from module):
  - `bSpriteFacingEnabled` (bool, default true)
  - `bSpriteAlignmentEnabled` (bool, default false)
  - `SpriteFacing` (vec3, default X-axis)
  - `SpriteAlignment` (vec3, default Y-axis)
- **Estimated Complexity**: MEDIUM (vertex shader modes already in UE5, UzEngine needs billboard matrix variants)
- **Integration**: Billboard vertex shader permutations (already has 1, expand to 5)

---

#### 4. Camera Offset
- **Visual Impact**: HIGH — enables closeup soft particles, depth fog fade near camera
- **What It Does**: Offsets particles along camera→particle vector (for visual softness near camera)
- **UE5 Source**: `NiagaraStatelessModule_CameraOffset.h` (32 lines)
- **Parameters**:
  - `CameraOffsetDistribution` (default 0.0) — offset distance (positive = away from camera)
- **Estimated Complexity**: LOW (vector math + 1 multiply-add per particle in vertex shader)
- **Integration**: Billboard position adjustment in vertex shader

---

#### 5. Particle Lights
- **Visual Impact**: HIGH — glowing particles with dynamic lighting, visible in dark scenes
- **What It Does**: Each particle emits a point light with dynamic falloff
- **UE5 Source**: `NiagaraStatelessModule_LightAttributes.h` (57 lines)
- **Parameters**:
  - `bApplyRadius` (bool)
  - `bApplyFalloffExponent` (bool)
  - `bApplyDiffuseScale` (bool)
  - `bApplySpecularScale` (bool)
  - `bApplyVolumetricScattering` (bool)
  - `Radius` (100 cm, default)
  - `FalloffExponent` (1.0, default) — controls light falloff curve
  - `DiffuseScale` (1.0, default) — light intensity multiplier
  - `SpecularScale` (1.0, default) — specular contribution
  - `VolumetricScattering` (0.0, default) — volumetric effect strength
- **Estimated Complexity**: HIGH (requires dynamic light culling, deferred light list management)
- **Integration**: Compute pass to generate light list from particle positions

---

### TIER 2: HIGH IMPACT + MEDIUM COMPLEXITY

#### 6. Mesh Index (Particle Mesh Variant Selection)
- **Visual Impact**: MEDIUM-HIGH — particles use different meshes based on index (variety, LOD)
- **What It Does**: Selects which mesh geometry to render for each particle
- **UE5 Source**: `NiagaraStatelessModule_MeshIndex.h` (42 lines)
- **Parameters**:
  - `MeshIndex` (int range, default 0)
  - `MeshIndexWeight` (array) — per-mesh selection weights
- **Estimated Complexity**: MEDIUM (requires mesh array, indirect draw with per-instance mesh selection)
- **Integration**: Extend particle buffer with mesh index, mesh renderer permutation

---

#### 7. Scale Sprite Size By Speed
- **Visual Impact**: MEDIUM-HIGH — motion blur illusion, speed-based visual scaling
- **What It Does**: Multiplies sprite size by velocity magnitude (stretches/shrinks based on speed)
- **UE5 Source**: `NiagaraStatelessModule_ScaleSpriteSizeBySpeed.h` (49 lines)
- **Parameters**:
  - `VelocityThreshold` (1000 cm/s, default) — speed at which scaling starts
  - `MinScaleFactor` (vec2, 1.0)
  - `MaxScaleFactor` (vec2, 2.0)
  - `bSampleScaleFactorByCurve` (bool) — use ramp curve instead of linear blend
  - `SampleFactorCurve` (distribution curve, default 0→1)
- **Estimated Complexity**: MEDIUM (speed calculation + scale curve lookup)
- **Integration**: GPU shader, read velocity and scale output

---

#### 8. Scale Mesh Size By Speed
- **Visual Impact**: MEDIUM-HIGH — mesh particles scale with speed
- **What It Does**: Scales 3D mesh particles based on velocity (same as #7 but for meshes)
- **UE5 Source**: `NiagaraStatelessModule_ScaleMeshSizeBySpeed.h`
- **Parameters**: Same as #7 but applies to 3D scale
- **Estimated Complexity**: MEDIUM (same math, different output)
- **Integration**: Mesh variant of sprite scaling module

---

#### 9. Sub-UV Animation (Texture Atlas Playback)
- **Visual Impact**: MEDIUM-HIGH — smooth flipbook animation on texture atlas
- **What It Does**: Animates texture coordinates through spritesheet frames
- **UE5 Source**: `NiagaraStatelessModule_SubUVAnimation.h` (73 lines)
- **Modes**:
  - `DirectSet` — fixed frame index
  - `InfiniteLoop` — loops through all frames at N fps
  - `Linear` — use particle age to index frame (0→1 age = 0→N frames)
  - `Random` — random frame selection with change interval
- **Parameters**:
  - `NumFrames` (16, default)
  - `FrameIndex` (int range, for DirectSet mode)
  - `bStartFrameRangeOverride_Enabled` (bool)
  - `bEndFrameRangeOverride_Enabled` (bool)
  - `AnimationMode` (enum)
  - `LoopsPerSecond` (1.0, for InfiniteLoop)
  - `RandomChangeInterval` (0.1s, for Random mode)
- **Estimated Complexity**: MEDIUM (frame index calculation, UV scaling)
- **Integration**: Texture UV offset in shader + animation state tracking

---

#### 10. Decal Attributes
- **Visual Impact**: MEDIUM — decal particles with rotation, size, fade
- **What It Does**: Sets decal-specific attributes (orientation, size fade) for decal-based rendering
- **UE5 Source**: `NiagaraStatelessModule_DecalAttributes.h` (51 lines)
- **Parameters**:
  - `bApplyOrientation` (bool)
  - `bApplySize` (bool)
  - `bApplyFade` (bool)
  - `Orientation` (vec3, zero)
  - `OrientationCoordinateSpace` (Local/World)
  - `Size` (vec3, 50cm default)
  - `Fade` (float, 1.0 default)
- **Estimated Complexity**: MEDIUM (requires decal renderer backend)
- **Integration**: New renderer type (decal vs. sprite/mesh)

---

### TIER 3: MEDIUM IMPACT + LOW COMPLEXITY

#### 11. Scale Color
- **Visual Impact**: MEDIUM — multiply particle color by gradient/curve
- **What It Does**: Scales color over lifetime or by parameter
- **UE5 Source**: `NiagaraStatelessModule_ScaleColor.h` (31 lines)
- **Parameters**:
  - `ScaleDistribution` (color, default white)
- **Estimated Complexity**: LOW (single multiply per particle)
- **Integration**: Already have color ramp; this is a scalar color multiplier

---

#### 12. Dynamic Material Parameters
- **Visual Impact**: MEDIUM — expose scalar/vector parameters to material for animation
- **What It Does**: Pass particle attributes to material shader parameters dynamically
- **UE5 Source**: `NiagaraStatelessModule_DynamicMaterialParameters.h`
- **Parameters**: User-defined per-emitter (scalar, vec2, vec3, vec4)
- **Estimated Complexity**: MEDIUM (material binding system)
- **Integration**: Material CB slots, data flow from particle → CB → shader

---

#### 13. Acceleration Force
- **Visual Impact**: LOW-MEDIUM — add constant/distributed acceleration to particles
- **What It Does**: Adds a force vector to particle acceleration each frame
- **UE5 Source**: `NiagaraStatelessModule_AccelerationForce.h`
- **Parameters**: Force vec3 distribution
- **Estimated Complexity**: LOW (add to acceleration in compute)
- **Integration**: Compute shader update

---

#### 14. Gravity Force (Explicit)
- **Visual Impact**: MEDIUM — applies gravitational acceleration
- **What It Does**: Adds gravity vector explicitly (separate from wind/vortex)
- **UE5 Source**: `NiagaraStatelessModule_GravityForce.h`
- **Parameters**: Gravity magnitude (separate from general acceleration)
- **Estimated Complexity**: LOW (add constant to acceleration)
- **Integration**: Compute shader update

---

#### 15. Curl Noise Force
- **Visual Impact**: MEDIUM — turbulent, non-repetitive motion
- **What It Does**: Samples curl noise (3D Perlin variant) as a force field
- **UE5 Source**: `NiagaraStatelessModule_CurlNoiseForce.h`
- **Parameters**:
  - Noise scale (frequency)
  - Noise strength (amplitude)
  - Noise tiling
  - Field tightness
- **Estimated Complexity**: LOW-MEDIUM (UzEngine already has curl noise texture; reuse)
- **Integration**: GPU texture fetch + force application (already implemented in Phase 3c)

---

## Not Found in Niagara (Possible Custom Features)

These were searched but not found in the standard Niagara module library:
- **Particle-to-particle attraction/repulsion** — would require spatial queries (not standard)
- **GPU Event Handlers** — death/collision events trigger new spawns (advanced, not core)
- **Attribute Readers** — read data from other emitters (data channel feature, not particle module)
- **Mesh Rotation Rate** — exists (`NiagaraStatelessModule_MeshRotationRate.h`) but similar to sprite rotation
- **Color by Speed** — not a standard module; implement as custom ramp (speed → color table lookup)

---

## Integration Priority for UzEngine

**Immediate (High ROI)**:
1. Drag/Air Resistance — 2 hours, huge visual impact
2. Sprite Facing Modes — 4 hours, fixes billboard variety
3. Camera Offset — 1 hour, depth sorting trick
4. Scale Size By Speed — 3 hours, motion blur effect
5. Orbit/Rotate Around Point — 6 hours, complex but creates dramatic effects

**Medium Term**:
6. Particle Lights — 8 hours, but requires light culling (blocks further work)
7. Sub-UV Animation — 4 hours, standard flipbook animation
8. Mesh Index — 5 hours, requires array support in renderer

**Later**:
9. Decal Attributes — 4 hours (new renderer type)
10. Dynamic Material Parameters — 6 hours (material binding system)
11. Gravity/Acceleration Forces — 2 hours each (simple adds)

---

## File References (UE5 Source)

| Feature | UE5 Path | Lines | Key Functions |
|---------|----------|-------|----------------|
| Orbit | `Stateless/Modules/NiagaraStatelessModule_RotateAroundPoint.h` | 49 | `BuildEmitterData()`, `SetShaderParameters()` |
| Drag | `Stateless/Modules/NiagaraStatelessModule_Drag.h` | 28 | `BuildEmitterData()` |
| Sprite Facing | `Shaders/Private/NiagaraSpriteVertexFactory.ush:20-37` + `.h` | 40 | Vertex shader facing modes |
| Camera Offset | `Stateless/Modules/NiagaraStatelessModule_CameraOffset.h` | 32 | `SetShaderParameters()` |
| Particle Lights | `Stateless/Modules/NiagaraStatelessModule_LightAttributes.h` | 57 | `GetFeatureMask()`, `SetShaderParameters()` |
| Mesh Index | `Stateless/Modules/NiagaraStatelessModule_MeshIndex.h` | 42 | Array binding |
| Scale Size/Speed | `Stateless/Modules/NiagaraStatelessModule_ScaleSpriteSizeBySpeed.h` | 49 | Curve lookup |
| Sub-UV | `Stateless/Modules/NiagaraStatelessModule_SubUVAnimation.h` | 73 | Frame indexing |
| Decal | `Stateless/Modules/NiagaraStatelessModule_DecalAttributes.h` | 51 | Feature mask |

---

## Summary Table

| # | Feature | Visual Impact | Complexity | Est. Hours | Priority | Notes |
|---|---------|---------------|-----------|-----------|----------|-------|
| 1 | Drag | HIGH | LOW | 2 | P0 | Velocity damping, huge visual difference |
| 2 | Sprite Facing | HIGH | MED | 4 | P0 | 5 billboard modes, variety |
| 3 | Camera Offset | HIGH | LOW | 1 | P0 | Depth sorting, closeup softness |
| 4 | Size by Speed | HIGH | MED | 3 | P1 | Motion blur illusion |
| 5 | Orbit | HIGH | MED | 6 | P1 | Spiral/vortex effects |
| 6 | Particle Lights | HIGH | HIGH | 8 | P2 | Requires light culling |
| 7 | Sub-UV | MED-HIGH | MED | 4 | P2 | Flipbook animation |
| 8 | Mesh Index | MED-HIGH | MED | 5 | P2 | Mesh variety |
| 9 | Scale Color | MED | LOW | 1 | P3 | Color multiplier |
| 10 | Gravity Force | MED | LOW | 2 | P3 | Explicit gravity |
| 11 | Dynamic Params | MED | MED | 6 | P3 | Material binding |
| 12 | Decal | MED | MED | 4 | P4 | New renderer |
| 13 | Accel Force | LOW-MED | LOW | 1 | P4 | General acceleration |
| 14 | Curl Noise | MED | LOW | 1 | P4 | Already have texture |
| 15 | Mesh Rotation | LOW | LOW | 1 | P4 | 3D rotation rate |

