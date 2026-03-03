## 1. `.uzterrain` Format and Parser

- [ ] 1.1 `TerrainDef` struct: `heightmap_path`, `splatmap_path`, `width_meters`, `depth_meters`, `height_scale`, `base_height`, `chunk_size`; `LayerDef[4]`; `FoliageDef[]`; `WaterDef`
- [ ] 1.2 `TerrainParser::load(path)` — INI-like parser, returns `TerrainDef`
- [ ] 1.3 Parse up to 4 `[material.layerN]` sections: `name`, `albedo`, `normal`, `roughness`, `tiling`
- [ ] 1.4 Parse multiple `[foliage.*]` sections: `mesh`, `density`, `min/max_scale`, `random_rotation`, `max_slope`, `min/max_height`, `wind_sway`, `layer_mask`, `cull_distance`
- [ ] 1.5 Parse optional `[water]` section: all water plane properties
- [ ] 1.6 Asset import: `.uzterrain` registered as asset type; imported output in `.import/terrain/`
- [ ] 1.7 Heightmap: load 16-bit grayscale PNG via stb_image-16 or DirectXTex; store as `float[]`
- [ ] 1.8 Splatmap: load RGBA PNG; store as `uint8_t[4][]`

## 2. Terrain Node Type (Plugin)

- [ ] 2.1 `uz_terrain` plugin registers `Terrain` and `WaterPlane` node types via `UzEngineAPI_t::register_node_type`
- [ ] 2.2 `CTerrain` component: `effect_path`, `TerrainDef`, runtime GPU resources
- [ ] 2.3 `.uzscene` `[node type=Terrain]` loaded; `terrain` property set to `.uzterrain` path
- [ ] 2.4 `SceneManager::load()` calls `TerrainSystem::load_terrain(node, path)` for Terrain nodes

## 3. Chunk System

- [ ] 3.1 `TerrainChunk` struct: `grid_x/z`, `AABB bounds`, `lod_level`, `center_dist`, `vb[4]`, `ib[4]`, `index_count[4]`
- [ ] 3.2 Chunk AABB: compute min/max Y from heightmap samples within chunk extent; XZ from grid position
- [ ] 3.3 Chunk grid construction: for a 1025×1025 terrain with 32m chunks → 32×32 = 1024 chunks
- [ ] 3.4 Frustum culling: per-frame test all chunk AABBs against camera 6-plane frustum; skip invisible chunks

## 4. LOD Mesh Generation (Cook Step)

- [ ] 4.1 Cook: for each chunk, generate 4 LOD vertex grids: 33×33, 17×17, 9×9, 5×5 vertices
- [ ] 4.2 Use meshoptimizer `meshopt_simplifyPoints` or manual sub-sampling for lower LODs
- [ ] 4.3 Geomorphing: store per-vertex morph target (position of same vertex in next LOD) in VB
- [ ] 4.4 Vertex shader: blend between LOD N and N+1 positions using per-chunk morph factor from CB
- [ ] 4.5 LOD distance thresholds: 0–80m = LOD0, 80–200m = LOD1, 200–400m = LOD2, >400m = LOD3
- [ ] 4.6 Cook output: `TerrainChunkData` binary per chunk stored in `.import/terrain/<name>_chunks.bin`

## 5. Terrain Shader (`terrain.hlsl`)

- [ ] 5.1 Vertex shader: sample heightmap texture, compute world Y; apply geomorph blend
- [ ] 5.2 Pixel shader: sample splatmap for 4 weights (normalize)
- [ ] 5.3 Pixel shader: sample albedo + normal textures for each of 4 layers at respective tiling UV
- [ ] 5.4 Pixel shader: weight-blend albedo, normal, roughness across 4 layers
- [ ] 5.5 Output to G-Buffer: albedo, world normal (TBN transform), roughness, metallic=0
- [ ] 5.6 Compatible with deferred PBR pipeline (same G-Buffer layout as `renderer.hlsl`)

## 6. Collision (Jolt HeightFieldShape)

- [ ] 6.1 Extract `float[]` heightmap from 16-bit PNG: `pixel / 65535.0f * height_scale + base_height`
- [ ] 6.2 `JPH::HeightFieldShapeSettings`: `heights`, zero origin, per-sample scale, sample count 1025
- [ ] 6.3 Create Jolt `StaticBody` with `HeightFieldShape` at terrain world position
- [ ] 6.4 Collision layer: `NON_MOVING`, no mass
- [ ] 6.5 Lua API: `ctx.terrain:get_height_at(x, z)` → float (Jolt point query, O(1))
- [ ] 6.6 Lua API: `ctx.terrain:get_normal_at(x, z)` → Vec3 (central difference from heightmap)
- [ ] 6.7 Lua API: `ctx.terrain:get_weights_at(x, z)` → `{ r, g, b, a }` (splatmap sample)
- [ ] 6.8 NavMesh baking: `NavBaker::collect_geometry` includes terrain mesh triangles

## 7. GPU-Instanced Foliage

- [ ] 7.1 `FoliageInstance` HLSL struct: `float4x4 world_matrix`, `float wind_phase`, `float3 _pad`
- [ ] 7.2 Cook step: scatter instances per foliage layer using deterministic RNG (seed = layer name hash + position hash)
- [ ] 7.3 Instance filtering: skip if slope > `max_slope`, height outside `[min_height, max_height]`, splatmap weight < threshold for `layer_mask`
- [ ] 7.4 Upload `StructuredBuffer<FoliageInstance>` to GPU per foliage layer
- [ ] 7.5 GPU culling compute shader: AABB test per instance, write visible indices to `IndirectArguments` buffer
- [ ] 7.6 `ExecuteIndirect` + vertex shader reads instance matrix from `StructuredBuffer[SV_InstanceID]`
- [ ] 7.7 Wind sway in vertex shader: sinusoidal displacement using `sin(g_Time * g_WindFrequency + instance.wind_phase) * vertex.y`
- [ ] 7.8 Cull distance: instance AABB center > `cull_distance` → culled by GPU compute

## 8. Water Plane

- [ ] 8.1 Generate flat N×N quad mesh covering terrain XZ extent (N = chunk_count)
- [ ] 8.2 Water vertex shader: fixed Y = `water.height`, scrolling normal map UV offsets
- [ ] 8.3 Water pixel shader: sample 2-layer scrolling normal map, compute Fresnel
- [ ] 8.4 Edge foam: sample scene depth buffer, compute depth delta, multiply foam texture
- [ ] 8.5 Blend mode: alpha blend (render after all opaque geometry)
- [ ] 8.6 Water uniform constants: `shallow_color`, `deep_color`, `fresnel_strength`, `foam_depth`, `time`

## 9. Editor Tools (VSCode Extension)

- [ ] 9.1 Terrain Editor panel (Webview): sculpt tool palette, brush radius + strength sliders, falloff selector
- [ ] 9.2 Sculpt tools: Raise, Lower, Smooth, Flatten — send IPC `terrain.sculpt` with tool params
- [ ] 9.3 Paint tool: layer selector (0–3), paint/clear/smooth — send IPC `terrain.paintLayer`
- [ ] 9.4 Foliage Painter mode: scatter/erase instances with brush
- [ ] 9.5 Heightmap edits: IPC → engine applies edit to CPU heightmap buffer → uploads to GPU heightmap texture → rebuilds Jolt shape
- [ ] 9.6 Splatmap edits: IPC → CPU splatmap buffer → re-upload GPU splatmap texture
- [ ] 9.7 Save: write edited heightmap buffer to `.heightmap` PNG, save `.uzterrain` (if foliage changed)

## 10. IPC Handlers

- [ ] 10.1 `terrain.getHeightAt { x, z }` → `{ height }`
- [ ] 10.2 `terrain.getNormalAt { x, z }` → `{ normal: [x,y,z] }`
- [ ] 10.3 `terrain.getWeightsAt { x, z }` → `{ r, g, b, a }`
- [ ] 10.4 `terrain.sculpt { tool, center: [x,z], radius, strength, falloff }` — modifies CPU buffer + GPU upload
- [ ] 10.5 `terrain.paintLayer { layer, center: [x,z], radius, strength }` — modifies splatmap
- [ ] 10.6 `terrain.rebuildCollision {}` — rebuilds Jolt HeightFieldShape after sculpt
- [ ] 10.7 `terrain.bakeNavmesh {}` — triggers NavBaker for terrain geometry
- [ ] 10.8 `terrain.addFoliageLayer { ... }` — adds entry to `TerrainDef.foliage` and re-cooks
- [ ] 10.9 `terrain.removeFoliageLayer { name }`

## 11. Cook Step Integration

- [ ] 11.1 `CookPipeline::cook_terrain(uzterrain_path)` — parse `.uzterrain`, generate chunk LOD meshes, scatter foliage
- [ ] 11.2 Outputs: `chunks.bin` (LOD meshes), `foliage.uzfoliage` (instance point cloud), compressed heightmap + splatmap textures
- [ ] 11.3 Stale check: re-cook if heightmap/splatmap mtime newer than cook output
- [ ] 11.4 All outputs included in `.uzpak` for shipping

## 12. Testing

- [ ] 12.1 1024×1024 m terrain loads without crash; all 1024 chunks render
- [ ] 12.2 Frustum culling: confirm draw call count drops when camera points away from terrain
- [ ] 12.3 LOD transitions: drive camera toward terrain — LOD switches are not visually jarring (geomorphing)
- [ ] 12.4 Splatmap blending: 4-layer blend renders correctly with distinct materials visible in painted areas
- [ ] 12.5 Collision: character walks on terrain surface, does not clip through or float
- [ ] 12.6 `ctx.terrain:get_height_at(x, z)` returns value matching rendered surface height
- [ ] 12.7 Foliage: grass instances visible on grass-painted areas; absent on rock/snow areas
- [ ] 12.8 Water plane: renders at correct height; edge foam visible where terrain meets water
- [ ] 12.9 Sculpt tool: Raise/Lower modifies heightmap, Jolt collision updated, rendered mesh updated
- [ ] 12.10 Cook + shipping: terrain loads from `.uzpak` in `UzEngine-Runtime.exe` without editor
