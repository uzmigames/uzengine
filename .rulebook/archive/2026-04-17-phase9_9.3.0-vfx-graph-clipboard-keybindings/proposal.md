# Proposal: phase9_9.3.0-vfx-graph-clipboard-keybindings

## Why
Phase 8.6.0 shipped the Copy/Paste subgraph helper with id remap + rename ladder. Authors can't invoke it until Ctrl+C / Ctrl+V / Ctrl+D are bound in the graph editor.

## What Changes
Wire `Ctrl+C → VfxNodeGraphClipboard::Copy(selectedIds)` and `Ctrl+V → Paste(clipboardJson)` in the node-graph editor input handler. Store the snippet JSON in the OS clipboard so copy/paste works across engine instances.

## Impact
- Affected specs: docs/engine/25-vfx/23-clipboard-format.md
- Affected code: vscode-extension / in-engine graph widget input handler
- Breaking change: NO
- User benefit: Cross-system subgraph reuse via keyboard shortcuts.
- Estimated effort: S

## Source
UE5 Blueprint editor Ctrl+C/V behaviour.
