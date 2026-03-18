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
