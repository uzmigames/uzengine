## 1. Implementation

- [x] 1.1 Implement PrimitiveComponent base (registers in RenderWorld on BeginPlay) <!-- engine/ecs/primitive_component.h -->
- [x] 1.2 Implement StaticMeshComponent (references .uzmesh + material) <!-- engine/ecs/static_mesh_component.h -->
- [x] 1.3 Implement LightComponent (point, directional, spot — color, intensity, range) <!-- engine/ecs/light_component.h -->
- [x] 1.4 Implement CameraComponent (FOV, near/far, active camera selection) <!-- engine/ecs/camera_component.h -->
- [x] 1.5 Implement RenderProxy: ECS → render-thread data sync via command queue <!-- engine/renderer/render_world.h (RenderProxy struct + RenderWorld pool) -->
- [ ] 1.6 Implement RenderSubsystem: collect visible → generate draw calls
- [ ] 1.7 Wire RenderSubsystem into deferred pipeline (geometry + lighting passes) — blocked on 3.3.0 deferred pipeline
- [ ] 1.8 Create merge demo: load .uzscene → entities render with deferred PBR + shadows + post-process — blocked on 3.3.0 + 3.4.0 + 3.5.0

## 2. Testing

- [x] 2.1 Test PrimitiveComponent registration in RenderWorld <!-- tests/engine/renderer/test_render_world.cpp -->
- [ ] 2.2 Test StaticMeshComponent loads and renders correct mesh
- [ ] 2.3 Test LightComponent feeds into deferred lighting pass — blocked on 3.3.0
- [x] 2.4 Test CameraComponent drives view/projection matrices <!-- tests/engine/renderer/test_camera.cpp -->
- [ ] 2.5 Test RenderProxy sync: game thread changes propagate to render thread
- [ ] 2.6 Test full pipeline: .uzscene → entities → deferred PBR output — blocked on 3.3.0 + 3.4b
- [ ] 2.7 Verify 95%+ coverage

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/01-render-world.md`
- [ ] 3.2 Document ECS → renderer bridge architecture
