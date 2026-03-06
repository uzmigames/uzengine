# Proposal: add-mcp-primitives

## Why
The engine IPC protocol (add-engine-ipc) provides the low-level communication channel. This task
wraps it in a **stable MCP server** that Claude Code can use natively as tools. The MCP server
is intentionally minimal — it exposes only low-level primitives and never grows with engine
features. High-level knowledge (how to create VFX, configure shaders, build scenes) lives in
Rulebook Skills, not in MCP tool definitions.

Architecture:
```
Claude Code
  ↓ calls MCP tools (stable, never changes)
tools/mcp-uzengine/ (MCP server, Node.js stdio)
  ↓ writes/reads files in build/preview/
render.exe IPC (add-engine-ipc)
  ↓ reads SceneParams, executes handlers

Skills = recipe documentation
  e.g. "how to create fire VFX":
  → call uz_call("scene.setParam", {...})
  → call uz_screenshot() to verify
  → call uz_call("file.write", {path: "shaders/fire.hlsl", content: "..."})
  → call uz_call("shader.reload", {name: "fire"})
```

## What Changes

### tools/mcp-uzengine/ (Node.js, @modelcontextprotocol/sdk)

**MCP Tools — stable primitives, never expand with engine features:**

| Tool | Input | Output | Description |
|------|-------|--------|-------------|
| `uz_call` | `{function: string, args?: object}` | `{ok, result, error}` | Call any registered engine function |
| `uz_screenshot` | `{wait_ms?: number}` | `{path, base64_png, width, height}` | Capture current frame as PNG |
| `uz_status` | — | full status.json content | Get real-time engine metrics |
| `uz_read_file` | `{path: string}` | `{content: string}` | Read a project file |
| `uz_write_file` | `{path: string, content: string}` | `{ok: boolean}` | Write a project file |

**That's the full API surface. It does not grow.**

Skills teach the AI how to combine these 5 tools to achieve any goal:
- VFX Skill: "call uz_call('file.write', {path:'shaders/fire.hlsl', content:...}), then uz_call('shader.reload', {name:'fire'}), then uz_screenshot() to verify"
- Camera Skill: "call uz_call('scene.setParam', {key:'camera.fov', value:90})"
- Debug Skill: "call uz_status() to read fps, call uz_screenshot() to detect artifacts"

### .mcp.json (project root)
Auto-discovered by Claude Code when opening the workspace:
```json
{
  "mcpServers": {
    "uzengine": {
      "command": "node",
      "args": ["tools/mcp-uzengine/index.js"],
      "cwd": "${workspaceFolder}"
    }
  }
}
```

### docs/ai-interaction.md
Documents the MCP tool API and the Skill-based knowledge model.

## Impact
- Affected specs: docs/architecture.md, docs/ai-interaction.md
- Affected code: new tools/mcp-uzengine/, new .mcp.json
- Breaking change: NO — additive only
- Prerequisite: add-engine-ipc must be complete
- User benefit: AI has full low-level access to the engine; skills provide high-level knowledge
