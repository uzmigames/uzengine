# Proposal: phase8_8.6.0-vfx-editor-clipboard-select

## Why
The node-graph editor (7.15.0) + the emitter stack have no multi-selection or copy/paste. Authors cannot move a module between systems without manual re-authoring. Niagara's clipboard model round-trips the selected subgraph as a JSON snippet that pastes into another graph.

## What Changes
Add a clipboard-serialisation layer that turns the selected node+wire subgraph into a JSON snippet (a standalone .uzvfxgraph fragment). Paste operation renames ids to avoid collisions and rewires endpoints.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_node_graph.h/.cpp
- Breaking change: NO
- User benefit: Authoring reuse via copy / paste across systems.
- Estimated effort: M

## Source
UE5 SNiagaraGraphCopyPaste.
