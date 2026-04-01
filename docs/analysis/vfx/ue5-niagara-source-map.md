<!-- doc-meta
title: UE5 Niagara Source Reference Map
author: Claude Code
date: 2026-03-30
status: reference
subsystem: vfx
-->

# UE5 Niagara Source Code Reference Map

**Base Path**: `U:\UnrealGITHUB\UnrealEngine\Engine\Plugins\FX\Niagara\Source\Niagara`

## Module Headers (Stateless Implementation)

All modules follow the same pattern:
- `.h` file in `Internal/Stateless/Modules/`
- Implements `UNiagaraStatelessModule` base class
- Methods: `BuildEmitterData()`, `BuildShaderParameters()`, `SetShaderParameters()`

### Physics Forces & Damping

| Module | File | Lines | Key Features |
|--------|------|-------|--------------|
| Drag | `NiagaraStatelessModule_Drag.h` | 28 | Simple velocity damping |
| Gravity Force | `NiagaraStatelessModule_GravityForce.h` | ? | Constant gravity acceleration |
| Acceleration Force | `NiagaraStatelessModule_AccelerationForce.h` | ? | General force vector |
| Curl Noise Force | `NiagaraStatelessModule_CurlNoiseForce.h` | ? | Turbulence force field |
| **Rotate Around Point** | `NiagaraStatelessModule_RotateAroundPoint.h` | 50 | Orbital motion (rate, radius, center, axis) |

### Sprite Rendering & Orientation

| Module | File | Lines | Key Features |
|--------|------|-------|--------------|
| Sprite Facing & Alignment | `NiagaraStatelessModule_SpriteFacingAndAlignment.h` | 40 | Facing mode + alignment vector |
| Camera Offset | `NiagaraStatelessModule_CameraOffset.h` | 32 | Offset along camera→particle vector |
| Scale Sprite Size | `NiagaraStatelessModule_ScaleSpriteSize.h` | ? | Size curve |
| **Scale Sprite Size By Speed** | `NiagaraStatelessModule_ScaleSpriteSizeBySpeed.h` | 49 | Speed-dependent scaling (threshold, min/max, curve) |
| Sprite Rotation Rate | `NiagaraStatelessModule_SpriteRotationRate.h` | ? | 2D rotation (deg/s) |

### Mesh Rendering

| Module | File | Lines | Key Features |
|--------|------|-------|--------------|
| Mesh Index | `NiagaraStatelessModule_MeshIndex.h` | 42 | Select mesh per-particle (int range, weights) |
| Scale Mesh Size | `NiagaraStatelessModule_ScaleMeshSize.h` | 41 | 3D scale distribution |
| **Scale Mesh Size By Speed** | `NiagaraStatelessModule_ScaleMeshSizeBySpeed.h` | ? | Speed-dependent 3D scaling |
| Initial Mesh Orientation | `NiagaraStatelessModule_InitialMeshOrientation.h` | ? | Starting rotation (quat or euler) |
| Mesh Rotation Rate | `NiagaraStatelessModule_MeshRotationRate.h` | ? | 3D rotation (deg/s) |

### Color & Material

| Module | File | Lines | Key Features |
|--------|------|-------|--------------|
| Scale Color | `NiagaraStatelessModule_ScaleColor.h` | 31 | Color multiplier (white = no-op) |
| Dynamic Material Parameters | `NiagaraStatelessModule_DynamicMaterialParameters.h` | ? | User-defined shader params (scalar, vec2-4) |

### Special Renderers

| Module | File | Lines | Key Features |
|--------|------|-------|--------------|
| **Light Attributes** | `NiagaraStatelessModule_LightAttributes.h` | 57 | Light: radius, falloff, diffuse/specular scale, volumetric |
| **Decal Attributes** | `NiagaraStatelessModule_DecalAttributes.h` | 51 | Decal: orientation, size, fade (with coordinate space) |
| Sub UV Animation | `NiagaraStatelessModule_SubUVAnimation.h` | 73 | Flipbook: numFrames, mode (DirectSet/InfiniteLoop/Linear/Random), frame range, loops/second, change interval |

### Utility & Setup

| Module | File | Lines | Key Features |
|--------|------|-------|--------------|
| Initialize Particle | `NiagaraStatelessModule_InitializeParticle.h` | ? | Set initial position, velocity, scale, color |
| Shape Location | `NiagaraStatelessModule_ShapeLocation.h` | ? | Spawn shape (sphere, box, etc.) |
| Calculate Accurate Velocity | `NiagaraStatelessModule_CalculateAccurateVelocity.h` | ? | Compute velocity from position delta |
| Add Velocity | `NiagaraStatelessModule_AddVelocity.h` | ? | Apply velocity to position |
| Solve Velocities & Forces | `NiagaraStatelessModule_SolveVelocitiesAndForces.h` | ? | Integration step (core solver) |

---

## Vertex Factory (Billboard Math)

**File**: `Shaders/Private/NiagaraSpriteVertexFactory.ush` (lines 1-200+)

### Facing Mode Macros (lines 20-37)

```hlsl
#define FACING_MODE_FACECAMERA              0  // Look at camera, preserve camera up
#define FACING_MODE_FACECAMERAPLANE         1  // Flat look, parallel to camera plane
#define FACING_MODE_CUSTOMFACINGVECTOR      2  // Face toward SpriteFacing attribute
#define FACING_MODE_FACECAMERAPOSITION      3  // Face camera pos, fixed up=(0,0,1)
#define FACING_MODE_FACECAMERADISTANCEBLEND 4  // Blend between mode 0 & 3 by distance
```

### Alignment Mode Macros (lines 22-26)

```hlsl
#define ALIGNMENT_MODE_UNALIGNED       0  // SpriteRotation only
#define ALIGNMENT_MODE_VELOCITYALIGNED 1  // Up arrow along velocity
#define ALIGNMENT_MODE_CUSTOMALIGNMENT 2  // Up arrow along SpriteAlignment attr
```

### Input Struct (lines 42-48)

```hlsl
struct FVertexFactoryInput {
    float2 TexCoord       : ATTRIBUTE0;
    uint VertexId         : SV_VertexID;
    uint InstanceId       : SV_InstanceID;
};
```

### Interpolants (lines 114-149)

Key outputs:
- `TangentToWorld0AndInterp_Sizer` — TBN + SubUV interpolation
- `TangentToWorld2` — normal vector
- `DynamicParameter[0-3]` — custom per-instance params
- `Color` — RGBA with alpha
- `TexCoords` — UV + SubUV UVs
- `ParticleSubUVs` — SubUV frame info

---

## Shader Template System

Each module can have a shader template (`.usf` file) in:
```
Engine/Plugins/FX/Niagara/Shaders/Private/Modules/
```

**Pattern** (optional per module):
```cpp
// In .h file:
#if WITH_EDITORONLY_DATA
    virtual const TCHAR* GetShaderTemplatePath() const override;
    virtual void GetOutputVariables(...) const override;
#endif

// In .cpp file:
const TCHAR* UNiagaraStatelessModule_Drag::GetShaderTemplatePath() const
{
    return TEXT("/Engine/Plugins/FX/Niagara/Shaders/Private/Modules/NiagaraModule_Drag.usf");
}
```

---

## Data Binding Pattern

Every module typically:

1. **BuildEmitterData()** — Sets feature flags, allocates GPU data
   ```cpp
   void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const
   {
       // Set feature mask flags
       // Allocate CB space
   }
   ```

2. **BuildShaderParameters()** — Describes CB layout to shader system
   ```cpp
   void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& Builder) const
   {
       // Add scalar/vector fields to CB
       Builder.AddParameter<float>("DragCoeff");
       Builder.AddParameter<FVector3f>("Center");
   }
   ```

3. **SetShaderParameters()** — Writes actual values to CB at runtime
   ```cpp
   void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& Context) const
   {
       // Read particle/emitter data
       // Write to CB
       Context.SetParameter<float>(0, DragValue);
   }
   ```

---

## Distribution System

UE5 uses `FNiagaraDistribution*` types for parameterization:

- `FNiagaraDistributionRangeFloat` — scalar with optional range/curve
- `FNiagaraDistributionRangeVector3` — 3D vector with optional variation
- `FNiagaraDistributionColor` — RGBA with color picker
- `FNiagaraDistributionRangeInt` — integer range
- `FNiagaraDistributionFloat` — single curve/value

**Meta tags** control editor UI:
```cpp
UPROPERTY(EditAnywhere, Category = "Parameters", meta = (
    DisplayName = "Drag",
    ClampMin = "0.0",
    ClampMax = "10.0",
    UIMin = "0.0",
    UIMax = "2.0",
    DisableRangeDistribution  // Hide min/max range picker
))
FNiagaraDistributionRangeFloat DragDistribution = FNiagaraDistributionRangeFloat(1.0f);
```

---

## Feature Mask System

Some modules require special feature bits in the renderer. Example:

```cpp
enum class ENiagaraStatelessFeatureMask : uint32
{
    None                    = 0,
    ParticleLights          = 1 << 0,
    Decals                  = 1 << 1,
    // ... etc
};

virtual ENiagaraStatelessFeatureMask GetFeatureMask() const override
{
    if (bApplyRadius || bApplyFalloffExponent || ...)
        return ENiagaraStatelessFeatureMask::ParticleLights;
    return ENiagaraStatelessFeatureMask::None;
}
```

Modules like `LightAttributes` and `DecalAttributes` set feature bits to enable special rendering paths.

---

## Quick Copy Template

When implementing a new module in UzEngine, use this structure:

```cpp
// module_name.h
struct ModuleNameModule : IVfxModule {
    // Parameters (UPROPERTY equivalents)
    float param1 = 1.0f;
    Vec3 param2 = Vec3::zero();
    
    // Core methods
    void Setup(VfxCompileContext& ctx) override;  // BuildEmitterData
    void WriteShaderParams(ConstantBuffer& cb) override;  // SetShaderParameters
};

// Shader side
// Reads from CB
// Modifies particle data (position, velocity, color, size, etc.)
```

---

## Files to Reference During Implementation

**Always read these FIRST**:
1. `NiagaraStatelessModule_Drag.h` — simplest example (28 lines)
2. `NiagaraStatelessModule_SpriteFacingAndAlignment.h` — parameter variety
3. `NiagaraStatelessModule_ScaleSpriteSizeBySpeed.h` — curve-based logic
4. `NiagaraStatelessModule_RotateAroundPoint.h` — complex math, multiple coordinate spaces
5. `NiagaraSpriteVertexFactory.ush` — billboard shader implementation

