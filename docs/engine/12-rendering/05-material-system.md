# Material System — Overview

> **Status**: Planned (Milestone 2.0)
> **Source**: Spec in `docs/ENGINE.md` §33
> **Depends on**: Shader Pipeline (§05), Render Graph (§32), Resource System (§08)
> **References**: UE5 `UMaterial` / `UMaterialInstance`, Godot `VisualShader`, Substance Designer

## Purpose

Artists and programmers need to author materials without writing low-level shader code. The Material System provides a **node-based graph compiler** that generates optimized HLSL shaders, manages permutations, and caches compiled PSOs.

A material is a **directed acyclic graph of nodes** that compose PBR parameters (BaseColor, Metallic, Roughness, etc.). The compiler:
- Evaluates the graph at cook-time and runtime
- Generates shader code with dead code elimination
- Bakes permutation combinations into separate PSOs
- Caches compiled code by content hash
- Supports dynamic parameter tweaking via Material Instances

```
Material Graph (Node-Based)
────────────────────────────
Texture Sample [diffuse.png]
       ▼
    Multiply ◄─── Constant [0.8]
       ▼
   BaseColor
       │
       ▼
Compile to HLSL + Cook to PSO
       │
       ▼
Game loads material → selects appropriate PSO → renders with parameters
```

## Material Domains

A material operates in a specific rendering context:

```cpp
enum class MaterialDomain : uint8_t {
    /// Standard surface rendering (lit geometry)
    Surface,

    /// Post-process effect (fullscreen quad)
    PostProcess,

    /// Decal projection (blends onto surfaces)
    Decal,

    /// UI widget rendering (2D)
    UI,

    /// Particle / VFX rendering
    Particle,
};
```

**Example: Surface domain**

```json
{
  "domain": "Surface",
  "shading_model": "DefaultLit",
  "nodes": [
    {
      "id": "tex_diffuse",
      "type": "TextureSample",
      "properties": { "texture": "res://textures/diffuse.uztexture" }
    },
    {
      "id": "base_color",
      "type": "Output",
      "input": "tex_diffuse.rgba"
    }
  ]
}
```

## Shading Models

Different material types require different lighting models:

| Model | Purpose | Parameters | Example |
|-------|---------|------------|---------|
| **DefaultLit** | Standard PBR | BaseColor, Metallic, Roughness, Normal, AO | Metal, plastic, stone |
| **Unlit** | No lighting calculation | BaseColor, Emissive | UI, neon, holograms |
| **Subsurface** | Light scattering through translucent geometry | BaseColor + Subsurface power | Skin, wax, marble |
| **ClearCoat** | Dual-layer (clear coat over base) | BaseColor + CoatRoughness | Car paint, varnish |
| **Hair** | Anisotropic specular + strand-based | TangentDirectionality, Specular | Hair, fur, grass |
| **Cloth** | Velvet-like scattering | Cloth density + drape | Fabric, upholstery |
| **Eye** | Iris caustics + corneal refraction | IrisNormal, Sclera mask | Character eyes |

## Shading Model Selection

```cpp
struct MaterialDesc {
    MaterialDomain domain;
    ShadingModel shading_model;
    // ...
};
```

**Example: Create a subsurface material (skin)**

```json
{
  "domain": "Surface",
  "shading_model": "Subsurface",
  "parameters": [
    { "name": "base_color", "type": "vec4", "default": [1, 0.8, 0.8, 1] },
    { "name": "subsurface_power", "type": "float", "default": 2.5 }
  ],
  "nodes": [
    {
      "id": "diffuse_tex",
      "type": "TextureSample",
      "properties": { "path": "res://textures/skin_diffuse.png" }
    },
    {
      "id": "out",
      "type": "Output",
      "connections": {
        "base_color": "diffuse_tex.rgba",
        "subsurface_power": "param:subsurface_power"
      }
    }
  ]
}
```

## Material Graph — Node Types

### Texture Sampling

```cpp
struct TextureSampleNode {
    std::string texture_path;  // "res://textures/..."
    SamplerMode sampler;       // Linear, Point, Aniso
    bool use_virtual_texture;  // Megatexture streaming
};

// Generated HLSL:
// float4 tex = tex2d(texture, uv);
```

### Math Operations

```cpp
struct MathNode {
    enum Operation {
        Add, Subtract, Multiply, Divide, Power,
        Dot, Cross, Normalize, Length,
        Min, Max, Clamp, Lerp,
        Sin, Cos, Tan, Abs, Frac,
    } op;
};
```

### Texture Coordinate Manipulation

```cpp
struct TexCoordNode {
    enum CoordType { UV0, UV1, UV2, UV3, WorldPos, Normal, Tangent };
    CoordType type;
};

struct TriplanarNode {
    std::string texture_path;
    float scale;  // World space scale
};

struct WorldAlignedNode {
    std::string texture_path;
    enum Axis { X, Y, Z } dominant_axis;
};
```

### Conditional / Control Flow

```cpp
struct SwitchNode {
    std::string condition;  // bool or float parameter
    // true branch + false branch inputs
};
```

## Material Parameters

Materials expose **named parameters** that can be:
1. Statically baked into shader code (compile-time)
2. Dynamically changed per-instance (runtime, via Material Instances)

```json
{
  "parameters": [
    {
      "name": "base_color",
      "type": "vec4",
      "default": [1, 1, 1, 1],
      "ui_name": "Base Color",
      "editable_at_runtime": true
    },
    {
      "name": "metallic",
      "type": "float",
      "default": 0.0,
      "range": [0, 1],
      "editable_at_runtime": true
    },
    {
      "name": "roughness",
      "type": "float",
      "default": 0.5,
      "range": [0, 1],
      "editable_at_runtime": true
    },
    {
      "name": "use_normal_map",
      "type": "bool",
      "default": true,
      "editable_at_compile_time_only": true
    }
  ]
}
```

## Shader Permutations

Different feature combinations generate different shaders:

```cpp
struct ShaderPermutation {
    std::string material_path;      // "res://materials/rock.uzmaterial"
    uint32_t feature_bits;          // Bitmask of enabled features
    ShaderTarget target;            // SM5.0, SM6.6
    uint64_t content_hash;          // Content-based cache key
};
```

**Example: Compile permutations**

```
Material: "rock.uzmaterial"
  ├─ Permutation 0: [Normal disabled, SM6.6]      → shader_0_sm66.dxil
  ├─ Permutation 1: [Normal enabled, SM6.6]       → shader_1_sm66.dxil
  ├─ Permutation 2: [Normal disabled, SM5.0]      → shader_0_sm50.dxbc
  └─ Permutation 3: [Normal enabled, SM5.0]       → shader_1_sm50.dxbc

Compiled PSOs:
  ├─ PSO_rock_sm66_0 (no normal map)
  ├─ PSO_rock_sm66_1 (with normal map)
  ├─ PSO_rock_sm50_0 (SM5.0 path, no normal)
  └─ PSO_rock_sm50_1 (SM5.0 path, with normal)
```

Permutation bits are typically derived from **feature toggles** in the material or scene:

```cpp
enum class MaterialFeature : uint32_t {
    UseNormalMap     = 1 << 0,
    UseParallaxMap   = 1 << 1,
    UseAOMap         = 1 << 2,
    UseDetailMap     = 1 << 3,
    // ...
};

// Compiler generates HLSL with:
// #if HAS_NORMAL_MAP
//   float3 normal = tex2d(normal_map, uv).xyz;
// #else
//   float3 normal = float3(0, 0, 1);
// #endif
//
// Dead code elimination removes branches not taken → smaller, faster shaders
```

## Material File Format (.uzmaterial)

```json
{
  "version": "1.0",
  "domain": "Surface",
  "shading_model": "DefaultLit",
  "parameters": [
    {
      "name": "base_color",
      "type": "vec4",
      "default": [1, 1, 1, 1]
    },
    {
      "name": "metallic",
      "type": "float",
      "default": 0.0,
      "range": [0, 1]
    }
  ],
  "nodes": [
    {
      "id": "base_color_tex",
      "type": "TextureSample",
      "properties": {
        "texture": "res://textures/diffuse.uztexture",
        "sampler": "LinearWrap"
      }
    },
    {
      "id": "metallic_tex",
      "type": "TextureSample",
      "properties": { "texture": "res://textures/metallic.uztexture" }
    },
    {
      "id": "output",
      "type": "Output",
      "domain": "Surface",
      "connections": {
        "base_color": "base_color_tex.rgba",
        "metallic": "metallic_tex.r",
        "roughness": 0.6
      }
    }
  ]
}
```

## Material Instances (.uzmaterial_instance)

Material Instances allow per-entity parameter overrides **without recompiling**:

```json
{
  "material": "res://materials/rock.uzmaterial",
  "parameters": {
    "base_color": [0.5, 0.5, 0.5, 1.0],
    "metallic": 0.1,
    "roughness": 0.7
  }
}
```

**Usage in code:**

```cpp
// Load base material + instance
auto rock_material = Assets::Load<Material>("res://materials/rock.uzmaterial");
auto instance = Assets::Load<MaterialInstance>("res://materials/rock_mossy.uzmaterial_instance");

// Render uses base shader (rock.uzmaterial) but with instance params (metallic=0.1)
mesh_component.material_instance = instance;

// Dynamic runtime changes (valid only for editable_at_runtime parameters)
mesh_component.SetParameter("base_color", Vec4(0.8, 0.6, 0.4, 1.0));
```

## Material Functions (Reusable Subgraphs)

Common patterns can be extracted into reusable functions:

```json
{
  "type": "MaterialFunction",
  "name": "NormalMapBlend",
  "inputs": [
    { "name": "normal1", "type": "vec3" },
    { "name": "normal2", "type": "vec3" },
    { "name": "blend", "type": "float" }
  ],
  "outputs": [
    { "name": "result", "type": "vec3" }
  ],
  "nodes": [
    {
      "id": "lerp",
      "type": "Lerp",
      "connections": {
        "a": "input:normal1",
        "b": "input:normal2",
        "t": "input:blend"
      }
    },
    {
      "id": "normalize",
      "type": "Normalize",
      "input": "lerp.output"
    },
    {
      "id": "out",
      "type": "FunctionOutput",
      "connections": { "result": "normalize.output" }
    }
  ]
}
```

**Using the function:**

```json
{
  "nodes": [
    {
      "id": "normal1_tex",
      "type": "TextureSample",
      "properties": { "texture": "res://textures/normal1.png" }
    },
    {
      "id": "normal2_tex",
      "type": "TextureSample",
      "properties": { "texture": "res://textures/normal2.png" }
    },
    {
      "id": "blend_normal",
      "type": "FunctionCall",
      "function": "res://functions/NormalMapBlend.uzfunc",
      "connections": {
        "normal1": "normal1_tex.rgb",
        "normal2": "normal2_tex.rgb",
        "blend": 0.5
      }
    }
  ]
}
```

## PBR Parameter Set

Standard PBR materials use this canonical parameter set:

| Parameter | Type | Range | Purpose |
|-----------|------|-------|---------|
| **BaseColor** | vec4 | [0, 1] | Diffuse color (sRGB gamma space) |
| **Metallic** | float | [0, 1] | Metal vs dielectric (0=matte, 1=mirror) |
| **Roughness** | float | [0, 1] | Surface smoothness (0=mirror, 1=diffuse) |
| **Normal** | vec3 | [-1, 1] | Perturbed surface normal (tangent space) |
| **Emissive** | vec3 | [0, ∞] | Self-illumination (HDR) |
| **AO** | float | [0, 1] | Ambient occlusion (darkens crevices) |
| **Opacity** | float | [0, 1] | Transparency (0=transparent, 1=opaque) |

## Material Parameter Blocks (GPU Storage)

Per-instance parameters are stored in a constant buffer:

```cpp
struct MaterialParameterBlock {
    Vec4 base_color;
    float metallic;
    float roughness;
    float opacity;
    float pad0;
    Vec3 emissive;
    float pad1;
    // Total: 48 bytes (aligned to 256-byte slots for GPU)
};
```

**GPU binding:**

```cpp
// During render pass execution:
RgContext ctx;
ctx.SetConstantBuffer(MATERIAL_PARAMS_SLOT, material_param_buffer);

// Shader accesses:
cbuffer MaterialParams : register(b0) {
    float4 base_color;
    float metallic;
    float roughness;
    float opacity;
    float3 emissive;
};
```

## Compilation Pipeline

```
.uzmaterial (JSON)
    ▼
Material Compiler
  ├─► Parse graph (AST)
  ├─► Validate (cycles, type checking)
  ├─► Generate HLSL (for each permutation)
  ├─► Compile HLSL → DXIL/DXBC (DXC/FXC)
  ├─► Reflect PSO layout (descriptor tables, root signature)
  └─► Create RHIPipeline (PSO)
    ▼
.uzmaterial_cooked (PSO cache)
    ▼
Runtime load .uzmaterial_cooked → RHIPipeline ready
```

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine** |
|---------|----------|---------|--------------|
| Graph representation | Node graph + custom C++ | VisualShader (visual nodes) | **JSON node graph** |
| Material domains | 5 (Surface, PostProc, Decal, UI, Particle) | 3 (Spatial, CanvasItem, Particles) | **5 (Surface, PostProcess, Decal, UI, Particle)** |
| Shading models | 7+ | 2 (Spatial, CanvasItem) | **7 (DefaultLit, Unlit, SubSurface, ClearCoat, Hair, Cloth, Eye)** |
| Permutations | Shader permutation system | Shader variants | **Feature-bit permutations + dead code elimination** |
| Material Instances | `UMaterialInstance` (param override) | `ShaderMaterial.set_shader_parameter` | **.uzmaterial_instance JSON** |
| Parameter caching | FMaterialShaderMap | Material::set_shader_param | **MaterialParameterBlock (GPU constant buffer)** |

## Rules

1. **NEVER manually author HLSL for materials** — use the node graph and compiler
2. **ALWAYS validate parameter ranges** — GPU requires float/int normalization ([-1, 1], [0, 1])
3. **NEVER create new Material assets per entity** — use Material Instances for per-entity tweaks
4. **ALWAYS use named parameters** — undocumented magic values are technical debt
5. **ALWAYS profile material permutations** — too many combinations → long compile times and large PSO cache
6. **NEVER use dynamic branching in material graphs** — pre-compute all feature combinations at cook-time
