# Material Editor — Material Graph Visual Authoring

> **Status**: Planned (Phase 2–3)
> **Source**: `extension/editor/material-editor.ts`, `engine/render/material-graph.h`
> **References**: Node-Graph Framework (§9), Shader Pipeline (§5), UE5 Material Editor, Godot VisualShader

## Purpose

The Material Editor allows artists to visually author materials by connecting nodes (math operations, texture samples, constants) into a graph that compiles to HLSL shader code. It replaces hand-written shader code with a visual node-based workflow, similar to UE5's Material Editor or Substance Designer.

Unlike the full shader pipeline (§5) which handles compilation targets, the Material Editor focuses on **visual authoring and real-time preview** of the material graph.

## Architecture

```
Material Editor UI (TypeScript)
    ├─ Graph Canvas
    │  ├─ Material nodes (Add, Multiply, Texture, etc.)
    │  └─ Output node (BaseColor, Normal, Metallic, etc.)
    │
    ├─ Preview Viewport
    │  ├─ 3D sphere/plane with material applied
    │  ├─ Real-time parameter sliders
    │  └─ Light probe selection
    │
    ├─ Material Parameters Panel
    │  ├─ Exposed inputs (promoted pins)
    │  └─ Scalar/Vector/Texture overrides
    │
    └─ Compiler
       ├─ Graph → HLSL code
       ├─ Type checking (only compatible connections)
       └─ Error reporting (inline on nodes)

Runtime Material System (C++)
    ├─ Compiled shader bytecode (DXIL/SPIR-V)
    ├─ Descriptor sets (texture/sampler bindings)
    └─ Runtime parameters (updated from viewport)
```

## Node Categories

### Math Nodes

| Node | Inputs | Output | Purpose |
|------|--------|--------|---------|
| **Add** | A (Float/Vec), B (Float/Vec) | Result (same type) | A + B |
| **Subtract** | A, B | Result | A - B |
| **Multiply** | A, B | Result | A * B (scalar or element-wise) |
| **Divide** | A, B | Result | A / B |
| **Power** | Base, Exponent | Result | Base^Exponent |
| **Clamp** | Value, Min, Max | Result | clamp(Value, Min, Max) |
| **Lerp** | A, B, Alpha | Result | mix(A, B, Alpha) |
| **Smoothstep** | Edge0, Edge1, Value | Result | Smooth Hermite interpolation |
| **Floor** | Value | Result | floor(Value) |
| **Ceil** | Value | Result | ceil(Value) |
| **Frac** | Value | Result | frac(Value) |
| **Abs** | Value | Result | abs(Value) |
| **Sign** | Value | Result | sign(Value) |
| **Normalize** | Vector | Normalized | normalize(Vector) |
| **Dot** | A (Vec), B (Vec) | Result (Float) | dot(A, B) |
| **Cross** | A (Vec3), B (Vec3) | Result (Vec3) | cross(A, B) |
| **Distance** | A (Vec), B (Vec) | Result (Float) | length(A - B) |
| **Length** | Vector | Result (Float) | length(Vector) |
| **Sine** | Value | Result | sin(Value) |
| **Cosine** | Value | Result | cos(Value) |
| **Tangent** | Value | Result | tan(Value) |

### Texture Nodes

| Node | Inputs | Output | Purpose |
|------|--------|--------|---------|
| **Sample Texture** | Texture (Path), UV (Vec2) | Color (Vec4) | Sample texture at UV coordinates |
| **Sample Normal Map** | Texture, UV | Normal (Vec3) | Sample + decompress normal map (tangent space) |
| **UV Tile** | UV, Scale (Vec2) | Tiled UV (Vec2) | Scale UV for tiling |
| **UV Pan** | UV, Speed (Float), Time | Panned UV (Vec2) | Animate UV over time |
| **UV Distortion** | UV, Distortion Map, Strength | Distorted UV | Distort UV using texture |
| **Parallax Map** | Height Map, UV, Height, Eye Dir | Parallax UV | Parallax mapping approximation |

### Constant Nodes

| Node | Inputs | Output | Purpose |
|------|--------|--------|---------|
| **Float Constant** | None | Value (Float) | Constant 1.0 float (editable in inspector) |
| **Vec3 Constant** | None | Value (Vec3) | Constant color/vector (color picker in inspector) |
| **Vec4 Constant** | None | Value (Vec4) | Constant RGBA (color picker) |
| **Bool Constant** | None | Value (Bool) | True/False constant |

### Utility Nodes

| Node | Inputs | Output | Purpose |
|------|--------|--------|---------|
| **World Position** | None | Position (Vec3) | Fragment position in world space |
| **Camera Vector** | None | Direction (Vec3) | Normalized view direction (camera → fragment) |
| **Time** | None | Elapsed (Float) | Global elapsed time (for animation) |
| **Object Color** | None | Color (Vec4) | Object parameter (set in material instance) |
| **Fresnel** | Normal (Vec3), Bias (Float) | Result (Float) | Fresnel effect (view-dependent reflection) |
| **Vertex Color** | None | Color (Vec4) | Interpolated vertex color from mesh |
| **Parallax Depth** | Height Map, UV, Camera Dir | Depth (Float) | Parallax mapping depth offset |

### Custom HLSL

For advanced effects, artists can write inline HLSL:

```typescript
interface CustomHLSLNode extends GraphNode {
    hlslCode: string;  // User-written HLSL code

    // Define inputs/outputs
    inputs: GraphPin[];
    outputs: GraphPin[];
}

// Example: Custom glow effect
const glowNode = new CustomHLSLNode();
glowNode.hlslCode = `
float3 glow = color.rgb * pow(brightness, 2.0) * glow_strength;
return glow;
`;
glowNode.inputs = [
    { name: "color", type: PinType.Vec3 },
    { name: "brightness", type: PinType.Float },
    { name: "glow_strength", type: PinType.Float },
];
glowNode.outputs = [
    { name: "result", type: PinType.Vec3 },
];
```

## Output Node

The output node defines the final material appearance. All pins are optional:

```typescript
interface MaterialOutputNode extends GraphNode {
    outputs: {
        // PBR channels
        baseColor: GraphPin;      // RGB, [0, 1]
        metallic: GraphPin;       // Float, [0, 1] (0=dielectric, 1=metal)
        roughness: GraphPin;      // Float, [0, 1] (0=mirror, 1=rough)
        normal: GraphPin;         // Vec3, world or tangent space
        emissive: GraphPin;       // RGB, any range (will be tonemapped)
        ambientOcclusion: GraphPin;  // Float, [0, 1]
        opacity: GraphPin;        // Float, [0, 1] for transparency
        opacityMask: GraphPin;    // Float, [0, 1] for cutout/dithering
        customData0: GraphPin;    // Custom user data (subsurface, anisotropy, etc.)
        customData1: GraphPin;    // Custom user data
    };

    // Material properties
    shadingModel: "DefaultLit" | "Unlit" | "Subsurface" | "ClearCoat" | "Hair" | "Cloth" | "Eye";
    blendMode: "Opaque" | "Masked" | "Transparent" | "Additive" | "Modulate";
    normal: "Tangent" | "World";  // Normal space
}
```

## Material Domains

Different materials use different output pins:

| Domain | Available Outputs | Use Cases |
|--------|-------------------|-----------|
| **Surface** | BaseColor, Normal, Metallic, Roughness, AO, Emissive, Opacity | Characters, props, terrain |
| **PostProcess** | Color, Depth, Normal | Screen-space effects (bloom, CA, motion blur) |
| **Decal** | BaseColor, Normal, Emissive, Opacity | Decals on surfaces |
| **UI** | Color (2D), Opacity | UI elements, HUDs |
| **Particle** | Color, Opacity, Size, Rotation | Particles, trails |

## Shading Models

Shading models affect how material inputs are interpreted:

| Model | Inputs | Notes |
|-------|--------|-------|
| **DefaultLit** | BaseColor, Normal, Metallic, Roughness, AO | Standard PBR |
| **Unlit** | Emissive, Opacity | No lighting, self-illuminated only |
| **Subsurface** | BaseColor, Normal, Subsurface Color, Falloff | Translucent materials (skin, candles) |
| **ClearCoat** | BaseColor, ClearCoat Roughness | Multi-layer (paint over clear coat) |
| **Hair** | Tangent, Shift | Anisotropic hair/fur |
| **Cloth** | BaseColor, Cloth Shading | Fabric-specific BRDF |
| **Eye** | Iris Color, Sclera | Specialized for eyes |

## Material Parameters

Parameters are material instance variables, edited in the Inspector:

```typescript
interface MaterialParameter {
    name: string;              // "base_color", "metallic_strength"
    displayName: string;       // "Base Color Strength"
    type: "Float" | "Vec3" | "Vec4" | "Texture" | "Bool";
    defaultValue: any;
    constraints?: {
        min?: number;
        max?: number;
    };
}

// Example: parametrized material
const params: MaterialParameter[] = [
    { name: "base_color_strength", displayName: "Color Strength", type: "Float", defaultValue: 1.0, constraints: { min: 0, max: 2 } },
    { name: "metallic_override", displayName: "Metallic Override", type: "Float", defaultValue: 0.5 },
    { name: "roughness_override", displayName: "Roughness Override", type: "Float", defaultValue: 0.5 },
    { name: "normal_map", displayName: "Normal Map", type: "Texture", defaultValue: "res://textures/default_normal.png" },
];
```

### Promoting Pins to Parameters

Artists can right-click on any input pin and promote it to a material parameter:

```typescript
class MaterialEditor extends BaseGraphEditor {
    onPinContextMenu(pin: GraphPin, node: GraphNode) {
        if (!pin.isInput) return;  // Only promote inputs

        const menu = [
            {
                label: "Promote to Parameter",
                onclick: () => {
                    // Create parameter from pin
                    const param: MaterialParameter = {
                        name: `${node.title}_${pin.name}`.toLowerCase(),
                        displayName: `${node.title} ${pin.name}`,
                        type: this.pinTypeToParamType(pin.type),
                        defaultValue: pin.defaultValue,
                    };
                    this.material.parameters.push(param);
                    this.recordUndo("Promote pin to parameter");
                }
            },
            {
                label: "Unpromote",
                onclick: () => {
                    this.material.parameters = this.material.parameters.filter(
                        p => p.name !== `${node.title}_${pin.name}`.toLowerCase()
                    );
                }
            }
        ];

        showContextMenu(menu, event.clientX, event.clientY);
    }
}
```

## Live Viewport Preview

The preview viewport updates in real-time as the artist edits:

```typescript
class MaterialPreviewViewport {
    private gl: WebGLRenderingContext;
    private mesh: Mesh;
    private material: Material;
    private lighting: LightProbe;
    private isDirty = false;

    /// Called when material graph changes
    onMaterialChanged() {
        this.isDirty = true;
    }

    /// Update and render
    render() {
        if (this.isDirty) {
            // Recompile material
            this.compileMaterial();
            this.isDirty = false;
        }

        const gl = this.gl;
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

        // Set up camera
        const view = this.getCameraMatrix();
        const proj = this.getProjectionMatrix();

        // Bind material and render mesh
        this.material.bind(view, proj);
        this.mesh.render();
        this.material.unbind();

        requestAnimationFrame(() => this.render());
    }

    /// Compile graph to GLSL, load into GPU
    private async compileMaterial() {
        const graphJson = JSON.stringify(this.material.graph);
        const result = await this.ipc.call('editor.compile_material_graph', [graphJson]);

        if (result.success) {
            this.material.setShaderCode(result.hlslCode);
            this.material.compile();
        } else {
            console.error("Compilation failed:", result.errors);
        }
    }
}
```

### Preview Controls

| Control | Action |
|---------|--------|
| **Left-drag** | Rotate mesh |
| **Right-drag** | Pan camera |
| **Scroll** | Zoom |
| **1/2/3 keys** | Switch mesh (sphere/plane/custom) |
| **L key** | Cycle lighting (studio, outdoor, custom) |
| **Space** | Toggle material vs base color |
| **Ctrl+S** | Save material |

## Material Functions

Material Functions are reusable subgraphs:

```typescript
interface MaterialFunction {
    name: string;              // "MakeWood", "MakeBrick"
    inputs: MaterialParameter[];
    outputs: GraphPin[];
    graph: GraphNode[];        // Internal node graph
}

// Usage in another material: right-click and search "MakeWood"
// Then drag onto canvas — it becomes a single node with inputs/outputs
```

## Permutation System

Materials can have multiple compiled variants based on feature flags:

```cpp
/// Compile material with different feature combinations
class MaterialCompiler {
    struct Permutation {
        bool useNormalMap;
        bool useParallax;
        bool useDisplacement;
        bool useEmissive;
    };

    std::string CompilePermutation(const Material& mat, const Permutation& perm) {
        // Add #defines based on permutation
        std::string hlsl = "#pragma shader_feature\n";
        if (perm.useNormalMap)    hlsl += "#define USE_NORMAL_MAP 1\n";
        if (perm.useParallax)     hlsl += "#define USE_PARALLAX 1\n";
        if (perm.useDisplacement) hlsl += "#define USE_DISPLACEMENT 1\n";
        if (perm.useEmissive)     hlsl += "#define USE_EMISSIVE 1\n";

        // Compile graph with defines
        hlsl += compileGraph(mat.graph);

        // Return compiled DXIL
        return dxcCompile(hlsl, "ps_6_6");
    }
};
```

## File Format

Materials are saved as `.uzmaterial` JSON:

```json
{
  "version": 1,
  "metadata": {
    "title": "Material_Brick",
    "created": "2026-03-12T10:00:00Z",
    "domain": "Surface",
    "shadingModel": "DefaultLit"
  },
  "graph": {
    "nodes": [
      {
        "id": "node-001",
        "type": "TextureSampleNode",
        "title": "Base Color",
        "category": "Texture",
        "position": { "x": 0, "y": 0 },
        "parameters": {
          "texture": "res://textures/brick_color.png"
        },
        "inputs": [
          { "id": "uv", "name": "UV", "type": "Vec2", "connections": [] }
        ],
        "outputs": [
          { "id": "color", "name": "Color", "type": "Vec4", "connections": ["node-003/base"] }
        ]
      },
      {
        "id": "node-002",
        "type": "TextureSampleNode",
        "title": "Normal",
        "category": "Texture",
        "position": { "x": 0, "y": 100 },
        "parameters": {
          "texture": "res://textures/brick_normal.png"
        },
        "outputs": [
          { "id": "normal", "name": "Normal", "type": "Vec3", "connections": ["node-003/normal"] }
        ]
      },
      {
        "id": "node-003",
        "type": "MaterialOutputNode",
        "title": "Material Output",
        "category": "Output",
        "position": { "x": 200, "y": 50 },
        "inputs": [
          { "id": "base", "name": "Base Color", "type": "Vec4", "connections": ["node-001/color"] },
          { "id": "normal", "name": "Normal", "type": "Vec3", "connections": ["node-002/normal"] },
          { "id": "metallic", "name": "Metallic", "type": "Float", "connections": [] },
          { "id": "roughness", "name": "Roughness", "type": "Float", "connections": [] }
        ]
      }
    ]
  },
  "parameters": [
    {
      "name": "base_color_tint",
      "displayName": "Base Color Tint",
      "type": "Vec4",
      "defaultValue": [1.0, 1.0, 1.0, 1.0]
    }
  ]
}
```

## Compilation to HLSL

The material graph compiles to a pixel shader:

```hlsl
// Compiled output from .uzmaterial

Texture2D BaseColorTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState LinearSampler : register(s0);

cbuffer MaterialParams : register(b0) {
    float4 baseColorTint;
};

struct PixelInput {
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 texCoord : TEXCOORD2;
};

float4 ps_main(PixelInput input) : SV_Target {
    // Node: TextureSampleNode (Base Color)
    float4 node_001_color = BaseColorTexture.Sample(LinearSampler, input.texCoord);

    // Node: TextureSampleNode (Normal)
    float3 node_002_normal = NormalTexture.Sample(LinearSampler, input.texCoord).rgb;
    node_002_normal = normalize(node_002_normal * 2.0 - 1.0);  // Decompress

    // Node: MaterialOutputNode
    float4 baseColor = node_001_color * baseColorTint;
    float3 normal = node_002_normal;
    float metallic = 0.0;
    float roughness = 0.5;

    // PBR shading
    float3 viewDir = normalize(cameraPos - input.worldPosition);
    float3 shadedColor = PBRShade(baseColor.rgb, normal, metallic, roughness, viewDir);

    return float4(shadedColor, baseColor.a);
}
```

## Comparison

| Feature | UE5 Material Editor | Godot VisualShader | Substance Designer | **UzEngine** |
|---|---|---|---|---|
| **Domains** | 6+ (Surface, Decal, UI, PostProcess, Light Function, etc.) | 5 (Vertex, Fragment, Light) | 1 (material) | **4–5 (Surface, PostProcess, Decal, UI, Particle)** |
| **Shading models** | 10+ | 1 (Standard) | N/A | **7 (DefaultLit, Unlit, Subsurface, ClearCoat, Hair, Cloth, Eye)** |
| **Parameter promotion** | Yes (via Promote Pin) | Limited | Yes | **Yes (right-click pin)** |
| **Permutations** | Automated (shader features) | Manual (preprocessor) | Automated | **Automated (permutation system)** |
| **Preview** | Real-time, sphere/plane/custom | Real-time | Real-time | **Real-time, sphere/plane/custom** |
| **Functions** | Yes (Material Functions) | No | Yes (Graphs) | **Yes (Material Functions)** |
| **Hot-reload** | Asset save | Automatic | Automatic | **File save (hot-reload)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Basic material graph (Math, Texture, Output nodes), live preview (sphere), parameter promotion, `.uzmaterial` serialization |
| **Phase 3** | Material functions, permutation system, advanced nodes (Fresnel, Parallax, Custom HLSL), Material Instances (per-object parameter overrides) |
| **Phase 4** | Material layers (composition), substance-like procedural materials, texture baking, LOD material variants |

