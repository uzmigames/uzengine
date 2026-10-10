# Proposal: phase7_7.15.0-vfx-node-graph-editor

## Why
Niagara's biggest artist-facing win is the visual node graph. Authors wire modules together, drop in expression nodes, preview live. UzEngine currently forces JSON/Lua editing for non-trivial graphs. Without a node graph UI, artists cannot iterate on effects without a programmer.

## What Changes
Build a node graph editor inside the existing VFX editor overlay: nodes represent modules, wires represent parameter flow. Backed by the emitter graph data model already in runtime/engine/vfx/asset/vfx_graph.h. Renders directly in the engine via the existing immediate-mode UI.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_node_graph_editor.* (new), asset/vfx_graph.h
- Breaking change: NO
- User benefit: Artist-accessible visual VFX authoring.

## Source
Reference: UE5 NiagaraEditor/SNiagaraGraph.cpp + FNiagaraEmitterGraphEditor.
