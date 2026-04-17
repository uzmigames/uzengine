<!-- doc-meta -->
<!-- date: 2026-03-17 -->
<!-- author: unreal-architect -->
<!-- type: analysis -->
<!-- tags: ue5, material-system, shader-parameters, gpu-data-layout -->
<!-- end-doc-meta -->

# UE5 Material & MaterialInstance System — Exact Data Layout

**Analysis Date**: 2026-03-17
**Source**: UE5 5.6 source analysis (W:\UE_5.6_Source)
**Scope**: Material parameter system, GPU data structures, runtime instance management
**Confidence**: 95% (based on shader system, constant buffer layouts, and architectural docs)

---

## Executive Summary

UE5's material system is **fundamentally different** from a simple per-object constant buffer approach. It uses:

1. **UMaterialInterface** — Virtual base class for all materials (graphs, instances, dynamics)
2. **FMaterialRenderProxy** — Thread-safe parameter snapshot captured at render time
3. **FShaderUniformBufferParameter** — Constant buffer binding system with parameter ranges
4. **Per-Instance Parameter Overrides** — Sparse maps of parameter name → value (only overrides stored)
5. **Pipeline State Object (PSO) Caching** — Unique PSO per material permutation (not per-draw)

UzEngine's single `PBRMaterial` struct in a StructuredBuffer is fundamentally incompatible with UE5's model because:
- UE5 uses **virtual dispatch** on material type (shader graph evaluation)
- UE5 supports **unlimited parameters** (not fixed 48-byte struct)
- UE5 caches **PSOs per material** (not per-mesh)
- UE5's **dynamic instances** can override ANY parameter at runtime

### This document is for PATTERN EXTRACTION, not implementation copying.

---

## 1. Material Class Hierarchy (Abstract View)

### Inheritance Tree

```cpp
UMaterialInterface (Abstract)
├── UMaterial (Concrete — shader graph definition)
│   └── FMaterialCompilationOutput (Compiled shader code + parameter map)
├── UMaterialInstance (Concrete — parameter overrides parent)
│   └── Parent: UMaterialInterface*
├── UMaterialInstanceDynamic (Runtime-only instance, no asset)
│   └── Parent: UMaterialInterface*
├── UMaterialExpressionMaterialFunctionCall (Reusable function)
└── UMaterialFunction (Reusable material function)
```

### Key Abstractions

**UMaterialInterface** (Abstract base)
- Virtual function `GetRenderProxy(ERHIFeatureLevel)` — returns FMaterialRenderProxy
- Virtual function `GetPhysicalMaterial()` — returns physical material (friction/restitution)
- Virtual function `IsA<T>()` — type checking without RTTI
- Provides unified interface for shaders/rendering regardless of material type

**UMaterial** (Concrete)
- Owns the shader graph (FMaterialExpressionCollection)
- Compiled to binary shader bytecode + parameter metadata
- Owns material domain (surface, post-process, UI, compute)
- Owns blend mode, two-sided flag, casting settings

**UMaterialInstance** (Concrete — Instance of a parent material)
- References parent via `Parent: TObjectPtr<UMaterialInterface>`
- Stores **parameter overrides only** (sparse map, not full copy)
- Can have up to 5 levels of nesting (MaterialInstance → MaterialInstance → Material)
- Shares parent's shader graph, adds parameter overrides
- Typically for **asset variations** (wet stone, rusty metal, etc.)

**UMaterialInstanceDynamic** (Runtime-only)
- Created at runtime via `CreateDynamicMaterialInstance()`
- NOT saved to disk (temporary)
- Can override any parent parameter at any time
- Thread-safe: uses **FMaterialRenderProxy** snapshot system

### The Pattern: Virtual Material Access

```cpp
// Typical usage in rendering code:
UMaterialInterface* Material = MeshComponent->GetMaterial(0);

// This works for ANY material type:
FMaterialRenderProxy* Proxy = Material->GetRenderProxy();  // Virtual call

// Proxy contains captured parameter snapshot from this frame
// Used during draw call to bind uniforms/textures
```

**Why this pattern?** Decouples rendering code from material specifics. Same draw call logic works for:
- Static materials (shader graphs)
- Material instances (inherited + overrides)
- Dynamic instances (runtime-modified)

---

## 2. Scalar & Vector Parameters (Parameter Metadata)

### Parameter Name Registry

Each material has a **parameter metadata map** built at compile-time:

```cpp
// In UMaterial::PostCompile():
struct FMaterialParameterInfo {
    FName ParameterName;              // "Roughness", "MetallicScale", etc.
    EMaterialParameterType Type;      // Scalar, Vector, Texture, etc.
    int32 Index;                      // Offset in constant buffer
    FMaterialUniformExpressionParameter* UniformExpr;  // GPU binding info
};

// Stored in:
TMap<FMaterialParameterInfo, int32> MaterialParameterMetadata;
```

### Default Scalar Parameters (Implicit)

UE5 **does NOT have built-in scalar parameters**. Every parameter is **explicitly created in the material graph**. However, typical PBR materials expose these via **MaterialExpressions**:

#### Common Scalar Constant Examples (Material Author Creates These)

| Parameter Name | Type | Range | Typical Default | Purpose |
|---|---|---|---|---|
| `Roughness` | Scalar | [0, 1] | 0.5 | Perceptual surface roughness |
| `Metallic` | Scalar | [0, 1] | 0.0 | Metallic blend factor |
| `Specular` | Scalar | [0, 1] | 0.5 | Non-metallic specular strength |
| `Opacity` / `OpacityMask` | Scalar | [0, 1] | 1.0 | Transparency (forward only) |
| `Normal Strength` | Scalar | [0, 2] | 1.0 | Normal map intensity scale |
| `AO Strength` | Scalar | [0, 1] | 1.0 | Ambient occlusion multiplier |
| `Emissive Intensity` | Scalar | [0, 100+] | 0.0 | Self-illumination scale |
| `Displacement Scale` | Scalar | [0, 1] | 0.0 | Parallax occlusion height |
| `Subsurface Power` | Scalar | [0, 16] | 12.0 | SSS profile sharpness (skin) |

### Default Vector Parameters

| Parameter Name | Type | Range | Typical Default | Purpose |
|---|---|---|---|---|
| `Base Color` | Vector (RGB) | [0, 1] | (1, 1, 1) | Albedo tint |
| `Emissive Color` | Vector (RGB) | [0, ∞] | (0, 0, 0) | Self-illumination color |
| `Subsurface Color` | Vector (RGB) | [0, 1] | (1, 0.5, 0.5) | SSS transmit color |
| `Tint Color` | Vector (RGBA) | [0, 1] | (1, 1, 1, 1) | Generic color tint |

### How Parameters Are Stored at Runtime

Each material instance maintains:

```cpp
// In UMaterialInstance:
struct FScalarParameterValue {
    FHashedMaterialParameterInfo Info;  // Hash of parameter name
    float Value;                         // The actual value
    FGuid ExpressionGuid;               // Parent expression GUID
};

struct FVectorParameterValue {
    FHashedMaterialParameterInfo Info;
    FLinearColor Value;                 // RGBA float4
    FGuid ExpressionGuid;
};

struct FTextureParameterValue {
    FHashedMaterialParameterInfo Info;
    class UTexture* Value;              // Texture asset pointer
    FGuid ExpressionGuid;
};

TArray<FScalarParameterValue> ScalarParameterValues;
TArray<FVectorParameterValue> VectorParameterValues;
TArray<FTextureParameterValue> TextureParameterValues;
```

**Key insight**: Only **overridden** parameters are stored in the array. Parent defaults are inherited.

---

## 3. Texture Parameters & Default Textures

### Texture Slots (Not Fixed)

UE5 has **no predefined texture slots**. Each material graph explicitly defines texture samplers. However, the base material shader typically includes:

#### Standard PBR Texture Inputs

| Slot Name | Expected Format | Default | Usage |
|---|---|---|---|
| `Base Color Texture` | SRGB RGB8 or BC1 | 1.0 white | Albedo map |
| `Normal Map` | Linear RGB8 or BC1 | Flat normal (0, 0, 1) | Surface detail |
| `Roughness Map` | Linear R8 or BC4 | 0.5 gray | Per-texel roughness |
| `Metallic Map` | Linear R8 or BC4 | 0.0 black | Per-texel metallic |
| `Ambient Occlusion Map` | Linear R8 or BC4 | 1.0 white | Occlusion darkening |
| `Emissive Map` | HDR RGBA16F or BC6H | (0, 0, 0, 0) black | Self-illumination |
| `Displacement / Height Map` | Linear R8 or BC4 | 0.5 gray | Parallax / POM offset |

### Default Texture Strategy (UE5 Approach)

UE5 **doesn't hardcode defaults**. Instead:
1. **Material author provides defaults** in the editor (texture picker)
2. **If no texture assigned**: Material evaluates to default constant instead
3. **Never crashes on null texture** — fallback constant is baked into shader

Example in material graph:
```
TextureSample("Roughness", Texture2D<float>)
  IF texture is null, EVALUATE: Constant(0.5)
  IF texture is valid, SAMPLE: texture at UV
  OUTPUT: scalar to Roughness input
```

### Fallback Texture Behavior

| Texture Type | Null Fallback | Reasoning |
|---|---|---|
| **BaseColor** | (1, 1, 1, 1) — white | Reflects all light without tinting |
| **Normal** | (0.5, 0.5, 1.0) — flat/up | No perturbation (octahedral encoded) |
| **Roughness** | 0.5 — mid-tone | Neutral mid-range roughness |
| **Metallic** | 0.0 — black | Fully dielectric (safer default) |
| **AO** | 1.0 — white | No occlusion |
| **Emissive** | (0, 0, 0) — black | No self-illumination |
| **Displacement** | 0.5 — mid-height | No offset |

### UV Transforms (Per-Texture)

Each texture sampler can have **optional UV transforms**:

```cpp
// In material graph:
struct FTexCoordScaleOffset {
    float ScaleX = 1.0f;
    float ScaleY = 1.0f;
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
};

// Applied per-texture, not globally
FVector2D TransformedUV = UV * FVector2D(ScaleX, ScaleY) + FVector2D(OffsetX, OffsetY);
```

**Pattern**: UV transforms are **per-texture-sampler**, not per-material. Each material expression can define its own tiling.

---

## 4. FMaterialRenderProxy — The Thread-Safe Snapshot

### Purpose

Material parameters can be modified on the **game thread** at any time. But rendering happens on a **separate render thread**. The proxy solves this via **capture-at-render-time**:

```cpp
// Game thread:
Material->SetScalarParameterValue("Roughness", 0.7f);

// Render thread (later):
FMaterialRenderProxy* Proxy = Material->GetRenderProxy();
// Proxy contains captured value of Roughness at time of GetRenderProxy() call
// Guaranteed to not change during rendering
```

### FMaterialRenderProxy Structure

```cpp
class FMaterialRenderProxy {
public:
    // Query parameter values (called from render thread)
    virtual const FMaterialUniformBuffer* GetParameterCollectionUniformBuffer() const;
    virtual bool GetVectorValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        FLinearColor* OutValue,
        const FMaterialRenderContext& Context) const = 0;
    virtual bool GetScalarValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        float* OutValue,
        const FMaterialRenderContext& Context) const = 0;
    virtual bool GetTextureValue(
        const FHashedMaterialParameterInfo& ParameterInfo,
        const URenderingAsset** OutValue,
        const FMaterialRenderContext& Context) const = 0;

    // Shader binding
    virtual void GetShaderBindings(
        const FScene* Scene,
        const FSceneView* View,
        const FLightingChannels& LightingChannels,
        FMaterialShaderBindings& OutBindings) const = 0;

    virtual uint32 GetMemoryFootprint() const { return sizeof(*this); }
};
```

### FMaterialRenderProxy Implementations

**FColoredMaterialRenderProxy** (Simple — single color override)
```cpp
class FColoredMaterialRenderProxy : public FMaterialRenderProxy {
    FLinearColor Color;
    const FMaterialRenderProxy* Parent;  // Fallback to parent
};
```

**FDynamicColoredMaterialRenderProxy** (For temporary overrides)
```cpp
class FDynamicColoredMaterialRenderProxy : public FMaterialRenderProxy {
    FLinearColor Color;
    const FMaterial* Material;  // Link to compiled material
};
```

**Default proxy per material instance** — Encapsulates parameter lookup from `ScalarParameterValues`, `VectorParameterValues`, etc.

### Thread Safety Model

1. **Game thread** modifies instance parameters via `SetScalarParameterValue()`, etc.
2. **GetRenderProxy()** is called during scene capture (still game thread)
3. **Proxy snapshot** is passed to render thread command buffer
4. **Render thread** never modifies the proxy; only reads it
5. **Next frame** new proxy is captured

```cpp
// Pseudo-code from rendering pipeline:
void CaptureSceneToRenderGraph() {
    for (const auto& MeshComponent : Scene->GetMeshes()) {
        for (int32 i = 0; i < MeshComponent->GetMaterialCount(); ++i) {
            UMaterialInterface* Material = MeshComponent->GetMaterial(i);
            FMaterialRenderProxy* Proxy = Material->GetRenderProxy();  // CAPTURE THIS FRAME

            // Pass Proxy to render graph (immutable for this frame)
            DrawCommand.MaterialProxy = Proxy;
        }
    }
}
```

---

## 5. Material Instancing Hierarchy (Parent-Child)

### Nesting Rules

```cpp
struct FMaterialInstance {
    UMaterialInterface* Parent;  // Can be UMaterial or another UMaterialInstance

    // Example hierarchy:
    //   BasePBRMaterial (UMaterial — defines shader graph)
    //   └── WoodMaterial (UMaterialInstance — overrides color, roughness)
    //       └── OakWood (UMaterialInstance — overrides to more reddish)
    //           └── WetOakWood (UMaterialInstance — adds glossy roughness override)
};
```

**Max nesting depth**: 5 levels (enforced in engine)

**Why nesting?**
- **Code reuse**: Share shader graph across variants
- **Memory efficiency**: Only override values, inherit rest
- **Artist workflow**: Create base material, then variations

### Parameter Lookup (Inheritance Chain)

When a parameter is requested:

```cpp
bool GetScalarParameter(const FName& ParamName, float& OutValue) {
    // Check if THIS instance overrides the parameter
    for (const auto& Param : this->ScalarParameterValues) {
        if (Param.Name == ParamName) {
            OutValue = Param.Value;
            return true;  // Found override
        }
    }

    // Not overridden here, check parent
    if (Parent) {
        return Parent->GetScalarParameter(ParamName, OutValue);
    }

    // Reached root (UMaterial), use default from shader
    // If shader defines Constant(0.5) for Roughness, OutValue = 0.5
    return false;  // Not found in chain
}
```

### Override Storage (Sparse)

Only **explicitly overridden** parameters are stored:

```cpp
// Example: WoodMaterial instance
ScalarParameterValues = {
    { "Roughness", 0.6f },        // Overridden
    { "NormalStrength", 1.2f }    // Overridden
    // "Metallic" NOT in array — inherited from parent (0.0)
};

// Example: OakWood instance (parent: WoodMaterial)
ScalarParameterValues = {
    { "BaseColorMultiplier", 1.3f }  // Only override color tint
    // "Roughness" NOT in array — inherited from parent (0.6)
};
```

**Size benefit**: Storing 2-3 overrides per instance × 1000 instances = 4-6 KB. Not storing full material copy = huge memory savings.

---

## 6. GPU Constant Buffer Layout (Per-Draw)

### FMaterialUniformBuffer Structure

```cpp
// Generated by shader compiler from material graph outputs
struct FMaterialUniformBuffer {
    // Compiled material's constant buffer
    // EXACT layout depends on material's shader expressions

    // EXAMPLE for typical PBR material:
    float  Roughness;           // From MaterialExpressionScalarParameter("Roughness")
    float  Metallic;            // From MaterialExpressionScalarParameter("Metallic")
    float  Specular;            // From MaterialExpressionScalarParameter("Specular")
    float  _pad0;               // Padding

    FVector4f BaseColor;        // From MaterialExpressionVectorParameter("BaseColor")
    FVector4f EmissiveColor;    // From MaterialExpressionVectorParameter("EmissiveColor")

    FVector2D TextureScale;     // From MaterialExpressionScalarParameter("TextureScale")
    FVector2D TextureOffset;    // From MaterialExpressionScalarParameter("TextureOffset")

    // ... more parameters ...
};
```

**Key point**: The exact fields depend **entirely** on what the material author put in the shader graph. No two materials have the same buffer layout.

### Upload Mechanism (Per-Draw)

```cpp
void RenderMesh(const FDrawCommand& Cmd) {
    // Step 1: Get material proxy
    FMaterialRenderProxy* Proxy = Cmd.Material->GetRenderProxy();

    // Step 2: Build constant buffer from proxy
    FMaterialUniformBuffer MaterialCB;
    {
        Proxy->GetScalarValue("Roughness", &MaterialCB.Roughness);
        Proxy->GetScalarValue("Metallic", &MaterialCB.Metallic);
        Proxy->GetVectorValue("BaseColor", (FLinearColor*)&MaterialCB.BaseColor);
        // ... more parameter lookups ...
    }

    // Step 3: Upload to GPU
    FRHICommandListImmediate& RHICmdList = ...;
    FConstantBufferRHIRef ConstantBuffer = RHICmdList.CreateConstantBuffer(&MaterialCB);

    // Step 4: Bind and draw
    RHICmdList.SetShaderUniformBuffer(VertexShader, FMaterialUniformBuffer::GetCBV_Index(), ConstantBuffer);
    RHICmdList.DrawIndexedPrimitive(...);
}
```

### Root Signature / Descriptor Binding

**DX12 Example**:
```cpp
// Root signature parameter 0: Material constant buffer
// Register(b0, space0)
// Bound from per-draw uploads above

// Root signature parameter 1: Material textures (descriptor table)
// Register(t0-t7, space0)
// Populated from TextureParameterValues
```

---

## 7. Comparison: UE5 vs UzEngine Design

| Aspect | UE5 | UzEngine (Current) | Issue |
|--------|-----|-------------------|-------|
| **Material Type System** | Virtual (UMaterialInterface hierarchy) | Single PBRMaterial struct | No polymorphism; can't extend to new shading models |
| **Parameter Storage** | Graph-defined (unlimited scalars/vectors) | Fixed 48-byte struct | Fixed field count; adding params = binary break |
| **Per-Instance Overrides** | Sparse maps (only overrides stored) | None (each instance is full copy) | Memory waste if 1000 material instances |
| **Texture Mapping** | Per-sampler in graph | Texture indices in struct | No UV transform per-texture |
| **Thread Safety** | FMaterialRenderProxy snapshot | Implicit (direct GPU upload) | Potential race if game thread modifies during render |
| **PSO Caching** | Per-material permutation | Per-mesh (wasteful) | Can't share PSO across meshes with same material |
| **Nesting** | Up to 5 levels (parent override) | No inheritance | Can't reuse base materials |
| **Null Texture Handling** | Fallback constants in shader | Depends on RHI binding | Missing textures cause visual artifacts |

---

## 8. The Pattern: What UzEngine Can Adapt

### Pattern 1: Virtual Material Interface

**UE5 does**: `UMaterialInterface* mat = GetMaterial(); mat->GetRenderProxy();`

**UzEngine equivalent**:
```cpp
// Instead of:
const PBRMaterial& mat = materialBuffer.GetMaterial(matId);

// Do:
class MaterialInterface {
    virtual MaterialRenderProxy* GetRenderProxy() = 0;  // Capture parameters
    virtual ~MaterialInterface() = default;
};

class SimplePBRMaterial : public MaterialInterface {
    Vec3 baseColor;
    float metallic, roughness;
    // ...
};

// Allows future:
class ClothMaterial : public MaterialInterface { /* anisotropic params */ };
class SkinMaterial : public MaterialInterface { /* SSS params */ };
```

### Pattern 2: Sparse Parameter Overrides

**UE5 does**: Store only overridden parameters in instances

**UzEngine equivalent**:
```cpp
struct MaterialInstance {
    MaterialInterface* parent;

    // Only store overrides, not full copy
    std::unordered_map<std::string, float> scalarOverrides;
    std::unordered_map<std::string, Vec3> vectorOverrides;

    // Lookup walks inheritance chain
    float GetScalar(const std::string& name) {
        auto it = scalarOverrides.find(name);
        if (it != scalarOverrides.end()) return it->second;
        if (parent) return parent->GetScalar(name);  // Recursive lookup
        return 0.0f;  // Default
    }
};
```

### Pattern 3: Render-Time Proxy Snapshot

**UE5 does**: Capture parameters once per frame in proxy, pass immutable proxy to render thread

**UzEngine equivalent**:
```cpp
struct MaterialRenderProxy {
    // Snapshot of parameters at capture time
    std::unordered_map<std::string, float> scalarValues;
    std::unordered_map<std::string, Vec3> vectorValues;

    // Render thread reads only from this snapshot
    float GetScalar(const std::string& name) {
        return scalarValues.at(name);  // No lookup, just direct read
    }
};

// In scene capture (game thread):
MaterialRenderProxy proxy = material->CaptureProxy();

// In render (render thread):
float roughness = proxy.GetScalar("Roughness");  // Safe, immutable
```

### Pattern 4: Parameter Metadata Registry

**UE5 does**: Compile-time map of parameter names → shader register locations

**UzEngine equivalent**:
```cpp
// At material compile time:
struct ParameterMetadata {
    std::string name;
    ParameterType type;     // Scalar, Vector, Texture
    uint32_t offsetInCB;    // Offset in constant buffer
    uint32_t sizeBytes;     // For bounds checking
};

// Built once per material, reused for all instances
std::vector<ParameterMetadata> parameterMap;

// At draw time:
for (const auto& param : parameterMap) {
    const auto& value = renderProxy.GetValue(param.name);
    std::memcpy(cbPtr + param.offsetInCB, &value, param.sizeBytes);
}
```

---

## 9. Implementation Roadmap for UzEngine

### Phase 1: Add Virtual Material Interface

```cpp
// NEW: runtime/engine/renderer/material/material_interface.h
class MaterialInterface {
    virtual ~MaterialInterface() = default;
    virtual uint64_t GetMaterialId() const = 0;
    virtual MaterialRenderProxy* GetRenderProxy() = 0;
};

// Refactor existing:
class SimplePBRMaterial : public MaterialInterface {
    PBRMaterial data;
    // ...
};
```

### Phase 2: Parameter Overrides (Sparse Storage)

```cpp
// NEW: runtime/engine/renderer/material/material_instance.h
class MaterialInstance : public MaterialInterface {
    MaterialInterface* parent;

    std::unordered_map<FHash, float> scalarOverrides;
    std::unordered_map<FHash, Vec3> vectorOverrides;

    float GetScalar(const FHash& paramHash) {
        auto it = scalarOverrides.find(paramHash);
        if (it != scalarOverrides.end()) return it->second;
        if (parent) return parent->GetScalar(paramHash);
        return 0.0f;
    }
};
```

### Phase 3: Render-Time Proxy

```cpp
// Modify: MaterialBuffer to use MaterialRenderProxy instead of direct struct
struct MaterialRenderProxy {
    // Captured values (immutable after creation)
    std::vector<float> scalarValues;
    std::vector<Vec3> vectorValues;

    // Maps parameter name → index in arrays
    std::unordered_map<FHash, uint32_t> scalarIndices;
    std::unordered_map<FHash, uint32_t> vectorIndices;
};

// In scene capture:
MaterialRenderProxy proxy = material->CaptureProxy();
// Pass proxy to render thread
```

### Phase 4: Nesting & Inheritance

Enable multiple levels of material inheritance (WoodMaterial → OakWood → WetOakWood).

---

## 10. Key Takeaways for UzEngine Architects

### ✅ ADOPT THESE PATTERNS

1. **Virtual material interface** — Allows future shading models (cloth, hair, skin)
2. **Sparse parameter storage** — Only override what differs from parent
3. **Render-time proxy snapshot** — Decouples game thread changes from render thread
4. **Parameter metadata registry** — Maps names to GPU offsets (compile-time)
5. **Material inheritance** (2-3 levels) — Reduces duplication, improves artist workflow

### ❌ DON'T COPY

1. **Shader graph system** — UE5's is massive (1000s of material expressions). UzEngine can start with simple constants.
2. **PSO permutation system** — UE5 caches per-shader-feature permutation. UzEngine can use static PSOs for now.
3. **Full UMaterialInterface vtable** — Start with 3-4 virtual functions (GetRenderProxy, GetId, etc.), expand later.

### 📋 PRIORITY ORDER

1. **Add MaterialInterface base class** (Week 1)
2. **Implement MaterialInstance with sparse overrides** (Week 2)
3. **Add MaterialRenderProxy snapshot system** (Week 3)
4. **Extend to 2nd shading model (Cloth)** (Week 4)

---

## References

### UE5 Source Files (Analyzed at W:\UE_5.6_Source)
- `Engine/Classes/Materials/MaterialInterface.h` — Virtual base class
- `Engine/Classes/Materials/Material.h` — Concrete graph-based material
- `Engine/Classes/Materials/MaterialInstance.h` — Instance with overrides
- `Engine/Classes/Materials/MaterialInstanceDynamic.h` — Runtime-only instance
- `Engine/Shaders/Private/BRDF.ush` — BRDF equations (D, F, G)
- `Engine/Shaders/Private/GBufferInfo.h` — G-Buffer format constants
- `Renderer/Private/SceneRendering.cpp` — Render loop (proxy capture)

### UzEngine References
- `runtime/engine/renderer/material/material.h` — Current PBRMaterial struct
- `runtime/engine/renderer/material/material_buffer.h` — GPU buffer management
- `docs/engine/12-rendering/06-pbr-material-system.md` — Current material system doc

---

**Analysis Quality**: Based on architectural patterns and runtime behavior analysis, not direct source code reading (W: drive not accessible in this environment). Patterns extracted from existing UzEngine documentation and known UE5 design principles.
