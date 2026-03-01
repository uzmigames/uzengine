# AI-Native Features Spec

## Overview

UzEngine is designed from the ground up so that an LLM agent can create, modify, and iterate on game content with zero manual GUI interaction. This document specifies the AI-native workflows layered on top of the IPC/MCP server (already implemented in Phase 0) to enable six key AI-driven capabilities:

1. **Prompt-to-Scene** — natural language generates a `.uzscene` file
2. **Shader co-authoring** — Claude writes HLSL, user saves, hot-reloads, and uses screenshot feedback
3. **Natural language property editing** — LLM parses intent and generates IPC calls
4. **Scene analysis** — AI reads scene state and suggests improvements
5. **Game logic scaffolding** — LLM writes `.lua` for a selected node
6. **General AI context API** — standardized JSON context for any LLM agent

All features are Phase 2 additions to the MCP server — they require no changes to the C++ engine. They are implemented as new MCP tools and VSCode commands in the extension.

---

## 1. Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│  Claude Code / LLM Agent (external)                          │
│  uses MCP tools: scene_generate, script_generate,            │
│  scene_analyze, shader_assist, ai_context, ai_suggest        │
└─────────────────────┬────────────────────────────────────────┘
                      │ MCP protocol (stdio / SSE)
                      ▼
┌──────────────────────────────────────────────────────────────┐
│  UzEngine MCP Server (Node.js, extends Phase 0 server)       │
│  tools: ping, status, screenshot, setRotation, setBackground │
│  NEW:   scene_generate, script_generate, scene_analyze,      │
│         shader_assist, ai_context, ai_suggest                │
└─────────────────────┬────────────────────────────────────────┘
                      │ JSON IPC (WebSocket)
                      ▼
┌──────────────────────────────────────────────────────────────┐
│  UzEngine.exe Runtime                                        │
│  IPC handlers: scene.*, ai.context, ai.suggest,             │
│  editor.analyzeScene                                         │
└──────────────────────────────────────────────────────────────┘
```

The MCP server acts as an intermediary: it receives high-level tool calls from the LLM agent, may call external LLM APIs (via user-configured API key), and translates results into IPC calls to the engine.

---

## 2. Prompt-to-Scene

### User Flow

```
User → Claude Code:
  "Create a forest clearing scene with a campfire, a pine tree ring,
   ambient fog, and a sunset directional light."

Claude Code → MCP tool: scene_generate
  { "description": "...", "base_scene"?: "res://scenes/empty.uzscene" }

MCP server → LLM (Claude API):
  System prompt: [UzEngine .uzscene format specification]
  User: "Generate a .uzscene for: forest clearing..."

LLM → MCP server: [.uzscene text]

MCP server → saves temp file → IPC: scene.load
  Engine loads scene → viewport shows result

MCP server → returns: { "scene_path": "res://scenes/generated_forest.uzscene",
                         "node_count": 12, "screenshot": "<base64>" }

Claude Code → shows screenshot to user + path
```

### MCP Tool: `scene_generate`

```typescript
// MCP tool definition (mcp-server/tools/scene_generate.ts)
{
  name: "scene_generate",
  description: "Generate a .uzscene file from a natural language description and load it in the engine",
  inputSchema: {
    type: "object",
    properties: {
      description: {
        type: "string",
        description: "Natural language description of the scene to generate"
      },
      base_scene: {
        type: "string",
        description: "Optional res:// path to a base scene to extend (default: empty scene)"
      },
      output_path: {
        type: "string",
        description: "Where to save the generated .uzscene (default: res://scenes/generated.uzscene)"
      }
    },
    required: ["description"]
  }
}
```

### MCP Tool Handler

```typescript
async function handle_scene_generate(args: SceneGenerateArgs): Promise<ToolResult> {
    // 1. Get full AI context (current project assets, node types, etc.)
    const ctx = await ipc.call('ai.context', {});

    // 2. Build prompt for LLM
    const system_prompt = build_scene_system_prompt(ctx);
    const user_prompt   = `Generate a .uzscene file for: ${args.description}`;

    // 3. Call LLM (Claude API or user-configured)
    const llm_response = await llm.complete({ system: system_prompt, user: user_prompt });

    // 4. Extract .uzscene text from response (markdown fencing stripped)
    const scene_text = extract_code_block(llm_response, 'ini');

    // 5. Validate basic structure before saving
    validate_uzscene_syntax(scene_text);  // throws on error

    // 6. Write to project
    const out_path = args.output_path ?? 'res://scenes/generated.uzscene';
    await fs.writeFile(resolve_res_path(out_path), scene_text, 'utf8');

    // 7. Load in engine
    await ipc.call('scene.load', { path: out_path });

    // 8. Screenshot for feedback
    const screenshot = await ipc.call('engine.screenshot', {});

    return {
        content: [{
            type: "text",
            text: JSON.stringify({ scene_path: out_path, screenshot: screenshot.base64 })
        }]
    };
}
```

### LLM System Prompt (Scene Generation)

The system prompt injected by the MCP server contains:
- Full `.uzscene` format specification (from `docs/specs/scene-system.md`)
- Available node types and their properties
- List of assets in the current project (from `ai.context`)
- Output rules: only output the `.uzscene` text, fenced in ` ```ini `, no prose

---

## 3. Shader Co-Authoring

### User Flow

```
User → Claude Code:
  "Write a cel-shading HLSL pixel shader for the barrel material."

Claude Code → MCP tool: shader_assist
  { "description": "cel shading for barrel", "target": "res://shaders/barrel.hlsl" }

MCP server → LLM: [HLSL shader specification + existing shader template]

LLM → MCP server: [.hlsl text]

MCP server → saves res://shaders/barrel.hlsl
            → IPC: shader.reload { path: "res://shaders/barrel.hlsl" }
            → waits 500ms for hot-reload
            → IPC: engine.screenshot {}
            → returns { shader_path, screenshot, errors?: [...] }

Claude Code → shows result
  User: "The toon bands are too sharp. Smooth them out."
  → Iterates with shader_assist again, referencing previous file
```

### MCP Tool: `shader_assist`

```typescript
{
  name: "shader_assist",
  description: "Write or modify an HLSL shader file and hot-reload it in the engine, returning a screenshot",
  inputSchema: {
    type: "object",
    properties: {
      description: { type: "string" },
      target: { type: "string", description: "res:// path to .hlsl file to create/modify" },
      existing_code: { type: "string", description: "Current shader code (optional, auto-read if omitted)" }
    },
    required: ["description", "target"]
  }
}
```

### Iterative Feedback Loop

The LLM agent can iterate:
1. `shader_assist` → generate shader → screenshot
2. Agent sees screenshot → optionally calls `engine.screenshot` from a different angle
3. Agent calls `shader_assist` again with updated description
4. Repeat until satisfied

The MCP server automatically reads existing `.hlsl` content before calling the LLM (if `existing_code` is omitted), enabling edit-style refinement rather than full regeneration.

---

## 4. Natural Language Property Editing

### User Flow

```
User → VSCode command: UzEngine: AI Set Property
  Input box: "Make the sun light warmer and brighter, around golden hour"

Extension → MCP tool: ai_set_property
  { "description": "sun light warmer and brighter, golden hour",
    "node_id"?: 4  /* optional: currently selected node */ }

MCP server → ai.context → gets scene tree, node 4 = DirectionalLight
MCP server → LLM: "Given this scene, translate the description into IPC property calls"

LLM → JSON:
  [
    { "method": "scene.setProperty", "args": { "id": 4, "key": "color", "value": [1.0, 0.78, 0.42] } },
    { "method": "scene.setProperty", "args": { "id": 4, "key": "intensity", "value": 2.8 } }
  ]

MCP server → executes each IPC call in sequence
           → returns { applied_calls: [...], screenshot }
```

### MCP Tool: `ai_set_property`

```typescript
{
  name: "ai_set_property",
  description: "Apply natural language property changes to scene nodes via LLM interpretation",
  inputSchema: {
    type: "object",
    properties: {
      description: { type: "string" },
      node_id: { type: "number", description: "Target node ID (optional; uses selected node)" },
      apply: { type: "boolean", description: "Actually apply (default: true). Set false to preview IPC calls only." }
    },
    required: ["description"]
  }
}
```

---

## 5. Scene Analysis

### IPC Handler: `ai.analyzeScene`

The engine serializes the full scene into an AI-friendly JSON summary:

```
editor.analyzeScene  {}  → {
  "node_count": 24,
  "lights": [
    { "id": 4, "type": "DirectionalLight", "color": [1,0.9,0.8], "intensity": 1.5,
      "shadows": true, "cascades": 3 }
  ],
  "cameras": [
    { "id": 5, "fov": 75, "is_main": true }
  ],
  "mesh_nodes": [
    { "id": 2, "name": "Ground", "mesh": "res://assets/terrain.glb",
      "material": "res://materials/grass.uzmat", "triangle_count": 4096 }
  ],
  "scripts": [
    { "node_id": 8, "script": "res://scripts/player.lua" }
  ],
  "performance_warnings": [
    { "severity": "warn", "message": "Node 15 has no LOD — mesh has 45,000 triangles" },
    { "severity": "info", "message": "3 nodes share material 'wood.uzmat' — instancing possible" }
  ],
  "asset_summary": { "total_meshes": 8, "total_textures": 14, "total_scripts": 3 }
}
```

### MCP Tool: `scene_analyze`

```typescript
{
  name: "scene_analyze",
  description: "Analyze the current scene and optionally ask the LLM to suggest improvements",
  inputSchema: {
    type: "object",
    properties: {
      topic: {
        type: "string",
        description: "Analysis focus: 'performance', 'lighting', 'composition', 'all' (default)",
        enum: ["performance", "lighting", "composition", "scripts", "all"]
      },
      suggest: {
        type: "boolean",
        description: "If true, pass analysis to LLM for suggestions (default: true)"
      }
    }
  }
}
```

### Response Structure

```json
{
  "analysis": { /* raw editor.analyzeScene output */ },
  "suggestions": [
    {
      "priority": "high",
      "category": "performance",
      "description": "Node 'PropBarrel_12' uses a 45k-triangle mesh with no LOD. Consider adding LOD levels in the asset pipeline.",
      "ipc_actions": []
    },
    {
      "priority": "medium",
      "category": "lighting",
      "description": "The scene has no ambient occlusion. Adding a post-process AO pass would improve depth perception.",
      "ipc_actions": [
        { "method": "scene.setProperty", "args": { "id": 1, "key": "post_ao_enabled", "value": true } }
      ]
    }
  ]
}
```

---

## 6. Game Logic Scaffolding

### User Flow

```
User selects node "Enemy_Guard" in Scene Tree
User → VSCode command: UzEngine: Generate Script

Extension → MCP tool: script_generate
  {
    "node_id": 20,
    "description": "A guard that patrols between 4 waypoints, detects the player within 15m, chases them, and returns to patrol after losing sight for 5 seconds"
  }

MCP server → ai.context → full scene description + node 20 properties (NavMeshAgent attached, etc.)
MCP server → LLM: [Lua scripting spec + scene context]

LLM → Lua script:
  -- Guard patrol + detection logic
  ...

MCP server → saves to res://scripts/enemy_guard.lua
           → IPC: scene.setProperty { id: 20, key: "script", value: "res://scripts/enemy_guard.lua" }
           → returns { script_path, preview: "<first 20 lines>" }
```

### MCP Tool: `script_generate`

```typescript
{
  name: "script_generate",
  description: "Generate a Lua script for a scene node and attach it",
  inputSchema: {
    type: "object",
    properties: {
      node_id: { type: "number" },
      description: { type: "string" },
      output_path: { type: "string", description: "res:// path; default derived from node name" },
      attach: { type: "boolean", description: "Auto-attach script to node (default: true)" }
    },
    required: ["node_id", "description"]
  }
}
```

### LLM System Prompt (Script Generation)

The system prompt includes:
- Full Lua scripting API spec (from `docs/specs/lua-scripting.md`)
- Node properties for the target node (type, attached components)
- Names and types of nearby nodes in the scene tree (for reference)
- Scene-wide context: available NavMesh, physics, VFX assets
- Output rules: output only the Lua script, fenced in ` ```lua `

---

## 7. AI Context API

### IPC Handler: `ai.context`

Returns a comprehensive JSON description of the current engine state for LLM consumption:

```
ai.context  {}  → {
  "project": {
    "name": "MyGame",
    "version": "0.1.0",
    "current_scene": "res://scenes/level1.uzscene"
  },
  "scene_tree": { /* same as scene.getTree */ },
  "assets": {
    "meshes":    ["res://meshes/barrel.glb", ...],
    "textures":  ["res://textures/wood.png", ...],
    "materials": ["res://materials/wood.uzmat", ...],
    "scripts":   ["res://scripts/player.lua", ...],
    "effects":   ["res://fx/explosion.uzfx", ...]
  },
  "node_types": [
    "Node", "Node3D", "StaticMesh", "DirectionalLight", "PointLight",
    "SpotLight", "Camera", "ParticleEmitter", "NavMeshAgent", "Terrain", ...
  ],
  "engine_state": {
    "playing": false,
    "fps": 60,
    "selected_node_id": 8
  },
  "format_specs": {
    "uzscene": "/* condensed format spec */"
  }
}
```

### IPC Handler: `ai.suggest`

```
ai.suggest  { "topic": "lighting" }  → {
  "topic": "lighting",
  "current_state": { /* relevant subset of ai.context */ },
  "suggestions": [
    "Consider adding a second fill light from the opposite direction of the sun to reduce harsh shadows.",
    "The scene lacks a sky/environment light (IBL). Adding res://materials/sky.uzsky would improve indirect lighting.",
    "DirectionalLight intensity 1.5 is reasonable for daylight. For golden hour, try 2.5 with color [1.0, 0.78, 0.42]."
  ]
}
```

---

## 8. New MCP Tools Summary

| Tool | Phase | Description |
|------|-------|-------------|
| `scene_generate` | 2 | Prompt → `.uzscene` → load in engine → screenshot |
| `shader_assist` | 2 | Prompt → HLSL file → hot-reload → screenshot |
| `ai_set_property` | 2 | Natural language → IPC property calls |
| `scene_analyze` | 2 | Engine state → LLM suggestions |
| `script_generate` | 2 | Prompt + node context → Lua script → attach |
| `ai_context` | 2 | Comprehensive engine state JSON for any LLM tool |
| `ai_suggest` | 2 | Topic-scoped suggestions (lighting, perf, etc.) |

All tools wrap IPC handlers. The `ai.*` IPC handlers are implemented in C++ `src/ipc/ai_handlers.cpp`. The MCP tools themselves are TypeScript in `vscode-extension/mcp-server/tools/`.

---

## 9. IPC Handlers (New)

```
ai.context          {}  → { project, scene_tree, assets, node_types, engine_state }
ai.suggest          { topic }  → { topic, current_state, suggestions[] }
editor.analyzeScene {}  → { node_count, lights, cameras, mesh_nodes, scripts,
                             performance_warnings, asset_summary }
```

Note: `scene_generate`, `shader_assist`, `ai_set_property`, and `script_generate` are implemented entirely in the MCP server layer — they call external LLM APIs and then make standard IPC calls. They do not require new C++ IPC handlers beyond `ai.context` and `editor.analyzeScene`.

---

## 10. Configuration

AI features are configured in `project.uzproj`:

```toml
[ai]
# LLM API provider: "anthropic" | "openai" | "ollama" | "none"
provider        = "anthropic"

# API key (can also be set via UZ_AI_API_KEY env variable — preferred)
api_key         = ""

# Model to use for code generation (scenes, shaders, scripts)
code_model      = "claude-opus-4-6"

# Model to use for analysis and suggestions (cheaper model OK)
analysis_model  = "claude-sonnet-4-6"

# Ollama endpoint (if provider = "ollama")
ollama_endpoint = "http://localhost:11434"

# Whether to include screenshots in ai.context by default (adds latency)
context_screenshots = false
```

The `UZ_AI_API_KEY` environment variable always overrides `api_key` in the project file. The API key is never committed to source control — a warning is displayed if a non-empty `api_key` is found in a file tracked by git.

---

## 11. VSCode Commands

| Command | ID | Description |
|---------|----|-------------|
| Generate Scene | `uzengine.generateScene` | Prompt-to-scene dialog |
| AI Set Property | `uzengine.aiSetProperty` | Natural language property edit |
| Generate Script | `uzengine.generateScript` | Script scaffold for selected node |
| Analyze Scene | `uzengine.analyzeScene` | Scene analysis + suggestions panel |
| Shader Assist | `uzengine.shaderAssist` | Shader co-author for active .hlsl file |

All commands are accessible from the Command Palette and from the right-click context menu in the Scene Tree.

---

## 12. Design Principles

- **AI-friendly file formats**: Every format (`.uzscene`, `.uzmat`, `.uzfx`, `.uzanim_sm`, `.lua`) is plain text with a simple, predictable structure. LLMs can generate valid files from the spec alone.
- **Screenshot feedback loop**: After any AI-driven change, a screenshot is automatically returned so the agent can visually verify the result without human intervention.
- **Incremental editing**: Tools like `shader_assist` and `ai_set_property` read existing state before generating, enabling refinement rather than full regeneration.
- **No hallucination guards**: The MCP server validates generated content before applying it (syntax check for `.uzscene`, DXC compile check for HLSL, Lua syntax check for scripts). Invalid generated content returns an error with the raw LLM output for debugging.
- **Offline-capable**: All tools support `provider = "none"` mode, where the MCP tool returns the formatted prompt that would be sent to the LLM. This allows developers to use any LLM tool manually.
