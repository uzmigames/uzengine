# Phase 2 — AI-Native Features (MCP Tools + IPC Handlers)

## Goal

Extend the Phase 0 MCP server with five high-level AI-native tools: `scene_generate` (prompt-to-scene), `shader_assist` (HLSL co-authoring with hot-reload feedback), `ai_set_property` (natural language property editing), `scene_analyze` (scene analysis + LLM suggestions), and `script_generate` (Lua scaffolding for selected nodes). Add two C++ IPC handlers: `ai.context` and `editor.analyzeScene`. All features require only MCP server changes (TypeScript) plus two new IPC handlers (C++) — no other engine systems are modified.

## Spec Reference

[AI-Native Features Spec](../../../docs/specs/ai-native-features.md)

## Dependencies

- phase0 IPC/MCP server (WebSocket JSON IPC already implemented)
- phase1-scene-system (`scene.getTree`, `scene.setProperty`, `scene.load` IPC handlers)
- phase1-shader-hot-reload (`shader.reload` IPC handler for `shader_assist` feedback loop)
- phase1-lua-scripting (Lua scripts are generated and attached via `scene.setProperty`)
