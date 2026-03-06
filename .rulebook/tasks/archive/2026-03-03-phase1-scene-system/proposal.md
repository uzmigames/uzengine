# Proposal: phase1-scene-system

## Why

UzEngine currently renders a single hard-coded DX12 pyramid with no scene abstraction whatsoever. Every object in the world is baked into C++ source code. There is no concept of a node, no transform hierarchy, no file format for describing what a scene contains, and no way for the editor or an AI agent to query or modify the scene at runtime. This is the most fundamental missing layer in the engine.

The scene system is the dependency that every other Phase 1 system builds on. The editor panels need `scene.*` IPC commands to query the node tree and push property changes. The asset browser needs scene nodes to attach assets to. Play mode needs a scene to load and simulate. Lua scripting needs nodes to attach scripts to. Without this foundation, none of those systems can be started.

The `.uzscene` INI-like file format is deliberately plain text and human-readable because the engine targets AI-native workflows: AI agents must be able to read, understand, and modify scene files without a binary parser. This design decision also means scene files are diffable and version-control friendly, which matters for a team using Git.

## What Changes

- Define the core data types in `src/scene/`: `NodeType` enum (Node, Node3D, StaticMesh, DirectionalLight, CameraNode), `Transform3D` POD struct (position, rotation, scale as `float3`), and `NodeBase` struct holding id, parent_id, name, type, transform, and enabled flag
- Implement `SceneManager` class with pool-based node storage using generation handles (`Handle<Node>` = 32-bit index + 16-bit generation) so handles become invalid after node deletion, preventing dangling references
- Implement the `.uzscene` file parser: INI-like format with `[node.name]` section headers and `key = value` property lines; hand-written parser (no third-party dependency) reading into the `SceneManager` node pool
- Implement the `.uzscene` file serializer that writes the in-memory node tree back to disk in the same INI-like format, preserving node order and parent-child relationships
- Implement parent-child transform inheritance: `Node3D` and derived types compute their world transform by multiplying the local `Transform3D` up the parent chain; dirty-flag propagation so only affected subtrees are recomputed each frame
- Replace the hard-coded pyramid draw call with a scene-driven render loop: `SceneManager` iterates `StaticMesh` nodes, submits their geometry and world transform matrix to the DX12 renderer, and dispatches `DirectionalLight` parameters to the lighting constant buffer
- Register IPC command handlers in `src/ipc/scene_handlers.cpp`: `scene.get_tree` (returns full node hierarchy as JSON), `scene.create_node` (creates node, returns new handle), `scene.delete_node`, `scene.set_property` (sets any property on a node by name/value), `scene.get_property`, `scene.reparent_node`, `scene.save`, `scene.load`
- Add `ArenaAllocator` infrastructure in `src/core/arena.h` with three arena instances: frame arena (reset each frame), level arena (reset on scene unload), and permanent arena (never reset); `SceneManager` allocates node pool from the level arena
- Add `project.uzproj` TOML config loader that reads the startup scene path, project name, and asset directory root; `render.exe` accepts `--project <path>` CLI argument
- Emit push events over WebSocket on scene mutation: `scene.node_created`, `scene.node_deleted`, `scene.node_changed` with the affected node's JSON snapshot so connected editor panels stay in sync without polling

## Impact

- Affected specs: `docs/specs/scene-system.md`, `docs/specs/memory-system.md`, `docs/specs/renderer.md`, `docs/specs/editor-panels.md`
- Affected code: `src/scene/scene_manager.cpp`, `src/scene/scene_manager.h`, `src/scene/node_types.h`, `src/scene/transform.h`, `src/scene/uzscene_parser.cpp`, `src/scene/uzscene_serializer.cpp`, `src/core/arena.h`, `src/ipc/scene_handlers.cpp`, `src/main.cpp`
- Breaking change: YES — the hard-coded pyramid render path is removed; the engine now requires a valid `.uzscene` file or `--project` argument to start
- User benefit: Scenes are authored as plain-text `.uzscene` files that both humans and AI agents can read and modify; the running engine reflects scene changes made through IPC in real time, enabling the full editor panel and AI-assisted scene authoring workflows
