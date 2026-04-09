## 1. Trail Data Structures

- [x] 1.1 Create `runtime/engine/vfx/trails/trail_point.h` — TrailPoint struct: position (Vec3), direction (Vec3, tangent), width (float), color (Vec4), age (float), distance_from_head (float). Ring buffer TrailPointBuffer: fixed capacity, head/tail indices, AddPoint(point), GetPoint(i), RemoveOldest(), Count().
- [x] 1.2 Create `runtime/engine/vfx/trails/trail_component.h` — TrailComponent ECS component: material (ResourceHandle<Material>), max_trail_points (uint32, default 100), width_start (float), width_end (float), width_curve (VFXCurve*), color_over_distance (VFXCurve*), uv_scroll_rate (float), point_lifetime (float, seconds per point), min_point_distance (float, min distance between consecutive points to avoid clumping). Holds TrailPointBuffer.
- [x] 1.3 Create `runtime/engine/vfx/trails/trail_component.cpp` — TrailComponent::OnTick(dt): (a) compute distance from last added point, (b) if distance > min_point_distance, add new TrailPoint at entity position, (c) age all points by dt, (d) remove points where age > point_lifetime, (e) recalculate distance_from_head for UV mapping.

## 2. Trail Geometry Generation

- [x] 2.1 Create `runtime/engine/vfx/trails/trail_mesh_builder.h/.cpp` — TrailMeshBuilder: generates camera-facing ribbon geometry from trail points. For each pair of adjacent points: (a) compute tangent = normalize(p1 - p0), (b) compute right = normalize(cross(tangent, camera_dir)) or cross(tangent, up) for stability, (c) compute width at this point from width_curve at normalized distance, (d) create 4 vertices (left/right × 2 points), (e) create 2 triangles (6 indices). UV: u=0/1 across width, v=normalized_distance along trail. Outputs vertex/index arrays.
- [x] 2.2 Create trail vertex format — TrailVertex struct: position (float3), uv (float2), color (float4), width (float). Total 36 bytes per vertex.
- [x] 2.3 Implement dynamic vertex/index buffer — Per-trail double-buffered upload buffers (ring allocator pattern). Updated each frame without CPU-GPU stall. Max vertices = max_trail_points × 2, max indices = (max_trail_points - 1) × 6.

## 3. Trail Shaders

- [x] 3.1 Create `runtime/shaders/vfx/trail_vs.hlsl` — Vertex shader: pass-through from structured vertex buffer. Transforms position to clip space via view_projection. Passes UV and color to pixel shader.
- [x] 3.2 Create `runtime/shaders/vfx/trail_ps.hlsl` — Pixel shader: sample trail material texture at UV (apply uv_scroll to V coordinate: v += time * uv_scroll_rate). Multiply by vertex color (from color_over_distance curve). Apply alpha blend. Optional soft depth fade at edges.
- [x] 3.3 Create trail PSO — Alpha blended (SrcAlpha, OneMinusSrcAlpha), depth test enabled, depth write disabled, backface cull disabled, two-sided rendering.

## 4. Trail System and Render Pass

- [x] 4.1 Create `runtime/engine/vfx/trails/trail_system.h/.cpp` — TrailSystem: manages all active TrailComponents. Update(dt) ticks all trails, generates geometry. GetRenderData() returns array of per-trail render info (vertex buffer, index buffer, index count, material). RegisterTrail/UnregisterTrail for ECS integration.
- [x] 4.2 Create `runtime/engine/vfx/passes/trail_render_pass.h/.cpp` — TrailRenderPass: render graph pass in transparent phase. For each active trail: bind material, bind vertex/index buffers, DrawIndexed. Sort trails by distance to camera (back-to-front). Scheduled after particle render pass.

## 5. Trail Asset

- [x] 5.1 Create `runtime/engine/vfx/trails/trail_asset.h/.cpp` — TrailAsset: loaded from .uztrail JSON. Fields: material path, width_start, width_end, width_curve path, color_over_distance curve path, uv_scroll_rate, max_points, point_lifetime, min_point_distance. Static Load(path) factory.

## 6. Testing

- [x] 6.1 Unit tests for TrailPointBuffer — add/remove/count, ring buffer wrap-around, capacity limit
- [x] 6.2 Unit tests for TrailComponent — point addition with min distance filtering, aging and expiry
- [x] 6.3 Unit tests for TrailMeshBuilder — generate geometry from 3 trail points, verify vertex count (6), index count (6), UV coordinates, width interpolation
- [x] 6.4 Unit tests for TrailAsset — load valid/invalid .uztrail JSON
- [x] 6.5 Unit tests for TrailSystem — register/unregister, update cycle

## 7. Demo

- [x] 7.1 Create `demos/trails/` — Projectile trail: entity moves in arc, leaves glowing trail that fades. Width tapers from thick→thin. Color fades from bright→transparent. UV scrolls for animated fire texture.
- [x] 7.2 Add sword swipe demo — Entity sweeps in arc, short-lived wide trail. Width curve: wide at center, thin at edges.
- [x] 7.3 Add performance overlay — active trail count, total segments, trail render time (ms)

## 8. Documentation

- [x] 8.1 Document .uztrail JSON format specification
- [x] 8.2 Document trail rendering pipeline and geometry generation
- [x] 8.3 Document trail configuration guide with visual examples

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
