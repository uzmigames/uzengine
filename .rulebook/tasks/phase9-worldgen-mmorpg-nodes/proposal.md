# Proposal: phase4-worldgen-mmorpg-nodes

## Why

Generic procedural terrain generators — Perlin noise stacks, thermal erosion, hydraulic
erosion — produce landscapes that look visually organic but are fundamentally hostile to
MMORPG gameplay. The core problem is that real-world terrain optimization and MMORPG gameplay
optimization pull in opposite directions: natural terrain maximizes slope variation and
minimizes flat ground, while MMORPG combat requires the opposite. Spell-casters need clear
line-of-sight across flat ground to aim AoE abilities. Tank-healer-DPS positioning breaks
down on a slope because characters slide or occupy different height layers that cause hitbox
mismatches. Melee abilities with fixed range look broken when one character is 3m higher than
another on a hill. The solution is not to flatten the entire world — that produces boring,
featureless terrain — but to create distinct flat combat plateaus at well-defined elevation
bands with natural organic terrain between them. This is the defining terrain pattern of
successful action MMORPGs: World of Warcraft's Arathi Highlands, Black Desert Online's
Mediah, Guild Wars 2's Lornar's Pass all use this exact structure. Producing it manually in
an editor takes professional level designers weeks. Automating it requires MMORPG-domain
knowledge encoded directly into the generator nodes.

The second critical constraint is vertical gameplay. Dungeon MMORPGs, castle-siege games, and
any game with tiered city layouts require multiple elevation levels with controlled transitions.
The nightmare scenario in these games is a cliff face that players cannot traverse — they walk
to the edge, see the dungeon entrance 30m below, and cannot get there. This produces support
tickets, frustrated players, and embarrassing patches to add invisible ramps. The
`TerracedLevels` and `AccessRamp` nodes encode the constraint that every elevation transition
must have walkable ramp geometry at a maximum slope that the character controller can traverse,
and they enforce it algorithmically — it is structurally impossible to generate a world with
`TerracedLevels` that lacks walkable access between levels.

The third uniquely MMORPG requirement is that cliff geometry must look natural and follow
terrain contours, not appear as a uniform slope-threshold cutoff. The difference between
generic slope-based cliff generation and the `CliffContour` approach is the difference between
early Minecraft terrain (uniform staircase cliffs) and ARK: Survival Evolved terrain (cliffs
that wrap organically around mountain silhouettes, following the iso-contour of the slope map).
The contour-tracing approach in `CliffContour` produces the characteristic layered mountain
profiles that players associate with high-quality open-world terrain.

## What Changes

- **`Plateau` node** (`tools/worldgen/nodes/mmorpg/Plateau.cpp`) — detects terrain within
  configurable height bands (`target_heights[]`) and snaps it toward those heights with
  configurable `flatness` strength; uses a two-pass algorithm: first pass identifies pixels
  within `blend_radius` of each target band, second pass applies Gaussian-weighted height
  correction proportional to distance from band center; post-process removes plateau patches
  below `min_size_m2` threshold (pixel-count test using connected component labeling); emits
  `plateau_mask` buffer (float32, 1.0 within plateau areas) alongside modified `height` output;
  the mask is consumed by `BiomeAssign` (to assign appropriate biome at plateau zones),
  `FoliageOutput` (no dense trees on flat combat areas), and `CombatArena` (preferred placement
  on plateau surfaces)

- **`TerracedLevels` node** (`tools/worldgen/nodes/mmorpg/TerracedLevels.cpp`) — quantizes
  height distribution into `num_levels` discrete bands, each `level_spacing_m` apart in world
  height; carves step edges between levels; auto-places `ramp_count` ramps per transition using
  an angular subdivision of the level boundary contour (equally-spaced angular positions around
  each level edge ring); each ramp is a bezier-spline path carved into the heightmap with width
  `ramp_width_m` and slope constrained to `<= ramp_slope_max` degrees; enforces the constraint
  `ramp_slope_max <= 35` at construction time (engine character controller walkable limit);
  emits `level_mask` (uint8 per-pixel level index 0..N-1) and `ramp_mask` (float32, 1.0 on
  ramp corridors) for consumption by downstream nodes

- **`CliffContour` node** (`tools/worldgen/nodes/mmorpg/CliffContour.cpp`) — computes a
  slope map via central difference gradient of the height buffer; runs marching squares
  algorithm to trace iso-contour polylines at `slope_threshold`; for each contour polyline
  segment longer than `min_cliff_length_m`, applies cliff extrusion: heighten uphill side by
  `cliff_height_m`, sharply drop downhill side; applies `cliff_noise_scale` Perlin noise to
  perturb contour positions horizontally (prevents perfectly straight cliff lines); for segments
  selected by `overhang_probability`, extends the uphill side 2–4m into an overhang; emits
  `cliff_mask` (float32, 1.0 on cliff faces) that downstream biome and foliage nodes use to
  assign cliff-specific materials and exclude non-cliff vegetation

- **`AccessRamp` node** (`tools/worldgen/nodes/mmorpg/AccessRamp.cpp`) — post-process ramp
  placement that works on any input heightmap (not just `TerracedLevels` output); detects
  steep edge regions by scanning for adjacent pixel pairs where height delta exceeds
  `detect_threshold`; clusters edge pixels into contiguous edge segments; for each edge segment
  longer than `min_edge_length_m`, carves a ramp at the segment midpoint (or at explicit
  `positions[]` if provided); ramp carving uses a Gaussian-profile height corridor from bottom
  to top of the edge; slope constraint `ramp_slope_max` enforced by computing required ramp
  length from height delta and slope limit; smooth approach and exit zones (30m) applied via
  one-dimensional Gaussian kernel along the path direction; accepts optional `level_mask` input
  from `TerracedLevels` to position ramps at precise level boundary midpoints

- **`CombatArena` node** (`tools/worldgen/nodes/mmorpg/CombatArena.cpp`) — flattens circular
  or rectangular footprints at specified positions (or auto-detected flat areas from
  `plateau_mask`); supports three border types: `cliff` (steep cliff drop at arena edge),
  `slope` (smooth slope transition), `wall_marker` (flat border, no height change — for
  collision-wall placement by spawn system); arena interior uses `flatness >= 0.98` (near-
  perfect flat for combat precision); `height_target` can be a literal normalized height value
  or `"nearest_plateau"` (auto-snaps to the nearest Plateau band in the height distribution);
  emits `arena_mask` used by `SpawnpointOutput` to categorize arena positions as combat zones

- **`SpawnpointOutput` node** (`tools/worldgen/nodes/mmorpg/SpawnpointOutput.cpp`) — terrain
  analysis pipeline that infers gameplay-meaningful spawn positions from buffer data: town
  centers are placed on large flat lowland areas (flatness score from local height variance);
  dungeon entrances at cliff bases (high cliff_mask adjacent to flat area); resource nodes in
  biome-specific areas (biome_map lookup per `resource` biome field); player spawns on flat
  ground with no cliff_mask or arena_mask within 50m; minimum spawn spacing enforced via
  Poisson disk sampling; outputs `{name}_spawns.json` with typed entries consumed by the scene
  spawner system

- **`RoadNetwork` node** (`tools/worldgen/nodes/mmorpg/RoadNetwork.cpp`) — builds a road
  graph between key points using Prim's MST over straight-line distances, then traces each
  road edge as a terrain-following spline using A* with slope cost (`G(n) = distance + slope *
  slope_penalty_factor`); carves a flat corridor of `road_width_m` into the heightmap along
  each spline using a trapezoidal cross-section (flat center, smooth shoulders); paints
  `splatmap_layer` index into the biome map along road corridors; emits `road_mask` for
  foliage exclusion; accepts `spawn_data` input (from `SpawnpointOutput`) to auto-connect
  town centers and dungeon entrances without manual position specification

- **MMORPG graph validation rules** (`tools/worldgen/WorldGenValidator.cpp` additions) —
  for graphs with `style: "mmorpg"` metadata field, enforce additional rules: at least one
  `TerracedLevels` or `Plateau` node must be present; all `AccessRamp` and `TerracedLevels`
  `ramp_slope_max` values must be `<= 35`; `SpawnpointOutput` must be present (ensures
  navigability metadata is always generated for MMORPG maps); `CliffContour` `overhang_probability`
  must be `<= 0.3` (higher values produce unplayable geometry)

- **Node thumbnail previews** — each MMORPG node outputs a 64×64 greyscale preview PNG after
  execution (using a dedicated downsample pass); this thumbnail is sent via IPC as part of
  `worldgen.progress` events so the WorldGen Panel can show per-node output previews in the
  node canvas without a full recompile

- **Unit tests** (`tools/worldgen/tests/`) — Google Test suite for each MMORPG node; tests
  verify: `Plateau` produces pixels within 0.01 of target heights in plateau zones, ramp slope
  never exceeds `ramp_slope_max + 0.5°` tolerance, `CliffContour` cliff_mask covers >= 80% of
  pixels with slope > threshold, `AccessRamp` carves a path that CCharacterController can
  traverse (simulated walk test), `SpawnpointOutput` places at least `town_center_count` spawns
  with mutual distance >= `min_spawn_spacing_m`

## Impact

- **Affected specs**: `docs/specs/engine/procedural-world-gen.md` (this task implements
  §§ 3.2, 3.6 fully — all MMORPG-specific nodes), `docs/specs/physics/terrain-system.md`
  (ramp geometry interacts with Jolt HeightFieldShape and CCharacterController walkable slope),
  `docs/specs/gameplay/navmesh.md` (ramp_mask and arena_mask feed NavMesh area type metadata)
- **Affected code**: `tools/worldgen/nodes/mmorpg/` (6 new node implementations),
  `tools/worldgen/WorldGenValidator.cpp` (MMORPG-style validation rules),
  `tools/worldgen/tests/` (unit test suite)
- **Breaking change**: NO — these are new nodes added to the existing WorldGenCompiler node
  registry; existing graphs without these nodes are unaffected; the MMORPG validation rules
  only activate when the graph root has `"style": "mmorpg"` set
- **User benefit**: MMORPG world designers can produce playable terrain with combat-ready flat
  plateaus, correctly-sloped walkable ramps between elevation levels, and dramatic ARK-style
  cliff formations — geometry that previously required multiple days of manual sculpting and
  extensive QA passes for walkability — in a single automated compilation pass that enforces
  gameplay constraints by construction
