# Proposal: phase2-editor-panels-documentation

## Why

Phase 1 of the editor panels feature implemented IpcClient, SceneTreeProvider, InspectorPanel,
and PlayControls but documentation updates were explicitly deferred to Phase 2. The `docs/`
directory does not yet reflect the new IPC wire protocol, the new VSCode commands, or the
panel architecture. Without documentation, contributors cannot implement the engine-side IPC
handlers or understand the extension's panel architecture.

## What Changes

- `docs/vscode-extension.md`: new sections for Scene Tree Panel, Inspector Panel, Play Controls,
  IpcClient, and a full command reference
- `docs/ipc-mcp.md`: new IPC handler specs for all scene.* and playmode.* methods introduced
  in Phase 1, including request/response shapes and the `scene.treeChanged` push event

## Impact

- Affected specs: `docs/vscode-extension.md`, `docs/ipc-mcp.md`
- Affected code: documentation only — no TypeScript changes
- Breaking change: NO
- User benefit: Engine developers can implement C++ IPC handlers from the spec; extension
  contributors can understand the panel architecture without reading source code
