# Node-Based Shader Editor

## Summary

Implement a prompt-driven shader graph system with the full internal behavior of a node-based material editor (similar to Unreal Engine's Material Editor). Graphs are created and modified via structured commands and chat instructions, compiled to HLSL in real-time, and previewed in the engine viewport.

## Motivation

UzEngine currently uses static `.uzmat` files with hard-coded `shader_name` references. Artists cannot compose shaders, iterate on material looks, or create custom effects without writing HLSL. A shader graph system unlocks:

- Programmatic shader authoring via prompts and structured commands
- Rapid iteration with live preview
- Material instancing from graph-based master materials
- Future: visual node editor UI (the graph system already supports it)
- Future: custom post-process effects, particle shaders, terrain blending

## Architecture Overview

Eight subsystems, split across C++ (data model + compilation + control + preview) and TypeScript (command interface):

1. **Shader Graph Core** (C++) — DAG storage, pin system, type validation, serialization
2. **Node System** (C++) — Extensible node library, each node implements `compile()`
3. **Compilation Pipeline** (C++) — Graph traversal, HLSL code generation, optimization
4. **Graph Control Layer** (C++) — Programmatic API for graph manipulation: create/delete nodes, connect/disconnect pins, insert between, replace, expose parameters
5. **Prompt Command Interface** (C++ + TS) — Structured command protocol translating high-level instructions into Graph Control Layer operations
6. **UzScene Preview Environment** (C++) — In-memory preview scene with procedural meshes (sphere/cube/plane), orbit camera, directional light, IBL sky, viewport streaming
7. **Live Shader Update System** (C++) — Event-driven auto-compilation with debounce, fallback shader retention, diagnostics push, MaterialManager integration
8. **Shader Graph Manager** (C++) — File loading/saving/caching for `.uzshader`, hot-reload, `graph:path` material integration

**No visual node editor UI.** The internal graph system is identical to a traditional node-based editor and fully supports a future UI layer, but interaction is prompt/command-driven. The UzScene provides immediate visual feedback for shader changes.

## Key Design Decisions

1. **Visitor-Compiler Pattern** (from UE5): Nodes call typed methods on `IShaderCompiler`, never emit raw HLSL text. This decouples graph structure from code generation strategy.

2. **Code Chunk Indexing** (SSA-like IR): Compiler maintains an array of code chunks. Each node `compile()` returns an index. Enables deduplication and caching.

3. **Bitmask ValueType**: Types are bitmasks (`Float1 | Float2 | Float3 | Float4 = Scalar`). Enables automatic scalar-to-vector promotion via bitwise AND.

4. **Graph Control Layer**: All graph mutations go through a typed API (`GraphOps`) that validates operations, maintains graph integrity, and emits change events. This is the same API a visual editor would call — prompts just translate to these operations.

5. **Command Protocol**: High-level instructions ("Add a Multiply node between TextureSample and MaterialOutput") are parsed into sequences of atomic `GraphOps` commands (`ADD_NODE`, `DISCONNECT`, `CONNECT`). The command layer is modular and can evolve with LLM integration.

## Dependencies

- Existing ShaderLibrary + PSO caching
- Existing Material/MaterialInstance system
- Existing RHI abstraction (rhi.h)
- Existing WebSocket IPC protocol
- Existing viewport streaming infrastructure

## Scope

### In Scope
- Core graph architecture (nodes, pins, connections, DAG)
- ~15 starter nodes (constants, math, texture sample, UV, time, etc.)
- HLSL compilation pipeline with type promotion and deduplication
- Graph Control Layer with programmatic operations API
- Prompt Command Interface (structured command parsing, not LLM)
- Live preview on sphere/cube/plane
- `.uzshader` file format (JSON serialization)
- IPC handlers for all graph and command operations

### Out of Scope (Designed For, Not Implemented)
- Visual node editor UI (Canvas/WebGL) — deferred, architecture supports it
- LLM-powered natural language parsing — deferred, command interface supports it
- Material instances from shader graphs
- Subgraphs / shader functions
- Custom HLSL code nodes
- Shader permutation system
- Preshader / uniform expression folding
