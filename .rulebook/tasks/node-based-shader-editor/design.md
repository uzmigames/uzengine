# Node-Based Shader Editor — Architecture Design

## 1. System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│               External Interface (Prompts / IPC / MCP)           │
│                                                                  │
│  "Add a Multiply node between TextureSample and MaterialOutput"  │
│  "Create a Lerp between two textures controlled by a parameter"  │
│  "Expose Roughness as a parameter"                               │
└──────────────────────────┬──────────────────────────────────────┘
                           │ Structured Commands
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│               Prompt Command Interface (C++ + TS)                │
│                                                                  │
│  ┌──────────────────────┐    ┌───────────────────────────────┐  │
│  │  CommandParser        │    │  CommandExecutor              │  │
│  │                       │    │                               │  │
│  │  - parse structured   │───►│  - validate commands          │  │
│  │    command JSON       │    │  - execute via GraphOps       │  │
│  │  - batch operations   │    │  - return results + errors    │  │
│  └──────────────────────┘    └──────────────┬────────────────┘  │
└─────────────────────────────────────────────┼───────────────────┘
                                              │ GraphOps calls
                                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      C++ Engine                                  │
│                                                                  │
│  ┌──────────────────┐  ┌──────────────┐  ┌───────────────────┐  │
│  │  GraphControlLayer│  │  NodeRegistry │  │  HlslTranslator   │  │
│  │  (GraphOps API)   │  │              │  │                   │  │
│  │                   │  │  - Add       │  │  - code_chunks[]  │  │
│  │  - create_node()  │  │  - Multiply  │  │  - expr_cache     │  │
│  │  - delete_node()  │  │  - TexSample │  │  - compile()      │  │
│  │  - connect_pins() │  │  - Lerp      │  │  - emit_hlsl()    │  │
│  │  - disconnect()   │  │  - UV, Time  │  └─────────┬─────────┘  │
│  │  - insert_between │  │  - Constant  │            │             │
│  │  - replace_node() │  │  - ...       │            ▼             │
│  │  - expose_param() │  └──────────────┘  ┌───────────────────┐  │
│  │  - query_graph()  │                    │  ShaderLibrary    │  │
│  └────────┬─────────┘                     │  (PSO creation)   │  │
│           │                               └─────────┬─────────┘  │
│           ▼                                         │             │
│  ┌──────────────────┐                               ▼             │
│  │   ShaderGraph     │                     ┌───────────────────┐  │
│  │                   │                     │  Renderer         │  │
│  │  - nodes[]        │                     │  (Preview mesh)   │  │
│  │  - connections[]  │                     └───────────────────┘  │
│  │  - validate_dag() │                                           │
│  │  - serialize()    │                                           │
│  └──────────────────┘                                            │
│                                                                  │
│  ┌──────────────────┐                                            │
│  │  IPC Handlers     │                                           │
│  │  shader_graph.*   │                                           │
│  │  shader_cmd.*     │                                           │
│  └──────────────────┘                                            │
└─────────────────────────────────────────────────────────────────┘
```

## 2. Data Structures (C++ — `src/engine/renderer/shader_graph/`)

### 2.1 Value Type System (Bitmask)

```cpp
// shader_graph_types.h
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

    // Composite masks — "accepts any float width"
    Scalar    = Float1 | Float2 | Float3 | Float4,
    AnyTexture = Texture2D | TextureCube,
};

// Compatibility: bitwise AND
inline bool types_compatible(ValueType a, ValueType b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

// Promotion: scalar → wider vector
ValueType promote_type(ValueType a, ValueType b);
// Float1 + Float3 → Float3
// Float3 + Float3 → Float3
// Float2 + Float4 → error (ambiguous)

// Component count for HLSL emission
int component_count(ValueType t);
// Float1→1, Float2→2, Float3→3, Float4→4

// HLSL type name
const char* hlsl_type_name(ValueType t);
// Float1→"float", Float2→"float2", Float3→"float3", Float4→"float4"
```

### 2.2 Pin System

```cpp
// shader_node.h
struct PinDesc {
    std::string name;         // "A", "B", "UV", "RGB"
    ValueType   type;         // Pin's declared type
    bool        is_required;  // Must be connected?
};

struct NodeInput {
    uint32_t source_node_id = 0;       // 0 = unconnected
    int32_t  source_output_index = -1;
    // Default value when unconnected (for scalar pins)
    float    default_value[4] = {0, 0, 0, 0};
};

struct NodeOutput {
    std::string name;
    ValueType   type;    // Concrete type (resolved after connection)
};
```

### 2.3 ShaderNode Base Class

```cpp
// shader_node.h
class ShaderNode {
public:
    uint32_t    id = 0;
    std::string type_name;      // "Add", "Multiply", "TextureSample", etc.

    // Visual layout (stored in .uzshader, used by editor UI)
    float       pos_x = 0.0f;
    float       pos_y = 0.0f;

    virtual ~ShaderNode() = default;

    // Pin descriptors (static per node type)
    virtual std::span<const PinDesc> input_descs() const = 0;
    virtual std::span<const PinDesc> output_descs() const = 0;

    // Input connections (mutable, one per input pin)
    virtual NodeInput& get_input(int32_t index) = 0;
    virtual int32_t    input_count() const = 0;

    // Compilation — visitor-compiler pattern
    virtual int32_t compile(class IShaderCompiler& compiler,
                            int32_t output_index) = 0;

    // Serialization
    virtual void serialize(nlohmann::json& j) const;
    virtual void deserialize(const nlohmann::json& j);
};
```

### 2.4 ShaderGraph

```cpp
// shader_graph.h
struct Connection {
    uint32_t from_node;
    int32_t  from_output;
    uint32_t to_node;
    int32_t  to_input;
};

class ShaderGraph {
public:
    // Node management
    uint32_t add_node(std::unique_ptr<ShaderNode> node);
    void remove_node(uint32_t id);
    ShaderNode* get_node(uint32_t id);
    const std::vector<std::unique_ptr<ShaderNode>>& nodes() const;

    // Connection management
    bool connect(uint32_t from_node, int32_t from_output,
                 uint32_t to_node, int32_t to_input);
    void disconnect(uint32_t to_node, int32_t to_input);
    const std::vector<Connection>& connections() const;

    // Validation
    bool validate_dag() const;        // No cycles
    bool validate_types() const;      // All connections type-compatible
    std::vector<std::string> get_errors() const;

    // Serialization (.uzshader JSON)
    nlohmann::json serialize() const;
    static std::unique_ptr<ShaderGraph> deserialize(const nlohmann::json& j);

    // Output node (final material output — albedo, normal, metallic, etc.)
    uint32_t output_node_id() const;

private:
    std::vector<std::unique_ptr<ShaderNode>> nodes_;
    std::vector<Connection> connections_;
    uint32_t next_id_ = 1;

    // Cycle detection (DFS)
    bool has_cycle_dfs(uint32_t node_id,
                       std::vector<uint8_t>& states) const;
};
```

### 2.5 Material Output Node

```cpp
// nodes/output_node.h
class MaterialOutputNode : public ShaderNode {
public:
    // PBR output pins (inputs TO this node = outputs FROM the material)
    NodeInput base_color;      // Float3 — albedo
    NodeInput metallic;        // Float1
    NodeInput roughness;       // Float1
    NodeInput normal;          // Float3 — tangent-space normal
    NodeInput emission;        // Float3
    NodeInput ao;              // Float1
    NodeInput opacity;         // Float1
    NodeInput displacement;    // Float1

    static constexpr PinDesc k_input_descs[] = {
        {"BaseColor",    ValueType::Float3, false},
        {"Metallic",     ValueType::Float1, false},
        {"Roughness",    ValueType::Float1, false},
        {"Normal",       ValueType::Float3, false},
        {"Emission",     ValueType::Float3, false},
        {"AO",           ValueType::Float1, false},
        {"Opacity",      ValueType::Float1, false},
        {"Displacement", ValueType::Float1, false},
    };

    // No outputs (this is the terminal node)
    std::span<const PinDesc> output_descs() const override { return {}; }

    // Compilation: each input generates its own HLSL expression
    // The compiler collects them into the final pixel shader output struct
    int32_t compile(IShaderCompiler& compiler, int32_t output_index) override;
};
```

## 3. Compilation Pipeline (C++ — `shader_graph/compiler/`)

### 3.1 IShaderCompiler Interface

```cpp
// shader_compiler.h
class IShaderCompiler {
public:
    virtual ~IShaderCompiler() = default;

    // Constants
    virtual int32_t constant(float x) = 0;
    virtual int32_t constant2(float x, float y) = 0;
    virtual int32_t constant3(float x, float y, float z) = 0;
    virtual int32_t constant4(float x, float y, float z, float w) = 0;

    // Arithmetic
    virtual int32_t add(int32_t a, int32_t b) = 0;
    virtual int32_t subtract(int32_t a, int32_t b) = 0;
    virtual int32_t multiply(int32_t a, int32_t b) = 0;
    virtual int32_t divide(int32_t a, int32_t b) = 0;

    // Math functions
    virtual int32_t dot(int32_t a, int32_t b) = 0;
    virtual int32_t cross(int32_t a, int32_t b) = 0;
    virtual int32_t normalize(int32_t a) = 0;
    virtual int32_t lerp(int32_t a, int32_t b, int32_t alpha) = 0;
    virtual int32_t clamp(int32_t x, int32_t min, int32_t max) = 0;
    virtual int32_t saturate(int32_t x) = 0;
    virtual int32_t abs(int32_t x) = 0;
    virtual int32_t power(int32_t base, int32_t exp) = 0;
    virtual int32_t sqrt(int32_t x) = 0;
    virtual int32_t sin(int32_t x) = 0;
    virtual int32_t cos(int32_t x) = 0;
    virtual int32_t frac(int32_t x) = 0;
    virtual int32_t floor(int32_t x) = 0;
    virtual int32_t ceil(int32_t x) = 0;

    // Swizzle / component access
    virtual int32_t component_mask(int32_t input,
                                    bool r, bool g, bool b, bool a) = 0;
    virtual int32_t append(int32_t a, int32_t b) = 0;

    // Textures
    virtual int32_t texture_sample(int32_t tex_index, int32_t uv) = 0;
    virtual int32_t texture_object(const std::string& param_name) = 0;

    // Built-in inputs
    virtual int32_t texture_coord(int32_t uv_index) = 0;
    virtual int32_t time() = 0;
    virtual int32_t world_position() = 0;
    virtual int32_t world_normal() = 0;
    virtual int32_t camera_position() = 0;

    // Type casting
    virtual int32_t force_cast(int32_t code, ValueType target) = 0;

    // Error reporting
    virtual int32_t error(const std::string& msg) = 0;

    // Call expression with caching + cycle guard
    virtual int32_t call_expression(uint32_t node_id,
                                     int32_t output_index) = 0;
};
```

### 3.2 Code Chunk System (SSA-like IR)

```cpp
// hlsl_translator.h
struct CodeChunk {
    uint64_t    hash;
    std::string definition;   // HLSL expression text
    std::string symbol;       // "local_42" or "" if inline
    ValueType   type;
    bool        is_inline;    // Inline into consumers vs. local variable
};

struct ExpressionKey {
    uint32_t node_id;
    int32_t  output_index;
    bool operator==(const ExpressionKey&) const = default;
};

struct ExpressionKeyHash {
    size_t operator()(const ExpressionKey& k) const {
        return std::hash<uint64_t>{}(
            (uint64_t(k.node_id) << 32) | uint32_t(k.output_index));
    }
};
```

### 3.3 HlslTranslator

```cpp
// hlsl_translator.h
class HlslTranslator : public IShaderCompiler {
public:
    struct CompileResult {
        bool        ok;
        std::string vs_code;       // Complete vertex shader HLSL
        std::string ps_code;       // Complete pixel shader HLSL
        std::vector<std::string> errors;
        std::vector<std::string> texture_params;  // Texture parameter names
        std::vector<std::string> scalar_params;   // Scalar parameter names
    };

    CompileResult compile(const ShaderGraph& graph);

    // IShaderCompiler overrides
    int32_t constant(float x) override;
    int32_t add(int32_t a, int32_t b) override;
    int32_t texture_sample(int32_t tex_index, int32_t uv) override;
    int32_t call_expression(uint32_t node_id, int32_t output_index) override;
    // ... all other overrides

private:
    std::vector<CodeChunk> chunks_;
    int32_t next_symbol_ = 0;

    // Expression caching (deduplication)
    std::unordered_map<ExpressionKey, int32_t, ExpressionKeyHash> expr_cache_;

    // Cycle detection (runtime guard)
    std::unordered_set<ExpressionKey, ExpressionKeyHash> compilation_stack_;

    // Graph reference (set during compile())
    const ShaderGraph* graph_ = nullptr;

    // Internal helpers
    int32_t add_chunk(const std::string& code, ValueType type, bool is_inline);
    std::string create_symbol();
    const CodeChunk& get_chunk(int32_t index) const;
    std::string get_code(int32_t index) const;  // Symbol or inline expansion
    ValueType get_type(int32_t index) const;

    // Type promotion
    int32_t promote(int32_t code, ValueType target);

    // HLSL assembly
    std::string assemble_pixel_shader() const;
    std::string assemble_vertex_shader() const;
};
```

### 3.4 Compilation Flow

```
ShaderGraph.validate_dag()
    ↓ (pass)
HlslTranslator.compile(graph)
    ↓
For each MaterialOutput pin (BaseColor, Metallic, ...):
    call_expression(output_node_id, pin_index)
        ↓
    Check expr_cache_ → hit: return cached chunk index
                       → miss: push to compilation_stack_
                               node.compile(*this, output_index)
                                 ↓ (recursive: node calls compiler methods)
                                 ↓ e.g. compiler.add(a, b) → returns chunk index
                               pop from compilation_stack_
                               store in expr_cache_
    ↓
assemble_pixel_shader()
    → Emit #include "common.hlsli"
    → Emit cbuffer MaterialParams { ... } from collected params
    → Emit Texture2D declarations from collected textures
    → Emit local variable declarations (non-inline chunks)
    → Emit PS output struct assignment from MaterialOutput pins
    ↓
assemble_vertex_shader()
    → Standard PBR vertex shader (MVP transform, tangent frame)
    → If displacement pin connected: vertex displacement
    ↓
CompileResult { vs_code, ps_code, texture_params, scalar_params }
```

## 4. Node System (C++ — `shader_graph/nodes/`)

### 4.1 Node Registry

```cpp
// node_registry.h
using NodeFactory = std::unique_ptr<ShaderNode>(*)();

class NodeRegistry {
public:
    static NodeRegistry& instance();

    void register_node(const std::string& type_name,
                       const std::string& category,
                       NodeFactory factory);

    std::unique_ptr<ShaderNode> create(const std::string& type_name) const;

    struct NodeInfo {
        std::string type_name;
        std::string category;
        NodeFactory factory;
    };
    std::span<const NodeInfo> all() const;
};

// Auto-registration macro
#define UZ_REGISTER_SHADER_NODE(Class, Category) \
    static bool s_##Class##_registered = [] { \
        NodeRegistry::instance().register_node( \
            #Class, Category, \
            [] { return std::make_unique<Class>(); }); \
        return true; \
    }();
```

### 4.2 Starter Node Set (Phase 2)

| Category | Node | Inputs | Outputs |
|----------|------|--------|---------|
| **Constants** | Constant1 | — | Float1 |
| | Constant3 | — | Float3 |
| | Constant4 | — | Float4 |
| | ScalarParam | — | Float1 |
| | VectorParam | — | Float4 |
| | TextureParam | — | Texture2D |
| **Math** | Add | A(Scalar), B(Scalar) | Result(Scalar) |
| | Subtract | A(Scalar), B(Scalar) | Result(Scalar) |
| | Multiply | A(Scalar), B(Scalar) | Result(Scalar) |
| | Divide | A(Scalar), B(Scalar) | Result(Scalar) |
| | Lerp | A(Scalar), B(Scalar), Alpha(Float1) | Result(Scalar) |
| | Dot | A(Scalar), B(Scalar) | Result(Float1) |
| | Normalize | Input(Scalar) | Result(Scalar) |
| | Power | Base(Scalar), Exp(Float1) | Result(Scalar) |
| **Texture** | TextureSample | Tex(Texture2D), UV(Float2) | RGB(Float3), R/G/B/A(Float1) |
| **Utility** | Append | A(Scalar), B(Scalar) | Result(wider) |
| | ComponentMask | Input(Scalar) | Result(masked) |
| | Time | — | Time(Float1), SinTime(Float1) |
| | UV | — | UV(Float2) |
| **Output** | MaterialOutput | BaseColor, Metallic, Roughness, Normal, Emission, AO, Opacity, Displacement | — |

### 4.3 Example Node Implementation

```cpp
// nodes/add_node.h
class AddNode : public ShaderNode {
public:
    NodeInput inputs_[2];

    static constexpr PinDesc k_inputs[] = {
        {"A", ValueType::Scalar, false},
        {"B", ValueType::Scalar, false},
    };
    static constexpr PinDesc k_outputs[] = {
        {"Result", ValueType::Scalar, false},
    };

    std::span<const PinDesc> input_descs() const override { return k_inputs; }
    std::span<const PinDesc> output_descs() const override { return k_outputs; }
    NodeInput& get_input(int32_t i) override { return inputs_[i]; }
    int32_t input_count() const override { return 2; }

    int32_t compile(IShaderCompiler& c, int32_t) override {
        int32_t a = inputs_[0].source_node_id
            ? c.call_expression(inputs_[0].source_node_id,
                                inputs_[0].source_output_index)
            : c.constant(inputs_[0].default_value[0]);
        int32_t b = inputs_[1].source_node_id
            ? c.call_expression(inputs_[1].source_node_id,
                                inputs_[1].source_output_index)
            : c.constant(inputs_[1].default_value[0]);
        return c.add(a, b);
    }
};

UZ_REGISTER_SHADER_NODE(AddNode, "Math")
```

## 5. IPC Protocol (WebSocket)

### 5.1 Graph Operations

```jsonc
// Get full graph state
{ "method": "shader_graph.getGraph", "params": { "path": "res://materials/stone.uzshader" } }
→ { "ok": true, "graph": { "nodes": [...], "connections": [...] } }

// Add node
{ "method": "shader_graph.addNode", "params": { "path": "...", "type": "AddNode", "x": 200, "y": 150 } }
→ { "ok": true, "nodeId": 5 }

// Remove node
{ "method": "shader_graph.removeNode", "params": { "path": "...", "nodeId": 5 } }
→ { "ok": true }

// Connect pins
{ "method": "shader_graph.connect", "params": { "path": "...", "fromNode": 3, "fromOutput": 0, "toNode": 5, "toInput": 0 } }
→ { "ok": true }

// Disconnect pin
{ "method": "shader_graph.disconnect", "params": { "path": "...", "toNode": 5, "toInput": 0 } }
→ { "ok": true }

// Move node (visual position only)
{ "method": "shader_graph.moveNode", "params": { "path": "...", "nodeId": 5, "x": 300, "y": 200 } }
→ { "ok": true }

// Set node parameter (e.g., constant value)
{ "method": "shader_graph.setParam", "params": { "path": "...", "nodeId": 2, "key": "value", "data": [0.5, 0.3, 0.1] } }
→ { "ok": true }
```

### 5.2 Compilation

```jsonc
// Compile graph to HLSL
{ "method": "shader_graph.compile", "params": { "path": "res://materials/stone.uzshader" } }
→ { "ok": true, "vs_code": "...", "ps_code": "...", "errors": [], "params": { "textures": [...], "scalars": [...] } }

// Compile + create PSO + start preview
{ "method": "shader_graph.preview", "params": { "path": "...", "mesh": "sphere" } }
→ { "ok": true, "previewId": 1 }

// Stop preview
{ "method": "shader_graph.stopPreview", "params": { "previewId": 1 } }
→ { "ok": true }
```

### 5.3 Push Events (Engine → Extension)

```jsonc
// Compilation error/warning update
{ "event": "shader_graph.diagnostics", "data": { "path": "...", "errors": [...], "warnings": [...] } }

// Graph changed externally (e.g., hot-reload from disk)
{ "event": "shader_graph.graphChanged", "data": { "path": "...", "graph": {...} } }
```

## 6. File Format (`.uzshader`)

JSON-based, human-readable, diff-friendly:

```json
{
  "version": 1,
  "name": "Stone_Master",
  "blend_mode": "Opaque",
  "nodes": [
    {
      "id": 1,
      "type": "MaterialOutput",
      "x": 600, "y": 200
    },
    {
      "id": 2,
      "type": "TextureSample",
      "x": 200, "y": 100,
      "params": { "texture_param": "BaseColorMap" }
    },
    {
      "id": 3,
      "type": "Constant1",
      "x": 200, "y": 300,
      "params": { "value": 0.8 }
    },
    {
      "id": 4,
      "type": "Multiply",
      "x": 400, "y": 200
    }
  ],
  "connections": [
    { "from": 2, "fromOutput": 0, "to": 4, "toInput": 0 },
    { "from": 3, "fromOutput": 0, "to": 4, "toInput": 1 },
    { "from": 4, "fromOutput": 0, "to": 1, "toInput": 0 }
  ],
  "params": {
    "BaseColorMap": { "type": "Texture2D", "default": "res://textures/stone_albedo.png" }
  }
}
```

## 7. Graph Control Layer (C++ — `shader_graph/graph_ops.h/.cpp`)

The Graph Control Layer is the programmatic API through which all graph mutations flow.
It replaces the visual Node Editor UI. Internally, it performs the exact same operations
a visual editor would: create nodes, connect pins, validate types, maintain the DAG.

### 7.1 GraphOps API

```cpp
// graph_ops.h
struct OpResult {
    bool ok;
    std::string error;           // Empty if ok
    nlohmann::json data;         // Operation-specific return data
};

class GraphOps {
public:
    explicit GraphOps(ShaderGraph& graph, NodeRegistry& registry);

    // --- Atomic Operations ---

    // Create a node by type name. Returns {ok, data: {nodeId}}.
    OpResult create_node(const std::string& type_name,
                         float x = 0.0f, float y = 0.0f);

    // Delete a node and all its connections.
    OpResult delete_node(uint32_t node_id);

    // Connect output pin to input pin. Validates type compatibility.
    // Automatically disconnects any existing connection on the input.
    OpResult connect_pins(uint32_t from_node, int32_t from_output,
                          uint32_t to_node, int32_t to_input);

    // Disconnect a specific input pin.
    OpResult disconnect_pin(uint32_t node_id, int32_t input_index);

    // Set a node's parameter (constant value, param name, texture path, etc.)
    OpResult set_param(uint32_t node_id, const std::string& key,
                       const nlohmann::json& value);

    // --- Compound Operations ---

    // Insert a node between two connected nodes.
    // Disconnects from_node.output → to_node.input,
    // connects from_node.output → new_node.input[0],
    // connects new_node.output[0] → to_node.input.
    OpResult insert_between(const std::string& new_type,
                            uint32_t from_node, int32_t from_output,
                            uint32_t to_node, int32_t to_input);

    // Replace a node with another type, re-wiring compatible connections.
    OpResult replace_node(uint32_t old_node_id,
                          const std::string& new_type);

    // Expose a node's output as a material parameter.
    // Inserts a ParamNode upstream if one doesn't exist.
    OpResult expose_parameter(uint32_t node_id, int32_t output_index,
                              const std::string& param_name);

    // --- Query Operations ---

    // Get full graph state as JSON (same as shader_graph.getGraph).
    OpResult query_graph() const;

    // Get a node's details (type, pins, connections, params).
    OpResult query_node(uint32_t node_id) const;

    // Find nodes by type name.
    OpResult find_nodes(const std::string& type_name) const;

    // List all registered node types with their categories and pin descs.
    OpResult list_node_types() const;

    // Get the connection feeding a specific input pin.
    OpResult query_connection(uint32_t node_id, int32_t input_index) const;

    // --- Validation ---

    // Validate entire graph (DAG + types). Returns errors if any.
    OpResult validate() const;

private:
    ShaderGraph& graph_;
    NodeRegistry& registry_;
};
```

### 7.2 Design Principles

1. **Atomic + Compound**: Atomic operations (create, delete, connect, disconnect) are
   the primitives. Compound operations (insert_between, replace_node, expose_parameter)
   are convenience methods built on atomics. A visual UI would call the same atomics.

2. **Validation on mutation**: Every `connect_pins()` call validates type compatibility
   before connecting. Every `delete_node()` auto-disconnects all related connections.
   The graph is always in a valid state after any operation.

3. **Result protocol**: Every operation returns `OpResult` with success/error status
   and optional JSON data. This maps directly to IPC responses.

4. **Query operations**: The control layer also provides graph introspection, so
   external systems (prompts, MCP tools, future UI) can query the graph state
   before deciding what operations to issue.

### 7.3 Event System

```cpp
// graph_ops.h (continued)
enum class GraphEventType {
    NodeAdded,
    NodeRemoved,
    ConnectionAdded,
    ConnectionRemoved,
    ParamChanged,
    GraphValidated,
    GraphCompiled,
};

struct GraphEvent {
    GraphEventType type;
    nlohmann::json data;  // Event-specific payload
};

using GraphEventCallback = std::function<void(const GraphEvent&)>;

// GraphOps emits events on every mutation — listeners can trigger
// auto-compilation, push IPC events, update preview, etc.
void GraphOps::set_event_listener(GraphEventCallback cb);
```

## 8. Prompt Command Interface (C++ + TS)

The Prompt Command Interface translates structured commands into GraphOps calls.
It sits between external input (IPC, MCP, chat) and the Graph Control Layer.

### 8.1 Command Protocol

Commands are structured JSON objects. Each command maps to one or more GraphOps calls.

```cpp
// shader_graph/commands/graph_command.h
enum class CommandType {
    ADD_NODE,
    DELETE_NODE,
    CONNECT,
    DISCONNECT,
    SET_PARAM,
    INSERT_BETWEEN,
    REPLACE_NODE,
    EXPOSE_PARAM,
    COMPILE,
    QUERY,
    BATCH,          // Execute multiple commands atomically
};

struct GraphCommand {
    CommandType type;
    nlohmann::json params;     // Command-specific parameters
};

struct CommandResult {
    bool ok;
    std::string error;
    nlohmann::json data;
    std::vector<GraphEvent> events;  // Events emitted during execution
};
```

### 8.2 Command Examples

```jsonc
// ADD_NODE — create a node
{ "type": "ADD_NODE", "params": { "nodeType": "Multiply" } }
→ { "ok": true, "data": { "nodeId": 5 } }

// CONNECT — wire two pins
{ "type": "CONNECT", "params": { "from": 3, "fromOutput": 0, "to": 5, "toInput": 0 } }
→ { "ok": true }

// INSERT_BETWEEN — insert a node in an existing connection
{ "type": "INSERT_BETWEEN", "params": {
    "nodeType": "Multiply",
    "from": 2, "fromOutput": 0,
    "to": 1, "toInput": 0
  }
}
→ { "ok": true, "data": { "nodeId": 6 } }

// EXPOSE_PARAM — make a value editable at the material instance level
{ "type": "EXPOSE_PARAM", "params": { "nodeId": 3, "outputIndex": 0, "paramName": "Roughness" } }
→ { "ok": true }

// BATCH — execute multiple commands atomically (all-or-nothing)
{ "type": "BATCH", "params": { "commands": [
    { "type": "ADD_NODE", "params": { "nodeType": "Lerp" } },
    { "type": "CONNECT", "params": { "from": 2, "fromOutput": 0, "to": "$0", "toInput": 0 } },
    { "type": "CONNECT", "params": { "from": 3, "fromOutput": 0, "to": "$0", "toInput": 1 } }
  ]
}}
→ { "ok": true, "data": { "results": [...] } }
// Note: "$0" references the nodeId from command[0] in the batch

// QUERY — introspect graph state
{ "type": "QUERY", "params": { "query": "graph" } }
→ { "ok": true, "data": { "nodes": [...], "connections": [...] } }

{ "type": "QUERY", "params": { "query": "nodeTypes" } }
→ { "ok": true, "data": { "types": [{ "name": "Add", "category": "Math", "inputs": [...], "outputs": [...] }, ...] } }

// COMPILE — compile the current graph
{ "type": "COMPILE", "params": {} }
→ { "ok": true, "data": { "vs_code": "...", "ps_code": "...", "errors": [] } }
```

### 8.3 CommandExecutor

```cpp
// shader_graph/commands/command_executor.h
class CommandExecutor {
public:
    CommandExecutor(GraphOps& ops, HlslTranslator& translator);

    // Execute a single command
    CommandResult execute(const GraphCommand& cmd);

    // Execute a batch of commands with rollback on failure
    CommandResult execute_batch(const std::vector<GraphCommand>& cmds);

    // Parse command from JSON (IPC input)
    static GraphCommand parse(const nlohmann::json& j);

private:
    GraphOps& ops_;
    HlslTranslator& translator_;

    // Batch variable resolution: "$0" → nodeId from command[0]
    nlohmann::json resolve_refs(const nlohmann::json& params,
                                 const std::vector<CommandResult>& prev) const;
};
```

### 8.4 IPC Integration

```jsonc
// New IPC handlers (via WebSocket)

// Execute a single command
{ "method": "shader_cmd.execute", "params": { "path": "res://materials/stone.uzshader", "command": { "type": "ADD_NODE", "params": { "nodeType": "Multiply" } } } }
→ { "ok": true, "data": { "nodeId": 5 }, "events": [...] }

// Execute a batch
{ "method": "shader_cmd.batch", "params": { "path": "...", "commands": [...] } }
→ { "ok": true, "data": { "results": [...] }, "events": [...] }

// Query graph (convenience wrapper)
{ "method": "shader_cmd.query", "params": { "path": "...", "query": "graph" } }
→ { "ok": true, "data": { ... } }
```

### 8.5 Prompt Translation Flow

```
User prompt: "Add a Multiply node between TextureSample and MaterialOutput"
                │
                ▼
Prompt → Parse intent → Identify:
    - Action: INSERT_BETWEEN
    - Node type: "Multiply"
    - Source: node of type "TextureSample" (find by query)
    - Target: node of type "MaterialOutput" (find by query)
                │
                ▼
Generate commands:
    1. QUERY { query: "findNodes", nodeType: "TextureSample" } → nodeId=2
    2. QUERY { query: "findNodes", nodeType: "MaterialOutput" } → nodeId=1
    3. QUERY { query: "connection", nodeId: 1, inputIndex: 0 } → from=2, output=0
    4. INSERT_BETWEEN { nodeType: "Multiply", from: 2, fromOutput: 0, to: 1, toInput: 0 }
                │
                ▼
Execute via CommandExecutor → Graph updated → Auto-compile → Preview updated
```

Note: The prompt-to-command translation is designed as a modular layer. Phase 5
implements the structured command protocol and executor. Natural language parsing
(LLM-powered) is a future extension that produces the same commands.

## 9. UzScene Preview Environment

The UzScene Preview Environment is the visual output stage of the shader pipeline.
It is **fully decoupled** from the graph logic — it only receives compiled shaders
and applies them to preview meshes. This ensures clean architecture separation.

### 9.1 Design Principle: Decoupled Receiver

```
                      BOUNDARY
                         │
  Graph Logic            │   Preview Environment
  ─────────────          │   ────────────────────
  ShaderGraph     ──────►│   UzScenePreview
  GraphOps               │     - receives CompileResult
  CommandExecutor        │     - applies shader to mesh
  HlslTranslator        │     - renders scene
                         │     - streams frame
                         │
  The preview system     │   The preview system
  NEVER reads graph      │   ONLY consumes:
  structure, node IDs,   │     - compiled HLSL (string)
  or connections.        │     - material parameters (JSON)
                         │     - mesh selection (enum)
```

### 9.2 UzScenePreview Class

```cpp
// src/engine/renderer/preview/uz_scene_preview.h
class UzScenePreview {
public:
    enum class PreviewMesh { Sphere, Cube, Plane };

    // Lifecycle
    void initialize();        // Create preview scene, allocate viewport
    void shutdown();          // Destroy scene, release viewport
    bool is_active() const;

    // Scene setup (called once at init, meshes generated procedurally)
    void create_preview_scene();   // Sphere + DirectionalLight + Camera + Sky
    void set_preview_mesh(PreviewMesh mesh);

    // Shader application (called on every successful compilation)
    void apply_compiled_shader(const std::string& vs_hlsl,
                               const std::string& ps_hlsl,
                               const std::vector<std::string>& texture_params,
                               const std::vector<std::string>& scalar_params);

    // Material parameter tweaking (no recompilation needed)
    void set_scalar_param(const std::string& name, float value);
    void set_vector_param(const std::string& name, float r, float g, float b);
    void set_texture_param(const std::string& name, const std::string& path);

    // Camera control
    void set_camera_orbit(float yaw, float pitch, float distance);
    void set_camera_auto_rotate(bool enabled);

    // Frame output
    ViewportId viewport_id() const;   // For viewport streaming to extension

private:
    // Scene (in-memory, never written to disk)
    std::unique_ptr<Scene> scene_;
    uint32_t mesh_node_id_ = 0;
    uint32_t camera_node_id_ = 0;
    uint32_t light_node_id_ = 0;
    uint32_t sky_node_id_ = 0;

    // GPU resources
    GpuMesh sphere_mesh_, cube_mesh_, plane_mesh_;
    PreviewMesh current_mesh_ = PreviewMesh::Sphere;

    // Material (dynamic, receives compiled shader)
    std::unique_ptr<MaterialInstance> preview_material_;
    RhiPipeline preview_pso_ = 0;       // Current compiled PSO
    RhiPipeline fallback_pso_ = 0;      // Last working PSO (error fallback)

    // Viewport
    ViewportId viewport_id_ = 0;

    // Internal
    void rebuild_pso(const std::string& vs, const std::string& ps);
    void swap_mesh_geometry(PreviewMesh mesh);
};
```

### 9.3 Preview Scene Structure

The preview scene is created programmatically at initialization (never loaded from disk):

```
Root
 ├── PreviewMesh (StaticMesh)
 │     - Generated sphere/cube/plane via mesh_primitives.h
 │     - Material: preview_material_ (dynamic, receives compiled shader)
 │     - Position: origin (0, 0, 0)
 │
 ├── PreviewCamera (Camera)
 │     - Orbit mode (auto-rotate optional)
 │     - Default: 45° pitch, 2.5m distance
 │     - FOV: 60°, near: 0.1, far: 100
 │
 ├── Sun (DirectionalLight)
 │     - Direction: (0.5, -0.7, 0.3) normalized
 │     - Color: warm white (1.0, 0.95, 0.9)
 │     - Intensity: 3.0
 │     - Shadow: 2048x2048 cascade
 │
 └── Sky (SkyAtmosphere)
       - Neutral studio HDRI colors
       - IBL baked via existing IblBaker
       - Provides irradiance + prefiltered cubemaps for PBR
```

### 9.4 Reused Infrastructure

The UzScene preview builds entirely on existing systems:

| Existing System | Usage in Preview |
|----------------|-----------------|
| `mesh_primitives.h` | `generate_sphere()`, `generate_cube()`, `generate_plane()` |
| `Scene` + `SceneNode` | In-memory scene graph (no .uzscene file) |
| `CameraComponent` | `camera_update_orbit()` for turntable camera |
| `LightComponent` | `light_collect_primary()` for directional sun |
| `SkyAtmosphereData` | Neutral studio sky colors |
| `IblBaker` | GPU-baked irradiance + prefiltered cubemaps |
| `ViewportRegistry` | `ViewportType::Material` allocation |
| `StaticMeshRenderer` | Renders preview mesh with PBR pipeline |
| `MaterialInstance` | Dynamic parameter overrides without recompilation |
| `ShaderLibrary` | PSO creation from compiled HLSL |

### 9.5 IPC Protocol — Preview Commands

```jsonc
// Create/start preview viewport
{ "method": "shader_graph.preview", "params": { "path": "res://materials/stone.uzshader", "mesh": "sphere" } }
→ { "ok": true, "viewportId": 3 }

// Change preview mesh
{ "method": "shader_graph.setPreviewMesh", "params": { "viewportId": 3, "mesh": "cube" } }
→ { "ok": true }

// Tweak material parameter (no recompile)
{ "method": "shader_graph.setPreviewParam", "params": { "viewportId": 3, "key": "Roughness", "value": 0.7 } }
→ { "ok": true }

// Set preview camera
{ "method": "shader_graph.setPreviewCamera", "params": { "viewportId": 3, "yaw": 45, "pitch": 30, "distance": 3.0 } }
→ { "ok": true }

// Stop preview
{ "method": "shader_graph.stopPreview", "params": { "viewportId": 3 } }
→ { "ok": true }
```

## 10. Live Shader Update System

The Live Shader Update System connects the compilation pipeline to the UzScene preview.
It listens for graph events, triggers recompilation, and pushes results to the preview.

### 10.1 Update Pipeline

```
Graph mutation (via GraphOps / CommandExecutor)
    │
    ▼
GraphEvent emitted (NodeAdded, ConnectionAdded, ParamChanged, etc.)
    │
    ▼
ShaderUpdateController (event listener on GraphOps)
    │
    ├── Debounce: coalesce rapid mutations (100ms window)
    │
    ├── Validate: GraphOps::validate() — check DAG + types
    │   └── On error → push shader_graph.diagnostics event, skip compile
    │
    ├── Compile: HlslTranslator::compile(graph)
    │   └── On error → push shader_graph.diagnostics, keep fallback shader
    │
    ├── Build PSO: ShaderLibrary::get_or_create_pipeline()
    │   └── On error → push shader_graph.diagnostics, keep fallback PSO
    │
    └── Apply: UzScenePreview::apply_compiled_shader()
        └── Atomically swap preview material + PSO
            Push shader_graph.compiled event (success)
```

### 10.2 ShaderUpdateController

```cpp
// src/engine/renderer/preview/shader_update_controller.h
class ShaderUpdateController {
public:
    ShaderUpdateController(GraphOps& ops,
                           HlslTranslator& translator,
                           UzScenePreview& preview);

    // Start listening for graph events
    void start();

    // Stop listening (cleanup)
    void stop();

    // Manual trigger (e.g., from COMPILE command)
    CompileResult compile_and_apply();

    // Callback for IPC push events
    using DiagnosticsCallback = std::function<void(const std::vector<std::string>&)>;
    void set_diagnostics_callback(DiagnosticsCallback cb);

private:
    GraphOps& ops_;
    HlslTranslator& translator_;
    UzScenePreview& preview_;

    // Debounce state
    std::chrono::steady_clock::time_point last_event_;
    bool compile_pending_ = false;

    // Fallback
    std::string last_good_vs_;
    std::string last_good_ps_;

    void on_graph_event(const GraphEvent& event);
    void do_compile_cycle();
};
```

### 10.3 Error Handling

| Error Stage | Behavior |
|-------------|----------|
| Graph validation fails | Push diagnostics with node IDs, skip compilation, keep current preview |
| HLSL generation fails | Push diagnostics with node context, keep fallback shader |
| PSO creation fails (D3DCompile error) | Push full compiler error message, keep fallback PSO |
| All succeed | Atomically swap to new shader, push `shader_graph.compiled` event |

### 10.4 Push Events (Engine → Extension)

```jsonc
// Compilation diagnostics (errors/warnings)
{ "event": "shader_graph.diagnostics", "data": {
    "path": "res://materials/stone.uzshader",
    "errors": [
        { "nodeId": 5, "message": "Type mismatch: Float3 cannot connect to Texture2D" }
    ],
    "warnings": []
}}

// Successful compilation
{ "event": "shader_graph.compiled", "data": {
    "path": "res://materials/stone.uzshader",
    "compile_ms": 12,
    "vs_lines": 45,
    "ps_lines": 82,
    "params": { "textures": ["BaseColorMap"], "scalars": ["Roughness", "Metallic"] }
}}
```

### 10.5 Performance Requirements

- **Debounce window**: 100ms — coalesces rapid edits (e.g., batch commands)
- **Compile budget**: <50ms for typical graphs (≤30 nodes)
- **PSO creation**: Async if possible; synchronous fallback with <100ms target
- **Frame continuity**: Preview never shows a blank frame — fallback shader always active
- **No editor blocking**: Compilation runs on a background thread; IPC responses return immediately

## 11. Extensibility Points (Designed For, Not Implemented)

### 11.1 Material Instances
- `ShaderGraph` produces a "parameter manifest" during compilation
- `MaterialInstance` overrides those parameters without recompilation
- Already supported by existing `MaterialInstance` system

### 11.2 Subgraphs / Shader Functions
- A `SubgraphNode` would reference another `.uzshader` file
- During compilation, inline the subgraph's code chunks
- Requires: recursive compilation, parameter forwarding

### 11.3 Custom HLSL Nodes
- A `CustomHlslNode` with a text field for raw HLSL
- Declares inputs/outputs via UI
- Compiles by wrapping the custom code in a function

### 11.4 Shader Permutations
- Static switch nodes (bool) create shader variants
- Compilation generates one variant per switch combination
- PSO cache keyed by (graph_hash, switch_bitmask)

### 11.5 Optimization Passes
- Dead code elimination (nodes not connected to output)
- Constant folding (all-constant subgraphs → single constant)
- Common subexpression elimination (already via expr_cache_)

### 11.6 Visual Node Editor UI
- Canvas-based webview in VSCode (CustomTextEditorProvider for `.uzshader`)
- Node rendering, connection drawing, zoom/pan/select, context menu
- Calls the same `GraphOps` API via IPC — no new backend code needed
- The Graph Control Layer is designed to be the backend for both prompt-driven
  and visual interaction models

## 12. Integration with Existing Systems

### 12.1 ShaderLibrary
- Compiled shader graphs register as named shaders in `ShaderLibrary`
- PSOs created via existing `get_or_create_pipeline()` with caching
- Hot-reload: recompile graph → re-register shader → invalidate PSO cache entry

### 12.2 Material System
- `.uzmat` files can reference a `.uzshader` graph instead of a built-in shader name
- `Material.shader_name` → `"graph:res://materials/stone.uzshader"`
- MaterialManager loads graph, compiles, registers shader automatically

### 12.3 G-Buffer / Deferred Pipeline
- Graph-compiled shaders output to G-Buffer MRT (same as `gbuffer_write.ps.hlsl`)
- MaterialOutput pins map to G-Buffer channels:
  - BaseColor → RT0.rgb
  - Metallic → RT2.r
  - Roughness → RT2.g
  - Normal → RT1.rg (octahedral)
  - Emission → RT3.rgb * emissionEnergy
  - AO → RT0.a

### 12.4 IPC Handlers
- New handlers registered in `src/editor/ws_handlers.cpp`
- Follows existing pattern: `ipcRegistry.add("shader_graph.compile", ...)`
- All handlers documented in `docs/API.md`
