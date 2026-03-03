# Proposal: phase2-vscode-asset-browser-panel

## Why
The C++ AssetRegistry backend and asset.* IPC handlers are implemented (phase1-asset-browser). The VSCode Asset Browser Panel is needed to give artists and developers a visual interface to browse, search, import, and create assets from within the editor. Without this panel, users must navigate the OS file system manually.

## What Changes
- Implement `AssetBrowserProvider` TypeScript class using `TreeDataProvider<AssetItem>` in the VSCode extension
- Group assets by type with import status indicators
- Add context menu actions for reimport, new material, new scene, new shader
- Wire file watcher to auto-refresh the tree on file changes

## Impact
- Affected specs: `docs/specs/editor-panels.md`
- Affected code: `vscode-uzengine/src/panels/AssetBrowserPanel.ts`, `vscode-uzengine/src/providers/AssetBrowserProvider.ts`
- Breaking change: NO
- User benefit: Visual asset browsing, drag-and-drop, and context menu actions enable a modern editor workflow
