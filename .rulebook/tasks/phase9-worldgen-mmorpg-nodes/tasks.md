# Tasks: phase9-worldgen-mmorpg-nodes

## 1. Plateau Node

- [ ] 1.1 Create `tools/worldgen/nodes/mmorpg/Plateau.cpp` implementing `Plateau::Execute(ctx, inputs, outputs)`
- [ ] 1.2 Implement two-pass algorithm: first pass identifies pixels within `blend_radius` of each `target_heights[]` band; second pass applies Gaussian-weighted height correction proportional to distance from band center
- [ ] 1.3 Implement `min_size_m2` threshold: remove plateau patches below the minimum area using connected component labeling (8-connected)
- [ ] 1.4 Emit `plateau_mask` buffer (float32, 1.0 within plateau areas) alongside modified `height` output
- [ ] 1.5 Parameters: `target_heights[]` (array of normalized heights 0–1), `flatness` (0–1), `blend_radius` (meters), `min_size_m2`

## 2. TerracedLevels Node

- [ ] 2.1 Create `tools/worldgen/nodes/mmorpg/TerracedLevels.cpp` implementing `TerracedLevels::Execute`
- [ ] 2.2 Implement height quantization: divide height range into `num_levels` discrete bands each `level_spacing_m` apart; snap pixels to nearest band
- [ ] 2.3 Implement ramp auto-placement: compute level boundary contour using marching squares; subdivide contour into `ramp_count` angular positions per level transition; carve Bezier-spline ramp paths with width `ramp_width_m`
- [ ] 2.4 Enforce slope constraint at construction time: `ramp_slope_max ≤ 35°` (character controller walkable limit); fail with structured error if violated
- [ ] 2.5 Emit `level_mask` (uint8 per-pixel level index 0..N-1) and `ramp_mask` (float32, 1.0 on ramp corridors)
- [ ] 2.6 Parameters: `num_levels`, `level_spacing_m`, `ramp_count`, `ramp_width_m`, `ramp_slope_max`

## 3. CliffContour Node

- [ ] 3.1 Create `tools/worldgen/nodes/mmorpg/CliffContour.cpp` implementing `CliffContour::Execute`
- [ ] 3.2 Compute slope map via central difference gradient of the height buffer
- [ ] 3.3 Run marching squares algorithm to trace iso-contour polylines at `slope_threshold`
- [ ] 3.4 For each contour polyline segment longer than `min_cliff_length_m`: heighten uphill side by `cliff_height_m`; drop downhill side sharply
- [ ] 3.5 Apply `cliff_noise_scale` Perlin noise to horizontally perturb contour positions (prevents straight cliff lines)
- [ ] 3.6 For segments selected by `overhang_probability`: extend uphill side 2–4m into an overhang geometry
- [ ] 3.7 Emit `cliff_mask` (float32, 1.0 on cliff faces)
- [ ] 3.8 Parameters: `slope_threshold`, `cliff_height_m`, `min_cliff_length_m`, `cliff_noise_scale`, `overhang_probability`

## 4. AccessRamp Node

- [ ] 4.1 Create `tools/worldgen/nodes/mmorpg/AccessRamp.cpp` implementing `AccessRamp::Execute`
- [ ] 4.2 Detect steep edge regions by scanning adjacent pixel pairs where height delta exceeds `detect_threshold`
- [ ] 4.3 Cluster edge pixels into contiguous edge segments using connected component analysis
- [ ] 4.4 For each edge segment longer than `min_edge_length_m`: carve ramp at segment midpoint (or at explicit `positions[]` if provided)
- [ ] 4.5 Carve ramp using Gaussian-profile height corridor; compute required ramp length from height delta and `ramp_slope_max` limit
- [ ] 4.6 Apply 30m smooth approach and exit zones via 1D Gaussian kernel along path direction
- [ ] 4.7 Accept optional `level_mask` input from `TerracedLevels` to position ramps at level boundary midpoints
- [ ] 4.8 Parameters: `detect_threshold`, `min_edge_length_m`, `ramp_slope_max`, `positions[]` (optional)

## 5. CombatArena Node

- [ ] 5.1 Create `tools/worldgen/nodes/mmorpg/CombatArena.cpp` implementing `CombatArena::Execute`
- [ ] 5.2 Flatten circular or rectangular footprints at specified `positions[]` or auto-detected flat areas from `plateau_mask`
- [ ] 5.3 Support three `border_type` options: `cliff` (steep drop at arena edge), `slope` (smooth transition), `wall_marker` (flat border for collision-wall placement)
- [ ] 5.4 Apply `flatness >= 0.98` (near-perfect flat) to arena interior for combat precision
- [ ] 5.5 Support `height_target: "nearest_plateau"` to auto-snap arena to nearest Plateau band
- [ ] 5.6 Emit `arena_mask` used by `SpawnpointOutput` to categorize positions as combat zones
- [ ] 5.7 Parameters: `positions[]`, `radius_m`, `shape: circle|rect`, `border_type`, `height_target`

## 6. SpawnpointOutput Node (MMORPG-specific)

- [ ] 6.1 Create `tools/worldgen/nodes/mmorpg/SpawnpointOutput.cpp` implementing MMORPG spawn placement
- [ ] 6.2 Detect town center candidates: large flat lowland areas (flatness score from local height variance)
- [ ] 6.3 Detect dungeon entrance candidates: cliff_mask adjacent to flat area
- [ ] 6.4 Place resource nodes in biome-specific areas using `biome_map` lookup
- [ ] 6.5 Place player spawns on flat ground with no `cliff_mask` or `arena_mask` within 50m
- [ ] 6.6 Enforce minimum spawn spacing using Poisson disk sampling
- [ ] 6.7 Output `{name}_spawns.json` with typed spawn entries

## 7. RoadNetwork Node (MMORPG-specific)

- [ ] 7.1 Create `tools/worldgen/nodes/mmorpg/RoadNetwork.cpp` implementing road graph generation
- [ ] 7.2 Build road graph between key points using Prim's MST over straight-line distances
- [ ] 7.3 Trace each road edge as a terrain-following spline using A* with slope cost: `G(n) = distance + slope × slope_penalty_factor`
- [ ] 7.4 Carve flat corridor of `road_width_m` into heightmap using trapezoidal cross-section (flat center, smooth shoulders)
- [ ] 7.5 Paint `splatmap_layer` index into biome map along road corridors; emit `road_mask` for foliage exclusion
- [ ] 7.6 Accept `spawn_data` input from `SpawnpointOutput` to auto-connect town centers and dungeon entrances

## 8. MMORPG Validation Rules

- [ ] 8.1 Add MMORPG-style validation rules to `tools/worldgen/WorldGenValidator.cpp`
- [ ] 8.2 For graphs with `"style": "mmorpg"`: require at least one `TerracedLevels` or `Plateau` node
- [ ] 8.3 Enforce `ramp_slope_max ≤ 35` on all `AccessRamp` and `TerracedLevels` nodes
- [ ] 8.4 Require `SpawnpointOutput` presence in MMORPG graphs
- [ ] 8.5 Enforce `CliffContour.overhang_probability ≤ 0.3`

## 9. Node Thumbnail Previews

- [ ] 9.1 Implement 64×64 greyscale preview PNG output after each MMORPG node execution using a dedicated downsample pass
- [ ] 9.2 Send thumbnail PNG via IPC as part of `worldgen.progress` events (base64-encoded)

## 10. Testing (≥95% line + branch coverage)

- [ ] 10.1 Write `tools/worldgen/tests/test_plateau.cpp`: run `Plateau` on 512×512 synthetic height map with two target heights; assert pixels within plateau zones are within 0.01 of target heights
- [ ] 10.2 Write `tools/worldgen/tests/test_terraced_levels_ramp_slope.cpp`: run `TerracedLevels`; walk each ramp pixel by pixel; assert slope never exceeds `ramp_slope_max + 0.5°` tolerance
- [ ] 10.3 Write `tools/worldgen/tests/test_cliff_contour_mask.cpp`: run `CliffContour` on a synthetic heightmap with a known steep edge; assert `cliff_mask` covers ≥ 80% of pixels with slope > threshold
- [ ] 10.4 Write `tools/worldgen/tests/test_access_ramp_walkable.cpp`: run `AccessRamp`; simulate character controller traversal (step-by-step slope check); assert entire ramp path has slope ≤ `ramp_slope_max`
- [ ] 10.5 Write `tools/worldgen/tests/test_spawnpoint_output.cpp`: run `SpawnpointOutput` on a known flat/cliff map; assert at least `town_center_count` spawns placed with mutual distance ≥ `min_spawn_spacing_m`
- [ ] 10.6 Write `tools/worldgen/tests/test_mmorpg_validation.cpp`: graphs missing `TerracedLevels`, with `ramp_slope_max > 35`, without `SpawnpointOutput` — assert each fails MMORPG validation
- [ ] 10.7 Verify ≥95% line and branch coverage across all `tools/worldgen/nodes/mmorpg/` source files using OpenCppCoverage
