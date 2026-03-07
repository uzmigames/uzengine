## 1. Free Ring Geometry (C++)

- [ ] 1.1 Add billboarded circle geometry builder in `gizmo_transform.cpp` — generate torus ring vertices in camera-facing plane
- [ ] 1.2 Accept camera forward vector in `drawMode()` to orient the free ring each frame
- [ ] 1.3 White color (#CCCCCC default, #FFFFFF highlight) matching spec
- [ ] 1.4 Radius = 2.0 * gScale (slightly larger than axis rings at 1.8 * gScale)

## 2. Draw Integration (C++)

- [ ] 2.1 Add free-ring vertex buffer alongside existing translate/rotate/scale buffers
- [ ] 2.2 Update `drawMode()` to draw free ring as a 4th axis index (3) with highlight support
- [ ] 2.3 Pass camera forward via SceneParams or a new draw parameter

## 3. Testing

- [ ] 3.1 Verify free ring renders at correct position and radius
- [ ] 3.2 Verify ring always faces camera (billboarded)
- [ ] 3.3 Verify highlight color changes on hover
