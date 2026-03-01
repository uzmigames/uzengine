# Phase 1 — Asset Browser + Import Pipeline

## Context

There is no asset management in the current engine. Assets (meshes, textures) need an import pipeline: raw source files (GLTF, PNG) are processed into engine-ready formats with `.import.json` sidecar metadata. The VSCode asset browser panel exposes this to the user and AI.

## Goal

- File-based asset registry: scan project directory, discover assets by extension
- `.import.json` sidecar generation for mesh and texture assets
- `asset.*` IPC handlers for listing, importing, and querying assets
- Asset browser panel in VSCode (TreeDataProvider)
- New asset templates (Material, Scene, Shader) via context menu

## Spec Reference

[Engine Vision — Asset Pipeline](../../../docs/engine-vision.md#39-asset-pipeline)

## Dependencies

- **phase1-scene-system** (resource handles, project config)
