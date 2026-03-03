# Proposal: phase3-auto-uv-unwrap

## Why
Meshes imported from glTF/FBX often have poor or entirely missing UV channel 1 (lightmap UVs), which is required for lightmap baking, decal projection, and normal map baking. Manual UV unwrapping in DCC tools is time-consuming and requires artists to leave the engine and re-export assets. Integrating xatlas into the asset pipeline generates chart-packed UV1 automatically at import time, unblocking lightmap and baking workflows without any manual artist intervention.

## What Changes
- xatlas library integrated as a vcpkg dependency in the asset pipeline
- UV1 auto-unwrap triggered per mesh during import when no UV channel 1 is present in the source asset
- Packing quality setting exposed in import settings (fast/balanced/best), controlling xatlas chart resolution and pack iterations
- Texel density normalization pass to ensure consistent lightmap resolution across meshes of varying physical size
- Import settings UI in the editor with a checkbox to force regenerate UV1 even when source UV1 exists
- Generated UV1 stored in the engine's internal mesh format alongside UV0
- Progress and warning messages (e.g., mesh has overlapping source UV1) surfaced via IPC status

## Impact
- Affected specs: `docs/specs/asset-pipeline.md`, `docs/specs/renderer.md`
- Affected code: `src/asset_pipeline/`, `src/ipc/`, `vcpkg.json`
- Breaking change: NO
- User benefit: Artists can import any glTF/FBX mesh and immediately use it for lightmap baking or decal projection without manually creating UV channel 1 in an external DCC tool.
