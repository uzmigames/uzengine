# Shader Graph Editor — Usage Guide

This guide covers creating, editing, and compiling shader graphs via the IPC
command interface. Shader graphs produce HLSL shaders for the deferred G-Buffer
pipeline without writing code.

---

## Quick Start

### 1. Create a graph with a red material

```json
// shader_cmd.batch
{
  "commands": [
    { "type": "ADD_NODE", "params": { "nodeType": "MaterialOutput" } },
    { "type": "ADD_NODE", "params": { "nodeType": "Constant3" } },
    { "type": "SET_PARAM", "params": { "nodeId": "$1", "key": "value", "value": [1.0, 0.0, 0.0] } },
    { "type": "CONNECT", "params": { "fromNode": "$1", "fromOutput": 0, "toNode": "$0", "toInput": 0 } }
  ]
}
```

`$0` and `$1` resolve to the `nodeId` from the first and second command results.

### 2. Compile to HLSL

```json
// shader_cmd.execute
{ "type": "COMPILE" }
```

Returns `vertexHlsl` and `pixelHlsl` strings ready for the renderer.

---

## IPC Commands

All commands go through three IPC handlers:

| Handler | Purpose |
|---------|---------|
| `shader_cmd.execute` | Execute a single command |
| `shader_cmd.batch` | Execute multiple commands with rollback on failure |
| `shader_cmd.query` | Query graph state |

### Command Types

| Type | Params | Description |
|------|--------|-------------|
| `ADD_NODE` | `nodeType, x?, y?` | Create a node |
| `DELETE_NODE` | `nodeId` | Remove a node and its connections |
| `CONNECT` | `fromNode, fromOutput, toNode, toInput` | Connect two pins |
| `DISCONNECT` | `toNode, toInput` | Remove a connection |
| `SET_PARAM` | `nodeId, key, value` | Set a node parameter |
| `INSERT_BETWEEN` | `fromNode, fromOutput, toNode, toInput, nodeType` | Insert a node on an existing wire |
| `REPLACE_NODE` | `nodeId, newType` | Replace a node, re-wiring connections |
| `EXPOSE_PARAM` | `nodeId, inputIndex, name` | Create a material parameter for an input |
| `COMPILE` | _(none)_ | Compile graph to HLSL |
| `QUERY` | `query, ...` | Query graph structure |

### Query Sub-Commands

```json
// List all nodes
{ "type": "QUERY", "params": { "query": "graph" } }

// Get a specific node
{ "type": "QUERY", "params": { "query": "node", "nodeId": 1 } }

// Find nodes by type
{ "type": "QUERY", "params": { "query": "findNodes", "nodeType": "Constant3" } }

// List available node types
{ "type": "QUERY", "params": { "query": "nodeTypes" } }

// Check a connection
{ "type": "QUERY", "params": { "query": "connection", "toNode": 1, "toInput": 0 } }
```

---

## Available Node Types

### Output
- **MaterialOutput** — 8 PBR inputs: BaseColor, Metallic, Roughness, Normal, AO, Emission, EmissionEnergy, Displacement

### Constants
- **Constant1** — single float (`value: float`)
- **Constant3** — float3 (`value: [r, g, b]`)
- **Constant4** — float4 (`value: [r, g, b, a]`)

### Parameters (exposed to material instances)
- **ScalarParam** — `param_name: string, default_value: float`
- **VectorParam** — `param_name: string, default_value: [r, g, b]`
- **TextureParam** — `param_name: string, default_path: string`

### Math
- **Add**, **Subtract**, **Multiply**, **Divide** — binary float ops
- **Lerp** — `A, B, Alpha` → linear interpolation
- **Dot** — dot product
- **Normalize** — unit vector
- **Power** — `Base^Exp`

### Texture
- **TextureSample** — sample a Texture2D, outputs: RGBA, R, G, B, A

### Utility
- **Append** — combine scalars into wider vectors
- **ComponentMask** — extract components (param: `mask`, e.g. `"xy"`, `"rgb"`)
- **Time** — `_Time` engine time
- **UV** — mesh UV coordinates

---

## Recipes

### PBR Material with Textures

```json
{
  "commands": [
    { "type": "ADD_NODE", "params": { "nodeType": "MaterialOutput" } },
    { "type": "ADD_NODE", "params": { "nodeType": "TextureSample" } },
    { "type": "SET_PARAM", "params": { "nodeId": "$1", "key": "texture_path", "value": "assets/textures/brick_albedo.png" } },
    { "type": "CONNECT", "params": { "fromNode": "$1", "fromOutput": 0, "toNode": "$0", "toInput": 0 } },
    { "type": "ADD_NODE", "params": { "nodeType": "TextureSample" } },
    { "type": "SET_PARAM", "params": { "nodeId": "$4", "key": "texture_path", "value": "assets/textures/brick_normal.png" } },
    { "type": "CONNECT", "params": { "fromNode": "$4", "fromOutput": 0, "toNode": "$0", "toInput": 3 } },
    { "type": "EXPOSE_PARAM", "params": { "nodeId": "$0", "inputIndex": 1, "name": "Metallic" } },
    { "type": "EXPOSE_PARAM", "params": { "nodeId": "$0", "inputIndex": 2, "name": "Roughness" } },
    { "type": "COMPILE" }
  ]
}
```

### Emissive Pulsing Material

```json
{
  "commands": [
    { "type": "ADD_NODE", "params": { "nodeType": "MaterialOutput" } },
    { "type": "ADD_NODE", "params": { "nodeType": "Constant3" } },
    { "type": "SET_PARAM", "params": { "nodeId": "$1", "key": "value", "value": [0.0, 0.5, 1.0] } },
    { "type": "CONNECT", "params": { "fromNode": "$1", "fromOutput": 0, "toNode": "$0", "toInput": 5 } },
    { "type": "ADD_NODE", "params": { "nodeType": "Time" } },
    { "type": "ADD_NODE", "params": { "nodeType": "Multiply" } },
    { "type": "CONNECT", "params": { "fromNode": "$4", "fromOutput": 0, "toNode": "$5", "toInput": 0 } },
    { "type": "ADD_NODE", "params": { "nodeType": "Constant1" } },
    { "type": "SET_PARAM", "params": { "nodeId": "$7", "key": "value", "value": 3.0 } },
    { "type": "CONNECT", "params": { "fromNode": "$7", "fromOutput": 0, "toNode": "$5", "toInput": 1 } },
    { "type": "CONNECT", "params": { "fromNode": "$5", "fromOutput": 0, "toNode": "$0", "toInput": 6 } },
    { "type": "COMPILE" }
  ]
}
```

### ORM Texture Unpacking

```json
{
  "commands": [
    { "type": "ADD_NODE", "params": { "nodeType": "MaterialOutput" } },
    { "type": "ADD_NODE", "params": { "nodeType": "TextureSample" } },
    { "type": "SET_PARAM", "params": { "nodeId": "$1", "key": "texture_path", "value": "assets/textures/metal_orm.png" } },
    { "type": "CONNECT", "params": { "fromNode": "$1", "fromOutput": 1, "toNode": "$0", "toInput": 4 } },
    { "type": "CONNECT", "params": { "fromNode": "$1", "fromOutput": 2, "toNode": "$0", "toInput": 2 } },
    { "type": "CONNECT", "params": { "fromNode": "$1", "fromOutput": 3, "toNode": "$0", "toInput": 1 } },
    { "type": "COMPILE" }
  ]
}
```

R → AO (input 4), G → Roughness (input 2), B → Metallic (input 1).

---

## G-Buffer Output Mapping

The MaterialOutput node maps to the deferred G-Buffer MRTs:

| Input | G-Buffer RT | Format |
|-------|-------------|--------|
| BaseColor | RT0.rgb | RGBA8_UNORM |
| AO | RT0.a | RGBA8_UNORM |
| Normal | RT1.rgb | RGB10A2_UNORM |
| Metallic | RT2.r | RGBA8_UNORM |
| Roughness | RT2.g | RGBA8_UNORM |
| Emission | RT3.rgb | R11G11B10_FLOAT |
| EmissionEnergy | RT3 multiplier | — |
| Displacement | Vertex offset | — |

---

## Error Handling

- **Batch rollback**: if any command in a batch fails, all previous commands are undone.
- **Type validation**: connections between incompatible pin types are rejected.
- **Cycle detection**: connecting nodes that would form a cycle is rejected.
- **Compilation errors**: returned in the `errors` array of the compile result.

---

## File Format (.uzshader)

Shader graphs serialize to JSON:

```json
{
  "version": 1,
  "output_node_id": 1,
  "nodes": [
    { "id": 1, "type": "MaterialOutput", "pos_x": 400, "pos_y": 200, "inputs": [...] },
    { "id": 2, "type": "Constant3", "pos_x": 100, "pos_y": 200, "value": [1.0, 0.0, 0.0] }
  ],
  "connections": [
    { "from": 2, "fromOutput": 0, "to": 1, "toInput": 0 }
  ]
}
```

Use `ShaderGraphManager` to load/save these files programmatically.
