# Proposal: Editor Visual Tools — Gizmos, Picking, Selection & Debug Draw (Phase 5)

## Why
The 0.1 POC had a full suite of editor visual tools: transform gizmos (move/rotate/scale), node picking (click-to-select via pick buffer), selection outline rendering, per-node-type gizmos (camera frustum, light sphere/arrow), wireframe mode, and bounding box display. Currently 0.2 has no editor interaction tools — you can view the scene but cannot select, transform, or inspect objects. These tools are foundational for the VSCode-embedded editor experience and for AI agents to interact with scenes visually.

## What Changes

### New Systems
1. **Transform gizmo renderer** — 3D move/rotate/scale gizmo overlay with axis handles (red X, green Y, blue Z), rendered on top of scene geometry
2. **Node picking** — Render-to-pick-buffer pass (1/4 res) encoding node ID as color, CPU readback for single-click and box (marquee) selection
3. **Selection outline** — Screen-space outline shader on selected nodes via stencil test
4. **Node-type gizmos** — Per-component visual helpers: camera frustum wireframe, directional light arrow, point light radius sphere, spot light cone
5. **Debug draw API** — Immediate-mode line/sphere/box/frustum drawing for runtime debugging, flushed each frame
6. **Wireframe mode** — Toggle wireframe rendering overlay on selected or all objects

### Refactoring
- Render graph gains editor overlay passes (after post-processing, before UI)
- Input system routes mouse clicks through pick buffer for selection

## Impact
- Affected specs: `docs/engine/22-developer-layer/`, `docs/engine/31-editor-framework/`
- Affected code: `runtime/editor/`, `runtime/shaders/editor/`
- Breaking change: NO — editor tools are additive overlay, disabled in shipping builds
- User benefit: Interactive scene editing, visual debugging, AI-driven object manipulation
- Depends on: 3.5b (render bridge — need entities to pick/select)
