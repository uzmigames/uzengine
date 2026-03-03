# Proposal: phase4-worldgen-graph-compiler

## Why

Every large open-world or MMORPG game reaches a point in production where hand-sculpting
terrain in an editor becomes the primary bottleneck for content creation. A world designer
who wants to place a mountain range, a series of forest-covered valleys, and several dungeon
approach zones must individually raise, lower, and paint tens of thousands of terrain pixels —
work that can take days for a single zone, and that must be redone every time a gameplay
constraint changes (the combat designer decides all arenas need to be 10m higher, or the
level designer reroutes the main road). The result is that world layouts ossify early in
production because revisiting them is prohibitively costly, even when gameplay testing reveals
the layout isn't working. A node graph compiler that executes the terrain generation
deterministically from a configuration file transforms this from a manual sculpting job into
a parameter-tuning job: change `num_levels` from 2 to 3, press Generate, and the entire world
re-derives its elevation structure, biomes, and foliage automatically.

The `.uzworldgen` JSON graph format is designed to be the authoritative source of truth for a
generated terrain zone. It is committed to version control, is diff-readable as plain text,
and can be generated either by a human designer using the WorldGen Panel node editor or by
Claude via the `world_generate` MCP tool. This dual authorship path is critical: designers
who are comfortable with node graphs can configure them precisely, while designers who think
in narrative terms ("a three-tiered highland fortress with pine forests at the base") can
describe what they want and receive a complete graph in seconds, then tweak individual
parameters afterwards. The graph format is the shared language between human intuition and AI
generation.

The WorldGenCompiler itself is a standalone tool-mode binary that links the engine's job
system and math libraries but does not require a GPU or DX12 context. This is intentional —
generation must be possible in a CI/CD pipeline (auto-generate terrain for each build), in
headless server environments, and on machines without a capable GPU. The progressive preview
architecture (256×256 result in under 2 seconds, then full resolution in the background)
ensures interactive use in the VSCode WorldGen Panel feels responsive even for complex graphs
with expensive erosion nodes.

## What Changes

- **`.uzworldgen` JSON format** — new file format: versioned JSON node graph with `nodes[]`
  array, `biomes{}` dictionary, and world-level settings (`world_size_km`, `heightmap_resolution`,
  `seed`, `output_dir`); format specified in `docs/specs/engine/procedural-world-gen.md § 2`;
  file extension registered with VSCode for syntax highlighting and custom editor association

- **`WorldGenCompiler` binary** (`tools/worldgen/WorldGenCompiler.cpp`) — standalone C++23
  tool-mode binary; parses `.uzworldgen` JSON (using the engine's existing JSON parser);
  performs topological sort (Kahn's algorithm) on the node DAG; allocates float32 heightmap
  buffers per slot; dispatches node execution using `uz_job_system` with parallel execution for
  independent topo-level nodes; writes output PNG files via `stb_image_write`; writes
  `.uzterrain` INI; sends IPC events for preview and progress

- **Height Generator nodes** (`tools/worldgen/nodes/generators/`) — `FractalNoise` (Perlin +
  Simplex + Value, multi-octave), `VoronoiIslands` (cell distance field with configurable
  falloff), `RadialGradient` (circular gradient with power falloff), `FlatPlane` (constant
  with optional micro-noise), `HeightmapImport` (load external 16-bit PNG via stb_image);
  all nodes implement the `IWorldGenNode` interface: `execute(ctx, inputs, outputs)`

- **Modifier nodes** (`tools/worldgen/nodes/modifiers/`) — `ThermalErosion` (slope-based
  material movement simulation), `HydraulicErosion` (particle droplet simulation, 2000
  iterations at 2048×2048 in < 30 seconds using SIMD-optimized inner loop), `GaussianSmooth`
  (separable Gaussian filter, tiled for cache efficiency), `Warp` (domain warp via secondary
  noise field), `HeightRemap` (linear/smooth/power/spline range remap), `Mask` (apply mask
  buffer to restrict modifier effect area)

- **Combiner nodes** (`tools/worldgen/nodes/combiners/`) — `HeightBlend` (weighted lerp or
  mask-driven), `HeightMax` (per-pixel maximum), `HeightMin` (per-pixel minimum),
  `BiomeSelect` (condition-based biome selection with moisture noise)

- **Biome nodes** (`tools/worldgen/nodes/biomes/`) — `BiomeAssign` (per-pixel biome ID
  assignment based on height + slope + optional moisture and level_mask conditions; outputs
  uint8 biome_map buffer), `BiomeDefinition` (declarative biome registration: height_range,
  slope_max, splatmap_layers[4], foliage defs, ambient_tint, wind_speed)

- **Output nodes** (`tools/worldgen/nodes/outputs/`) — `TerrainOutput` (float32 → uint16 →
  16-bit PNG heightmap + `.uzterrain` INI writer), `SplatmapOutput` (biome_map → RGBA8 PNG
  with Gaussian blend zones at biome boundaries), `FoliageOutput` (per-foliage-type R8 PNG
  density maps derived from biome foliage defs + slope/height exclusion), `SpawnpointOutput`
  (terrain analysis → `{name}_spawns.json` with typed spawn positions), `RoadNetwork` (A*
  path on slope map → heightmap carve + splatmap paint + road_mask output)

- **Buffer memory management** (`tools/worldgen/WorldGenBufferPool.h`) — fixed-size pool of
  float32 buffers; size = `resolution × resolution × sizeof(float)`; maximum 32 buffers
  allocated simultaneously; buffers returned to pool after downstream nodes complete
  consumption (reference counted); at 2048×2048 maximum pool RAM = 512 MB; streaming tile
  fallback for 4096×4096 (1024×1024 tiles with 128px overlap margins)

- **IPC integration** (`tools/worldgen/WorldGenIPC.cpp`) — sends `worldgen.preview`,
  `worldgen.progress`, `worldgen.complete`, `worldgen.error` events over the named pipe /
  WebSocket shared with the engine; receives `worldgen.generate` and `worldgen.cancel`
  commands; the compiler can run in interactive mode (stays alive, accepts multiple generate
  commands) or one-shot mode (compile one graph and exit)

- **Validation layer** (`tools/worldgen/WorldGenValidator.cpp`) — runs before execution;
  checks: single TerrainOutput present, no DAG cycles (DFS), all input connections resolve
  to declared outputs, `ramp_slope_max <= 35`, biome keys exist in biomes dict, splatmap
  layer count <= 4; outputs structured error JSON to stderr and IPC on validation failure

- **CMake integration** — `tools/worldgen/CMakeLists.txt` adds `UzWorldGenCompiler` target;
  links `uz_core`, `uz_job_system`, `uz_math`; does not link `uz_renderer` or `uz_physics`;
  output to `tools/bin/UzWorldGenCompiler.exe`

## Impact

- **Affected specs**: `docs/specs/engine/procedural-world-gen.md` (this task implements
  §§ 2–4), `docs/specs/physics/terrain-system.md` (Integration with WorldGen section)
- **Affected code**: `tools/worldgen/` (new directory), `tools/CMakeLists.txt` (new
  target), `vscode-extension/src/worldgenPanel.ts` (IPC interface for compiler events)
- **Breaking change**: NO — the WorldGen compiler is a new standalone tool; the existing
  terrain system, renderer, and physics are unchanged; generated `.uzterrain` files are
  valid inputs to the existing `uz_terrain` plugin with no modifications
- **User benefit**: World designers can go from text description or node graph configuration
  to a fully playable terrain zone (heightmap, splatmap, foliage, navmesh-ready) in under
  2 minutes, replacing days of manual sculpting; changes to world structure (add a level,
  change biome distribution) take seconds to recompile rather than hours to re-sculpt
