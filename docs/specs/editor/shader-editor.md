# Node-Based Shader Editor

> Prompt-driven shader graph system with full internal behavior of a node-based material editor.
> Graphs are created/modified via structured commands, compiled to HLSL in real-time, and previewed in the engine viewport.

## Overview

The shader editor provides programmatic shader authoring through 8 subsystems:

| # | Subsystem | Location | Purpose |
|---|-----------|----------|---------|
| 1 | Shader Graph Core | `src/engine/renderer/shader_graph/` | DAG storage, pin system, type validation, serialization |
| 2 | Node System | `shader_graph/nodes/` | Extensible node library, each node implements `compile()` |
| 3 | Compilation Pipeline | `shader_graph/compiler/` | Graph traversal, HLSL code generation, optimization |
| 4 | Graph Control Layer | `shader_graph/graph_ops.h` | Programmatic API for graph manipulation |
| 5 | Prompt Command Interface | `shader_graph/commands/` | Structured command protocol → GraphOps operations |
| 6 | UzScene Preview | `renderer/preview/` | In-memory preview scene with procedural meshes |
| 7 | Live Shader Update | `renderer/preview/` | Event-driven auto-compilation + fallback retention |
| 8 | Shader Graph Manager | `shader_graph/` | `.uzshader` file loading/saving/caching, hot-reload |

**No visual node editor UI.** Interaction is prompt/command-driven. The internal graph system is identical to a traditional node-based editor and supports a future visual UI layer.

## 1. Value Type System

Bitmask-based type system enabling automatic scalar-to-vector promotion:

```cpp
enum class ValueType : uint32_t {
    None      = 0,
    Float1    = 1 << 0,   // scalar float
    Float2    = 1 << 1,   // vec2
    Float3    = 1 << 2,   // vec3
    Float4    = 1 << 3,   // vec4
    Texture2D = 1 << 4,
    TextureCube = 1 << 5,
    Sampler   = 1 << 6,
    Bool      = 1 << 7,
    Int       = 1 << 8,
    Scalar    = Float1 | Float2 | Float3 | Float4,
    AnyTexture = Texture2D | TextureCube,
};
```

**Compatibility**: `(a & b) != 0` — a `Scalar` pin accepts any float width.
**Promotion**: `Float1 + Float3 → Float3` (scalar broadcast). `Float2 + Float4 → error` (ambiguous).

## 2. Pin System

```cpp
struct PinDesc {
    std::string name;         // "A", "B", "UV", "RGB"
    ValueType   type;         // Declared type (may be composite like Scalar)
    bool        is_required;  // Must be connected for valid compilation?
};

struct NodeInput {
    uint32_t source_node_id = 0;       // 0 = unconnected
    int32_t  source_output_index = -1;
    float    default_value[4] = {};    // Used when unconnected
};
```

**Rule**: Each input pin accepts exactly ONE connection. Output pins support fan-out (multiple consumers).

## 3. ShaderNode Base Class

Visitor-compiler pattern (from UE5): nodes call typed methods on `IShaderCompiler`, never emit raw HLSL.

```cpp
class ShaderNode {
public:
    uint32_t    id = 0;
    std::string type_name;
    float       pos_x = 0.0f, pos_y = 0.0f;  // Stored for future visual editor

    virtual std::span<const PinDesc> input_descs() const = 0;
    virtual std::span<const PinDesc> output_descs() const = 0;
    virtual NodeInput& get_input(int32_t index) = 0;
    virtual int32_t    input_count() const = 0;
    virtual int32_t compile(IShaderCompiler& compiler, int32_t output_index) = 0;
    virtual void serialize(nlohmann::json& j) const;
    virtual void deserialize(const nlohmann::json& j);
};
```

**`compile()` returns**: A code chunk index (int32_t) into the compiler's SSA-like IR.

## 4. ShaderGraph

DAG of `ShaderNode` instances with typed connections:

- `add_node()` / `remove_node()` — node lifecycle
- `connect()` / `disconnect()` — connection management (auto-disconnects existing input)
- `validate_dag()` — DFS 3-state cycle detection (Unvisited/InProgress/Done)
- `validate_types()` — all connections checked via `types_compatible()`
- `serialize()` / `deserialize()` — `.uzshader` JSON format
- `output_node_id()` — returns the `MaterialOutputNode` ID

## 5. Starter Nodes (15)

| Category | Nodes |
|----------|-------|
| Output | `MaterialOutputNode` (8 PBR inputs: BaseColor, Metallic, Roughness, Normal, Emission, AO, Opacity, Displacement) |
| Constants | `Constant1Node`, `Constant3Node`, `Constant4Node` |
| Parameters | `ScalarParamNode`, `VectorParamNode`, `TextureParamNode` |
| Math | `AddNode`, `SubtractNode`, `MultiplyNode`, `DivideNode`, `LerpNode`, `DotNode`, `NormalizeNode`, `PowerNode` |
| Texture | `TextureSampleNode` (Tex + UV inputs → RGB + R/G/B/A outputs) |
| Utility | `AppendNode`, `ComponentMaskNode`, `TimeNode`, `UVNode` |

## 6. Compilation Pipeline

### IShaderCompiler Interface

```cpp
class IShaderCompiler {
public:
    virtual int32_t constant(float v) = 0;
    virtual int32_t constant_vec(const float* v, int32_t count) = 0;
    virtual int32_t add(int32_t a, int32_t b) = 0;
    virtual int32_t multiply(int32_t a, int32_t b) = 0;
    virtual int32_t lerp(int32_t a, int32_t b, int32_t alpha) = 0;
    virtual int32_t dot(int32_t a, int32_t b) = 0;
    virtual int32_t normalize(int32_t v) = 0;
    virtual int32_t power(int32_t base, int32_t exp) = 0;
    virtual int32_t texture_sample(int32_t tex, int32_t uv) = 0;
    virtual int32_t component_mask(int32_t v, const char* mask) = 0;
    virtual int32_t append(int32_t a, int32_t b) = 0;
    virtual int32_t world_position() = 0;
    virtual int32_t world_normal() = 0;
    virtual int32_t texcoord(int32_t index) = 0;
    virtual int32_t time() = 0;
    virtual int32_t camera_position() = 0;
    // ... promote, param, subtract, divide
};
```

### HlslTranslator

Concrete `IShaderCompiler` implementation:

- **Code chunks**: SSA-like IR array. Each `add_chunk()` returns an index. Constants and simple swizzles inline; complex expressions get local variables.
- **Expression caching**: `ExpressionKey` → chunk index map. Diamond graph patterns compile once.
- **Type promotion**: `promote()` — `Float1→Float3` via `float3(x,x,x)`.
- **HLSL assembly**: `assemble_pixel_shader()` emits:
  1. `cbuffer MaterialParams` from collected parameters
  2. `Texture2D` + `SamplerState` declarations
  3. Local variable definitions (dependency order)
  4. `GBufferOutput` struct assignment mapping to engine's 4-MRT layout

### G-Buffer Output Mapping

The compiled pixel shader must output the engine's G-Buffer layout:

| SV_Target | Format | Content |
|-----------|--------|---------|
| 0 | RGBA8 | RGB = BaseColor, A = AO |
| 1 | RGB10A2 | RG = octahedral Normal, B = envMultiplier, A = flags |
| 2 | RGBA8 | R = Metallic, G = Roughness, B = emissionScale, A = flags |
| 3 | R11G11B10F | RGB = Emission * energy (HDR) |

## 7. Graph Control Layer (GraphOps)

All graph mutations go through a typed API. This is the same API a visual editor would call.

### Atomic Operations
- `create_node(type, params)` → `OpResult{ok, nodeId}`
- `delete_node(nodeId)` → auto-disconnects all connections
- `connect_pins(fromNode, fromOutput, toNode, toInput)` → type-checks, auto-disconnects existing
- `disconnect_pin(nodeId, inputIndex)`
- `set_param(nodeId, key, value)`

### Compound Operations
- `insert_between(connectionId, nodeType, params)` — disconnect, create, re-wire
- `replace_node(nodeId, newType, params)` — transfer compatible connections
- `expose_parameter(nodeId, inputIndex, paramName)` — insert param node upstream

### Event System
- `GraphEventType`: NodeAdded, NodeRemoved, ConnectionAdded, ConnectionRemoved, ParamChanged
- `set_event_listener(callback)` — used by Live Shader Update for auto-compilation

## 8. Prompt Command Interface

Structured JSON commands dispatched to GraphOps:

```json
{"type": "ADD_NODE", "params": {"nodeType": "Multiply", "x": 200, "y": 100}}
{"type": "CONNECT", "params": {"from": 2, "fromOutput": 0, "to": 1, "toInput": 0}}
{"type": "BATCH", "commands": [
    {"type": "ADD_NODE", "params": {"nodeType": "Lerp"}},
    {"type": "CONNECT", "params": {"from": "$0", "fromOutput": 0, "to": 1, "toInput": 2}}
]}
```

**Variable references**: `"$0"`, `"$1"` resolve to `nodeId` from previous batch results.
**Batch rollback**: On failure, all previous commands in the batch are reverted.

### IPC Handlers

| Command | Description |
|---------|-------------|
| `shader_cmd.execute` | Execute single command |
| `shader_cmd.batch` | Execute batch with rollback |
| `shader_cmd.query` | Convenience query wrapper |

## 9. UzScene Preview Environment

In-memory preview scene using existing engine infrastructure:

| Component | Reused Infrastructure |
|-----------|----------------------|
| Preview mesh | `mesh_primitives.h` — `generate_sphere/cube/plane()` |
| Orbit camera | `camera_update_orbit()` from `camera_component.h` |
| IBL lighting | `IblBaker::bake()` — irradiance + prefiltered cubemaps |
| Viewport | `ViewportRegistry` with `ViewportType::Material` |
| Shader compilation | `ShaderLibrary::get_or_create_pipeline()` |
| Material params | `MaterialInstance` with scalar/vector/texture overrides |

### IPC Handlers

| Command | Description |
|---------|-------------|
| `shader_graph.preview` | Start preview (returns viewportId) |
| `shader_graph.stopPreview` | Stop and release viewport |
| `shader_graph.setPreviewMesh` | Switch mesh (sphere/cube/plane) |
| `shader_graph.setPreviewCamera` | Set orbit camera (yaw, pitch, distance) |
| `shader_graph.setPreviewParam` | Tweak material parameter without recompilation |

## 10. Live Shader Update System

Event-driven auto-compilation with fallback retention:

1. `ShaderUpdateController` listens to `GraphOps` events
2. Debounce: coalesce rapid mutations within 100ms window
3. Compile cycle: validate → HLSL generation → PSO creation → apply to preview
4. **Fallback**: stores `last_good_vs_`/`last_good_ps_` — preview never shows blank frame on error
5. Push `shader_graph.diagnostics` event with errors/warnings (includes node IDs)
6. Push `shader_graph.compiled` event on success with timing + parameter manifest

## 11. File Format (.uzshader)

```json
{
    "version": 1,
    "name": "My Material",
    "nodes": [
        {"id": 1, "type": "MaterialOutput", "x": 400, "y": 200, "inputs": [...]},
        {"id": 2, "type": "TextureSample", "x": 0, "y": 0, "params": {"texture": "rock_albedo.png"}}
    ],
    "connections": [
        {"from": 2, "fromOutput": 0, "to": 1, "toInput": 0}
    ],
    "params": {
        "roughness": {"type": "Float1", "default": [0.5]},
        "albedo_tex": {"type": "Texture2D", "default": "default_white.png"}
    }
}
```

**Material integration**: `Material.shader_name = "graph:path/to/material.uzshader"` triggers graph compilation via `ShaderGraphManager`.

## 12. Module Boundary Rules

- `shader_graph/` MUST NOT include `rhi.h`, scene headers, or any DX12/Vulkan/Metal types
- `shader_graph/` depends only on: `core/math.h`, `nlohmann/json`, standard library
- `renderer/preview/` MAY use `rhi.h`, `ShaderLibrary`, `MaterialInstance`, `ViewportRegistry`
- The compilation pipeline outputs HLSL text strings — the preview layer compiles them to GPU objects
- This boundary ensures the graph system is testable without GPU hardware

## Dependencies

- Existing `ShaderLibrary` + PSO caching
- Existing `Material`/`MaterialInstance` system
- Existing RHI abstraction (`rhi.h`)
- Existing WebSocket IPC protocol
- Existing viewport streaming infrastructure
- `nlohmann/json` (already in vcpkg)

## Deferred (Architecture Supports)

- Visual node editor UI (Canvas/WebGL in VSCode)
- LLM-powered natural language → command translation
- Material instances from shader graphs
- Subgraphs / shader functions
- Custom HLSL code nodes
- Undo/redo system for graph operations
- Shader permutation system (static switches)
- Preshader / uniform expression folding
- Optimization passes (dead code elimination, constant folding)
