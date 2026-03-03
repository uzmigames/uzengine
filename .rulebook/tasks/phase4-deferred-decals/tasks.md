## 1. Prerequisites

- [ ] 1.1 Confirm `phase1-render-graph` is complete — `RgPass` API works
- [ ] 1.2 Confirm `phase2-pbr-renderer` is complete — GBuffer layout (albedo, normal, roughness, depth) finalized
- [ ] 1.3 Read `docs/specs/renderer.md` — understand GBuffer texture formats and lighting pass inputs
- [ ] 1.4 Read `docs/specs/render-graph.md` — understand persistent resource registration and compute/graphics pass mix

## 2. CDecal Component

- [ ] 2.1 Add `CDecal` to `ecs/components.h`
- [ ] 2.2 Fields: `material_path: string`, `half_extents: Vec3` (projection box), `blend_mode: DecalBlendMode`, `fade_start_distance: float`, `fade_end_distance: float`, `sort_order: int`
- [ ] 2.3 Define `DecalBlendMode` enum: `Albedo` (write only albedo), `Normal` (write only normal), `Full` (write albedo + normal + roughness)
- [ ] 2.4 Update `.uzscene` parser — serialize/deserialize `CDecal`

## 3. DBuffer Render Targets

- [ ] 3.1 Define DBuffer RT layout: 3 render targets at GBuffer resolution
  - `DBuffer_Albedo`: `RGBA8_UNORM` (premultiplied albedo + alpha mask)
  - `DBuffer_Normal`: `R10G10B10A2_UNORM` (encoded normal XY + roughness + write mask)
  - `DBuffer_Roughness`: `R8_UNORM` (roughness override mask)
- [ ] 3.2 Register DBuffer RTs as persistent `RgResource` in `RenderGraph` (resized with viewport)
- [ ] 3.3 Clear DBuffer RTs at frame start (clear albedo to 0, normal to (0.5, 0.5, 1, 0))

## 4. Decal Pass

- [ ] 4.1 Create `src/engine/renderer/passes/decal_pass.h/cpp`
- [ ] 4.2 Collect all entities with `CDecal` component each frame; sort by `sort_order`
- [ ] 4.3 Implement `DecalPass` as `RgPass`:
  - Reads: GBuffer depth (persistent), per-decal material textures (SRV)
  - Writes: DBuffer_Albedo, DBuffer_Normal, DBuffer_Roughness (RTV)
- [ ] 4.4 Per-decal draw call: render unit cube transformed by decal OBB
- [ ] 4.5 Back-face culling for decal box — cull when camera is inside decal box (reverse winding)
- [ ] 4.6 Sort decals back-to-front for correct alpha blend ordering

## 5. Decal Projection Shader

- [ ] 5.1 Create `shaders/decal.hlsl`
- [ ] 5.2 Implement depth → world position reconstruction: `UnprojectDepth(screenUV, depth, inv_view_proj)`
- [ ] 5.3 Transform world position into decal local space: `mul(world_pos, decal_inv_world)`
- [ ] 5.4 Clip pixels outside [-1, 1] in decal local space (discard)
- [ ] 5.5 Compute decal UV: XZ of local position (Y is projection axis)
- [ ] 5.6 Sample decal albedo, normal, roughness textures using decal UV
- [ ] 5.7 Apply fade: compute screen-space distance from decal center, lerp alpha by [fade_start, fade_end]
- [ ] 5.8 Write to DBuffer RTs using blend mode mask

## 6. Lighting Pass Integration

- [ ] 6.1 Update lighting pass shader — read `DBuffer_Albedo`, `DBuffer_Normal`, `DBuffer_Roughness`
- [ ] 6.2 Composite DBuffer over GBuffer: `final_albedo = lerp(gbuffer_albedo, dbuffer_albedo.rgb, dbuffer_albedo.a)`
- [ ] 6.3 Composite DBuffer normal: `final_normal = lerp(gbuffer_normal, decode_normal(dbuffer_normal), dbuffer_normal.a)`
- [ ] 6.4 Composite DBuffer roughness: `final_roughness = lerp(gbuffer_roughness, dbuffer_roughness, dbuffer_roughness_mask)`

## 7. Editor Gizmo

- [ ] 7.1 Render `CDecal` projection box as wireframe OBB in editor using editor gizmo pass
- [ ] 7.2 Show selection highlight (yellow wireframe) when `CDecal` entity selected
- [ ] 7.3 Allow scale handles to resize `half_extents` via editor gizmo

## 8. Testing

- [ ] 8.1 Unit test: `CDecal` component serializes/deserializes in `.uzscene` correctly
- [ ] 8.2 Unit test: decal shader clips pixels outside local [-1,1] box (zero alpha outside)
- [ ] 8.3 Unit test: depth reconstruction produces correct world position for known camera/depth values
- [ ] 8.4 Unit test: DBuffer clear — all pixels start at zero alpha (no decal influence before DecalPass)
- [ ] 8.5 Integration test: place decal on floor mesh — DBuffer_Albedo shows decal texture at correct screen region
- [ ] 8.6 Integration test: `blend_mode = Normal` — only normal DBuffer written; albedo unchanged
- [ ] 8.7 Integration test: fade — decal alpha zero at `fade_end_distance` from camera
- [ ] 8.8 Visual test: DX12 validation layer reports zero errors with 16 overlapping decals

## 9. Documentation

- [ ] 9.1 Update `docs/specs/renderer.md` — "Deferred Decal System" section with DBuffer layout
- [ ] 9.2 Update `docs/guides/scene-authoring.md` — "Placing Decals" section with `CDecal` fields
