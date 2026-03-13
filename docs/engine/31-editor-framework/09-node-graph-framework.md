# Node-Graph Framework — Shared Foundation for Visual Editors

> **Status**: Planned (Phase 2–3)
> **Source**: `extension/editor/graph/graph-editor.ts`, `engine/core/graph.h`
> **References**: Material Editor (§10), Animation Controller (§11), Control Rig (§12), UE5 UEdGraph, Godot VisualShader

## Purpose

The Node-Graph Framework is the shared webview-based foundation for ALL visual editors in UzEngine (Material, VFX, Animation Controller, Behavior Tree, Procedural Content Generation). Rather than reimplementing graph UI, compilation, undo/redo, and live preview for each editor, all editors subclass `BaseGraphEditor` and specialize only node types, pin types, and compilation targets.

This design follows UE5's UEdGraph pattern, where Material, Blueprint, Niagara, and AnimBP all reuse the same graph infrastructure.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ BaseGraphEditor (TypeScript)                                    │
│                                                                 │
│  ├─ Graph Canvas                                                │
│  │  ├─ WebGL renderer for nodes/connections                     │
│  │  ├─ Zoom, pan, minimap                                       │
│  │  ├─ Virtual node rendering (1000+ nodes)                     │
│  │  └─ Selection, marquee, focus                                │
│  │                                                               │
│  ├─ Node Management                                              │
│  │  ├─ Create node (right-click menu / search)                  │
│  │  ├─ Delete, duplicate, cut/copy/paste                        │
│  │  ├─ Node arrangement (auto-layout, align, distribute)        │
│  │  └─ Serialization (.uzgraph JSON format)                     │
│  │                                                               │
│  ├─ Connection Management                                        │
│  │  ├─ Pin type checking (only compatible types connect)        │
│  │  ├─ Visual feedback (red X if type mismatch)                 │
│  │  └─ Multi-connection (1 input to N outputs)                  │
│  │                                                               │
│  ├─ Compilation Pipeline                                         │
│  │  ├─ Graph → Intermediate Representation                       │
│  │  ├─ IR → Target code (HLSL, state machine, etc.)            │
│  │  ├─ Incremental compilation (only changed nodes)             │
│  │  └─ Error reporting (inline on failing nodes)                │
│  │                                                               │
│  ├─ Live Preview                                                 │
│  │  ├─ Real-time viewport (material sphere, animation, etc.)    │
│  │  └─ Parameter changes update preview instantly               │
│  │                                                               │
│  └─ Undo/Redo                                                    │
│     └─ ICommand integration (node add, delete, connect, param)   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
          ▲
          │ subclass & override
          │
┌─────────────────────────────────────────────────────────────────┐
│ Specialized Editors                                             │
│                                                                 │
│  MaterialEditor         — Material node types & HLSL compilation │
│  AnimControllerEditor   — State/Blend Space types               │
│  VFXEditor              — Emitter nodes & particle simulation    │
│  BehaviorTreeEditor     — Task/Decorator/Composite types        │
│  PCGEditor              — Generator/Sampler types               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Node Structure

Every node in a graph has:

```typescript
interface GraphNode {
    /// Unique identifier (UUID, stable across edits)
    id: string;

    /// Display name + category (determines icon color)
    title: string;
    category: "Math" | "Texture" | "Utility" | "Custom" | string;

    /// Input pins (left side, accept connections from other nodes)
    inputs: GraphPin[];

    /// Output pins (right side, connect to other nodes' inputs)
    outputs: GraphPin[];

    /// Node-specific parameters (exposed in inspector)
    parameters: Record<string, any>;

    /// Position in graph canvas
    position: { x: number; y: number };

    /// Optional: preview thumbnail for texture/material nodes
    preview?: {
        texture: WebGLTexture;
        size: { width: number; height: number };
    };

    /// Metadata (title, description for tooltips)
    metadata: {
        description: string;
        examples: string[];
    };
}
```

## Pin System

Pins are the connection points on nodes. Each pin has a type and direction:

```typescript
interface GraphPin {
    /// Unique ID within node
    id: string;

    /// User-friendly name ("Color", "Opacity", etc.)
    name: string;

    /// Pin type (controls compatibility)
    type: PinType;

    /// True = input, False = output
    isInput: boolean;

    /// Connected pin IDs (can have multiple outputs → one input)
    connections: string[];

    /// Optional: default value if not connected
    defaultValue?: any;

    /// Optional: visibility toggle (hide unused pins)
    hidden?: boolean;
}

/// Pin types — defines what can connect to what
enum PinType {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Texture,
    Bool,
    Int,
    Color,
    Matrix,
    Enum,
    Any  // Connects to anything (rare)
}

/// Type compatibility matrix
const typeCompatibility: Map<[PinType, PinType], boolean> = new Map([
    [[PinType.Float, PinType.Float], true],
    [[PinType.Float, PinType.Vec2], false],  // Float can't implicitly promote to Vec2
    [[PinType.Vec3, PinType.Color], true],   // Both RGBA, same memory layout
    [[PinType.Any, PinType.Any], true],      // Any connects to any
]);
```

## Graph Operations

### Create Node

Users create nodes via right-click context menu (categorized) or search:

```typescript
class GraphEditor {
    /// Display right-click menu at mouse position
    onContextMenu(event: MouseEvent) {
        const menu = {
            "Math": [
                { label: "Add", factory: () => new AddNode() },
                { label: "Multiply", factory: () => new MultiplyNode() },
                { label: "Lerp", factory: () => new LerpNode() },
            ],
            "Texture": [
                { label: "Sample Texture", factory: () => new TextureSampleNode() },
                { label: "UV Tile", factory: () => new UVTileNode() },
            ],
            "Utility": [
                { label: "Constant", factory: () => new ConstantNode() },
                { label: "Time", factory: () => new TimeNode() },
            ],
        };

        showContextMenu(menu, event.clientX, event.clientY, (factory) => {
            const node = factory();
            node.position = this.screenToWorld(event.clientX, event.clientY);
            this.addNode(node);
            this.recordUndo("Add node");
        });
    }

    /// Search for nodes (Ctrl+Shift+A)
    onSearchNodes() {
        const results = searchAllNodes("");
        showSearchDialog(results, (factory) => {
            const node = factory();
            node.position = this.centerViewport();
            this.addNode(node);
        });
    }
}
```

### Delete & Duplicate

```typescript
class GraphEditor {
    /// Delete selected nodes and any orphaned connections
    deleteSelection() {
        for (const nodeId of this.selection) {
            // Remove all connections to/from this node
            this.connections = this.connections.filter(
                conn => conn.fromNode !== nodeId && conn.toNode !== nodeId
            );
            this.nodes.delete(nodeId);
        }
        this.recordUndo("Delete nodes");
        this.compile();
    }

    /// Duplicate selected nodes (preserving internal connections)
    duplicateSelection() {
        const oldToNewId = new Map<string, string>();
        const newNodes: GraphNode[] = [];

        for (const nodeId of this.selection) {
            const oldNode = this.nodes.get(nodeId)!;
            const newNode = structuredClone(oldNode);
            newNode.id = generateUUID();
            newNode.position.x += 50;  // Offset slightly
            newNode.position.y += 50;

            oldToNewId.set(nodeId, newNode.id);
            newNodes.push(newNode);
        }

        // Recreate internal connections with new IDs
        for (const conn of this.connections) {
            if (oldToNewId.has(conn.fromNode) && oldToNewId.has(conn.toNode)) {
                const newConn = structuredClone(conn);
                newConn.fromNode = oldToNewId.get(conn.fromNode)!;
                newConn.toNode = oldToNewId.get(conn.toNode)!;
                this.connections.push(newConn);
            }
        }

        for (const node of newNodes) {
            this.addNode(node);
        }
        this.recordUndo("Duplicate nodes");
    }
}
```

### Cut, Copy, Paste

```typescript
class GraphEditor {
    /// Copy selected nodes to clipboard (JSON)
    copy() {
        const nodes = this.selection.map(id => this.nodes.get(id));
        const connections = this.connections.filter(
            conn => this.selection.has(conn.fromNode) && this.selection.has(conn.toNode)
        );

        const clipboard = { nodes, connections };
        navigator.clipboard.writeText(JSON.stringify(clipboard));
    }

    /// Paste from clipboard
    paste() {
        const clipboard = JSON.parse(await navigator.clipboard.readText());

        // Remap IDs
        const idMap = new Map<string, string>();
        for (const node of clipboard.nodes) {
            const newId = generateUUID();
            idMap.set(node.id, newId);
            node.id = newId;
            node.position.x += 100;
            this.addNode(node);
        }

        // Remap connections
        for (const conn of clipboard.connections) {
            conn.fromNode = idMap.get(conn.fromNode)!;
            conn.toNode = idMap.get(conn.toNode)!;
            this.addConnection(conn);
        }

        this.recordUndo("Paste");
    }
}
```

## Node Arrangement

### Auto-Layout (Hierarchical Sugiyama Algorithm)

```typescript
/// Arrange selected nodes in hierarchical layers
arrangeAutoLayout() {
    // 1. Assign layers (DFS from root nodes)
    const layers = new Map<number, GraphNode[]>();
    const visited = new Set<string>();

    const assignLayer = (nodeId: string, layer: number) => {
        if (visited.has(nodeId)) return;
        visited.add(nodeId);

        if (!layers.has(layer)) layers.set(layer, []);
        layers.get(layer)!.push(this.nodes.get(nodeId)!);

        // Find downstream nodes
        for (const conn of this.connections) {
            if (conn.fromNode === nodeId && !visited.has(conn.toNode)) {
                assignLayer(conn.toNode, layer + 1);
            }
        }
    };

    for (const nodeId of this.selection) {
        assignLayer(nodeId, 0);
    }

    // 2. Position nodes in layers
    let y = 50;
    for (const [layer, nodes] of layers) {
        const x = layer * 200;
        let yOffset = y;
        for (const node of nodes) {
            node.position = { x, y: yOffset };
            yOffset += 150;
        }
        y = Math.max(y, yOffset);
    }

    this.recordUndo("Auto-layout");
}
```

### Align & Distribute

```typescript
class GraphEditor {
    alignLeft() {
        const minX = Math.min(...Array.from(this.selection).map(
            id => this.nodes.get(id)!.position.x
        ));
        for (const nodeId of this.selection) {
            this.nodes.get(nodeId)!.position.x = minX;
        }
        this.recordUndo("Align left");
    }

    alignHorizontally() {
        const positions = Array.from(this.selection).map(
            id => this.nodes.get(id)!.position.y
        ).sort((a, b) => a - b);

        const selected = Array.from(this.selection);
        const spacing = (positions[positions.length - 1] - positions[0]) / (selected.length - 1);

        for (let i = 0; i < selected.length; i++) {
            this.nodes.get(selected[i])!.position.y = positions[0] + i * spacing;
        }
        this.recordUndo("Distribute vertically");
    }
}
```

## Navigation

### Zoom & Pan

```typescript
class GraphCanvas {
    private zoom = 1.0;
    private pan = { x: 0, y: 0 };

    /// Mouse wheel zoom (centered at mouse position)
    onMouseWheel(event: WheelEvent) {
        const factor = event.deltaY > 0 ? 0.9 : 1.1;
        const before = this.screenToWorld(event.clientX, event.clientY);

        this.zoom *= factor;
        this.zoom = Math.max(0.1, Math.min(3.0, this.zoom));  // Clamp [0.1, 3.0]

        const after = this.screenToWorld(event.clientX, event.clientY);
        this.pan.x += (before.x - after.x);
        this.pan.y += (before.y - after.y);

        this.render();
    }

    /// Middle-click drag to pan
    onMouseDown(event: MouseEvent) {
        if (event.button === 1) {  // Middle button
            let lastX = event.clientX, lastY = event.clientY;

            const onMove = (moveEvent: MouseEvent) => {
                this.pan.x += (moveEvent.clientX - lastX) / this.zoom;
                this.pan.y += (moveEvent.clientY - lastY) / this.zoom;
                lastX = moveEvent.clientX;
                lastY = moveEvent.clientY;
                this.render();
            };

            const onUp = () => {
                document.removeEventListener('mousemove', onMove);
                document.removeEventListener('mouseup', onUp);
            };

            document.addEventListener('mousemove', onMove);
            document.addEventListener('mouseup', onUp);
        }
    }

    /// Convert screen coordinates to world coordinates
    screenToWorld(screenX: number, screenY: number) {
        const rect = this.canvas.getBoundingClientRect();
        return {
            x: (screenX - rect.left) / this.zoom - this.pan.x,
            y: (screenY - rect.top) / this.zoom - this.pan.y,
        };
    }
}
```

### Minimap

```typescript
class Minimap {
    private canvas: HTMLCanvasElement;
    private miniCanvas: HTMLCanvasElement;

    render(editor: GraphEditor) {
        // Compute minimap bounds
        const bounds = this.getGraphBounds(editor.nodes);
        const scale = Math.min(
            this.canvas.width / bounds.width,
            this.canvas.height / bounds.height,
            0.2  // Max 20% size
        );

        // Draw nodes as small rectangles
        const ctx = this.canvas.getContext('2d')!;
        ctx.fillStyle = '#1e1e1e';
        ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);

        for (const [, node] of editor.nodes) {
            ctx.fillStyle = node === this.hoveredNode ? '#44ff44' : '#888888';
            ctx.fillRect(
                (node.position.x - bounds.x) * scale,
                (node.position.y - bounds.y) * scale,
                20 * scale,
                20 * scale
            );
        }

        // Draw viewport rectangle
        ctx.strokeStyle = '#00ff00';
        ctx.lineWidth = 2;
        const viewBounds = this.getViewportBounds(editor);
        ctx.strokeRect(
            (viewBounds.x - bounds.x) * scale,
            (viewBounds.y - bounds.y) * scale,
            viewBounds.width * scale,
            viewBounds.height * scale
        );
    }
}
```

## Serialization Format

All graphs are saved as `.uzgraph` JSON files:

```json
{
  "version": 1,
  "metadata": {
    "title": "Material_Basic",
    "created": "2026-03-12T10:30:00Z",
    "modified": "2026-03-12T14:45:30Z",
    "author": "artist"
  },
  "nodes": [
    {
      "id": "node-001",
      "type": "AddNode",
      "title": "Add",
      "category": "Math",
      "position": { "x": 100, "y": 200 },
      "parameters": {
        "a": 0.5,
        "b": 0.5
      },
      "inputs": [
        { "id": "pin-001", "name": "A", "type": "Float", "connections": ["node-000/out-0"] },
        { "id": "pin-002", "name": "B", "type": "Float", "connections": [] }
      ],
      "outputs": [
        { "id": "pin-003", "name": "Result", "type": "Float", "connections": ["node-002/pin-001"] }
      ]
    }
  ],
  "connections": [
    {
      "id": "conn-001",
      "fromNode": "node-000",
      "fromPin": "out-0",
      "toNode": "node-001",
      "toPin": "pin-001"
    }
  ]
}
```

## Compilation Pipeline

The graph compilation follows a standard flow:

```
Graph (JSON)
    ↓
Parse & Validate
    ↓ (detect cycles, type errors)
Intermediate Representation (IR)
    ↓
Target Code Generation
    ↓ (HLSL, Lua script, etc.)
Compile (DXC, etc.)
    ↓
Load Runtime Asset
    ↓
Error Reporting (inline on nodes)
```

```cpp
/// C++ compilation pipeline
class GraphCompiler {
public:
    /// Compile .uzgraph to target code
    /// Returns error messages (node ID, message, severity)
    struct CompileResult {
        bool success;
        std::string targetCode;  // HLSL, Lua script, etc.
        struct Error {
            std::string nodeId;
            std::string message;
            int severity;  // 0=warning, 1=error
        };
        std::vector<Error> errors;
    };

    /// Parse graph JSON and compile to intermediate representation
    CompileResult Compile(const std::string& graphJson) {
        // 1. Parse JSON
        auto graph = nlohmann::json::parse(graphJson);

        // 2. Build node list and validate connections
        std::unordered_map<std::string, GraphNode*> nodes;
        for (const auto& nodeJson : graph["nodes"]) {
            auto node = CreateNodeFromType(nodeJson["type"]);
            nodes[nodeJson["id"]] = node;
        }

        // 3. Detect cycles (topological sort)
        std::vector<std::string> sorted;
        std::set<std::string> visited, visiting;
        for (const auto& [id, node] : nodes) {
            if (!visited.count(id)) {
                if (!topologicalSort(id, nodes, visited, visiting, sorted)) {
                    return { false, "", { { "", "Cycle detected in graph", 1 } } };
                }
            }
        }

        // 4. Compile each node in topological order
        std::string code;
        for (const auto& nodeId : sorted) {
            auto node = nodes[nodeId];
            code += node->CompileToCode();
        }

        return { true, code, {} };
    }
};
```

## Live Preview

Material and VFX editors show real-time preview in a viewport panel:

```typescript
class GraphEditor {
    private previewViewport: WebGLRenderingContext;
    private previewMesh: Mesh;
    private compilationDebounce: number = 0;

    /// Called when graph changes (debounced)
    onGraphChanged() {
        clearTimeout(this.compilationDebounce);
        this.compilationDebounce = setTimeout(() => {
            this.recompile();
        }, 300);  // Wait 300ms before recompiling
    }

    /// Recompile and reload preview
    async recompile() {
        const graphJson = JSON.stringify(this.graph);
        const result = await this.ipc.call('editor.compile_graph', [graphJson]);

        if (result.success) {
            // Load compiled shader/material into preview
            this.previewMaterial = await this.loadMaterial(result.targetCode);
            this.render();
        } else {
            // Show errors inline on nodes
            for (const error of result.errors) {
                const node = this.nodes.get(error.nodeId);
                node.hasError = true;
                node.errorMessage = error.message;
            }
            this.render();
        }
    }

    /// Render preview viewport
    render() {
        const gl = this.previewViewport;
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

        // Draw mesh with material
        this.previewMaterial.bind();
        this.previewMesh.render();
        this.previewMaterial.unbind();
    }
}
```

## Hot-Reload

When a graph file is saved on disk, the runtime automatically recompiles and updates:

```cpp
/// Runtime reaction to graph changes
class GraphHotReloadManager {
    void OnGraphFileChanged(const std::string& path) {
        auto graph = LoadGraphFromDisk(path);
        auto result = Compile(graph);

        if (result.success) {
            // Replace material/shader/animation in runtime
            auto material = GetMaterial(path);
            material->SetShaderCode(result.targetCode);
            material->Recompile();

            UZ_LOG(Editor, Info, "Hot-reloaded graph: {}", path);
        } else {
            UZ_LOG(Editor, Error, "Failed to compile graph: {}", result.errors[0].message);
        }
    }
};
```

## Virtual Node Rendering

For graphs with 1000+ nodes, rendering all nodes every frame is slow. Use virtual rendering:

```typescript
class VirtualGraphCanvas {
    private nodePool: GraphNodeUI[] = [];
    private visibleNodes = new Set<string>();

    render() {
        // Compute viewport bounds
        const viewportBounds = this.getViewportBounds();

        // Reset visible set
        this.visibleNodes.clear();

        // Only render nodes inside viewport (with padding)
        const padding = 200;
        for (const [id, node] of this.graph.nodes) {
            if (this.isInBounds(node.position, viewportBounds, padding)) {
                this.visibleNodes.add(id);
            }
        }

        // Reuse node UI elements from pool
        for (let i = 0; i < this.nodePool.length; i++) {
            if (i < this.visibleNodes.size) {
                this.nodePool[i].visible = true;
                this.nodePool[i].update(/* node data */);
            } else {
                this.nodePool[i].visible = false;
            }
        }

        // Request animation frame
        requestAnimationFrame(() => this.render());
    }
}
```

## Error Reporting

Failed compilations show errors inline on nodes:

```typescript
/// Highlight failing node with error message
renderNodeError(node: GraphNode, error: CompileError) {
    const nodeUI = this.getNodeUI(node.id);
    nodeUI.classList.add('error');

    // Add error tooltip
    const tooltip = document.createElement('div');
    tooltip.className = 'error-tooltip';
    tooltip.textContent = error.message;
    nodeUI.appendChild(tooltip);

    // Show error in console
    console.error(`[${node.title}] ${error.message}`);
}
```

## Performance Considerations

- **Incremental compilation**: Only recompile changed nodes and their dependents
- **Virtual rendering**: Render only visible nodes
- **Debounced recompilation**: Wait for user to stop editing before recompiling (300ms)
- **WebGL canvas**: Use hardware acceleration for graph rendering
- **Connection caching**: Cache pin positions to avoid re-layout on every frame

## Comparison

| Feature | UE5 (UEdGraph) | Godot (VisualShader) | Substance Designer | **UzEngine** |
|---|---|---|---|---|
| **Shared framework** | Yes (Blueprint, Material, AnimBP, Niagara) | Per-system | N/A | **Yes (all editors)** |
| **Node types** | Defined in UObject | Node class hierarchy | Plugin system | **Node factory + type registry** |
| **Compilation** | Incremental, caching | Real-time to GLSL | Incremental to SBSAR | **Incremental to target code** |
| **Virtual rendering** | Yes (for large graphs) | No | Yes | **Yes (1000+ nodes)** |
| **Undo/Redo** | FTransaction system | UndoRedo class | Full undo stack | **ICommand integration** |
| **Hot-reload** | On asset save | Automatic | Automatic | **FileWatcher integration** |
| **Performance** | 120 FPS (small graphs) | 30-60 FPS | 60 FPS | **Target: 120 FPS** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Basic graph canvas, node creation/deletion, pan/zoom, serialization, simple compilation |
| **Phase 3** | Connection type checking, undo/redo, live preview, minimap, virtual rendering, hot-reload |
| **Phase 4** | Subgraph nodes (collapse subgraph into single node), comment boxes, node search, performance profiling |

