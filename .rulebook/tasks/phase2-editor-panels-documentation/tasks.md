## 1. VSCode Extension Documentation

- [ ] 1.1 Update `docs/vscode-extension.md` — add Scene Tree Panel section (NodeItem types, icons, context menu commands, IPC calls)
- [ ] 1.2 Update `docs/vscode-extension.md` — add Inspector Panel section (property types: string/float/int/bool/vec3, group layout, live editing)
- [ ] 1.3 Update `docs/vscode-extension.md` — add Play Controls section (status bar items, state machine: idle/playing/paused)
- [ ] 1.4 Update `docs/vscode-extension.md` — add IpcClient section (WebSocket protocol, UUID correlation, auto-reconnect, push events)
- [ ] 1.5 Update `docs/vscode-extension.md` — add new commands reference (uzengine.addNode, deleteNode, renameNode, duplicateNode, selectNode, play, pause, stop)

## 2. IPC Protocol Documentation

- [ ] 2.1 Update `docs/ipc-mcp.md` — add `scene.getTree` handler spec (request/response shape)
- [ ] 2.2 Update `docs/ipc-mcp.md` — add `scene.selectNode` handler spec
- [ ] 2.3 Update `docs/ipc-mcp.md` — add `scene.addNode` handler spec (params: parentId, type)
- [ ] 2.4 Update `docs/ipc-mcp.md` — add `scene.deleteNode` handler spec
- [ ] 2.5 Update `docs/ipc-mcp.md` — add `scene.duplicateNode` handler spec
- [ ] 2.6 Update `docs/ipc-mcp.md` — add `scene.renameNode` handler spec
- [ ] 2.7 Update `docs/ipc-mcp.md` — add `scene.setProperty` handler spec (params: nodeId, key, value)
- [ ] 2.8 Update `docs/ipc-mcp.md` — add `scene.treeChanged` push event spec
- [ ] 2.9 Update `docs/ipc-mcp.md` — add `playmode.start` / `playmode.stop` / `engine.pause` handler specs
