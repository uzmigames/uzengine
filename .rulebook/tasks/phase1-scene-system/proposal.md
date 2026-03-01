# Phase 1 — Scene System

## Context

The current engine renders a hard-coded pyramid. There is no scene abstraction, no node tree, no file format for describing scenes. This task implements the core scene system: node types, the `.uzscene` file format, a runtime scene manager, and IPC handlers exposing the scene to the editor and AI agents.

## Goal

A running `UzEngine.exe` that:
- Loads a `.uzscene` file at startup (path from CLI arg or `project.uzproj`)
- Maintains a live node tree in memory (Node, Node3D, StaticMesh, DirectionalLight, Camera)
- Exposes full scene CRUD via IPC (`scene.*` handlers)
- Renders the scene contents instead of the hard-coded pyramid
- Saves the modified scene back to disk

## Spec Reference

[Scene System Spec](../../../docs/specs/scene-system.md)

## Approach

1. Define `NodeType`, `NodeBase`, `Transform3D` POD structs
2. Implement `SceneManager` with pool-based node storage (generation handles)
3. Implement `.uzscene` file parser (INI-like, hand-written)
4. Implement `.uzscene` file serializer
5. Replace hard-coded pyramid with scene-driven render pass
6. Register `scene.*` IPC handlers
7. Add `Arena` allocator infrastructure (`src/common/arena.h`)

## Out of Scope

- GLTF mesh import (Phase 2)
- Physics nodes (Phase 3)
- Script nodes (Phase 3)
- Visual editor UI (Phase 1 Editor Panels task)
