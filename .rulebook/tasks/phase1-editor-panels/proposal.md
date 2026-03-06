# Editor Panels — Scene Tree, Inspector, Asset Browser, Play Controls

## Priority: HIGH

## Summary

Implement the core VS Code editor panels for UzEngine: Scene Tree, Inspector, Asset Browser, and Play Controls. These panels provide the primary UI for scene editing, property inspection, and playback control.

## Why

The engine needs a fully integrated editor experience inside VS Code. Without these panels, developers must manually edit JSON scene files and use IPC commands to interact with the engine, which is slow and error-prone.

## Scope

### Phase 1 (Implemented)
- Scene Tree panel with hierarchy display, selection, rename, and context menu
- Inspector panel with live property editing (float, vec3, bool)
- Play Controls status bar items (Play, Stop, Pause)
- IPC client with reconnect and request/response correlation

### Phase 2 (Deferred)
- Asset Browser panel with file system integration
- Color and resource path property editors in Inspector
- Viewport click-to-select synchronization
- Play mode crash detection

## Dependencies

- VS Code Extension Host
- WebSocket IPC infrastructure
- Scene system with node hierarchy
