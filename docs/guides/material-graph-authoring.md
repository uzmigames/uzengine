<!-- doc-meta
title: Material Graph Authoring Guide
category: guide
engine: uzengine
theme: rendering
created: 2026-03-18
updated: 2026-03-18
status: active
author: docs-operator
-->

# Material Graph Authoring Guide

## Overview

Create procedural and data-driven materials in UzEngine using **Material Graphs** — node-based visual systems compiled to HLSL. This guide teaches you how to design, author, and test materials via `.uzmat` JSON files without writing shader code.

## What Is a Material Graph?

A material graph is a **directed acyclic graph (DAG)** of expression nodes that compute PBR parameters in real-time. Each node represents a mathematical operation (add, multiply, texture sample, etc.), and nodes connect via typed pins. The compiler walks the graph, generates HLSL, and caches the result by content hash.

### Example: Checker Pattern Floor

```
TexCoord (UV input)
    ↓
CheckerPattern (scale=16)
    ↓
Lerp (mix 0.85 gray and 0.65 gray)
    ↓
PBROutput.BaseColor (final surface color)
```

The compiler generates HLSL that samples UV, applies checker logic, and outputs the blended color — all without hand-written shader code.

## File Format: `.uzmat`

Materials are serialized as JSON files with `.uzmat` extension. Each file defines:
- **nodes[]** — array of expression nodes (types, parameters, connections)
- **metadata** — material name, domain, shading model
- **connections** — pin-to-pin wiring between nodes

### Anatomy of a `.uzmat` File

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
            "pos": [-400, 0],
            "uvIndex": 0
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

### Key Fields

| Field | Type | Purpose |
|-------|------|---------|
| `version` | int | File format version (always 1) |
| `name` | string | Material name displayed in editor/runtime |
| `domain` | string | "Surface", "PostProcess", "Decal", or "UI" |
| `shadingModel` | string | "DefaultLit", "Unlit", or "Subsurface" |
| `expressions` | array | Array of nodes (see node structure below) |

### Node Structure

Every node has:
```json
{
    "id": 2,
    "type": "TexCoord",
    "pos": [x, y],
    "uvIndex": 0,
    "inputs": [
        {"pin": 0, "srcExpr": nodeId, "srcPin": outputPin}
    ]
}
```

| Field | Type | Purpose |
|-------|------|---------|
| `id` | int | Unique node ID (1 is always PBROutput) |
| `type` | string | Node type (TexCoord, CheckerPattern, Add, etc.) |
| `pos` | [x, y] | Canvas position for visual editor (unused in JSON) |
| `inputs` | array | Connections to this node's input pins |
| `*Params` | object | Node-specific parameters (scalarParams, vectorParams, etc.) |

### Connections

A connection links an output pin from one node to an input pin of another:

```json
"inputs": [
    {"pin": 0, "srcExpr": 3, "srcPin": 0}
]
```

- `pin` — input pin index on this node (e.g., CheckerPattern pin 0 = UV, pin 1 = Scale)
- `srcExpr` — source node ID
- `srcPin` — output pin index on source node (usually 0, except for multi-output nodes)

## Node Types Reference

### Constants (Output Static Values)

| Node | Outputs | Parameters | Example |
|------|---------|------------|---------|
| `ScalarConstant` | Value(float) | `value` | `{"scalarParams": {"value": 0.5}}` |
| `Vector2Constant` | Value(float2) | `x`, `y` | `{"vector2Params": {"x": 1.0, "y": 0.5}}` |
| `Vector3Constant` | Value(float3) | `x`, `y`, `z` | `{"vector3Params": {"x": 1, "y": 0.8, "z": 0.6}}` |
| `Vector4Constant` | Value(float4) | `x`, `y`, `z`, `w` | `{"vector4Params": {...}}` |
| `ColorConstant` | Color(float3) | `r`, `g`, `b` | `{"colorParams": {"r": 0.9, "g": 0.2, "b": 0.1}}` |

### Math Operations

| Node | Inputs | Output | Purpose |
|------|--------|--------|---------|
| `Add` | A, B | A + B | Sum |
| `Multiply` | A, B | A * B | Product |
| `Subtract` | A, B | A - B | Difference |
| `Divide` | A, B | A / B | Quotient |
| `Pow` | Base, Exponent | Base^Exp | Power |
| `Clamp` | Value, Min, Max | clamp(V, Min, Max) | Constrain to range |
| `Lerp` | A, B, Alpha | mix(A, B, Alpha) | Linear blend |
| `SmoothStep` | Edge0, Edge1, X | Hermite interp | Smooth threshold |
| `Min`, `Max` | A, B | min/max(A, B) | Minimum/maximum |
| `Dot` | A(Vec3), B(Vec3) | float | Dot product |
| `Cross` | A(Vec3), B(Vec3) | Vec3 | Cross product |
| `Abs` | Value | abs(V) | Absolute value |
| `OneMinus` | Value | 1 - V | Inverse |
| `Frac` | Value | frac(V) | Fractional part |
| `Floor`, `Ceil`, `Sqrt` | Value | — | Standard math |

### Textures

| Node | Inputs | Outputs | Parameters |
|------|--------|---------|------------|
| `TextureSample` | UV(float2) | RGBA, R, G, B, A | `texturePath` (e.g., "assets/texture.png") |

**Usage**: Sample texture at UV coordinates. Supports per-channel output (use RGBA for color, R for grayscale).

### Coordinates (Context Inputs)

| Node | Outputs | Purpose |
|------|---------|---------|
| `TexCoord` | UV(float2) | Texture UV (param: `uvIndex` = 0) |
| `WorldPosition` | Position(float3) | Fragment position in world space |
| `ObjectPosition` | Position(float3) | Object center in world space |
| `VertexNormal` | Normal(float3) | Interpolated vertex normal (tangent space) |
| `VertexColor` | Color(float4) | Interpolated vertex color per-vertex |
| `CameraVector` | Direction(float3) | Normalized direction from camera to fragment |
| `ScreenPosition` | UV(float2) | Fragment position in screen space (0-1) |
| `Time` | Seconds(float) | Global elapsed time (for animation) |

### Procedural Patterns

| Node | Inputs | Output | Parameters |
|------|--------|--------|------------|
| `CheckerPattern` | UV, Scale | Mask(float) | None (inputs control behavior) |
| `NoisePerlin` | UV, Scale | Value(float) | None |
| `GradientLinear` | UV | Value(float) | Direction (horizontal, vertical, diagonal) |
| `GradientRadial` | UV | Value(float) | Center (X, Y coordinates) |

### Utility Nodes

| Node | Inputs | Outputs |
|------|--------|---------|
| `TriPlanarMapping` | WorldPos, WorldNormal, Sharpness | UV, BlendWeights |
| `Fresnel` | Normal(float3), Exponent | Value(float) |
| `Panner` | UV, SpeedX, SpeedY | UV(float2) |
| `ComponentMask` | Input(float4) | Result (type varies) |
| `Append` | A, B | Result (concatenated) |

### Runtime Parameters (Artist-Tweakable)

| Node | Outputs | Parameters | Purpose |
|------|---------|------------|---------|
| `ScalarParameter` | Value(float) | `paramName`, `default` | Runtime float override |
| `VectorParameter` | Value(float3) | `paramName`, `defaultR/G/B` | Runtime color/vector |
| `TextureParameter` | Texture | `paramName`, `texturePath` | Runtime texture override |

## PBR Output Node

The **PBROutput** node (always id=1) receives final surface parameters:

| Input Pin | Type | Default | Purpose |
|-----------|------|---------|---------|
| 0 — BaseColor | Float3 | (1,1,1) | Diffuse albedo (linear sRGB) |
| 1 — Metallic | Float | 0.0 | 0 = dielectric, 1 = fully metallic |
| 2 — Roughness | Float | 0.5 | 0 = mirror, 1 = fully diffuse |
| 3 — Normal | Float3 | (0,0,1) | Surface normal (tangent space) |
| 4 — Emissive | Float3 | (0,0,0) | Self-illumination (HDR) |
| 5 — AO | Float | 1.0 | Ambient occlusion multiplier |
| 6 — Opacity | Float | 1.0 | Transparency (alpha) |

If an input is not connected, the default value is used.

## Step-by-Step Example: Creating a Checker Material

Let's create `MI_Checkerboard.uzmat` with alternating colors.

### Step 1: Start with TexCoord

```json
{
    "version": 1,
    "name": "MI_Checkerboard",
    "domain": "Surface",
    "shadingModel": "DefaultLit",
    "expressions": [
        {
            "id": 1,
            "type": "PBROutput",
            "pos": [400, 0],
            "inputs": []
        },
        {
            "id": 2,
            "type": "TexCoord",
            "pos": [-400, 0],
            "uvIndex": 0
        }
    ]
}
```

### Step 2: Add Checker Node

```json
{
    "id": 3,
    "type": "ScalarConstant",
    "pos": [-400, 100],
    "scalarParams": {"value": 10.0}
}
```

Then the checker node that uses UV and scale:

```json
{
    "id": 4,
    "type": "CheckerPattern",
    "pos": [-200, 50],
    "inputs": [
        {"pin": 0, "srcExpr": 2, "srcPin": 0},
        {"pin": 1, "srcExpr": 3, "srcPin": 0}
    ]
}
```

### Step 3: Create Two Colors

```json
{
    "id": 5,
    "type": "Vector3Constant",
    "pos": [-200, -100],
    "vector3Params": {"x": 0.85, "y": 0.85, "z": 0.85}
},
{
    "id": 6,
    "type": "Vector3Constant",
    "pos": [-200, -200],
    "vector3Params": {"x": 0.65, "y": 0.65, "z": 0.65}
}
```

### Step 4: Blend Colors with Lerp

```json
{
    "id": 7,
    "type": "Lerp",
    "pos": [0, 0],
    "inputs": [
        {"pin": 0, "srcExpr": 5, "srcPin": 0},
        {"pin": 1, "srcExpr": 6, "srcPin": 0},
        {"pin": 2, "srcExpr": 4, "srcPin": 0}
    ]
}
```

### Step 5: Connect to PBROutput

```json
{
    "id": 1,
    "type": "PBROutput",
    "pos": [400, 0],
    "inputs": [
        {"pin": 0, "srcExpr": 7, "srcPin": 0}
    ]
}
```

This creates a checker pattern that blends between two gray values!

## GPU Baking and Rendering

### MaterialBakerGpu

The `MaterialBakerGpu` system evaluates material graphs on the GPU, creating texture atlases that the renderer uses. Current status (Phase 1):

- **CPU-side baking** — materials evaluated on CPU, baked to RGBA8 textures
- **Phase 2** — GPU shader integration via MaterialPipeline (PSO caching, parameter binding)

### Material Pipeline Integration (Phase 1.5+)

When `MaterialPipeline` is active for a material:

1. Material graph HLSL is compiled via `HlslMaterialTranslator`
2. PSO is cached by content hash in `MaterialShaderCache`
3. GBuffer pass includes material graph-specific PSO
4. `ComputeMaterialOutputs()` HLSL function is injected with `#ifdef UZ_MATERIAL_GRAPH`
5. Runtime parameters passed via `MaterialParamsCB` at register b3 (16 x float4 capacity)

### Current Rendering Path

Currently, materials are CPU-baked and fed to the standard Material pipeline:

```
.uzmat (JSON)
    ↓
MaterialGraph::LoadFromFile()
    ↓
HlslMaterialTranslator::Compile() → HLSL ComputeMaterialOutputs()
    ↓
MaterialBaker::BakeBaseColor() → RGBA8 texture
    ↓
Material assigned texture → standard PBR pipeline renders
```

## Lua API (Runtime Control)

Once a material is loaded, control runtime parameters via Lua:

```lua
-- Get material instance
local mat = uz.scene:get_material("node_name")

-- Set scalar parameter
mat:set_scalar_param("scale", 20.0)

-- Set vector parameter
mat:set_vector_param("color", {0.9, 0.2, 0.1})

-- Set texture parameter
mat:set_texture_param("custom_texture", "assets/noise.png")
```

Also control global post-process effects:

```lua
-- Auto-exposure
uz.cvar:set("r.AutoExposureMethod", 1)
uz.cvar:set("r.AutoExposureMinBrightness", 0.03)

-- Bloom
uz.cvar:set("r.BloomIntensity", 1.0)

-- Temporal upscaling
uz.renderer:set_upscaler("tsr")
uz.renderer:set_screen_percentage(67.0)
```

## Common Patterns

### Animated Checker (Time-Based Frequency)

Combine `Time` with `Multiply` to vary checker frequency over time:

```json
{
    "id": 10,
    "type": "Multiply",
    "inputs": [
        {"pin": 0, "srcExpr": 3, "srcPin": 0},
        {"pin": 1, "srcExpr": 11, "srcPin": 0}
    ]
},
{
    "id": 11,
    "type": "Time"
}
```

### Metallic Variation (Texture-Based Mask)

Sample a texture and feed to metallic input of PBROutput:

```json
{
    "id": 12,
    "type": "TextureSample",
    "pos": [0, 200],
    "inputs": [
        {"pin": 0, "srcExpr": 2, "srcPin": 0}
    ],
    "stringParams": {"texturePath": "assets/metallic_mask.png"}
}
```

Then connect output R channel to PBROutput pin 1 (Metallic).

### Normal Map Integration

Use `TextureSample` for normal maps and connect to PBROutput pin 3 (Normal):

```json
{
    "id": 13,
    "type": "TextureSample",
    "inputs": [
        {"pin": 0, "srcExpr": 2, "srcPin": 0}
    ],
    "stringParams": {"texturePath": "assets/normal.png"}
}
```

Normal maps are stored in tangent space (red=X, green=Y, blue=Z).

## Troubleshooting

| Issue | Cause | Solution |
|-------|-------|----------|
| Black material | No connection to PBROutput | Check `inputs` array in PBROutput node (id=1) |
| Compilation fails | Invalid JSON syntax | Use JSON validator tool, check quotes and commas |
| White material | Metallic defaulted to 0 | Explicitly check metallic input or use a parameter |
| Texture not sampling | Wrong texture path or missing file | Verify path is relative to project root, check asset exists |
| Graph not updating | Cache not invalidated | Check xxHash64 content hash in MaterialShaderCache |

## Rules

1. **Node ID=1 is always PBROutput** — required in every material
2. **Use typed pins** — don't connect float3 to float input (compiler will error)
3. **Avoid cycles** — DAG requires acyclic graphs; cycles are detected at load time
4. **Optimize for baking** — CPU baking is slow; keep graphs simple for real-time preview
5. **Parameter names are case-sensitive** — `roughness_multiplier` ≠ `RoughnessMultiplier`
6. **Content hash identifies shaders** — same graph content → same compiled shader, cached forever

## Related Documentation

- [Material System Architecture](../engine/12-rendering/16-material-system.md)
- [PBR Material System Data Layout](../engine/12-rendering/06-pbr-material-system.md)
- [Shader Parameter Binding Guide](shader-parameter-binding-guide.md)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-18 | Initial creation — material graph JSON format, node reference, examples, troubleshooting |
