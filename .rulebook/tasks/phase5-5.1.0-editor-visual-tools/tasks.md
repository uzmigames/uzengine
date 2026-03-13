## 1. Implementation

- [ ] 1.1 Implement transform gizmo mesh generation (move arrows, rotate rings, scale cubes)
- [ ] 1.2 Implement gizmo rendering pass (overlay on scene, depth-tested against gizmo only)
- [ ] 1.3 Implement gizmo interaction (mouse ray intersection with axis handles, drag transform)
- [ ] 1.4 Implement pick buffer render pass (1/4 res, node ID encoded as RGB color)
- [ ] 1.5 Implement pick buffer CPU readback (single click → node ID, box select → multiple IDs)
- [ ] 1.6 Implement selection outline shaders (stencil write on selected, outline blur pass)
- [ ] 1.7 Implement camera frustum gizmo (wireframe pyramid for camera nodes)
- [ ] 1.8 Implement directional light gizmo (arrow + shadow direction indicator)
- [ ] 1.9 Implement point light gizmo (wireframe sphere at light range radius)
- [ ] 1.10 Implement spot light gizmo (wireframe cone at cone angles)
- [ ] 1.11 Implement debug draw API (DrawLine, DrawSphere, DrawBox, DrawFrustum — immediate mode)
- [ ] 1.12 Implement wireframe mode toggle (fill + wireframe overlay, or wireframe only)
- [ ] 1.13 Register all editor passes in render graph (after post-processing, before UI overlay)

## 2. Testing

- [ ] 2.1 Test pick buffer node ID encoding/decoding roundtrip
- [ ] 2.2 Test gizmo ray intersection math (hit detection on axis handles)
- [ ] 2.3 Test selection outline shader compilation (SM6.6 and SM5)
- [ ] 2.4 Test debug draw line rendering (correct start/end positions)
- [ ] 2.5 Test wireframe mode toggle (on/off without visual artifacts)
- [ ] 2.6 Test gizmo visibility (only shown when node is selected)
- [ ] 2.7 Test box selection (multiple nodes within marquee rectangle)
- [ ] 2.8 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Add `docs/engine/22-developer-layer/` editor tools documentation
- [ ] 3.2 Add debug draw API reference (functions, colors, lifetime)
- [ ] 3.3 Add gizmo interaction guide (keyboard shortcuts, snap settings)
