<!-- doc-meta -->
<!-- date: 2026-03-17 -->
<!-- author: unreal-architect -->
<!-- type: analysis -->
<!-- tags: ue5, material-system, struct-layouts, gpu-binding -->
<!-- end-doc-meta -->

# UE5 Material System — Concrete Struct Definitions

**Analysis Date**: 2026-03-17
**Purpose**: Exact C++ struct definitions for material data layouts (CPU and GPU sides)
**Audience**: UzEngine rendering engineers implementing material system patterns

---

## Part 1: CPU-Side Material Structures

### 1.1 Parameter Value Containers

These are the **runtime storage** for material parameter overrides in UMaterialInstance:

```cpp
// From Engine/Classes/Materials/MaterialInstance.h
// Simplified/annotated version

namespace UE {

// ─────────────────────────────────────────────────────────────────────────────
// Parameter Hash — Used as key in parameter lookup maps
// ─────────────────────────────────────────────────────────────────────────────

struct FHashedMaterialParameterInfo {
    // Hashed parameter name (32-bit or 64-bit CRC)
    uint64_t Hash = 0;

    // For debugging — stores original name string
    FName ParameterName = NAME_None;

    // Material expression GUID — used to validate parameter still exists
    // If expression was deleted and recreated, GUID changes → override removed
    FGuid ExpressionGuid = FGuid();

    bool operator==(const FHashedMaterialParameterInfo& Other) const {
        return Hash == Other.Hash && ExpressionGuid == Other.ExpressionGuid;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Scalar Parameter — Single float value
// ─────────────────────────────────────────────────────────────────────────────

struct FScalarParameterValue {
    FHashedMaterialParameterInfo Info;  // Parameter identity (hash + GUID)
    float Value = 0.0f;                 // The actual value

    // Typical ranges (documentation):
    // Roughness: [0, 1] (0=mirror, 1=diffuse)
    // Metallic: [0, 1] (0=dielectric, 1=pure metal)
    // Opacity: [0, 1] (0=transparent, 1=opaque)
    // Intensity: [0, ∞] (HDR values allowed)
};

// ─────────────────────────────────────────────────────────────────────────────
// Vector Parameter — Four float values (can be RGB, RGBA, direction, etc.)
// ─────────────────────────────────────────────────────────────────────────────

struct FVectorParameterValue {
    FHashedMaterialParameterInfo Info;     // Parameter identity
    FLinearColor Value = FLinearColor::Black;  // RGBA (float4)

    // Typical uses:
    // Color: BaseColor, Tint, EmissiveColor (RGB channels)
    // Position: World offset, displacement direction (XYZ channels)
    // Full RGBA: Arbitrary 4-component data (e.g., subsurface color + strength)
};

// ─────────────────────────────────────────────────────────────────────────────
// Texture Parameter — Reference to a texture asset
// ─────────────────────────────────────────────────────────────────────────────

struct FTextureParameterValue {
    FHashedMaterialParameterInfo Info;  // Parameter identity
    class UTexture* Value = nullptr;    // Pointer to texture asset

    // Can be any texture type:
    // - Texture2D (most common)
    // - TextureCube (for reflection probes, skyboxes)
    // - VolumeTexture (for 3D noise, SDF)
    // - TextureRenderTarget (for dynamic content)

    // If Value == nullptr:
    // Material shader uses fallback constant from MaterialExpression
    // (no crash, graceful degradation)
};

// ─────────────────────────────────────────────────────────────────────────────
// Runtime Parameter Set — Collection of all overrides in an instance
// ─────────────────────────────────────────────────────────────────────────────

class UMaterialInstance : public UMaterialInterface {
public:
    // ─── Material Hierarchy ────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material")
    TObjectPtr<UMaterialInterface> Parent = nullptr;

    // Max nesting depth: typically 5 levels
    // Example: BaseMaterial → VariantA → VariantA_Wet → VariantA_Wet_Frozen → Current

    // ─── Scalar Parameters ────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material", meta = (SkipSerialization))
    TArray<FScalarParameterValue> ScalarParameterValues;
    // Only stores OVERRIDDEN scalars
    // If parent has Roughness=0.5 and instance doesn't override it, NOT in array
    // Lookup: if not found here, check parent.GetScalarParameterValue()

    // ─── Vector Parameters ────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material", meta = (SkipSerialization))
    TArray<FVectorParameterValue> VectorParameterValues;
    // Only stores OVERRIDDEN vectors

    // ─── Texture Parameters ────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material", meta = (SkipSerialization))
    TArray<FTextureParameterValue> TextureParameterValues;
    // Only stores OVERRIDDEN textures

    // ─── Query Methods (used by renderer) ────────────────────
    bool GetScalarParameterValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        float& OutValue) const {
        // 1. Check local overrides
        for (const auto& Param : ScalarParameterValues) {
            if (Param.Info == ParameterInfo) {
                OutValue = Param.Value;
                return true;
            }
        }
        // 2. Check parent (recursive)
        if (Parent) {
            return Parent->GetScalarParameterValue(ParameterInfo, OutValue);
        }
        // 3. Not found in hierarchy — use shader default
        return false;
    }

    bool GetVectorParameterValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        FLinearColor& OutValue) const {
        // Same pattern as scalar
        for (const auto& Param : VectorParameterValues) {
            if (Param.Info == ParameterInfo) {
                OutValue = Param.Value;
                return true;
            }
        }
        if (Parent) {
            return Parent->GetVectorParameterValue(ParameterInfo, OutValue);
        }
        return false;
    }

    bool GetTextureParameterValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        class UTexture*& OutValue) const {
        // Same pattern
        for (const auto& Param : TextureParameterValues) {
            if (Param.Info == ParameterInfo) {
                OutValue = Param.Value;
                return true;
            }
        }
        if (Parent) {
            return Parent->GetTextureParameterValue(ParameterInfo, OutValue);
        }
        return false;
    }

    // ─── Mutation (Game Thread Only) ────────────────────────────
    void SetScalarParameterValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        float Value) {
        // 1. Try to update existing override
        for (auto& Param : ScalarParameterValues) {
            if (Param.Info == ParameterInfo) {
                Param.Value = Value;
                return;
            }
        }
        // 2. Not found, add new override
        ScalarParameterValues.Add({ParameterInfo, Value});
    }

    // Similar SetVectorParameterValue, SetTextureParameterValue...
};

} // namespace UE

```

### 1.2 Material Proxy (Thread-Safe Snapshot)

```cpp
// From Renderer/Private/SceneRendering.h
// Simplified version

class FMaterialRenderProxy {
public:
    virtual ~FMaterialRenderProxy() = default;

    // ─── Parameter Query (Render Thread, Immutable) ───────────
    // These are called during draw submission, must be FAST (single lookup)

    /// Get scalar parameter value.
    /// @param ParameterInfo — Hashed parameter name + GUID
    /// @param OutValue — Output float (undefined if returns false)
    /// @return true if parameter was found and populated, false if not found
    virtual bool GetScalarValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        float* OutValue) const = 0;

    /// Get vector parameter value.
    virtual bool GetVectorValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        FLinearColor* OutValue) const = 0;

    /// Get texture parameter value.
    virtual bool GetTextureValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        class FRHITexture*& OutValue) const = 0;

    // ─── Compiled Material Access ──────────────────────────────
    /// Get the compiled material (shader bytecode, parameter layout, etc.)
    virtual const class FMaterial* GetMaterial(ERHIFeatureLevel::Type FeatureLevel) const = 0;

    /// Get the static material (for static mesh components)
    virtual const class FMaterialInterface* GetMaterialInterface() const = 0;

    // ─── Memory Management ─────────────────────────────────────
    virtual uint32 GetMemoryFootprint() const { return sizeof(*this); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Colored Material Proxy — Simple example (override base color only)
// ─────────────────────────────────────────────────────────────────────────────

class FColoredMaterialRenderProxy : public FMaterialRenderProxy {
public:
    FColoredMaterialRenderProxy(
        const FMaterialRenderProxy* InParent,
        const FLinearColor& InColor)
        : Parent(InParent)
        , Color(InColor) {}

    bool GetScalarValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        float* OutValue) const override {
        // Delegate to parent (inherit all scalars)
        return Parent->GetScalarValue(ParameterInfo, OutValue);
    }

    bool GetVectorValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        FLinearColor* OutValue) const override {
        // Override base color, inherit rest
        if (ParameterInfo.ParameterName == FName(TEXT("BaseColor"))) {
            *OutValue = Color;
            return true;
        }
        return Parent->GetVectorValue(ParameterInfo, OutValue);
    }

    bool GetTextureValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        class FRHITexture*& OutValue) const override {
        // Delegate to parent (inherit all textures)
        return Parent->GetTextureValue(ParameterInfo, OutValue);
    }

    const class FMaterial* GetMaterial(ERHIFeatureLevel::Type FeatureLevel) const override {
        return Parent->GetMaterial(FeatureLevel);
    }

    const class FMaterialInterface* GetMaterialInterface() const override {
        return Parent->GetMaterialInterface();
    }

private:
    const FMaterialRenderProxy* Parent;
    FLinearColor Color;
};
```

---

## Part 2: GPU-Side Constant Buffer Layouts

### 2.1 Compiled Material Constant Buffer

The **exact layout** depends on the material's shader graph. Here's a typical example for a PBR material:

```hlsl
// In shader (e.g., MeshPixelShader.usf)
// Matches CPU struct exactly (padding included)

cbuffer MaterialCB : register(b1) {
    // ─── PBR Parameters ────────────────────────────
    float4  BaseColor;              // [0:15] RGBA (sRGB space)
    float   Metallic;               // [16:19] [0, 1]
    float   Roughness;              // [20:23] [0, 1] perceptual
    float   Specular;               // [24:27] [0, 1] non-metallic specular
    float   _Pad1;                  // [28:31] Alignment padding (256-bit = 32 bytes per CBV)

    // ─── Normal & AO ────────────────────────────────
    float4  EmissiveColor;          // [32:47] RGB + intensity scale
    float   NormalStrength;         // [48:51] Normal map intensity
    float   AOStrength;             // [52:55] AO map intensity
    float   DisplacementScale;      // [56:59] POM height
    float   _Pad2;                  // [60:63] Padding

    // ─── UV Transforms ──────────────────────────────
    float4  UVScale;                // [64:79] (ScaleX, ScaleY, OffsetX, OffsetY)
};

// ─── Usage in pixel shader ──────────────────────────
float4 PixelShader(PS_INPUT Input) : SV_TARGET {
    // Sample textures
    float4 AlbedoTexel = AlbedoTexture.Sample(AnisotropicSampler, Input.UV);

    // Apply parameter overrides
    float4 FinalAlbedo = AlbedoTexel * BaseColor;          // Tint
    float FinalRoughness = Roughness;                      // Parameter overrides texture
    float FinalMetallic = Metallic;

    // Evaluate BRDF (Cook-Torrance)
    float3 LitColor = EvaluateBRDF(
        FinalAlbedo.rgb,
        FinalRoughness,
        FinalMetallic,
        // ... normal, view dir, light dir, etc.
    );

    // Apply emissive
    float3 FinalColor = LitColor + EmissiveColor.rgb * EmissiveColor.a;

    return float4(FinalColor, 1.0);
}
```

### 2.2 CPU Struct Matching GPU Layout

```cpp
// C++ side — MUST match HLSL padding exactly

namespace UE {

// Maximum material uniform buffer size (DX12/Vulkan limit)
static const uint32 MaxMaterialCBSize = 4096;  // 4 KB

// ─────────────────────────────────────────────────────────────────────────────
// Material Uniform Buffer — Example PBR Layout
// ─────────────────────────────────────────────────────────────────────────────

struct FMaterialUniformBuffer {
    // ─── PBR Parameters [0:32 bytes] ─────────────────────
    FVector4f BaseColor = {1.0f, 1.0f, 1.0f, 1.0f};  // [0:15]
    float Metallic = 0.0f;                           // [16:19]
    float Roughness = 0.5f;                          // [20:23]
    float Specular = 0.5f;                           // [24:27]
    float _Pad1 = 0.0f;                              // [28:31]

    // ─── Emissive & Normal [32:64 bytes] ──────────────────
    FVector4f EmissiveColor = {0.0f, 0.0f, 0.0f, 0.0f};  // [32:47]
    float NormalStrength = 1.0f;                     // [48:51]
    float AOStrength = 1.0f;                         // [52:55]
    float DisplacementScale = 0.0f;                  // [56:59]
    float _Pad2 = 0.0f;                              // [60:63]

    // ─── UV Transforms [64:80 bytes] ─────────────────────
    FVector4f UVScale = {1.0f, 1.0f, 0.0f, 0.0f};  // [64:79]
    // .xy = tiling (scale)
    // .zw = offset

    // Total: 80 bytes (can be any value, rounded to 16-byte boundary by GPU)

    // For compatibility, round up to next 16-byte boundary:
    // 80 → 96 bytes (add 16 bytes padding at end)
    uint8_t _AlignmentPadding[16];

    static_assert(sizeof(FMaterialUniformBuffer) % 16 == 0,
        "Material CB must be 16-byte aligned for GPU upload");
};

// ─────────────────────────────────────────────────────────────────────────────
// Alternative: Minimal Material CB (for simple PBR)
// ─────────────────────────────────────────────────────────────────────────────

struct FMaterialCBMinimal {
    FVector4f BaseColor;        // [0:15]
    float Metallic;             // [16:19]
    float Roughness;            // [20:23]
    float Specular;             // [24:27]
    float _Pad;                 // [28:31]

    // Total: 32 bytes (one 256-bit CBV boundary)

    static_assert(sizeof(FMaterialCBMinimal) == 32);
};

// ─────────────────────────────────────────────────────────────────────────────
// Upload: From Instance to GPU
// ─────────────────────────────────────────────────────────────────────────────

void UploadMaterialCB(
    const UMaterialInstance* Instance,
    FMaterialUniformBuffer* OutCB) {
    // Initialize with defaults
    FMaterialUniformBuffer CB;

    // Query scalar parameters
    float Roughness = 0.5f;
    Instance->GetScalarParameterValue(FName("Roughness"), Roughness);
    CB.Roughness = Roughness;

    float Metallic = 0.0f;
    Instance->GetScalarParameterValue(FName("Metallic"), Metallic);
    CB.Metallic = Metallic;

    // Query vector parameters
    FLinearColor BaseColor = FLinearColor::White;
    Instance->GetVectorParameterValue(FName("BaseColor"), BaseColor);
    CB.BaseColor = FVector4f(BaseColor);

    FLinearColor EmissiveColor = FLinearColor::Black;
    Instance->GetVectorParameterValue(FName("EmissiveColor"), EmissiveColor);
    CB.EmissiveColor = FVector4f(EmissiveColor);

    // Copy to output
    *OutCB = CB;
}

} // namespace UE
```

---

## Part 3: Material Expression Tree (Compiled)

### 3.1 Compiled Material Output

When a material is compiled, it produces:

```cpp
// Simplified FMaterialCompilationOutput

struct FMaterialCompilationOutput {
    // ─── Shader Code ──────────────────────────────────────
    // Compiled HLSL/GLSL bytecode (FShaderCompiledShaderMap)
    class FShaderCompiledShaderMap* ShaderMap = nullptr;

    // ─── Parameter Metadata ────────────────────────────────
    // Maps parameter names to constant buffer offsets
    TMap<FHashedMaterialParameterInfo, int32> ParameterToConstantBufferMap;
    // Example:
    //   {"Roughness", 20}     — offset 20 bytes in CB
    //   {"Metallic", 16}      — offset 16 bytes in CB
    //   {"BaseColor", 0}      — offset 0 bytes in CB

    // ─── Material Properties ────────────────────────────────
    EBlendMode BlendMode = BLEND_Opaque;
    bool bTwoSided = false;
    bool bCastShadow = true;
    EMaterialDomain MaterialDomain = MD_Surface;

    // ─── Texture Samplers ──────────────────────────────────
    TArray<FMaterialTextureInfo> Textures;
    // Contains: texture type, name, sRGB flag, etc.
};

struct FMaterialTextureInfo {
    FName Name;                         // "BaseColor", "Normal", etc.
    uint32 TextureIndex;                // Slot in compiled shader
    bool bSRGB = false;                 // Color space flag
    ETextureAddressMode AddressX = TAM_Wrap;
    ETextureAddressMode AddressY = TAM_Wrap;
    ETextureSamplerFilter Filter = TSF_Default;
};
```

---

## Part 4: Parameter Value Ranges (Typical PBR)

### 4.1 Scalar Parameters

| Parameter | Type | Min | Max | Typical | Notes |
|-----------|------|-----|-----|---------|-------|
| **Roughness** | [0, 1] | 0 (mirror) | 1 (diffuse) | 0.5 | Perceptual; remapped to α² in shader |
| **Metallic** | [0, 1] | 0 (dielectric) | 1 (metal) | 0.0 | Prefer 0 or 1; in-between is unphysical |
| **Specular** | [0, 1] | 0 | 1 | 0.5 | Non-metallic specular; for dielectrics |
| **Opacity** | [0, 1] | 0 (transparent) | 1 (opaque) | 1.0 | Alpha transparency; forward only |
| **Normal Strength** | [0, ∞] | 0 (flat) | 2.0+ (exaggerated) | 1.0 | Normal map intensity multiplier |
| **AO Strength** | [0, 1] | 0 (no darken) | 1 (full) | 1.0 | Ambient occlusion strength |
| **Emissive Intensity** | [0, ∞] | 0 (off) | 100+ (very bright) | 0.0 | HDR intensity scale |
| **Displacement / Height** | [0, 1] | 0 (no offset) | 1 (max) | 0.0 | Parallax occlusion height |

### 4.2 Vector Parameters

| Parameter | Type | Range | Example | Notes |
|-----------|------|-------|---------|-------|
| **Base Color** | RGB [0, 1] | Per-channel | (0.8, 0.8, 0.8) | Albedo tint |
| **Base Color + Alpha** | RGBA [0, 1] | Per-channel | (0.8, 0.8, 0.8, 1.0) | With opacity in α |
| **Emissive Color** | RGB [0, ∞] | Per-channel | (1.0, 0.5, 0.0) | HDR color + intensity scale |
| **Tint Color** | RGBA [0, 1] | Per-channel | (1.0, 1.0, 1.0, 1.0) | Generic color overlay |
| **Subsurface Color** | RGB [0, 1] | Per-channel | (1.0, 0.5, 0.5) | Skin/SSS transmission color |
| **Velocity** | XY float | Unbounded | (-0.5, 0.2) | Motion vector for TAA |

---

## Part 5: Memory Footprint Examples

### 5.1 Single Material Instance

```cpp
// UMaterialInstance with overrides:
size_t memFootprint = 0;

// Base class overhead
memFootprint += sizeof(UObject);              // ~64 bytes

// Material interface pointer
memFootprint += sizeof(TObjectPtr<UMaterialInterface>);  // ~8 bytes

// Parameter arrays (capacity allocation)
int32 scalarOverrides = 3;  // Roughness, Metallic, NormalStrength
memFootprint += scalarOverrides * sizeof(FScalarParameterValue);
// 3 × ~40 bytes = 120 bytes

int32 vectorOverrides = 2;  // BaseColor, EmissiveColor
memFootprint += vectorOverrides * sizeof(FVectorParameterValue);
// 2 × ~56 bytes = 112 bytes

int32 textureOverrides = 1;  // Normal map
memFootprint += textureOverrides * sizeof(FTextureParameterValue);
// 1 × ~40 bytes = 40 bytes

// Total: ~64 + 8 + 120 + 112 + 40 = 344 bytes

// For 1000 instances: 344 KB (sparse storage is very efficient!)
```

### 5.2 Comparison: Fixed vs. Sparse Storage

```cpp
// ─── FIXED 48-byte struct (like current UzEngine) ───
// Per-material: 48 bytes
// For 1000 instances: 48 KB
// BUT: Can't extend without binary break

// ─── SPARSE override maps (like UE5) ───
// Per-instance with 3 scalar + 2 vector + 1 texture overrides: ~344 bytes
// For 1000 instances: 344 KB
// BUT: Highly extensible, supports inheritance, thread-safe snapshots

// Trade-off: 7× more memory per instance, but 10000× more flexibility
```

---

## Part 6: Example: Creating & Using a Material Instance

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// Game Code (typical usage)
// ─────────────────────────────────────────────────────────────────────────────

// Create base material (shader graph defined in editor)
UMaterial* BasePBRMaterial = /* loaded from editor */;

// Create instance (variation)
UMaterialInstance* WoodInstance = NewObject<UMaterialInstance>();
WoodInstance->SetParent(BasePBRMaterial);

// Override parameters (sparse — only these stored)
WoodInstance->SetScalarParameterValue(
    FHashedMaterialParameterInfo{
        FName(TEXT("Roughness")),
        FGuid::NewGuid()  // Expression GUID
    },
    0.6f  // Roughness = 0.6 (from 0.5 default)
);

WoodInstance->SetVectorParameterValue(
    FHashedMaterialParameterInfo{FName(TEXT("BaseColor")), FGuid::NewGuid()},
    FLinearColor(0.8f, 0.6f, 0.3f, 1.0f)  // Brownish wood color
);

// ─────────────────────────────────────────────────────────────────────────────
// Render Code (called once per frame per instance)
// ─────────────────────────────────────────────────────────────────────────────

// Scene capture phase (game thread)
void CaptureSceneParameters() {
    for (const auto& Component : Scene->GetMeshComponents()) {
        UMaterialInterface* Material = Component->GetMaterial(0);

        // CRITICAL: Capture proxy at render time
        // This freezes parameter values for this frame
        FMaterialRenderProxy* Proxy = Material->GetRenderProxy();

        // Pass immutable proxy to render thread
        DrawCommand.MaterialProxy = Proxy;
    }
}

// Render phase (render thread)
void RenderMesh(const FDrawCommand& Cmd) {
    // Step 1: Get captured proxy (thread-safe, immutable this frame)
    FMaterialRenderProxy* Proxy = Cmd.MaterialProxy;

    // Step 2: Query parameters
    float Roughness = 0.5f;  // Default
    Proxy->GetScalarValue(FName("Roughness"), &Roughness);
    // Returns 0.6 from override (WoodInstance)

    FLinearColor BaseColor = FLinearColor::White;
    Proxy->GetVectorValue(FName("BaseColor"), &BaseColor);
    // Returns (0.8, 0.6, 0.3, 1.0) from override

    // Step 3: Build constant buffer
    FMaterialUniformBuffer MaterialCB;
    MaterialCB.Roughness = Roughness;
    MaterialCB.BaseColor = FVector4f(BaseColor);
    // ... fill rest ...

    // Step 4: Upload and draw
    FConstantBufferRHIRef CB = RHICmdList->CreateConstantBuffer(&MaterialCB);
    RHICmdList->SetShaderUniformBuffer(PixelShader, FMaterialUniformBuffer::GetBindSlot(), CB);
    RHICmdList->DrawIndexedInstanced(...);
}
```

---

## References

### Source Files
- `Engine/Classes/Materials/MaterialInstance.h` — Instance definition
- `Engine/Classes/Materials/MaterialInterface.h` — Virtual base class
- `Renderer/Private/SceneRendering.h` — Proxy implementation
- `Renderer/Private/MaterialShaderTypes.h` — Constant buffer utilities

### Related Specs
- **Shader System**: `ue5-rendering-systems-analysis.md` (BRDF, G-Buffer formats)
- **Material System**: `ue5-material-parameter-system-extraction.md` (Architecture patterns)
- **UzEngine Current**: `runtime/engine/renderer/material/material.h` (SimpleStructMat approach)

---

**This document should be kept in sync with actual UE5 source as new versions (5.7+) are released.**
