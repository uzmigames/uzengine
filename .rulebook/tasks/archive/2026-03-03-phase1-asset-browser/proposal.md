# Proposal: phase1-asset-browser

## Why

UzEngine currently has no asset management layer. Raw source files (glTF meshes, PNG/DDS textures, HLSL shaders, audio clips) live on disk with no registry, no import metadata, and no way to reference them by stable handle from within scenes or scripts. Every time the engine starts it has no knowledge of what assets exist in the project folder.

The VSCode editor workflow requires an Asset Browser panel so that artists and developers can see, search, and organize their project assets without leaving the editor. Without this panel, users must navigate the OS file system manually and type asset paths by hand into scene files — which is error-prone, slow, and not AI-toolable.

Drag-and-drop from the Asset Browser into the scene viewport is the primary way nodes are created in a modern game editor. Without it, the editor feels incomplete and the scene authoring workflow is broken. Thumbnail generation for textures provides immediate visual feedback that the correct asset is being used.

## What Changes

- Implement a file-based `AssetRegistry` C++ class that scans the project `assets/` directory recursively and discovers assets by file extension (.gltf, .png, .dds, .hlsl, .wav, .uzmat, .uzscene)
- Generate `.import.json` sidecar files alongside each discovered asset storing import settings, UUID, last-modified timestamp, and engine-ready output path
- Add `asset.list`, `asset.import`, `asset.get_info`, `asset.rename`, `asset.delete`, `asset.duplicate` IPC command handlers in the engine MCP server
- Implement VSCode `AssetBrowserProvider` using `TreeDataProvider<AssetItem>` that renders the project asset tree with type icons and thumbnail previews for texture assets
- Add thumbnail generation pipeline: load PNG/DDS at import time, generate 64x64 JPEG thumbnail, store alongside `.import.json`, serve via WebSocket `asset.get_thumbnail` command
- Implement search and filter bar in the Asset Browser panel using VS Code `TreeView` filter API and asset-type filter buttons (All, Meshes, Textures, Materials, Scenes, Shaders)
- Add context menu entries on asset items: Rename, Delete, Duplicate, Show in Explorer, Copy Path, Re-import
- Implement drag-and-drop from Asset Browser tree items onto the scene viewport canvas: on drop, send `scene.create_node` IPC command with the asset UUID and drop position
- Add "New Asset" context menu on folders: Material (.uzmat), Scene (.uzscene), Shader (.hlsl) templates are written to disk with default content
- Expose `asset.*` MCP tools for AI agents: `asset_list`, `asset_import`, `asset_search` so AI can query and manipulate the asset registry programmatically

## Impact

- Affected specs: `docs/specs/asset-pipeline.md`, `docs/specs/scene-system.md`, `docs/specs/editor-panels.md`
- Affected code: `src/editor/asset_registry.cpp`, `src/editor/asset_registry.h`, `src/ipc/asset_handlers.cpp`, `vscode-extension/src/panels/AssetBrowserPanel.ts`, `vscode-extension/src/providers/AssetBrowserProvider.ts`
- Breaking change: NO
- User benefit: Artists and developers can browse, search, and drag assets directly into the scene viewport from within VSCode, eliminating manual path entry and enabling AI agents to query and manipulate the full asset library
