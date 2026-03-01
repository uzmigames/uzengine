## 1. ECS Components

- [ ] 1.1 Extend `CPointLight` in `src/engine/components/point_light.h` -- add `cast_shadow: bool` field (default false)
- [ ] 1.2 Create `CSpotLight` in `src/engine/components/spot_light.h` -- color, intensity, range, inner_angle, outer_angle, cast_shadow
- [ ] 1.3 Register both components in ECS component registry
- [ ] 1.4 Scene loader: parse `PointLight` and `SpotLight` node types from `.uzscene`
- [ ] 1.5 IPC handlers: `light.setPointLight`, `light.setSpotLight` for runtime property changes

## 2. Shadow Atlas Infrastructure

- [ ] 2.1 Create `src/renderer/shadow_system/shadow_atlas.h/cpp` -- generic tile allocator
- [ ] 2.2 Atlas allocate/release API: assign a 512x512 tile to a light, free when light is destroyed
- [ ] 2.3 Point light shadow atlas: `D32_FLOAT` 2048x2048, 4x4 grid of 512x512 tiles (16 faces = 8 lights)
- [ ] 2.4 Spot light shadow atlas: `D32_FLOAT` 2048x2048, 4x4 grid of 512x512 tiles (16 lights)
- [ ] 2.5 Create DSV and SRV descriptors for both atlases
- [ ] 2.6 Atlas resize support via `project.uzproj` settings (default 2048x2048)

## 3. Point Light Shadow Rendering (Dual-Paraboloid)

- [ ] 3.1 Create `src/renderer/shadow_system/point_shadow_system.h/cpp`
- [ ] 3.2 Build light-space view matrix for each shadow-casting point light (front + back hemisphere)
- [ ] 3.3 Create `shaders/builtin/point_shadow_depth.hlsl` -- paraboloid projection vertex shader
- [ ] 3.4 Paraboloid VS: transform to light-space, normalize, project via `xy / (z * sign + 1.0)`, store `L/radius` as depth
- [ ] 3.5 Render front hemisphere (dot(L_to_obj, forward) >= 0) to front atlas tile
- [ ] 3.6 Render back hemisphere to back atlas tile (adjacent row in atlas)
- [ ] 3.7 Depth bias: constant + slope-scaled to prevent shadow acne
- [ ] 3.8 Frustum cull objects against light sphere before rendering

## 4. Spot Light Shadow Rendering

- [ ] 4.1 Create `src/renderer/shadow_system/spot_shadow_system.h/cpp`
- [ ] 4.2 Build perspective projection from spot light cone: fov = outer_angle * 2, near=0.1, far=range
- [ ] 4.3 Build view matrix from spot light position + direction
- [ ] 4.4 Reuse existing `shaders/builtin/shadow_depth.hlsl` (standard depth-only VS)
- [ ] 4.5 Render shadow casters within spot light frustum to atlas tile
- [ ] 4.6 Depth bias: constant + slope-scaled
- [ ] 4.7 Frustum cull objects against spot light cone before rendering

## 5. Render Pass Registration

- [ ] 5.1 Create `src/renderer/passes/point_shadow_pass.h/cpp` -- register at sort_key=110
- [ ] 5.2 Create `src/renderer/passes/spot_shadow_pass.h/cpp` -- register at sort_key=120
- [ ] 5.3 Each pass iterates shadow-casting lights, renders to respective atlas
- [ ] 5.4 Resource barriers: transition atlas from SRV to DSV before rendering, DSV to SRV after

## 6. Shadow Data Upload

- [ ] 6.1 Create `PointLightShadowData` struct: position, radius, atlas_offset, atlas_scale, texel_size
- [ ] 6.2 Create `SpotLightShadowData` struct: view_proj matrix, atlas_offset, atlas_scale, texel_size
- [ ] 6.3 Upload via `StructuredBuffer<PointLightShadowData>` and `StructuredBuffer<SpotLightShadowData>`
- [ ] 6.4 Bind both structured buffers in lighting pass descriptor table

## 7. Shadow Sampling in Lighting Shader

- [ ] 7.1 Add `sample_point_shadow()` function in deferred_lighting.hlsl (uses shadow.hlsli PCF 2x2)
- [ ] 7.2 Add `sample_spot_shadow()` function in deferred_lighting.hlsl (uses shadow.hlsli PCF 2x2)
- [ ] 7.3 In point light loop: if cast_shadow, multiply radiance by shadow factor
- [ ] 7.4 In spot light loop: if cast_shadow, multiply radiance by shadow factor
- [ ] 7.5 Bind point and spot shadow atlas SRVs in lighting pass

## 8. Light Culling Compute Pass

- [ ] 8.1 Create `src/renderer/passes/light_cull_pass.h/cpp` -- compute pass at sort_key=350
- [ ] 8.2 Divide screen into 16x16 tiles
- [ ] 8.3 Per-tile: compute min/max depth from depth buffer
- [ ] 8.4 Per-tile: test each point/spot light sphere/cone against tile frustum
- [ ] 8.5 Write per-tile light index lists to `RWStructuredBuffer<TileLightList>`
- [ ] 8.6 Lighting pass reads per-tile lists instead of iterating all lights per pixel

## 9. Shadow Update Budget

- [ ] 9.1 Max shadow renders per frame: configurable (default 12)
- [ ] 9.2 Priority: lights whose transform changed > closer lights > further lights
- [ ] 9.3 Static light optimization: if light + all objects in radius are static, cache shadow map
- [ ] 9.4 Track per-light dirty flag: set when light moves or nearby objects change

## 10. Configuration (project.uzproj)

- [ ] 10.1 `renderer.shadows.point_shadow_atlas_size` (default 2048)
- [ ] 10.2 `renderer.shadows.spot_shadow_atlas_size` (default 2048)
- [ ] 10.3 `renderer.shadows.max_shadow_point_lights` (default 8)
- [ ] 10.4 `renderer.shadows.max_shadow_spot_lights` (default 16)
- [ ] 10.5 `renderer.shadows.shadow_tile_resolution` (default 512)
- [ ] 10.6 `renderer.shadows.shadow_update_budget` (default 12)

## 11. IPC Handlers

- [ ] 11.1 `renderer.getShadowStats` -- return shadow atlas usage, render counts
- [ ] 11.2 `light.setPointLight` / `light.setSpotLight` -- update light properties at runtime

## 12. Testing

- [ ] 12.1 Point light + ground plane: visible circular shadow
- [ ] 12.2 Spot light + wall: visible cone-shaped shadow
- [ ] 12.3 No shadow acne on flat surfaces (verify depth bias)
- [ ] 12.4 No Peter-Panning (objects do not float above shadow)
- [ ] 12.5 Shadow atlas tiling: 8 point lights + 16 spot lights all render correctly
- [ ] 12.6 Shadow budget: only N lights update per frame, others use cached maps
- [ ] 12.7 Performance: point shadow pass < 0.5ms per light, spot < 0.3ms per light on RTX 3070
- [ ] 12.8 Tile culling: verify lights not affecting a tile are skipped

## 13. Documentation

- [ ] 13.1 Update `docs/specs/point-spot-light-shadows.md` with implementation changes
- [ ] 13.2 Update `docs/specs/renderer.md` lighting section
- [ ] 13.3 Update `docs/modules/renderer/README.md` passes table and data contract
