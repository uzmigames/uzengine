<!-- doc-meta
title: UE5 Material System — Architectural Patterns for UzEngine
description: Design patterns extracted from UE5 material system implementation
date: 2026-03-18
status: published
-->

# UE5 Material System — Key Architectural Patterns for UzEngine

## Pattern 1: Expression Node = Data Container

### UE5 Implementation

```cpp
// MaterialExpressionAdd.h
UCLASS(...)
class UMaterialExpressionAdd : public UMaterialExpression
{
    UPROPERTY()
    FExpressionInput A;  // Connection to upstream expression

    UPROPERTY()
    FExpressionInput B;  // Connection to upstream expression

    // NO implementation here — just data
};
```

**Key Insight:** Expression nodes are **purely data-driven**. They hold:
- Input connections (`FExpressionInput`)
- Output definitions (`FExpressionOutput`)
- Material parameters (e.g., texture reference, scalar value)
- No compilation logic

### Compilation Happens in Translator

```cpp
// HLSLMaterialTranslator.cpp
int32 FHLSLMaterialTranslator::MaterialExpressionAdd(UMaterialExpressionAdd* Node)
{
    // Recursively compile inputs
    int32 CodeA = CompileExpression(Node->A);
    int32 CodeB = CompileExpression(Node->B);

    // Generate HLSL
    return Add(CodeA, CodeB);  // Return code index
}
```

**Key Insight:** **All compilation logic lives in the translator**, not in expression nodes.

### UzEngine Adaptation

```cpp
// uz_engine/renderer/materials/material_expression.h
struct MaterialExpressionAdd {
    MaterialExpressionInput a;
    MaterialExpressionInput b;
    // No Compile() method
};

// uz_engine/renderer/materials/hlsl_material_translator.cpp
int32 HLSLMaterialTranslator::CompileAdd(const MaterialExpressionAdd* node) {
    int32 a_code = CompileExpression(node->a);
    int32 b_code = CompileExpression(node->b);
    return Add(a_code, b_code);
}
```

---

## Pattern 2: Recursive Compilation with Memoization

### UE5 Implementation

```cpp
// HLSLMaterialTranslator.h
class FHLSLMaterialTranslator : public FMaterialCompiler
{
private:
    // Cache compiled expressions to handle DAG (directed acyclic graph) properly
    TMap<FMaterialExpressionKey, int32> CachedExpressions;

public:
    virtual int32 CallExpression(FMaterialExpressionKey ExpressionKey,
                                  FMaterialCompiler* InCompiler) override
    {
        // Check cache first
        if (int32* CachedCode = CachedExpressions.Find(ExpressionKey))
        {
            return *CachedCode;  // Return cached result
        }

        // Compile expression recursively
        int32 CodeIndex = Expression->Compile(this);

        // Cache for next use
        CachedExpressions.Add(ExpressionKey, CodeIndex);

        return CodeIndex;
    }
};
```

**Key Insight:** Material graphs are **DAGs not trees**. If expression A is used by both B and C, compile it once and reuse the result.

**File:** `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h`

### UzEngine Adaptation

```cpp
struct MaterialTranslatorState {
    std::unordered_map<uint64_t, int32_t> cached_expressions;  // hash → code index
};

int32_t HLSLMaterialTranslator::CompileExpression(const MaterialExpressionInput& input) {
    uint64_t key = HashExpression(input.expression, input.output_index);

    // Check cache
    if (cached_expressions.find(key) != cached_expressions.end()) {
        return cached_expressions[key];
    }

    // Compile
    int32_t code_index = CompileExpressionRecursive(input.expression);

    // Cache
    cached_expressions[key] = code_index;

    return code_index;
}
```

---

## Pattern 3: Compiler Interface (Abstract)

### UE5 Implementation

```cpp
// MaterialCompiler.h
class FMaterialCompiler
{
public:
    virtual ~FMaterialCompiler() { }

    // Pure virtual — all implementations must override
    virtual int32 CallExpression(FMaterialExpressionKey Key,
                                  FMaterialCompiler* InCompiler) = 0;
    virtual EMaterialValueType GetType(int32 Code) = 0;
    virtual EShaderFrequency GetCurrentShaderFrequency() const = 0;
    virtual ERHIFeatureLevel::Type GetFeatureLevel() = 0;
    virtual EShaderPlatform GetShaderPlatform() = 0;

    // ... 100+ more pure virtual methods
};
```

**Key Insight:** Compiler is **an interface, not an implementation**. Multiple concrete implementations exist:
- `FHLSLMaterialTranslator` — HLSL code generation
- `FLightmassProxy` — lightmap data export
- `FMaterialProxy` — material preview proxy

**File:** `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/MaterialCompiler.h` (line 119)

### UzEngine Adaptation

```cpp
// uz_engine/renderer/materials/material_compiler.h
class IMaterialCompiler {
public:
    virtual ~IMaterialCompiler() = default;

    virtual int32_t CompileExpression(const MaterialExpressionInput& input) = 0;
    virtual MaterialValueType GetType(int32_t code_index) = 0;
    virtual ShaderFrequency GetCurrentFrequency() const = 0;
    virtual RHIFeatureLevel GetFeatureLevel() const = 0;
};

// uz_engine/renderer/materials/hlsl_material_translator.h
class HLSLMaterialTranslator : public IMaterialCompiler {
public:
    int32_t CompileExpression(const MaterialExpressionInput& input) override;
    MaterialValueType GetType(int32_t code_index) override;
    // ... implementations
};
```

---

## Pattern 4: Material Graph as Serialized Array

### UE5 Implementation

```cpp
// Material.h
UCLASS(...)
class UMaterial : public UMaterialInterface
{
    UPROPERTY()
    FMaterialExpressionCollection ExpressionCollection;  // Serialized array
};

// MaterialExpression.h
USTRUCT()
struct FMaterialExpressionCollection
{
    UPROPERTY()
    TArray<TObjectPtr<UMaterialExpression>> Expressions;  // Linear array

    UPROPERTY()
    TArray<TObjectPtr<UMaterialExpressionComment>> EditorComments;
};
```

**Key Insight:** The material graph is **not a tree structure**. It's a **flat array of expressions** where connections are encoded as pointers:

```cpp
// Graph connectivity = pointers
struct FExpressionInput {
    TObjectPtr<UMaterialExpression> Expression;  // Pointer to upstream node
    int32 OutputIndex;                           // Which output to use
};
```

**Benefit:** Serialization is trivial. Just serialize the array. Connections are maintained via object pointers.

**File:** `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h` (line 120)

### UzEngine Adaptation

```cpp
// uz_engine/renderer/materials/material.h
struct Material {
    std::vector<std::unique_ptr<MaterialExpression>> expressions;
    uint32_t root_expression_indices[16];  // Albedo, Normal, Roughness, etc.

    // Serialize as:
    // 1. Count of expressions
    // 2. For each expression: type + data
    // 3. Root expression indices
};

struct MaterialExpressionInput {
    uint32_t expression_index;  // Index into expressions[] array
    uint8_t output_index;       // Which output
};
```

---

## Pattern 5: Connection Model = Pointer + OutputIndex

### UE5 Implementation

```cpp
// MaterialExpression.h — line 42
USTRUCT(noexport)
struct FExpressionInput
{
    /** Pointer to upstream expression, or NULL if not connected */
    UPROPERTY()
    TObjectPtr<class UMaterialExpression> Expression;

    /** Index into Expression's outputs array that this input is connected to */
    UPROPERTY()
    int32 OutputIndex;
};
```

**Key Insight:**
- Each input pin stores **one pointer** to the connected expression
- Plus **one output index** (because expressions can have multiple outputs)
- Each expression can have **0-N outputs** defined

```cpp
// MaterialExpression.h — line 88
USTRUCT(noexport)
struct FExpressionOutput
{
    UPROPERTY()
    FName OutputName;      // e.g., "RGB", "A", "R"

    UPROPERTY()
    int32 Mask;           // Which components are valid
};
```

**Example:** TextureSample has outputs: RGB, R, G, B, A (5 outputs)
```cpp
Texture Sample
├─ Output 0: RGB (RGB channels)
├─ Output 1: R
├─ Output 2: G
├─ Output 3: B
└─ Output 4: A
```

Connection: `FExpressionInput{ TextureSampleNode, 0 }` = connect to RGB output.

**File:** `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h` (lines 42-107)

### UzEngine Adaptation

```cpp
struct MaterialExpressionOutput {
    char name[32];      // "RGB", "R", etc.
    uint32_t mask;      // Component mask
};

struct MaterialExpressionInput {
    uint32_t expression_index;  // Index into Material::expressions[]
    uint8_t output_index;        // Which output of that expression
};

struct MaterialExpression {
    const char* type;  // "Add", "TextureSample", etc.

    // Variable-length: depends on type
    // Add:          FExpressionInput inputs[2]
    // TextureSample: FExpressionInput uv; uint32_t texture_id;
    // Constant:     float value;
};
```

---

## Pattern 6: Shader Output Routing

### UE5 Implementation

```cpp
// Material.h
UCLASS(...)
class UMaterial : public UMaterialInterface
{
    // Each shader output (Albedo, Normal, Roughness, etc.) is rooted here
    UPROPERTY()
    FMaterialInput DiffuseColorInput;        // Connected to Albedo root expression

    UPROPERTY()
    FMaterialInput NormalInput;               // Connected to Normal root expression

    UPROPERTY()
    FMaterialInput RoughnessInput;            // Connected to Roughness root expression

    // ... 16+ more material properties
};

// Compilation:
void Compile() {
    SetMaterialProperty(MP_Diffuse);    // Compile Albedo
    CompileInput(DiffuseColorInput);

    SetMaterialProperty(MP_Normal);     // Compile Normal
    CompileInput(NormalInput);

    SetMaterialProperty(MP_Roughness);  // Compile Roughness
    CompileInput(RoughnessInput);

    // ... compile all properties
}
```

**Key Insight:** Material has **multiple root expressions** (one per output). Compilation is **property-by-property**, not one monolithic pass.

**File:** `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/Material.h`

### UzEngine Adaptation

```cpp
enum MaterialProperty {
    ALBEDO = 0,
    NORMAL,
    ROUGHNESS,
    METALLIC,
    AO,
    EMISSIVE,
    // ... more properties
};

struct Material {
    std::vector<std::unique_ptr<MaterialExpression>> expressions;

    // Root expressions, one per output
    uint32_t property_roots[16];  // Index into expressions[] for each property

    // Compile by property:
    for (int i = 0; i < NUM_PROPERTIES; ++i) {
        SetCurrentProperty(i);
        int32_t code = CompileExpression(property_roots[i]);
        EmitPropertyOutput(i, code);  // Generate shader code
    }
};
```

---

## Pattern 7: Separation of Concerns — Graph vs Compilation

### UE5 Module Structure

```
Runtime (shipped with game):
    /Public/Materials/
        Material.h                  ← Serialized data (graph)
        MaterialExpression.h        ← Graph nodes (data only)
        MaterialCompiler.h          ← Abstract interface
    /Private/Materials/
        HLSLMaterialTranslator.cpp  ← Concrete implementation (optional)

Editor (NOT shipped):
    /Editor/UnrealEd/Classes/MaterialGraph/
        MaterialGraph.h             ← Visual representation
    /Editor/MaterialEditor/
        ...                         ← UI panels, preview, etc.
```

**Key Insight:**
- **Runtime layer** includes the data model (Material, expressions, compiler interface)
- **Compiler implementation** is in Private — can be replaced/updated without changing runtime
- **Editor layer** is completely optional — can be stripped from shipping builds

### UzEngine Adaptation

```
runtime/engine/renderer/materials/
    material.h               ← Core data model (ships)
    material_expression.h    ← Node types (ships)
    material_compiler.h      ← Abstract interface (ships)

runtime/engine/renderer/materials/compiler/
    hlsl_material_translator.cpp  ← HLSL implementation (editor + cooking)

runtime/editor/panels/materials/
    material_graph_view.h    ← Visual graph (editor only)
    material_editor.h        ← Editor UI (editor only)
```

---

## Pattern 8: Validation Before Compilation

### UE5 Implementation

```cpp
// HLSLMaterialTranslator.h
class FHLSLMaterialTranslator : public FMaterialCompiler
{
private:
    bool bSuccess = true;
    TArray<FString> CompileErrors;

public:
    virtual int32 Error(const TCHAR* Text) override
    {
        bSuccess = false;
        CompileErrors.Add(Text);
        return INDEX_NONE;  // Return error code
    }

    virtual void AppendExpressionError(UMaterialExpression* Expression,
                                        const TCHAR* Text) override
    {
        CompileErrors.Add(FString::Printf(TEXT("%s: %s"),
                          Expression->GetName(), Text));
    }
};
```

**Key Insight:** Compiler tracks errors during compilation. If any expression fails to compile, the entire material compile fails gracefully.

**File:** `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h`

### UzEngine Adaptation

```cpp
enum CompileResult {
    SUCCESS = 0,
    ERROR_TYPE_MISMATCH = 1,
    ERROR_UNCONNECTED_INPUT = 2,
    ERROR_INVALID_TEXTURE = 3,
    // ... more error codes
};

struct MaterialCompileError {
    uint32_t expression_index;
    const char* message;
    CompileResult code;
};

class MaterialCompiler {
protected:
    std::vector<MaterialCompileError> errors;

public:
    bool CompileMaterial(Material* mat) {
        errors.clear();

        for (auto& expr : mat->expressions) {
            if (!CompileExpression(expr.get())) {
                return false;  // Stop on first error
            }
        }
        return errors.empty();
    }

    void ReportError(uint32_t expr_idx, const char* msg) {
        errors.push_back({expr_idx, msg, ERROR_UNCONNECTED_INPUT});
    }
};
```

---

## Pattern 9: Intermediate Representation (IR)

### UE5 Implementation (NEW in 5.6+)

UE5 5.6 introduced a new **Material Intermediate Representation (MIR)** layer:

```cpp
// HLSLMaterialTranslator.cpp (old approach)
// Directly generate HLSL strings from expressions

// NEW approach (5.6+):
// 1. Expression tree → MIR
// 2. MIR → HLSL
```

**Files:**
- `/Private/Materials/MaterialIREmitter.cpp` (89,879 bytes)
- `/Private/Materials/MaterialIRToHLSLTranslator.cpp` (43,912 bytes)
- `/Private/Materials/MaterialIRValueAnalyzer.cpp` (11,384 bytes)

**Why?** Separation of concerns:
1. **IR generation** — understand material graph structure
2. **IR to HLSL** — generate shader code from IR
3. **IR analysis** — optimize, validate, analyze before code gen

**Benefits:**
- Can retarget to other languages (GLSL, Metal, Wgpu) by writing new "IR to X" translator
- Can optimize IR before code generation
- Can serialize IR for caching

### UzEngine Adaptation

```cpp
// uz_engine/renderer/materials/material_ir.h
enum class IROpcode {
    IMM_FLOAT,
    IMM_VECTOR,
    INPUT_PARAM,
    OP_ADD,
    OP_MULTIPLY,
    OP_SAMPLE,
    // ... more opcodes
};

struct IRNode {
    IROpcode op;
    std::vector<uint32_t> inputs;  // Indices to other IR nodes
    std::string result_name;       // e.g., "temp_42"
};

struct MaterialIR {
    std::vector<IRNode> nodes;
    uint32_t root_nodes[16];  // Roots for each material property
};

// Express material graph as IR
MaterialIR GenerateIR(const Material* mat);

// Convert IR to HLSL
std::string GenerateHLSL(const MaterialIR& ir);

// Benefits: Easy to retarget to GLSL/other shaders later
std::string GenerateGLSL(const MaterialIR& ir);
```

**File Reference:** `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Materials/MaterialIREmitter.cpp` (line 89,879)

---

## Pattern 10: Material Instances for Parameter Overrides

### UE5 Implementation

```cpp
// Material.h
UCLASS(...)
class UMaterial : public UMaterialInterface
{
    // Master material with expressions
};

// MaterialInstance.h
UCLASS(...)
class UMaterialInstance : public UMaterialInterface
{
    UPROPERTY()
    TObjectPtr<UMaterial> Parent;

    UPROPERTY()
    TArray<FScalarParameterValue> ScalarParameterValues;

    UPROPERTY()
    TArray<FVectorParameterValue> VectorParameterValues;

    UPROPERTY()
    TArray<FTextureParameterValue> TextureParameterValues;
};
```

**Key Insight:** Material instance wraps a master material and overrides only parameters, not the graph.

**Dynamic Variant:**
```cpp
// MaterialInstanceDynamic.h
UCLASS()
class UMaterialInstanceDynamic : public UMaterialInstance
{
    // Runtime-mutable parameters (used during gameplay)
};
```

**Use Case:**
- Master material defines graph: `Albedo = Texture Sample * Color`
- Instance 1: `Color = Red`
- Instance 2: `Color = Blue`
- Both share the same compiled graph, different parameter values

**File:** `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/MaterialInstance.h`

### UzEngine Adaptation

```cpp
struct MaterialParameterValue {
    uint32_t parameter_index;
    union {
        float scalar_value;
        float vector_value[4];
        uint32_t texture_handle;
    };
};

struct MaterialInstance {
    Handle<Material> parent_material;
    std::vector<MaterialParameterValue> parameter_overrides;
};

// Usage:
Material master = LoadMaterial("rocks.umat");
MaterialInstance instance1;
instance1.parent_material = &master;
instance1.SetScalarParameter("Color", 1.0f, 0.0f, 0.0f);  // Red

MaterialInstance instance2;
instance2.parent_material = &master;
instance2.SetScalarParameter("Color", 0.0f, 0.0f, 1.0f);  // Blue

// Both share the same compiled shader, different parameter values
```

---

## Summary: UzEngine Material System Design

Based on UE5 patterns, UzEngine should implement:

1. **Expression nodes as data containers** — no compilation logic in nodes
2. **Recursive compiler with memoization** — handle DAG graphs efficiently
3. **Abstract compiler interface** — allow multiple implementations
4. **Flat array storage** — serialize as linear array with pointer connections
5. **Per-property compilation** — Albedo, Normal, Roughness, etc. compiled separately
6. **Optional IR layer** — decouple graph understanding from code generation
7. **Material instances** — separate master graph from parameter overrides
8. **Clear module boundary** — Runtime (data) + Optional Editor (UI)
9. **Error collection** — accumulate errors before failing
10. **Pluggable code generators** — HLSL today, GLSL/Metal/Wgpu tomorrow

This design scales from simple MVP (Add, Multiply, TextureSample) to AAA complexity (custom outputs, layer blending, Substrate BRDF) without architectural rewrites.

