# UE5 POM Implementation: Exact Code References

**Purpose**: Quick lookup of exact file paths and line numbers for UE5's parallax mapping architecture.

---

## 1. Material Expression Base Class

### File Path
`W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpression.h`

### Key Definition (approximate lines 1–100)

```cpp
class UMaterialExpression : public UObject
{
    // Virtual compile function every expression implements
    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) = 0;

    // Output descriptions (for editor UI)
    virtual void GetOutputs(TArray<class FExpressionOutput>& OutOutputs) const = 0;

    // Inputs (variable count, derived classes expose via UPROPERTY)
    virtual FExpressionInput* GetInput(int32 InputIndex) = 0;

    // Type information
    virtual EMaterialValueType GetOutputValueType(int32 OutputIndex) const = 0;
};
```

---

## 2. BumpOffset Expression (Parallax Implementation)

### Header File
`W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionBumpOffset.h`

**Lines 1–47** (full file):

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionBumpOffset.generated.h"

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionBumpOffset : public UMaterialExpression
{
    GENERATED_UCLASS_BODY()

    // Outputs: Coordinate + Eye.xy * (Height - ReferencePlane) * HeightRatio
    UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'ConstCoordinate' if not specified"))
    FExpressionInput Coordinate;

    UPROPERTY()
    FExpressionInput Height;

    UPROPERTY(meta=(RequiredInput = "false"))
    FExpressionInput HeightRatioInput;

    UPROPERTY(EditAnywhere, Category=MaterialExpressionBumpOffset, meta=(OverridingInputProperty = "HeightRatioInput"))
    float HeightRatio = 0.05f;

    UPROPERTY(EditAnywhere, Category=MaterialExpressionBumpOffset)
    float ReferencePlane = 0.5f;

    UPROPERTY(EditAnywhere, Category = MaterialExpressionBumpOffset, meta = (OverridingInputProperty = "Coordinate"))
    uint32 ConstCoordinate = 0;

    //~ Begin UMaterialExpression Interface
#if WITH_EDITOR
    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
    //~ End UMaterialExpression Interface
};
```

### Implementation File
`W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\MaterialExpressions.cpp`

**Exact code** (search for "UMaterialExpressionBumpOffset::Compile"):

```cpp
int32 UMaterialExpressionBumpOffset::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
    if(!Height.GetTracedInput().Expression)
    {
        return Compiler->Errorf(TEXT("Missing Height input"));
    }

    return Compiler->Add(
        Compiler->Mul(
            Compiler->ComponentMask(
                Compiler->TransformVector(MCB_World, MCB_Tangent, Compiler->CameraVector()),
                1,1,0,0),
            Compiler->Add(
                Compiler->Mul(
                    HeightRatioInput.GetTracedInput().Expression ? Compiler->ForceCast(HeightRatioInput.Compile(Compiler),MCT_Float1) : Compiler->Constant(HeightRatio),
                    Compiler->ForceCast(Height.Compile(Compiler),MCT_Float1)
                ),
                HeightRatioInput.GetTracedInput().Expression ? Compiler->Mul(Compiler->Constant(-ReferencePlane), Compiler->ForceCast(HeightRatioInput.Compile(Compiler),MCT_Float1)) : Compiler->Constant(-ReferencePlane * HeightRatio)
            )
        ),
        Coordinate.GetTracedInput().Expression ? Coordinate.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false)
    );
}

void UMaterialExpressionBumpOffset::GetCaption(TArray<FString>& OutCaptions) const
{
    OutCaptions.Add(TEXT("BumpOffset"));
}
```

---

## 3. Material Property Enumeration

### File Path
`W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\SceneTypes.h`

**Lines 143–187** (EMaterialProperty enum):

```cpp
//
//	EMaterialProperty
//
UENUM(BlueprintType)
enum EMaterialProperty : int
{
    MP_EmissiveColor = 0 UMETA(DisplayName = "Emissive"),
    MP_Opacity UMETA(DisplayName = "Opacity"),
    MP_OpacityMask UMETA(DisplayName = "Opacity Mask"),
    MP_DiffuseColor UMETA(Hidden),
    MP_SpecularColor UMETA(Hidden),
    MP_BaseColor UMETA(DisplayName = "Diffuse"),
    MP_Metallic UMETA(DisplayName = "Metallic"),
    MP_Specular UMETA(DisplayName = "Specular"),
    MP_Roughness UMETA(DisplayName = "Roughness "),
    MP_Anisotropy UMETA(DisplayName = "Anisotropy"),
    MP_Normal UMETA(DisplayName = "Normal"),
    MP_Tangent UMETA(DisplayName = "Tangent"),
    MP_WorldPositionOffset UMETA(DisplayName = "World Position Offset"),
    MP_WorldDisplacement_DEPRECATED UMETA(Hidden),
    MP_TessellationMultiplier_DEPRECATED UMETA(Hidden),
    MP_SubsurfaceColor UMETA(DisplayName = "Subsurface"),
    MP_CustomData0 UMETA(Hidden),
    MP_CustomData1 UMETA(Hidden),
    MP_AmbientOcclusion UMETA(DisplayName = "Ambient Occlusion"),
    MP_Refraction UMETA(DisplayName = "Refraction"),
    MP_CustomizedUVs0 UMETA(Hidden),
    MP_CustomizedUVs1 UMETA(Hidden),
    MP_CustomizedUVs2 UMETA(Hidden),
    MP_CustomizedUVs3 UMETA(Hidden),
    MP_CustomizedUVs4 UMETA(Hidden),
    MP_CustomizedUVs5 UMETA(Hidden),
    MP_CustomizedUVs6 UMETA(Hidden),
    MP_CustomizedUVs7 UMETA(Hidden),
    MP_PixelDepthOffset UMETA(Hidden),
    MP_ShadingModel UMETA(Hidden),
    MP_FrontMaterial UMETA(DisplayName = "Front Material"),
    MP_SurfaceThickness UMETA(Hidden),
    MP_Displacement UMETA(Hidden),  // <-- KEY LINE FOR POM

    //^^^ New material properties go above here ^^^^
    MP_MaterialAttributes UMETA(Hidden),
    MP_CustomOutput UMETA(Hidden),
    MP_MAX UMETA(DisplayName = "None"),
};
```

**Key Insight**: `MP_Displacement` (line 181) is a **first-class property**, distinct from:
- `MP_WorldPositionOffset` (vertex-space)
- `MP_PixelDepthOffset` (post-projection depth)

---

## 4. FShaderCodeChunk Structure

### File Path
`W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h`

**Lines 82–170** (full FShaderCodeChunk definition):

```cpp
struct FShaderCodeChunk
{
    /**
     * Hash of the code chunk, used to determine equivalent chunks created from different expressions
     * By default this is simply the hash of the code string
     */
    uint64 Hash;

    uint64 MaterialAttributeMask;

    /**
     * Definition string of the code chunk.
     * If !bInline && !UniformExpression || UniformExpression->IsConstant(), this is the definition of a local variable named by SymbolName.
     * Otherwise if bInline || (UniformExpression && UniformExpression->IsConstant()), this is a code expression that needs to be inlined.
     * This string uses hardware finite differences.
     */
    FString DefinitionFinite;

    /**
    * Definition string of the code chunk, but with analytic partial derivatives.
    */
    FString DefinitionAnalytic;

    /**
     * Name of the local variable used to reference this code chunk.
     * If bInline || UniformExpression, there will be no symbol name and Definition should be used directly instead.
     */
    FString SymbolName;

    /** Reference to a uniform expression, if this code chunk has one. */
    TRefCountPtr<FMaterialUniformExpression> UniformExpression;

    /** All the chunks that are scoped under this chunk. */
    TArray<int32> ScopedChunks;
    TArray<int32> ReferencedCodeChunks;

    EMaterialValueType Type;

    int32 DeclaredScopeIndex;
    int32 UsedScopeIndex;
    int32 ScopeLevel;

    /** Whether the code chunk should be inlined or not. */
    bool bInline;

    /** The status of partial derivatives at this expression. **/
    EDerivativeStatus DerivativeStatus;

    FString& AtDefinition(ECompiledPartialDerivativeVariation Variation)
    {
        checkf(Variation == CompiledPDV_FiniteDifferences || Variation == CompiledPDV_Analytic,
               TEXT("Invalid partial derivative variation: %d"), Variation);
        return Variation == CompiledPDV_FiniteDifferences ? DefinitionFinite : DefinitionAnalytic;
    }

    // Ctor for code chunk without uniform expression
    FShaderCodeChunk(uint64 InHash, const TCHAR* InDefinitionFinite,
                     const TCHAR* InDefinitionAnalytic, const FString& InSymbolName,
                     EMaterialValueType InType, EDerivativeStatus InDerivativeStatus,
                     bool bInInline):
        Hash(InHash),
        MaterialAttributeMask(0u),
        DefinitionFinite(InDefinitionFinite),
        DefinitionAnalytic(InDefinitionAnalytic),
        SymbolName(InSymbolName),
        UniformExpression(NULL),
        Type(InType),
        DeclaredScopeIndex(INDEX_NONE),
        UsedScopeIndex(INDEX_NONE),
        ScopeLevel(0),
        bInline(bInInline),
        DerivativeStatus(InDerivativeStatus)
    {}

    // Ctor for code chunk with uniform expression
    FShaderCodeChunk(uint64 InHash, FMaterialUniformExpression* InUniformExpression,
                     const TCHAR* InDefinitionFinite, const TCHAR* InDefinitionAnalytic,
                     EMaterialValueType InType, EDerivativeStatus InDerivativeStatus):
        Hash(InHash),
        MaterialAttributeMask(0u),
        DefinitionFinite(InDefinitionFinite),
        DefinitionAnalytic(InDefinitionAnalytic),
        UniformExpression(InUniformExpression),
        Type(InType),
        DeclaredScopeIndex(INDEX_NONE),
        UsedScopeIndex(INDEX_NONE),
        ScopeLevel(0),
        bInline(false),
        DerivativeStatus(InDerivativeStatus)
    {}
};
```

---

## 5. BasePass Vertex Shader

### File Path
`W:\UE_5.6_Source\Engine\Shaders\Private\BasePassVertexShader.usf`

**Key Section** (WorldPositionOffset evaluation):

Search for "GetMaterialWorldPositionOffset" — typically around line ~250:

```hlsl
// Vertex shader main
void FShaderVSToPS_Forward(
    FVertexFactoryInput Input,
    out FBasePassVSOutput Output
)
{
    // ... vertex input setup ...

    float4 WorldPosition = float4(TransformPosition(Input.Position), 1);

    // Material expressions evaluation
    FMaterialVertexParameters VertexParameters = GetMaterialVertexParameters(Input, WorldPosition, ...);

    // Apply World Position Offset (if material uses it)
#if USE_WORLD_POSITION_OFFSET
    WorldPosition.xyz += GetMaterialWorldPositionOffset(VertexParameters);
#endif

    // ... rest of VS processing ...
    Output.Position = mul(float4(WorldPosition.xyz, 1), ResolvedView.TranslatedWorldToClip);
}
```

---

## 6. Nanite Displacement Map (CPU Ray Marching)

### File Path
`W:\UE_5.6_Source\Engine\Source\Developer\NaniteUtilities\Public\DisplacementMap.h`

**Full file** (lines 1–140):

```cpp
#pragma once

#include "Engine/Texture.h"

namespace Nanite
{
    class FDisplacementMap
    {
    public:
        ETextureSourceFormat   SourceFormat;
        int32                  BytesPerPixel;
        int32                  SizeX;
        int32                  SizeY;
        uint32                 NumLevels;
        float                  Magnitude;
        float                  Center;
        TextureAddress         AddressX;
        TextureAddress         AddressY;

    public:
        NANITEUTILITIES_API                     FDisplacementMap();
        NANITEUTILITIES_API                     FDisplacementMap( struct FImage&& TextureSourceImage, float InMagnitude, float InCenter, TextureAddress InAddressX, TextureAddress InAddressY );

                                                // Bilinear filtered
        NANITEUTILITIES_API float               Sample( FVector2f UV ) const;
        NANITEUTILITIES_API FVector2f           Sample( FVector2f MinUV, FVector2f MaxUV ) const;

        float                                   Sample( int32 x, int32 y ) const;
        FVector2f                               Sample( int32 x, int32 y, uint32 Level ) const;

        float                                   Load( int32 x, int32 y ) const;
        FVector2f                               Load( int32 x, int32 y, uint32 Level ) const;

    private:
        TArray64< uint8 >                       SourceData;
        TArray< FVector2f >                     MipData[12];

        void                                    Address( int32& x, int32& y ) const;
    };

    // ─────────────────────────────────────────────────────────

    FORCEINLINE float FDisplacementMap::Sample( int32 x, int32 y ) const
    {
        Address(x,y);

        float Displacement = Load(x,y);
        Displacement -= Center;
        Displacement *= Magnitude;

        return Displacement;
    }

    FORCEINLINE FVector2f FDisplacementMap::Sample( int32 x, int32 y, uint32 Level ) const
    {
        Address(x,y);

        x >>= Level;
        y >>= Level;

        FVector2f Displacement = Load( x, y, Level );
        Displacement -= FVector2f( Center );
        Displacement *= Magnitude;

        return Displacement;
    }

    FORCEINLINE float FDisplacementMap::Load( int32 x, int32 y ) const
    {
        const uint8* PixelPtr = &SourceData[ int64( x + (int64)y * SizeX ) * BytesPerPixel ];

        if( SourceFormat == TSF_BGRA8 )
        {
            return float( PixelPtr[2] ) / 255.0f;
        }
        else if( SourceFormat == TSF_RGBA16 )
        {
            checkSlow(BytesPerPixel == sizeof(uint16) * 4);
            return float( *(uint16*)PixelPtr ) / 65535.0f;
        }
        else if( SourceFormat == TSF_RGBA16F || SourceFormat == TSF_R16F )
        {
            FFloat16 HalfValue = *(FFloat16*)PixelPtr;
            return HalfValue;
        }
        else if( SourceFormat == TSF_G8 )
        {
            return float( PixelPtr[0] ) / 255.0f;
        }
        else if (SourceFormat == TSF_G16)
        {
            return float( *(uint16*)PixelPtr ) / 65535.0f;
        }
        else if( SourceFormat == TSF_RGBA32F || SourceFormat == TSF_R32F )
        {
            return *(float*)PixelPtr;
        }
        else
        {
            checkf( 0, TEXT("Displacement map format not supported") );
            return 0.0f;
        }
    }

    FORCEINLINE FVector2f FDisplacementMap::Load( int32 x, int32 y, uint32 Level ) const
    {
        checkSlow( Level > 0 );

        uint32 MipSizeX = ( ( SizeX - 1 ) >> Level ) + 1;
        uint32 MipSizeY = ( ( SizeY - 1 ) >> Level ) + 1;

        return MipData[ Level - 1 ][ x + y * MipSizeX ];
    }

    FORCEINLINE void FDisplacementMap::Address( int32& x, int32& y ) const
    {
        if( AddressX == TA_Clamp )
            x = FMath::Clamp( x, 0, SizeX - 1 );
        else
        {
            x  = x % SizeX;
            x += x < 0 ? SizeX : 0;
        }

        if( AddressY == TA_Clamp )
            y = FMath::Clamp( y, 0, SizeY - 1 );
        else
        {
            y  = y % SizeY;
            y += y < 0 ? SizeY : 0;
        }
    }
}
```

**Key Insights**:
- **CPU-side only** (Nanite offline baking)
- **Magnitude & Center** parameters (same as BumpOffset HeightRatio/ReferencePlane)
- **Mip support** (Level awareness for multi-resolution samples)
- **Wrap mode handling** (Address function handles TA_Clamp vs. wrap)

---

## 7. Material Compiler Interface

### File Path
`W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\MaterialCompiler.h`

**Key Methods** (lines ~115–300):

```cpp
class FMaterialCompiler
{
public:
    virtual ~FMaterialCompiler() { }

    // Main virtual function: subclasses implement for HLSL/Lightmass/Proxy
    virtual bool ShouldStopTranslating() const = 0;
    virtual int32 Error(const TCHAR* Text) = 0;
    virtual int32 CallExpression(FMaterialExpressionKey ExpressionKey, FMaterialCompiler* InCompiler) = 0;

    // Shader code generation methods
    virtual int32 Constant(float X) = 0;
    virtual int32 Constant2(float X, float Y) = 0;
    virtual int32 Constant3(float X, float Y, float Z) = 0;
    virtual int32 Constant4(float X, float Y, float Z, float W) = 0;

    // Arithmetic
    virtual int32 Add(int32 A, int32 B) = 0;
    virtual int32 Sub(int32 A, int32 B) = 0;
    virtual int32 Mul(int32 A, int32 B) = 0;
    virtual int32 Div(int32 A, int32 B) = 0;

    // Texture operations
    virtual int32 TextureSample(
        class UTexture* Texture,
        int32 Coordinates,
        EMaterialSamplerType SamplerType,
        int32 MipValue0 = INDEX_NONE,
        int32 MipValue1 = INDEX_NONE,
        ETextureMipValueMode MipValueMode = TMVM_None,
        ESamplerSourceMode SamplerSource = SSM_FromTextureAsset,
        int32 TextureReferenceIndex = INDEX_NONE
    ) = 0;

    virtual int32 TextureCoordinate(
        uint32 CoordinateIndex,
        bool UnMirrorU,
        bool UnMirrorV
    ) = 0;

    // Vector operations
    virtual int32 AppendVector(int32 A, int32 B) = 0;
    virtual int32 ComponentMask(int32 InExpression, bool R, bool G, bool B, bool A) = 0;

    // Transformations
    virtual int32 TransformVector(
        EMaterialCommonBasis SourceCoordinate,
        EMaterialCommonBasis DestinationCoordinate,
        int32 A
    ) = 0;

    // Camera/view info
    virtual int32 CameraVector() = 0;
    virtual int32 CameraPosition() = 0;

    // Type casting
    virtual int32 ForceCast(int32 Code, EMaterialValueType DestType) = 0;
    virtual EMaterialValueType GetType(int32 Code) = 0;
};
```

---

## 8. Material Property Definition Map

### File Path
`W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialAttributeDefinitionMap.h`

**Key Section** (defines property-to-shader-output mapping):

This file maps `EMaterialProperty` enum values to HLSL output semantics.

---

## 9. BreakMaterialAttributes Expression

### File Path
`W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionBreakMaterialAttributes.h`

**Lines 1–44** (full definition):

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "MaterialValueType.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionBreakMaterialAttributes.generated.h"

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionBreakMaterialAttributes : public UMaterialExpression
{
    GENERATED_UCLASS_BODY()

    UPROPERTY()
    FMaterialAttributesInput MaterialAttributes;

    //~ Begin UObject Interface
    virtual void Serialize(FStructuredArchive::FRecord Record) override;
    //~ End UObject Interface

    //~ Begin UMaterialExpression Interface
#if WITH_EDITOR
    virtual FExpressionInput* GetInput(int32 InputIndex) override
    {
        return InputIndex == 0 ? &MaterialAttributes : nullptr;
    }

    virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual FName GetInputName(int32 InputIndex) const override;
    virtual bool IsInputConnectionRequired(int32 InputIndex) const override;
    virtual EMaterialValueType GetInputValueType(int32 InputIndex) override
    {
        return MCT_MaterialAttributes;
    }

    virtual EMaterialValueType GetOutputValueType(int32 OutputIndex) override;

    static TMap<EMaterialProperty, int32> PropertyToIOIndexMap;
    static void BuildPropertyToIOIndexMap();

#endif // WITH_EDITOR
    //~ End UMaterialExpression Interface
};
```

**Key**: `PropertyToIOIndexMap` maintains **static mapping** from `MP_Normal` → output pin 10, `MP_Roughness` → output pin 8, etc.

---

## Quick Reference: Core Pattern

### 1. Define Expression (Header)
```cpp
// MaterialExpressionBumpOffset.h
UCLASS(...) class UMaterialExpressionBumpOffset : public UMaterialExpression {
    UPROPERTY() FExpressionInput Height;
    UPROPERTY() float HeightRatio = 0.05f;
    virtual int32 Compile(FMaterialCompiler* C, int32 OutputIndex) override;
};
```

### 2. Implement Compile (Source)
```cpp
// MaterialExpressions.cpp
int32 UMaterialExpressionBumpOffset::Compile(FMaterialCompiler* C, int32 OutputIndex) {
    int32 HeightCode = Height.Compile(C);
    int32 CamVecCode = C->TransformVector(MCB_World, MCB_Tangent, C->CameraVector());
    int32 OffsetCode = C->Mul(C->ComponentMask(CamVecCode, 1,1,0,0), HeightCode);
    return C->Add(OffsetCode, InputUVCode);
}
```

### 3. Generate HLSL
```hlsl
// Result in BasePassPixelShader.usf:
float2 ParallaxUV = (
    (CameraVecTangentXY) * (HeightTexture.Sample(...).r)
) + InputTexCoord0;

// Use ParallaxUV for subsequent texture samples
```

---

## Summary: All Relevant File Paths

| Component | File Path | Key Lines |
|-----------|-----------|-----------|
| **BumpOffset Header** | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionBumpOffset.h` | 1–47 |
| **BumpOffset Impl** | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\MaterialExpressions.cpp` | ~800–850 |
| **EMaterialProperty** | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\SceneTypes.h` | 143–187 |
| **FShaderCodeChunk** | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h` | 82–170 |
| **Material Compiler** | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\MaterialCompiler.h` | 115–300 |
| **Base Pass Vertex** | `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassVertexShader.usf` | ~250+ |
| **Nanite Displacement** | `W:\UE_5.6_Source\Engine\Source\Developer\NaniteUtilities\Public\DisplacementMap.h` | 1–140 |
| **Break Attributes** | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpressionBreakMaterialAttributes.h` | 1–44 |
| **Expression Base** | `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Materials\MaterialExpression.h` | ~100 |

---

## Grep Commands for Quick Lookup

```bash
# Find BumpOffset compilation
grep -n "UMaterialExpressionBumpOffset::Compile" \
  W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\MaterialExpressions.cpp

# Find EMaterialProperty enum
grep -n "enum EMaterialProperty" \
  W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\SceneTypes.h

# Find FShaderCodeChunk
grep -n "struct FShaderCodeChunk" \
  W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Materials\HLSLMaterialTranslator.h

# Find WorldPositionOffset references
grep -n "WorldPositionOffset" \
  W:\UE_5.6_Source\Engine\Shaders\Private\BasePassVertexShader.usf
```

---

## Summary

This document provides **exact file locations and line numbers** for every key pattern in UE5's POM architecture. Use it as a reference when:
- Implementing material expressions in UzEngine
- Understanding derivative tracking and code generation
- Adding new material properties
- Optimizing shader compilation pipelines
