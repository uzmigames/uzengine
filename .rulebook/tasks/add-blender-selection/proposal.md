# Proposal: Add Blender-Style Object Selection and Outline

## Why
The current object selection system only detects clicks near a node's world-space origin point (projected to screen, 50px radius). This means users cannot select objects by clicking on their visible mesh body — only by clicking near their center point. This is unintuitive and breaks the expected Blender-style interaction where clicking anywhere on an object's visible surface selects it. Additionally, clicking on empty space does not reliably deselect, and the selection outline needs to match Blender's always-visible silhouette style.

## What Changes
- **GPU Pick Buffer**: New 1/4-resolution RGBA8 render target that renders each mesh proxy with its node_id encoded as RGB. This enables pixel-accurate, full-object click detection.
- **Pick Handler Priority**: `editor.pickNode` in ws_handlers.cpp restructured to use GPU pick as primary (Layer 1), origin-distance as fallback for non-mesh nodes (Layer 2), and null for empty space clicks (Layer 3 = deselect).
- **Outline PSO**: Changed from `CULL_NONE + depth_enable=true` to `CULL_FRONT + depth_enable=false` for Blender-style always-visible silhouette outline.
- **TS Mousedown**: Minor fix to ensure click-to-switch works when camera data isn't cached.

## Impact
- Affected specs: renderer, editor
- Affected code: renderer.cpp, renderer.h, shader_library.cpp, ws_handlers.cpp, viewport-panel.ts, new pick.vs.hlsl + pick.ps.hlsl shaders
- Breaking change: NO (existing gizmo system untouched)
- User benefit: Full-object clickability, intuitive deselection, Blender-quality selection outline
