# Proposal: phase2-asset-importer-pipeline

## Why
The AssetRegistry from phase1-asset-browser currently handles import inline. The import pipeline needs to be extracted into a dedicated Importer class to support per-type import logic (mesh processing, texture dimension extraction, PBR shader templates). This also enables future asset processing (compression, format conversion).

## What Changes
- Extract import logic into `src/engine/editor/importer.h/cpp` with per-type handlers
- Add mesh import stub that copies `.glb` to `.import/` directory with settings metadata
- Add texture import that reads PNG header for width/height/channels
- Create PBR shader template `assets/templates/shader_pbr.hlsl`

## Impact
- Affected specs: `docs/specs/asset-pipeline.md`
- Affected code: `src/engine/editor/importer.h/cpp`, `src/engine/editor/asset_registry.cpp`
- Breaking change: NO
- User benefit: Per-type import processing enables correct asset metadata extraction and future asset pipeline extensions
