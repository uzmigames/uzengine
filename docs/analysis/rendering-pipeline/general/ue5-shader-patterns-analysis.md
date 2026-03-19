# UE5 Shader Code Patterns: Material Property Compilation

**Purpose**: Document exact shader compilation patterns UE5 uses for material properties (relevant to POM, displacement, and texture coordinate modification)

---

## Pattern 1: Property Compilation Flow

### Material Expression → Shader Code

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\MaterialExpressions.cpp`

**BumpOffset Compilation**:
```cpp
int32 UMaterialExpressionBumpOffset::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    // Step 1: Validate input
    if(!Height.GetTracedInput().Expression)
    {
        return Compiler->Errorf(TEXT("Missing Height input"));
    }

    // Step 2: Compile Height to HLSL code (returns code index)
    int32 HeightCodeIdx = Height.Compile(Compiler);

    // Step 3: Compile optional HeightRatio (with fallback constant)
    int32 RatioCodeIdx = HeightRatioInput.GetTracedInput().Expression
        ? Compiler->ForceCast(HeightRatioInput.Compile(Compiler), MCT_Float1)
        : Compiler->Constant(HeightRatio);

    // Step 4: Get camera vector (world space)
    int32 CameraVecCodeIdx = Compiler->CameraVector();

    // Step 5: Transform to tangent space
    int32 TangentCameraVecCodeIdx = Compiler->TransformVector(
        MCB_World,      // From world space
        MCB_Tangent,    // To tangent space
        CameraVecCodeIdx
    );

    // Step 6: Mask XY only (discard Z and W)
    int32 MaskedVecCodeIdx = Compiler->ComponentMask(
        TangentCameraVecCodeIdx,
        1,  // R (X) = yes
        1,  // G (Y) = yes
        0,  // B (Z) = no
        0   // A (W) = no
    );

    // Step 7: Compute offset = (Height - ReferencePlane) * HeightRatio
    int32 OffsetMagnitudeCodeIdx = Compiler->Add(
        Compiler->Mul(RatioCodeIdx, HeightCodeIdx),
        HeightRatioInput.GetTracedInput().Expression
            ? Compiler->Mul(
                Compiler->Constant(-ReferencePlane),
                Compiler->ForceCast(HeightRatioInput.Compile(Compiler), MCT_Float1)
            )
            : Compiler->Constant(-ReferencePlane * HeightRatio)
    );

    // Step 8: Scale camera vector by offset: EyeDir.xy * offset
    int32 FinalOffsetCodeIdx = Compiler->Mul(
        MaskedVecCodeIdx,
        OffsetMagnitudeCodeIdx
    );

    // Step 9: Add offset to original UV coordinates
    int32 OutputUVCodeIdx = Coordinate.GetTracedInput().Expression
        ? Coordinate.Compile(Compiler)
        : Compiler->TextureCoordinate(ConstCoordinate, false, false);

    return Compiler->Add(FinalOffsetCodeIdx, OutputUVCodeIdx);
}
```

### What Happens in FMaterialCompiler::Compile()

The compiler maintains a **FShaderCodeChunk array**:

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h` (lines 82–154)

```cpp
struct FShaderCodeChunk
{
    uint64 Hash;
    FString DefinitionFinite;   // HLSL code (finite differences)
    FString DefinitionAnalytic; // HLSL code (analytic derivatives)
    FString SymbolName;         // Variable name (if not inlined)
    TRefCountPtr<FMaterialUniformExpression> UniformExpression;
    EMaterialValueType Type;    // Float1, Float3, Float4, etc.
    bool bInline;               // Inline or declare as local var?
};

// Compiler state:
TArray<FShaderCodeChunk> CodeChunks;  // Index = code index returned by Compile()

// When an expression calls Compiler->Add(a, b):
int32 AddIndex = CodeChunks.Add(FShaderCodeChunk(
    Hash,
    TEXT("(") + CodeChunks[a].DefinitionFinite + TEXT(" + ")
        + CodeChunks[b].DefinitionFinite + TEXT(")"),
    TEXT("(") + CodeChunks[a].DefinitionAnalytic + TEXT(" + ")
        + CodeChunks[b].DefinitionAnalytic + TEXT(")"),
    SymbolName,
    nullptr,  // No uniform expression for arithmetic
    MCT_Float3,  // Type inference
    false  // Not inlined (create temp var)
));
return AddIndex;
```

**Result**: A final HLSL snippet like:

```hlsl
float2 BumpOffsetUV = (
    (CameraVectorWorldToTangent.xy) * (
        (HeightInput * HeightRatio) + (-0.5 * HeightRatio)
    )
) + TexCoord0;
```

---

## Pattern 2: Property Evaluation in Base Pass Shader

### Vertex Shader

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassVertexShader.usf`

```hlsl
// Input from mesh, vertex factory, material parameters
void FShaderPixelShaderInputOutput main(
    FVertexFactoryInput Input,
    out FBasePassVSOutput Output
) {
    // ... vertex deformation ...

    // Material world position offset (if connected)
    if (MATERIAL_USES_WORLD_POSITION_OFFSET) {
        WorldPosition.xyz += GetMaterialWorldPositionOffset(VertexParameters);
    }

    // ... transform to clip space ...
    Output.Position = mul(WorldPosition, ResolvedView.TranslatedWorldToClip);

    // Interpolate texture coordinates and other VS outputs
    Output.FactoryInterpolants = VertexFactoryGetInterpolants(...);
}
```

**Key**: WorldPositionOffset is **vertex-space** only; BumpOffset happens in **pixel shader** (screen-space).

### Pixel Shader

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassPixelShader.usf`

```hlsl
void FShaderPixelShaderInputOutput main(FPixelShaderInput Input) {
    // Evaluate material properties
    FMaterialPixelParameters MaterialParameters;
    MaterialParameters.WorldPosition = Input.WorldPosition;
    MaterialParameters.CameraVector = normalize(ResolvedView.WorldCameraOrigin - Input.WorldPosition);
    MaterialParameters.TangentToWorld = Input.TangentToWorld;
    // ... more setup ...

    // **Parallax Occlusion Mapping happens here**
    // BumpOffset expression compiles to:
    float2 ParallaxUV = BumpOffsetExpression(MaterialParameters.TexCoords[0]);

    // Use parallax-modified UV for actual texture samples
    float3 Normal = SampleNormal(ParallaxUV);
    float3 Albedo = SampleAlbedo(ParallaxUV);
    float Roughness = SampleRoughness(ParallaxUV);
    float Metallic = SampleMetallic(ParallaxUV);

    // ... rest of shading ...
}
```

---

## Pattern 3: Coordinate Space Handling

### TransformVector() Machinery

**How UE5 handles space transformations** (in compiled HLSL):

```hlsl
// MaterialCommon.ush (included by all material shaders)

// Get transform matrices from constant buffers
float3x3 GetTangentToWorld(FMaterialPixelParameters Parameters) {
    return Parameters.TangentToWorld;  // From VS interpolation
}

// Vector transformation chain:
float3 TransformVector(float3 InVec, float3x3 FromBasis, float3x3 ToBasis) {
    return mul(InVec, mul(FromBasis, ToBasis));
}

// Example: World to Tangent space
float3 WorldToTangent(float3 WorldVec, float3x3 TangentToWorld) {
    // Inverse (transpose for orthonormal bases)
    float3x3 WorldToTangentMat = transpose(TangentToWorld);
    return mul(WorldVec, WorldToTangentMat);
}

// In BumpOffset evaluation:
float3 CameraVecWorld = normalize(Parameters.CameraVector);
float3 CameraVecTangent = WorldToTangent(CameraVecWorld, Parameters.TangentToWorld);
float2 ParallaxOffset = CameraVecTangent.xy * (SampleHeight(UV) - 0.5) * HeightRatio;
```

---

## Pattern 4: Sampling & Component Masking

### Texture Sampling with Type Coercion

From compiled material expression:

```cpp
// Each texture sample returns a 4-component value
int32 HeightmapSampleCodeIdx = Compiler->TextureSample(
    Texture_Height,            // Texture reference
    CoordinateCodeIdx,         // UV code index
    SAMPLERTYPE_Color,         // (uint8) MSAA, filter mode, etc.
    nullptr,                   // No manual derivatives
    -1,                        // Auto LOD
    true                       // Enable derivatives for LOD calc
);

// Component mask extracts single channel:
// Heightmap typically uses R or A channel
int32 HeightChannelCodeIdx = Compiler->ComponentMask(
    HeightmapSampleCodeIdx,
    1,  // R = yes
    0,  // G = no
    0,  // B = no
    0   // A = no
);

// Result: FShaderCodeChunk with
// DefinitionFinite = "Texture_Height.Sample(...).r"
// Type = MCT_Float1
```

---

## Pattern 5: Adaptive Quality via Preprocessor

### Material Quality Levels

**How UE5 bakes quality tiers** (compile-time, not runtime):

```hlsl
// BasePass.usf includes this early:
#if MATERIAL_QUALITY_LEVEL == EMaterialQualityLevel_High
    #define PARALLAX_STEPS 32
    #define ENABLE_PARALLAX_REFINEMENT 1
#elif MATERIAL_QUALITY_LEVEL == EMaterialQualityLevel_Medium
    #define PARALLAX_STEPS 16
    #define ENABLE_PARALLAX_REFINEMENT 0
#else
    #define PARALLAX_STEPS 8
    #define ENABLE_PARALLAX_REFINEMENT 0
#endif

// Later, BumpOffset compiled code becomes:
#if PARALLAX_STEPS > 0
{
    float2 ParallaxUV = Input.TexCoords[0];
    float StepSize = 1.0 / PARALLAX_STEPS;
    float CurrentHeight = 0.0;

    [unroll(PARALLAX_STEPS)]
    for(int i = 0; i < PARALLAX_STEPS; i++) {
        float SampledHeight = Texture_Height.Sample(..., ParallaxUV).r;
        if (SampledHeight > CurrentHeight) {
            ParallaxUV -= CameraVecTangent.xy * StepSize * HeightRatio;
            CurrentHeight = SampledHeight;
        }
    }

    #if ENABLE_PARALLAX_REFINEMENT
    // Linear refinement between last two samples
    // ...
    #endif
}
#endif
```

**Platform-Specific Variants**:
```cpp
// In FMaterialShaderMap::Compile():
for (int32 QualityLevel = 0; QualityLevel < EMaterialQualityLevel::Num; ++QualityLevel) {
    for (int32 FeatureLevel = 0; FeatureLevel < ERHIFeatureLevel::Num; ++FeatureLevel) {
        FShaderCompileEnvironment CompileEnv;
        CompileEnv.SetDefine(TEXT("MATERIAL_QUALITY_LEVEL"), QualityLevel);
        CompileEnv.SetDefine(TEXT("FEATURE_LEVEL"), FeatureLevel);
        // ... compile shader with these defines ...
    }
}
```

---

## Pattern 6: Optional Expression Gates

### Compiled Conditionals

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h`

When an expression **optionally connects an input**:

```cpp
// UMaterialExpressionBumpOffset::Compile()
if(HeightRatioInput.GetTracedInput().Expression) {
    // Input IS connected: use dynamic value
    int32 RatioCodeIdx = Compiler->ForceCast(
        HeightRatioInput.Compile(Compiler),
        MCT_Float1
    );
    // Compiled to:  "HeightRatioDynamic"
} else {
    // Input NOT connected: bake constant
    int32 RatioCodeIdx = Compiler->Constant(HeightRatio);
    // Compiled to:  "0.05"
}
```

**Resulting HLSL**:
```hlsl
// Case 1: Dynamic input connected
float HeightRatioDynamic = GetDynamicHeightRatio();  // From CB or interpolated
float2 ParallaxUV = Input.TexCoords[0] +
    CameraVecTangent.xy * (SampleHeight(Input.TexCoords[0]) - 0.5) * HeightRatioDynamic;

// Case 2: Constant baked
float2 ParallaxUV = Input.TexCoords[0] +
    CameraVecTangent.xy * (SampleHeight(Input.TexCoords[0]) - 0.5) * 0.05;  // Folded by compiler
```

---

## Pattern 7: Derivative Tracking (Analytic vs. Finite)

### Derivative Computation

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h` (lines 99–132)

UE5 tracks **two versions** of every code chunk:

```cpp
struct FShaderCodeChunk {
    FString DefinitionFinite;   // Finite differences: ddx/ddy of neighbors
    FString DefinitionAnalytic; // Analytic chain rule
};

// Example for BumpOffset:
// DefinitionFinite (for Mip LOD calculation):
// ddx(ParallaxUV) = ddx(BaseUV + ParallaxOffset)
//                 = ddx(BaseUV) + ddx(CameraVec.xy * Height)
//                 = ddx(BaseUV) + CameraVec.xy * ddx(Height)

// DefinitionAnalytic (exact chain rule):
// ParallaxUV = BaseUV + CameraVec.xy * (Height(BaseUV) - 0.5) * Ratio
// ∂ParallaxUV/∂x = ∂BaseUV/∂x + CameraVec.xy * (∂Height/∂x * Ratio)
```

**Why**: Texture sampling **needs derivatives** for hardware-managed LOD calculation. Mip selection depends on screen-space rate of change.

```hlsl
// In final shader:
float2 ParallaxUV = ...;
float3 Albedo = Texture_Diffuse.Sample(
    Sampler,
    ParallaxUV,
    ddx(ParallaxUV),  // <-- Required for LOD
    ddy(ParallaxUV)   // <-- Required for LOD
);
```

---

## Pattern 8: Uniform Expression Pooling

### Constant & Parameter Deduplication

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h`

Instead of inlining every constant:

```cpp
// BEFORE (wasteful):
float2 UV1 = BaseUV + CameraVec.xy * (Height(UV1) - 0.5) * 0.05;
float2 UV2 = BaseUV + CameraVec.xy * (Height(UV2) - 0.5) * 0.05;
// Compiler sees "0.05" twice, but they're different literals in code

// AFTER (UE5's approach):
struct FUniformExpression {
    uint64 Hash;  // Hash of value
    FString Code;  // "0.05"
};

// Compiler deduplicates by hash:
FUniformExpression ConstHalfRatio(
    Hash64("0.05"),
    "0.05"
);

// All references to 0.05 become a single constant buffer entry
cbuffer MaterialConstants {
    float MaterialHeightRatio;  // 0.05
    // ...
};
```

**Benefit**: Smaller shader code; fewer constant buffer entries needed.

---

## Summary: Key Architectural Decisions

| Aspect | Pattern | Benefit |
|--------|---------|---------|
| **Property Composition** | Virtual `Compile()` per expression | No type switching; extensible |
| **Space Transforms** | Compile-time matrix generation | No runtime matrix inversion |
| **Texture Sampling** | Derivatives tracked (finite + analytic) | LOD calculation robust |
| **Quality Tiers** | Preprocessor defines (compile variants) | Zero runtime branching cost |
| **Optional Inputs** | Check connectivity at compile-time | Constant baking vs. dynamic binding |
| **Constant Pooling** | Hash-based deduplication | Minimal constant buffer footprint |
| **Component Masking** | Swizzle in HLSL code | SIMD-friendly (no scalar ops) |

---

## For UzEngine: Recommended Implementation

1. **Adopt Expression Compilation Pattern**:
   - Base class: `struct MaterialExpression { virtual int32 Compile(Compiler*) = 0; };`
   - Derived: `struct BumpOffsetExpression : MaterialExpression { ... };`

2. **Track Derivative Information**:
   - Even if not using LOD yet, maintain `ddx/ddy` through the expression tree
   - Prep for future screen-space derivatives (TAA, motion blur)

3. **Use Bitmasks for Optional Properties**:
   ```cpp
   struct PerMaterialCB {
       uint32_t connectedPropertiesMask;
       // Bit 0: hasParallax, Bit 1: hasHeightShadow, etc.
   };
   ```

4. **Precompile Quality Variants**:
   ```cpp
   enum class MaterialQuality { Low, Medium, High };
   // Compile shader 3 times with different PARALLAX_STEPS
   ```

5. **Hash Constants for Deduplication**:
   - Pool identical numerical constants across all materials
   - Reduces constant buffer pressure
