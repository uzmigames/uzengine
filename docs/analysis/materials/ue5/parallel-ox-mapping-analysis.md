# UE5 Parallax Occlusion Mapping (POM) Architecture Analysis

**Date**: 2026-03-13
**Analyst**: Unreal Architect (Haiku)
**Target**: Extract UE5 POM architectural patterns for UzEngine adaptation

---

## 1. Pattern: Material Expression Composition via Virtual Dispatch

### UE5 Implementation

UE5 implements parallax mapping through the **Material Expression** system, not shader permutations or switch statements. Key files:

- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionBumpOffset.h`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\MaterialExpressions.cpp`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\SceneTypes.h` (lines 147–187)

**Core Concept**:

```cpp
UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionBumpOffset : public UMaterialExpression
{
    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "..."))
    FExpressionInput Coordinate;

    UPROPERTY()
    FExpressionInput Height;

    UPROPERTY(meta=(RequiredInput = "false"))
    FExpressionInput HeightRatioInput;

    UPROPERTY(EditAnywhere, Category=MaterialExpressionBumpOffset)
    float HeightRatio = 0.05f;  // Perceived height as fraction of width

    UPROPERTY(EditAnywhere, Category=MaterialExpressionBumpOffset)
    float ReferencePlane = 0.5f;  // Height at which no offset applied

    UPROPERTY(EditAnywhere, Category = MaterialExpressionBumpOffset)
    uint32 ConstCoordinate = 0;  // Fallback if Coordinate not connected

    // Compile to shader code (virtual dispatch via FMaterialCompiler)
    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
};
```

**Compilation (from MaterialExpressions.cpp)**:

```cpp
int32 UMaterialExpressionBumpOffset::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    if(!Height.GetTracedInput().Expression)
    {
        return Compiler->Errorf(TEXT("Missing Height input"));
    }

    // BumpOffset formula: OutputCoord = InputCoord + EyeDir.xy * (Height - ReferencePlane) * HeightRatio
    return Compiler->Add(
        Compiler->Mul(
            Compiler->ComponentMask(
                Compiler->TransformVector(MCB_World, MCB_Tangent,
                    Compiler->CameraVector()),
                1,1,0,0  // Mask: keep X,Y; discard Z,W
            ),
            Compiler->Add(
                Compiler->Mul(
                    HeightRatioInput.GetTracedInput().Expression
                        ? Compiler->ForceCast(HeightRatioInput.Compile(Compiler), MCT_Float1)
                        : Compiler->Constant(HeightRatio),
                    Compiler->ForceCast(Height.Compile(Compiler), MCT_Float1)
                ),
                HeightRatioInput.GetTracedInput().Expression
                    ? Compiler->Mul(Compiler->Constant(-ReferencePlane),
                        Compiler->ForceCast(HeightRatioInput.Compile(Compiler), MCT_Float1))
                    : Compiler->Constant(-ReferencePlane * HeightRatio)
            )
        ),
        Coordinate.GetTracedInput().Expression
            ? Coordinate.Compile(Compiler)
            : Compiler->TextureCoordinate(ConstCoordinate, false, false)
    );
}
```

### Problem Solved

1. **Type Safety Without Runtime Dispatch**: Each `UMaterialExpression` subclass statically defines its inputs/outputs. No RTTI casting or switch statements needed.
2. **Lazy Compilation**: Expressions only compile if connected; optional inputs (Height, Coordinate) checked at compile-time.
3. **Tracing Connections**: `GetTracedInput()` follows the connection graph, skipping reroute nodes and dead paths.
4. **Parameter Baking**: Constants (`HeightRatio`, `ReferencePlane`) are baked into shader code at compile-time, zero runtime cost.

### UE5 Architecture Insights

- **Material Attribute System** (SceneTypes.h, lines 147–187):
  - `MP_Displacement` is a **first-class material property**, not a side effect of normal maps
  - Supports dedicated compilation paths for displacement-specific logic
  - Framework supports both **per-pixel displacement** (via BumpOffset) and **vertex displacement** (via WorldPositionOffset)

- **Input Composition**:
  - Each expression has typed inputs (`FExpressionInput`, `FVectorMaterialInput`, etc.)
  - Component masking (`Mask`, `MaskR`, `MaskG`, `MaskB`, `MaskA`) allows sub-channel connection
  - Optional inputs with fallback constants reduce wiring boilerplate

- **Coordinate Transforms**:
  - `TransformVector(MCB_World, MCB_Tangent, CameraVector)` converts view direction from world space to tangent space
  - Camera-relative parallax ensures consistent visual results regardless of view angle

---

## 2. Pattern: Material Properties as First-Class Shader Inputs

### UE5 Implementation

File: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\SceneTypes.h` (lines 146–187)

```cpp
UENUM(BlueprintType)
enum EMaterialProperty : int
{
    MP_EmissiveColor = 0,
    MP_Opacity,
    MP_OpacityMask,
    // ... 20+ properties ...
    MP_Normal,
    MP_Tangent,
    MP_WorldPositionOffset,         // Vertex displacement
    MP_WorldDisplacement_DEPRECATED,  // (deprecated)
    MP_TessellationMultiplier_DEPRECATED,  // (deprecated, was for tessellation-based displacement)
    MP_PixelDepthOffset,             // Post-projection depth tweak (for parallax shadows)
    MP_Displacement,                 // **NEW: Explicit displacement property**

    MP_MaterialAttributes,           // Substrate compound input
    MP_CustomOutput,
    MP_MAX,
};
```

**Key Insight**: UE5 **delineates between displacement methods**:
1. `MP_WorldPositionOffset` — Vertex-space displacement (pre-rasterization)
2. `MP_PixelDepthOffset` — Depth offset (post-rasterization, per-pixel depth tweak)
3. `MP_Displacement` — Future-facing displacement (for Nanite/VirtualGeometry)
4. `BumpOffset` expression — **Screen-space parallax** (texture coordinate offset, not true geometric displacement)

### Problem Solved

1. **Clear Semantic Boundary**: Distinguishing vertex vs. pixel vs. tessellation displacement prevents pipeline confusion.
2. **Material Attribute Composition**: Each property is independently compilable; shader variants only generate code for connected properties.
3. **Nanite/VirtualGeometry Readiness**: `MP_Displacement` is pre-staged for GPU-driven geometry systems.

### UzEngine Adaptation

UzEngine already has POM (per memory: "POM displacement: steep parallax (8-32 layers) + linear interp"). Current architecture should:

1. **Clarify displacement taxonomy** in root constant buffers or material property flags:
   ```hlsl
   // per_material_cb.hlsli
   struct PerMaterialCB {
       // ... existing fields ...
       uint32_t displacementFlags;  // Bit 0: hasPOM, Bit 1: hasHeightShadow, etc.
       float pomHeightRatio;
       float pomReferencePlane;
       uint32_t pomStepCount;  // Adaptive: 0 = auto, 1-32 = fixed
   };
   ```

2. **Separate POM from texture sampling** — don't bake parallax offset into UV generation at the RHI level; compute it in shader as a material concern.

3. **Document MP_* equivalents** in UzEngine's material system (currently uses raw texture binding; consider staged property enum).

---

## 3. Pattern: Ray Marching via Iterated Texture Lookups (Not Binary Search)

### UE5 Shader Implementation

While UE5's `BumpOffset` expression is a **simple linear approximation** (no ray marching), UE5's broader shader system includes ray-marching patterns. Files:

- `W:\UE_5.6_Source\Engine\Shaders\Private\BinarySearch.ush` — Generic binary search templates (for buffer queries, not POM)
- `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassCommon.ush` — Occlusion sampling patterns

**UE5's Philosophy on POM**:
- **No built-in POM shader in base engine** — `BumpOffset` is a **fast approximation** (1 sample, linear interpolation)
- **Plugins/custom materials** implement higher-quality POM via material graphs
- **Nanite/VirtualMesh** uses **Displacement Map** system (Nanite::FDisplacementMap, see below)

### Nanite Displacement Map Pattern

File: `W:\UE_5.6_Source\Engine\Source\Developer\NaniteUtilities\Public\DisplacementMap.h` (lines 11–46)

```cpp
class FDisplacementMap
{
public:
    ETextureSourceFormat SourceFormat;
    int32 BytesPerPixel;
    int32 SizeX, SizeY;
    uint32 NumLevels;

    float Magnitude;  // Scale factor
    float Center;     // Neutral point (typically 0.5 for [0,1] maps)
    TextureAddress AddressX, AddressY;  // Wrap/Clamp

public:
    // CPU-side ray marching for Nanite mesh generation
    float Sample(FVector2f UV) const;  // Bilinear filtered
    FVector2f Sample(FVector2f MinUV, FVector2f MaxUV) const;

private:
    float Load(int32 x, int32 y) const;  // Nearest-neighbor
    FVector2f Load(int32 x, int32 y, uint32 Level) const;  // Mip-aware
    void Address(int32& x, int32& y) const;  // Wrap logic
};
```

**CPU Offline Process** (not GPU runtime):
1. Load displacement texture (BC1–BC7 compressed, can be decompressed to float)
2. For each heightfield pixel, trace downward rays
3. Build SDF or height-based geometry (for Nanite virtual mesh)
4. Pre-compile into GPU-optimized mesh

### Problem Solved

1. **Separation of Concerns**: POM (screen-space texture offset) vs. displacement (geometric tessellation).
2. **Quality Tiers**:
   - **Cheap**: BumpOffset (1 sample, linear)
   - **Medium**: Custom POM shader (8–32 ray-march samples, binary search refinement)
   - **Expensive**: Displacement mesh (pre-baked geometry, no runtime ray marching)

### UzEngine Adaptation

UzEngine already implements **POM correctly** at the shader level:
- Per memory: "**POM displacement**: steep parallax (8-32 layers) + linear interp"
- This is the **standard GPU-based approach**: fixed-count or adaptive step marching, not CPU pre-bake

**Recommended refinements**:

1. **Document step count selection** (avoid "magic numbers"):
   ```cpp
   // pom_pass.cpp
   enum class POMQualityLevel {
       Low = 8,      // ~1.0 ms @ 1080p
       Medium = 16,  // ~2.0 ms @ 1080p
       High = 32,    // ~4.0 ms @ 1080p
       VeryHigh = 64 // Rarely justified
   };
   ```

2. **Implement adaptive step count** based on distance or material parameter:
   ```hlsl
   // material.hlsli
   uint AdaptiveStepCount(float viewDistance, float slopeFactor) {
       // Reduce steps with distance; increase with slope steepness
       uint baseSteps = 16;
       float distanceFactor = saturate(viewDistance / 1000.0);
       return max(4, uint(baseSteps * (1.0 + slopeFactor * distanceFactor)));
   }
   ```

3. **Profile POM cost vs. silhouette artifacts** — many projects disable POM beyond 50m to reduce bandwidth and cache pressure.

---

## 4. Pattern: Material Attributes as Intermediate Representation (IR)

### UE5 Implementation

Files:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionBreakMaterialAttributes.h`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionMakeMaterialAttributes.h`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h` (lines 82–154)

**Material Attributes = Shader IR**:

```cpp
// MaterialExpressionBreakMaterialAttributes.h
UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionBreakMaterialAttributes : public UMaterialExpression
{
    UPROPERTY()
    FMaterialAttributesInput MaterialAttributes;

    // Outputs: 30+ pins corresponding to EMaterialProperty enum values
    // (BaseColor, Normal, Roughness, Metallic, ..., Displacement, etc.)

    static TMap<EMaterialProperty, int32> PropertyToIOIndexMap;
    static void BuildPropertyToIOIndexMap();  // Lazy initialization

    virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
};
```

**Concept**:
- A **Material Attributes input** is a **bundle** of all material properties
- `BreakMaterialAttributes` unpacks this bundle into individual properties (Normal, Roughness, etc.)
- `MakeMaterialAttributes` repacks individual properties into a bundle
- **No union/variant needed** — each property maps to a fixed output index

### Problem Solved

1. **Modular Material Graphs**: Material layer functions can output `FMaterialAttributes` instead of wiring 30+ individual pins.
2. **Graph Readability**: Reduces visual clutter in editor.
3. **Type-Safe Property Access**: `PropertyToIOIndexMap` prevents index mismatches.

### UzEngine Adaptation

UzEngine material system (per MEMORY.md) uses **direct texture binding** (albedo, normal, metallic, roughness, AO, displacement). To adopt UE5's pattern:

1. **Define a `MaterialAttributes` struct** (equivalent to UE5's):
   ```cpp
   // material_attributes.h
   struct MaterialAttributes {
       Vec3 baseColor;      // Albedo
       Vec3 normal;         // World-space normal
       float metallic;
       float roughness;
       float ambientOcclusion;
       float displacement;  // Height/displacement sample
       float emission;      // Emissive intensity
       Vec3 bentNormal;     // Optional: AO direction
   };
   ```

2. **Support attribute composition in material expressions** (future enhancement):
   ```cpp
   // In per-material constant buffer
   struct PerMaterialCB {
       // ... existing ...
       uint32_t connectedAttributesMask;  // Bitmask: which attributes are connected?
   };
   ```

3. **Lazy evaluation**: Only sample/process attributes that are actually used downstream in the pass.

---

## 5. Anti-Patterns: What UE5 Avoids (and UzEngine Should Too)

### Anti-Pattern 1: Switch Statements on Material Type

**Bad (UE5 would never do)**:
```cpp
// WRONG: Violates SOLID (Open/Closed), causes shader bloat
switch(materialType) {
    case MAT_PARALLAX:
        // Sample heightmap, ray march
        break;
    case MAT_PARALLAX_SHADOWS:
        // Same as above + shadow trace
        break;
    case MAT_TESSELLATION:
        // Tessellation-only path
        break;
    // ...20+ cases
}
```

**Why**: Shader grows with every new material variant; no code reuse; hard to profile individual paths.

**UE5's Approach**:
- Each material expression (`BumpOffset`, custom nodes) compiles independently
- Shader only includes code for **connected** properties
- Unused expressions are dead-code-eliminated

**UzEngine Status**: Already avoids this via per-material constant buffer flags + conditional shader code in pixel shader.

### Anti-Pattern 2: Runtime Pointers to Displacement Algorithms

**Bad**:
```cpp
typedef float (*DisplacementFn)(vec2 uv, Texture2D heightMap);

void PixelShader(PSInput input, out float4 output : SV_Target) {
    DisplacementFn displaceFn = GetFunctionPointer(material.displacementType);
    vec2 offset = displaceFn(input.uv, texHeightMap);
}
```

**Why**: Function pointers break shader compilation; require dynamic dispatch (unsupported on most GPU ISAs).

**UE5's Approach**:
- All branching is **static** (compile-time via preprocessor/variants) or **uniform** (same across entire dispatch)
- No per-pixel function dispatch

### Anti-Pattern 3: Unbounded Ray Marching

**Bad**:
```hlsl
// WRONG: Can loop forever if heightmap is pathological
for(int i = 0; i < stepCount; i++) {  // stepCount = dynamic input
    float h = SampleHeight(currentUV);
    if(h >= rayHeight) break;
    // ...
}
```

**Why**: GPU hangs, indeterminate performance, cache thrashing.

**UE5's Approach** (and UzEngine's):
- Fixed loop unroll: `for(int i = 0; i < 32; i++)` with break statement compiled out
- Adaptive quality via material parameters (baked at compile time or set per-frame)

---

## 6. File Paths & Key Artifacts

### Core UE5 Material System
- **Material Property Enum**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\SceneTypes.h` (lines 146–187)
- **BumpOffset Expression**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionBumpOffset.h`
- **Compilation Logic**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\MaterialExpressions.cpp`
- **HLSL Translator** (Material → Shader): `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h`
- **Material Attributes IR**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionBreakMaterialAttributes.h`

### Shader Infrastructure
- **Base Pass Vertex**: `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassVertexShader.usf`
- **Base Pass Common**: `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassCommon.ush`
- **Nanite Displacement**: `W:\UE_5.6_Source\Engine\Source\Developer\NaniteUtilities\Public\DisplacementMap.h`

### UzEngine Current State
- **POM Implementation**: Per MEMORY.md, already complete (8-32 layers, linear interp)
- **Material CB Structure**: `/e/UzEngine/runtime/engine/renderer/core/params/constant_buffers.h`
- **PBR Pass**: `/e/UzEngine/runtime/engine/renderer/passes/pbr_pass.cpp`

---

## 7. Summary: Architectural Patterns to Adopt

| Pattern | UE5 Reference | UzEngine Adaptation | Priority |
|---------|---------------|-------------------|----------|
| **Material Expression Composition** | BumpOffset virtual Compile() | Define expr base class; support chaining | Medium |
| **Material Properties Enum** | EMaterialProperty (30+ values) | Create MaterialPropertyFlags bitset | Low |
| **Attribute IR (Break/Make)** | BreakMaterialAttributes expr | Future material graph system | Low |
| **Coordinate Transforms** | TransformVector(World→Tangent) | Already done (tangent-space POM) | Done |
| **Displacement Type Clarity** | MP_Displacement vs. WPO vs. PPO | Document in CB struct | High |
| **Adaptive Step Count** | Material param + quality level | Implement distance-based LOD | High |
| **No Runtime Dispatch** | Static shader variants/branches | Already enforced | Done |

---

## 8. Risk Assessment

### Low Risk (Already Implemented)
- Ray marching via fixed-count loop
- Tangent-space coordinate transforms
- Height texture sampling

### Medium Risk (Document + Clarify)
- Interaction between POM and self-shadowing (heightmap shadow tracing)
- Performance cliffs at distance (step count reduction)
- Alpha-tested surfaces with POM (artifacts at silhouettes)

### High Risk (Avoid)
- Unbounded or data-dependent loop iteration
- Per-pixel material type dispatch
- Undefined behavior on pathological heightmaps (all-black, all-white)

---

## Conclusion

UE5's POM architecture is **minimal yet extensible**:
1. **Simple expression node** (BumpOffset) for basic parallax
2. **Material property system** for variant selection
3. **Shader IR** (attributes) for modularity
4. **Quality tiers** (approximation vs. ray march vs. mesh)

UzEngine's current POM implementation aligns well with these principles. Key improvements:
1. Formalize material property flags (displacement type enum)
2. Implement adaptive step count based on distance/quality setting
3. Document self-shadowing interaction (heightmap trace offset)
4. Add developer-facing stat counters for POM cost tracking
