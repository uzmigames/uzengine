# Proposal: phase2-ai-native-features

## Why

The Phase 0 MCP server exposes only five low-level primitives (ping, status, screenshot, setRotation, setBackground), which are sufficient for basic engine control but useless for AI-assisted game development. An AI agent helping a developer build a scene must currently issue dozens of individual low-level commands with no understanding of scene structure, shader language constraints, or Lua API conventions. This makes AI agents spectators rather than contributors in the content creation workflow.

High-level MCP tools that map directly to game development tasks — generating scenes from text descriptions, writing engine-correct HLSL shaders, scaffolding Lua scripts for selected nodes, and auto-tagging imported assets — turn Claude Code and GitHub Copilot into active collaborators. The AI can draft a full scene with lights, meshes, and materials from a single sentence, then let the developer refine it. This reduces scene authoring time from hours of manual node placement to minutes of iterative prompting.

All four tools require only MCP server additions (TypeScript) and two new C++ IPC handlers. No existing engine systems are modified, so there is zero risk of regression in Phase 1 features. The design is deliberately additive: each tool composes existing IPC primitives (`scene.load`, `scene.setProperty`, `shader.reload`) and adds an LLM translation layer on top.

## What Changes

- Add `scene_generate` MCP tool: accepts a natural-language prompt, injects the `.uzscene` INI schema and current scene tree as LLM context, generates a complete scene INI document, and loads it via the existing `scene.load` IPC handler; returns the created node IDs and a screenshot for verification
- Add `shader_assist` MCP tool: accepts a shader description and optional partial HLSL, injects UzEngine HLSL conventions (GlobalParams cbuffer at b15, standard vertex layout, include paths) as LLM context, generates a complete `.hlsl` file, writes it to the project shaders directory, triggers `shader.reload` IPC for immediate hot-reload feedback, and returns the compilation result
- Add `script_generate` MCP tool: accepts a node path and a behavior description, queries `scene.getTree` for the target node's component set to use as LLM context, generates a LuaJIT script scaffolded to the node's available components and the engine's Lua API, and attaches it to the node via `scene.setProperty`
- Add `asset_tag` MCP tool: accepts an asset path, reads asset metadata (mesh vertex count, texture dimensions and format, audio duration), passes them to an LLM, and writes AI-generated semantic tags (e.g., `character`, `environment`, `prop`, `ui`) back to the asset's `.import.json` sidecar for use in the Asset Browser search and filter UI
- Implement `ai.context` IPC handler in C++ (`src/ipc/IpcServer.cpp`): returns a single JSON payload containing the scene tree, active shader file list, Lua script registry, and engine build version — used as LLM context injection by all four MCP tools
- Implement `editor.analyzeScene` IPC handler in C++: serializes the full scene graph with per-node component data (transform, mesh handle, material handle, light params) into a structured JSON document for LLM-driven scene analysis queries
- Register all four tools in `vscode-extension/src/mcp/McpServer.ts` as proper MCP tool definitions with JSON Schema input validators and typed response objects matching the MCP protocol
- Add integration tests in `tests/mcp/` that mock the LLM call, verify correct IPC command dispatch, and assert response structure for each of the four tools
- Update `docs/specs/ai-native-features.md` with tool input/output schemas, LLM prompt templates, context injection strategy, and example prompts with expected outputs

## Impact

- Affected specs: docs/specs/ai-native-features.md
- Affected code: vscode-extension/src/mcp/McpServer.ts, src/ipc/IpcServer.cpp (ai.context and editor.analyzeScene handlers), src/assets/AssetRegistry.cpp (asset_tag sidecar write)
- Breaking change: NO — all new tools are additive; the existing five MCP primitives are unchanged and all Phase 1 IPC handlers are untouched
- User benefit: Developers using Claude Code or VSCode Copilot chat can generate complete scenes, write engine-correct HLSL shaders, scaffold Lua scripts, and auto-tag imported assets from natural-language prompts without leaving VSCode, cutting content authoring time from hours to minutes
