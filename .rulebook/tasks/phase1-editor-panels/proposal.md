# Phase 1 — Editor Panels (VSCode Extension)

## Context

The current VSCode extension has a single viewport panel that streams JPEG frames. This task adds the full editor layout: Scene Tree, Inspector, Asset Browser, and Play Controls. These panels communicate with the engine via the existing IPC/WebSocket infrastructure.

## Goal

A VSCode extension with:
- **Scene Tree** panel (native `TreeDataProvider`) showing the live node hierarchy
- **Inspector** panel (Webview UI Toolkit) showing and editing properties of the selected node
- **Asset Browser** panel (native `TreeDataProvider`) showing project files by type
- **Play Controls** in the status bar (▶ Play, ⏸ Pause, ■ Stop)
- All panels synchronized with engine state via IPC

## Spec Reference

[Editor Panels Spec](../../../docs/specs/editor-panels.md)

## Dependencies

- **phase1-scene-system** must be complete (requires `scene.*` IPC handlers)

## Approach

1. Register new VSCode views: `uzengine.sceneTree`, `uzengine.assets`
2. Implement `SceneTreeProvider` as `TreeDataProvider<NodeItem>`
3. Implement `InspectorPanel` as WebviewPanel with Webview UI Toolkit
4. Implement `AssetBrowserProvider` as `TreeDataProvider<AssetItem>`
5. Add status bar play controls with process lifecycle management
6. Wire all panels to IPC calls via `ipc-client.ts`
