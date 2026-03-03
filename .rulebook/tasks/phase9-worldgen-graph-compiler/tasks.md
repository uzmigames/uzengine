# Tasks: phase9-worldgen-graph-compiler

## 1. `.uzworldgen` File Format

- [ ] 1.1 Define the `.uzworldgen` JSON schema: `{schema_version, world_size_km, heightmap_resolution, seed, output_dir, style, nodes[], biomes{}}` with `nodes[]` containing `{id, type, params{}, inputs{}, position{}}`
- [ ] 1.2 Register `.uzworldgen` file extension with VSCode for syntax highlighting via `vscode-uzengine/syntaxes/`
- [ ] 1.3 Associate `.uzworldgen` files with the custom WorldGen editor provider in the VSCode extension
- [ ] 1.4 Document the full format specification in `docs/specs/engine/procedural-world-gen.md § 2`

## 2. WorldGenCompiler Binary

- [ ] 2.1 Create `tools/worldgen/WorldGenCompiler.cpp` as a standalone C++23 binary; link `uz_core`, `uz_job_system`, `uz_math`; do NOT link `uz_renderer` or `uz_physics`
- [ ] 2.2 Implement `.uzworldgen` JSON parsing using the engine's existing JSON parser
- [ ] 2.3 Implement topological sort (Kahn's algorithm) on the node DAG; abort with structured error on cycle detection
- [ ] 2.4 Allocate `float32` heightmap buffers using `WorldGenBufferPool`
- [ ] 2.5 Dispatch node execution using `uz_job_system` with parallel execution for independent topological-level nodes
- [ ] 2.6 Write output PNG files via `stb_image_write`; write `.uzterrain` INI file
- [ ] 2.7 Add `tools/worldgen/CMakeLists.txt` target `UzWorldGenCompiler`; output to `tools/bin/UzWorldGenCompiler.exe`

## 3. WorldGenBufferPool

- [ ] 3.1 Create `tools/worldgen/WorldGenBufferPool.h` implementing a fixed-size pool of `float32` buffers; size = `resolution × resolution × sizeof(float)`
- [ ] 3.2 Maximum 32 buffers allocated simultaneously; use reference counting for downstream consumption tracking
- [ ] 3.3 Return buffers to pool after all downstream nodes complete consumption
- [ ] 3.4 Implement streaming tile fallback for 4096×4096: process in 1024×1024 tiles with 128px overlap margins

## 4. Height Generator Nodes

- [ ] 4.1 Implement `FractalNoise` node in `tools/worldgen/nodes/generators/FractalNoise.cpp`: Perlin + Simplex + Value multi-octave; parameters `octaves`, `frequency`, `persistence`, `lacunarity`
- [ ] 4.2 Implement `VoronoiIslands` node: cell distance field with configurable falloff type (linear/smooth/cubic)
- [ ] 4.3 Implement `RadialGradient` node: circular gradient with power falloff; parameters `center`, `radius`, `power`
- [ ] 4.4 Implement `FlatPlane` node: constant height with optional micro-noise for texture variation
- [ ] 4.5 Implement `HeightmapImport` node: load external 16-bit PNG via `stb_image`; normalize to [0, 1] float
- [ ] 4.6 Define `IWorldGenNode` interface: `Execute(ctx, inputs, outputs)` pure virtual method; all nodes implement this interface

## 5. Modifier Nodes

- [ ] 5.1 Implement `ThermalErosion` node: slope-based material movement simulation; parameters `iterations`, `talus_angle`, `strength`
- [ ] 5.2 Implement `HydraulicErosion` node: particle droplet simulation; 2000 iterations at 2048×2048 in < 30 seconds using SIMD-optimized inner loop; parameters `num_droplets`, `evaporation`, `deposit_speed`
- [ ] 5.3 Implement `GaussianSmooth` node: separable Gaussian filter with tiled cache-efficient implementation; parameter `sigma`
- [ ] 5.4 Implement `Warp` node: domain warp via secondary noise field; parameters `strength`, `frequency`
- [ ] 5.5 Implement `HeightRemap` node: linear/smooth/power/spline range remap; parameters `input_min`, `input_max`, `output_min`, `output_max`, `curve_type`
- [ ] 5.6 Implement `Mask` node: apply mask buffer to restrict modifier effect to masked area

## 6. Combiner Nodes

- [ ] 6.1 Implement `HeightBlend` node: weighted lerp or mask-driven blend between two height buffers
- [ ] 6.2 Implement `HeightMax` node: per-pixel maximum of two height buffers
- [ ] 6.3 Implement `HeightMin` node: per-pixel minimum of two height buffers
- [ ] 6.4 Implement `BiomeSelect` node: condition-based biome selection using moisture noise

## 7. Biome, Output, and Validation

- [ ] 7.1 Implement `BiomeAssign` node: per-pixel biome ID assignment based on height + slope + moisture/level_mask conditions; output `uint8` biome_map buffer
- [ ] 7.2 Implement `BiomeDefinition` node: declarative biome registration with `height_range`, `slope_max`, `splatmap_layers[4]`, foliage defs, `ambient_tint`, `wind_speed`
- [ ] 7.3 Implement `TerrainOutput` node: `float32 → uint16 → 16-bit PNG` heightmap + `.uzterrain` INI writer
- [ ] 7.4 Implement `SplatmapOutput` node: biome_map → RGBA8 PNG with Gaussian blend zones at biome boundaries
- [ ] 7.5 Implement `FoliageOutput` node: per-foliage-type R8 PNG density maps derived from biome foliage defs + slope/height exclusion
- [ ] 7.6 Implement `SpawnpointOutput` node: terrain analysis → `{name}_spawns.json` with typed spawn positions
- [ ] 7.7 Implement `RoadNetwork` node: A* pathfinding on slope map → heightmap carve + splatmap paint + road_mask
- [ ] 7.8 Create `tools/worldgen/WorldGenValidator.cpp`: validate single `TerrainOutput` present, no DAG cycles, all input connections resolve, `ramp_slope_max ≤ 35`, biome keys exist, splatmap layers ≤ 4

## 8. IPC Integration

- [ ] 8.1 Create `tools/worldgen/WorldGenIPC.cpp`: send `worldgen.preview`, `worldgen.progress`, `worldgen.complete`, `worldgen.error` events over named pipe/WebSocket
- [ ] 8.2 Receive `worldgen.generate` and `worldgen.cancel` commands
- [ ] 8.3 Implement interactive mode (stays alive, accepts multiple generate commands) and one-shot mode
- [ ] 8.4 Update `vscode-extension/src/worldgenPanel.ts` to handle compiler IPC events

## 9. Testing (≥95% line + branch coverage)

- [ ] 9.1 Write `tools/worldgen/tests/test_topo_sort.cpp`: create DAG with known ordering; assert Kahn's algorithm produces correct topological sequence; assert cycle detection triggers error
- [ ] 9.2 Write `tools/worldgen/tests/test_fractal_noise.cpp`: run `FractalNoise` on 256×256 buffer with fixed seed; assert output range is [0, 1]; assert non-zero variance
- [ ] 9.3 Write `tools/worldgen/tests/test_hydraulic_erosion_perf.cpp`: run `HydraulicErosion` at 2048×2048; assert completes in < 30 seconds
- [ ] 9.4 Write `tools/worldgen/tests/test_terrain_output.cpp`: run minimal graph (FlatPlane → TerrainOutput) on 256×256; assert PNG file written and readable; assert `.uzterrain` INI contains required fields
- [ ] 9.5 Write `tools/worldgen/tests/test_validator.cpp`: graphs missing `TerrainOutput`, containing cycles, with invalid biome keys — assert `WorldGenValidator` emits structured error for each
- [ ] 9.6 Write `tools/worldgen/tests/test_buffer_pool.cpp`: allocate 32 buffers; assert 33rd allocation blocks; release one; assert 33rd succeeds
- [ ] 9.7 Verify ≥95% line and branch coverage across all `tools/worldgen/` source files using OpenCppCoverage
