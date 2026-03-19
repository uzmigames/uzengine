# Material System — Overview

> **Status**: Phase 1 implemented (graph compiler + CPU baker). Phase 2 planned (GPU shader integration).
> **Source**: `runtime/engine/material/` (graph system), `runtime/engine/renderer/material/` (renderer integration)
> **Depends on**: Shader Pipeline (§05), Render Graph (§32), Resource System (§08)
> **References**: UE5 `UMaterial` / `UMaterialExpression`, Godot `VisualShader`

## Purpose

Artists and programmers author materials without writing low-level shader code. The Material System provides a **node-based graph compiler** that generates HLSL shaders, manages compilation, and caches results.

A material is a **directed acyclic graph of expressions** that compute PBR parameters (BaseColor, Metallic, Roughness, etc.). The compiler:
- Walks the graph in topological order
- Generates HLSL code with dead-code elimination
- Caches compiled code by xxHash64 content hash
- Supports runtime parameter overrides via Material Instances (planned)

```
.uzmat (JSON)
       ▼
MaterialGraph::LoadFromFile()
       ▼
HlslMaterialTranslator::Compile()
       ▼
ComputeMaterialOutputs() — HLSL function
       ▼
MaterialBaker::BakeBaseColor() — CPU texture bake (current)
       ▼
Texture assigned to standard Material pipeline → renders
```

## Architecture

### File Layout

```
runtime/engine/material/
├── material_expression.h/.cpp          # Base expression + 37 node types
├── material_graph.h/.cpp               # DAG container + JSON serialization
├── material_shader_cache.h/.cpp        # Content-addressed compilation cache
├── material_asset_graph.h/.cpp         # .uzmat file loader + compile orchestrator
├── material_baker.h/.cpp               # CPU-side graph evaluator → texture bake
└── compiler/
    └── hlsl_material_translator.h/.cpp # Graph → HLSL code generator
```

### UE5 Pattern

The architecture follows UE5's separation of concerns:

| UE5 | UzEngine | Role |
|-----|----------|------|
| `UMaterialExpression` | `MaterialExpression` | Data container (pins + params, no logic) |
| `UMaterial` (expression array) | `MaterialGraph` (expression vector) | Owns nodes, manages connections |
| `FHLSLMaterialTranslator` | `HlslMaterialTranslator` | Visitor that generates HLSL |
| `FMaterialShaderMap` | `MaterialShaderCache` | Content-addressed compiled shader cache |
| `UMaterialInstanceDynamic` | `MaterialAssetGraph` | Asset wrapper + compilation orchestrator |

Expressions are **pure data containers** — they hold pins, parameters, and connections but have NO compilation logic. The compiler visits expressions to emit HLSL (visitor pattern).

## Material Domains

```cpp
enum class MaterialDomain : uint8_t {
    Surface,      ///< Standard lit geometry (deferred/forward)
    PostProcess,  ///< Fullscreen post-process effect
    Decal,        ///< Projected decal
    UI,           ///< 2D UI widget
};
```

## Shading Models

```cpp
enum class MaterialShadingModel : uint8_t {
    DefaultLit,   ///< Standard PBR (Cook-Torrance + Burley)
    Unlit,        ///< No lighting, emissive only
    Subsurface,   ///< SSS approximation
};
```

## Expression Types (37 implemented)

### Constants
| Type | Outputs | Parameters |
|------|---------|------------|
| `ScalarConstant` | Value(Float) | `value` |
| `Vector2Constant` | Value(Float2) | `x`, `y` |
| `Vector3Constant` | Value(Float3) | `x`, `y`, `z` |
| `Vector4Constant` | Value(Float4) | `x`, `y`, `z`, `w` |
| `ColorConstant` | Color(Float3) | `r`, `g`, `b` |

### Math (20 operations)
| Type | Inputs | Output |
|------|--------|--------|
| `Add`, `Subtract`, `Multiply`, `Divide` | A, B | Result |
| `Lerp` | A, B, Alpha | Result |
| `Clamp` | Value, Min, Max | Result |
| `Saturate`, `Abs`, `OneMinus`, `Frac`, `Floor`, `Ceil`, `Sqrt` | Value | Result |
| `Pow` | Base, Exp | Result |
| `Min`, `Max` | A, B | Result |
| `Dot` | A(Float3), B(Float3) | Result(Float) |
| `Cross` | A(Float3), B(Float3) | Result(Float3) |
| `Normalize`, `Length` | Value | Result |
| `Step` | Edge, X | Result |
| `SmoothStep` | Edge0, Edge1, X | Result |

### Texture
| Type | Inputs | Outputs | Parameters |
|------|--------|---------|------------|
| `TextureSample` | UV(Float2) | RGBA, R, G, B, A | `texturePath` |

### Coordinates
| Type | Outputs | Notes |
|------|---------|-------|
| `TexCoord` | UV(Float2) | `uvIndex` param |
| `WorldPosition` | Position(Float3) | |
| `ObjectPosition` | Position(Float3) | |
| `VertexNormal` | Normal(Float3) | |
| `VertexColor` | Color(Float4) | |
| `CameraVector` | Direction(Float3) | |
| `ScreenPosition` | UV(Float2) | |
| `Time` | Seconds(Float) | |

### Procedural
| Type | Inputs | Output |
|------|--------|--------|
| `CheckerPattern` | UV, Scale(default=10) | Mask(Float) |
| `NoisePerlin` | UV, Scale(default=1) | Value(Float) |
| `GradientLinear` | UV | Value(Float) |
| `GradientRadial` | UV | Value(Float) |

### Utility
| Type | Inputs | Outputs |
|------|--------|---------|
| `TriPlanarMapping` | WorldPos, WorldNormal, Sharpness | UV, BlendWeights |
| `Fresnel` | Exponent(default=5) | Value(Float) |
| `Panner` | UV, SpeedX, SpeedY | UV(Float2) |
| `ComponentMask` | Input(Float4) | Result (type varies) |
| `Append` | A, B | Result (A+B components) |

### Parameters (runtime-tweakable)
| Type | Outputs | Parameters |
|------|---------|------------|
| `ScalarParameter` | Value(Float) | `paramName`, `default` |
| `VectorParameter` | Value(Float3) | `paramName`, `defaultR/G/B` |
| `TextureParameter` | Texture | `paramName`, `texturePath` |

## PBR Output Node

Every material graph has exactly one PBR Output node (id=1) with these input pins:

| Pin | Type | Default | Purpose |
|-----|------|---------|---------|
| BaseColor | Float3 | (1,1,1) | Diffuse albedo (linear) |
| Metallic | Float | 0.0 | Metal vs dielectric |
| Roughness | Float | 0.5 | Surface smoothness |
| Normal | Float3 | (0,0,1) | Surface normal (tangent space) |
| Emissive | Float3 | (0,0,0) | Self-illumination (HDR) |
| Opacity | Float | 1.0 | Transparency |
| AO | Float | 1.0 | Ambient occlusion |

## .uzmat File Format

Materials are serialized as JSON:

```json
{
    "version": 1,
    "name": "MI_ProcGrid",
    "domain": "Surface",
    "shadingModel": "DefaultLit",
    "expressions": [
        {
            "id": 1,
            "type": "PBROutput",
            "pos": [400, 0],
            "inputs": [
                {"pin": 0, "srcExpr": 7, "srcPin": 0}
            ]
        },
        {
            "id": 2,
            "type": "TexCoord",
            "pos": [-400, 0]
        },
        {
            "id": 3,
            "type": "ScalarConstant",
            "pos": [-400, 100],
            "scalarParams": {"value": 16.0}
        },
        {
            "id": 4,
            "type": "CheckerPattern",
            "pos": [-200, 0],
            "inputs": [
                {"pin": 0, "srcExpr": 2, "srcPin": 0},
                {"pin": 1, "srcExpr": 3, "srcPin": 0}
            ]
        }
    ]
}
```

## HLSL Code Generation

The `HlslMaterialTranslator` generates a `ComputeMaterialOutputs()` function:

```hlsl
/// Auto-generated by UzEngine MaterialCompiler
/// Material: "MI_ProcGrid"
void ComputeMaterialOutputs(
    in float4 materialParams[16],
    in float2 uv,
    in float3 positionWS,
    in float3 normalWS,
    in float4 vertexColor,
    in float3 objectPosition,
    in float2 screenUV,
    in float3 cameraPosition,
    in float totalTime,
    out float3 out_baseColor,
    out float out_metallic,
    out float out_roughness,
    out float3 out_normal,
    out float3 out_emissive,
    out float out_opacity,
    out float out_ao)
{
    float2 _node2_out0 = uv;
    float _node3_out0 = 16.0;
    float _node4_out0 = fmod(floor(_node2_out0.x * _node3_out0) +
                             floor(_node2_out0.y * _node3_out0), 2.0);
    float3 _node5_out0 = float3(0.85, 0.85, 0.85);
    float3 _node6_out0 = float3(0.65, 0.65, 0.65);
    float3 _node7_out0 = lerp(_node5_out0, _node6_out0,
                              float3(_node4_out0, _node4_out0, _node4_out0));

    out_baseColor = _node7_out0;
    out_metallic = 0.0;
    out_roughness = 0.5;
    out_normal = normalWS;
    out_emissive = float3(0.0, 0.0, 0.0);
    out_opacity = 1.0;
    out_ao = 1.0;
}
```

Key features:
- **Memoized emission** — diamond dependencies emitted once
- **Dead-code elimination** — only nodes reachable from PBR output are emitted
- **Auto type promotion** — Float→Float3 splat, Float3→Float truncation
- **Parameter slots** — `materialParams[N]` for runtime overrides
- **Texture slots** — `ResourceDescriptorHeap[texIndex_N]` for bindless sampling

## Material Baker (CPU)

The `MaterialBaker` evaluates material graphs on CPU for each texel, producing RGBA8 texture data. Used for:
- Material preview thumbnails
- Fallback rendering (current — baked textures fed to standard pipeline)
- Texture export for static UV-only materials

```cpp
auto result = MaterialBaker::BakeBaseColor(graph, 1024, 1024);
// result.pixels = RGBA8 data, result.width/height = dimensions
stbi_write_png("baked.png", result.width, result.height, 4, result.pixels.data(), ...);
```

## Content Hashing

Each graph is hashed via xxHash64 over:
- All expression types (sorted by ID)
- All scalar/string/int parameters
- All pin connections

Same hash = same shader. The `MaterialShaderCache` uses this to skip recompilation.

## Material Pipeline (Phase 1.5+) — GPU Integration

The **MaterialPipeline** system compiles material graphs to DX12 PSOs and manages their lifecycle in the renderer. Instead of CPU baking, materials are evaluated on the GPU during the GBuffer pass.

### Architecture

```cpp
// Material graph + runtime parameters
MaterialAssetGraph* graph = ...;  // from .uzmat file

// Get or create PSO
auto psoHandle = g_materialPipeline.GetOrCreatePSO(
    graph->GetContentHash(),
    ShaderTarget::SM6_6,
    RenderPath::Deferred);

// PSO is cached by xxHash64(graphContent + shaderTarget + renderPath)
// If cache hit: return cached PSO
// If cache miss: Compile graph HLSL → DXC → DXIL → CreateGraphicsPipelineState()
```

### Render Graph Integration

During the GBuffer pass, draw commands with material graphs trigger PSO switching:

```cpp
// In GBuffer RenderGraph callback:
for (const auto& cmd : drawList.GetCommands()) {
    if (cmd.materialGraphPipeline.IsValid()) {
        // Switch to material graph PSO
        cmdList->SetPipelineState(g_materialPipeline.GetPSO(cmd.materialGraphPipeline));

        // Bind material parameters
        auto matParamsCB = cbAllocator->Allocate(sizeof(MaterialParamsCB));
        matParamsCB->params[0] = cmd.materialParams[0];  // ScalarParameter
        matParamsCB->params[1] = cmd.materialParams[1];  // VectorParameter, etc.
        cmdList->SetGraphicsRootConstantBufferView(3, matParamsCB.gpuAddr);
    } else {
        // Use standard material PSO (non-graph path)
        cmdList->SetPipelineState(g_pipelineLibrary.GetForwardPBR());
    }

    // Draw as usual
    cmdList->DrawIndexedInstanced(...);
}
```

### HLSL Injection

The `gbuffer_ps.hlsl` pixel shader includes conditional material graph evaluation:

```hlsl
// In gbuffer_ps.hlsl:
#ifdef UZ_MATERIAL_GRAPH
// Material-graph-generated function (injected at compile time)
void ComputeMaterialOutputs(
    in float4 materialParams[16],
    in float2 uv,
    in float3 positionWS,
    in float3 normalWS,
    in float4 vertexColor,
    in float3 objectPosition,
    in float2 screenUV,
    in float3 cameraPosition,
    in float totalTime,
    out float3 out_baseColor,
    out float out_metallic,
    out float out_roughness,
    out float3 out_normal,
    out float3 out_emissive,
    out float out_opacity,
    out float out_ao);

// Pixel shader main:
void main(PS_INPUT input, out GBufferOutput output) {
    // ... standard GBuffer setup ...

    // Call material graph
    float3 baseColor;
    float metallic, roughness, ao, opacity;
    float3 normal, emissive;

    ComputeMaterialOutputs(
        materialParams,
        input.uv,
        input.positionWS,
        input.normalWS,
        input.vertexColor,
        input.objectPosition,
        input.screenUV,
        input.cameraVec.xyz,
        g_perFrame.totalTime,
        baseColor, metallic, roughness, normal, emissive, opacity, ao);

    // Write GBuffer with computed values
    output.rt0 = float4(baseColor, 1.0);  // Albedo
    output.rt1 = float4(normal, metallic);  // Normal + Metallic
    output.rt2 = float4(roughness, ao, 0, 0);  // Roughness + AO
    output.rt3 = float4(emissive, opacity);  // Emissive + Opacity
}
#else
// Standard material path (original gbuffer_ps.hlsl logic)
// ...
#endif
```

### Material Parameters Buffer (register b3)

Runtime parameters are passed via a 16 x float4 constant buffer at register b3:

```hlsl
// Register b3: Material parameters (64 bytes)
cbuffer MaterialParamsCB : register(b3) {
    float4 params[16];  // 16 slots for ScalarParameter, VectorParameter, etc.
};
```

C++ side:

```cpp
struct MaterialParamsCB {
    Vec4 params[16];  // 16 x float4 = 64 bytes
};

// Fill from material instance overrides:
MaterialParamsCB matParams{};
matParams.params[0] = Vec4(roughnessMultiplier, 0, 0, 0);  // slot 0
matParams.params[1] = Vec4(baseColorTint.x, baseColorTint.y, baseColorTint.z, 1.0);  // slot 1
// ... etc up to 16 parameters
```

### Content Hash Stability

Two material graphs with **identical structure and parameters** produce the same PSO:

```cpp
// Graph 1: TexCoord → CheckerPattern(16) → Lerp → BaseColor
uint64_t hash1 = graph1->GetContentHash();

// Graph 2: TexCoord → CheckerPattern(16) → Lerp → BaseColor (same)
uint64_t hash2 = graph2->GetContentHash();

// hash1 == hash2, so both share the same compiled PSO
assert(hash1 == hash2);  // true
```

This enables **automatic deduplication** — multiple scenes with identical procedural materials only compile once.

### Compilation Pipeline

```
.uzmat (JSON)
    ↓
MaterialGraph::LoadFromFile()
    ↓
HlslMaterialTranslator::Compile() → ComputeMaterialOutputs() HLSL text
    ↓
Inject into gbuffer_ps.hlsl template
    ↓
DXC compile with -D UZ_MATERIAL_GRAPH
    ↓
xxHash64(graphContent) → cache key
    ↓
MaterialPipelineCache lookup
    ├─ Hit: return cached PSO, zero compilation cost
    └─ Miss: CreateGraphicsPipelineState(), store in cache
    ↓
Bind PSO at render time via DrawCommand.materialGraphPipeline handle
```

### Texture Parameter Binding

Material graphs that reference textures via `TextureParameter` nodes bind textures as follows:

**SM6.6 (Bindless)**:
- `TextureParameter` node emits `ResourceDescriptorHeap[heapIndex]` in generated HLSL
- Heap indices allocated at material compile time
- No root descriptor table overhead

**SM5 (Fallback, Phase 2)**:
- Use descriptor table slots (t0-t15) for texture parameters
- Dynamic descriptor heap for per-draw overrides

### Current Phase (1.5) Features

- ✅ PSO caching by content hash
- ✅ Material parameter binding (16 float4 slots)
- ✅ HLSL injection via `#ifdef UZ_MATERIAL_GRAPH`
- ✅ GBuffer pass PSO switching
- ⚠️ Texture parameters (SM6.6 bindless only, SM5 fallback pending Phase 2)
- ⚠️ Hot-reload of .uzmat files (Phase 2)
- ⚠️ Visual editor (Phase 5)

### Rules

1. **Material graphs are GPU-first** — compiler targets DX12 PSO, not CPU evaluation
2. **PSOs are cached forever** — same hash → same PSO. Delete cache manually to recompile.
3. **Register b3 is reserved** — do not bind other buffers to this register in material graphs
4. **Texture parameters require SM6.6** — SM5 fallback planned. Use ScalarParameter/VectorParameter for portability.
5. **Compiled PSOs are immutable** — create new DrawCommand if parameters change (via MaterialInstance)

## Current Limitations (Phase 1)

- **CPU bake only** — materials are evaluated on CPU and baked to textures. No GPU shader permutation yet.
- **No hot-reload** — `.uzmat` changes require restart (hot-reload planned for Phase 2).
- **No editor UI** — materials authored via JSON. Visual editor planned for Phase 5.6.
- **No normal mapping from graph** — normal output uses vertex normal; tangent-space perturbation from graph nodes not yet wired.
- **Single shading model** — DefaultLit only. Subsurface/Unlit planned.

## Rules

1. **NEVER manually author HLSL for materials** — use the node graph and compiler
2. **ALWAYS use named parameters** for runtime-tweakable values
3. **Expressions are data containers** — NO compilation logic in expression classes
4. **The compiler is a visitor** — all HLSL generation lives in `HlslMaterialTranslator`
5. **Content hash = identity** — same graph content = same compiled result
