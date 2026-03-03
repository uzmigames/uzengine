# Procedural World Generation System

## Overview

The Procedural World Generation system (`uz_worldgen`) is a Phase 4 tool-layer feature that
enables MMORPG and open-world designers to generate complete terrains — heightmaps, splatmaps,
foliage density maps, and spawn-point tables — from a visual node graph configuration or a
natural-language AI description, without hand-sculpting every hill and plateau in the editor.

The system is purpose-built for the constraints of magic-combat MMORPGs: terrain must be
relatively flat for spell line-of-sight, combat arenas need defensible plateau geometry, cliffs
should contour naturally around mountain edges (ARK: Survival Evolved style), and vertical
gameplay (dungeons below, midlands at ground level, highland citadels on plateaus) must emerge
automatically from configuration rather than manual sculpting.

The entire generation pipeline runs outside the engine at author time. Output is a standard
`.uzterrain` INI file compatible with the existing terrain system (Phase 4 `uz_terrain` plugin)
plus companion PNG assets. No new runtime code paths are required; the runtime simply loads the
generated `.uzterrain` through the existing loader, gets full LOD, collision, foliage, and
navmesh for free.

---

## 1. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│  Authoring Layer                                                                  │
│                                                                                  │
│   VSCode WorldGen Panel (Webview)                                                 │
│   ├── Node graph canvas (React + reactflow)                                      │
│   ├── Properties inspector (per-node parameter editor)                           │
│   ├── 3D heightmap preview (Three.js displacement mesh)                          │
│   ├── Biome editor (color swatches, foliage list, layer weights)                 │
│   ├── "Generate with AI" modal  → world_generate MCP call                       │
│   └── "Refine with AI" panel    → world_refine MCP call + screenshot feedback   │
│                                                                                  │
│   MCP Server (TypeScript)                                                        │
│   ├── world_generate(description, style, num_plateaus, biomes, seed, ...)       │
│   └── world_refine(graph_path, feedback, screenshot_base64)                     │
│                                                                                  │
└──────────────────────────────────────────────────────────────────────────────────┘
                              │  saves
                              ▼
              .uzworldgen  (JSON node graph)
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│  WorldGenCompiler  (C++23, tool-mode binary: UzWorldGenCompiler.exe)             │
│                                                                                  │
│   1. Parse .uzworldgen JSON                                                      │
│   2. Topological sort (DAG execution order)                                      │
│   3. Allocate float32 heightmap buffer (resolution × resolution)                 │
│   4. Execute nodes in order on thread pool (uz_job_system)                       │
│   5. Progressive preview: emit 256×256 result immediately via IPC               │
│   6. Refine to full resolution in background                                     │
│   7. Write output files (PNG heightmap, splatmap, foliage density maps)         │
│   8. Write .uzterrain INI file                                                   │
│   9. Trigger terrain hot-reload in engine via IPC                                │
│                                                                                  │
└──────────────────────────────────────────────────────────────────────────────────┘
                              │  writes
                              ▼
              .uzterrain  +  *.png  assets
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│  Runtime (existing uz_terrain plugin — no changes needed)                        │
│                                                                                  │
│   TerrainChunkSystem     — 4-LOD chunk rendering                                 │
│   Jolt HeightFieldShape  — O(1) collision                                        │
│   FoliageSystem          — GPU-instanced foliage from density maps               │
│   NavMeshSystem          — Recast/Detour baking over generated terrain           │
│   SplatmapShader         — PBR material blending from generated splatmap         │
│                                                                                  │
└──────────────────────────────────────────────────────────────────────────────────┘
```

### AI Generation Data Flow

```
world_generate MCP call
    │
    ├─ Step 1: System prompt = full node catalogue (this spec § 3)
    │          User prompt  = description + style + constraints
    │          → Claude API → .uzworldgen JSON graph
    │
    ├─ Step 2: WorldGenCompiler runs at 256×256 preview resolution
    │
    ├─ Step 3: engine.screenshot IPC call → base64 PNG
    │
    ├─ Step 4: Claude evaluates screenshot
    │          ("are plateaus flat? are ramps present? does biome match?")
    │          If unsatisfied → adjust node params → goto Step 2
    │          Max 3 refinement iterations
    │
    ├─ Step 5: Return { graph_path, preview_screenshot } to caller
    │
    └─ Step 6: Full-resolution compilation runs in background thread
               Sends IPC progress events to the WorldGen panel
```

---

## 2. WorldGen Graph Format (`.uzworldgen`)

### 2.1 File Structure

The `.uzworldgen` file is a UTF-8 JSON document. It is first-class project asset: stored in
`res://worldgen/` alongside the generated terrain outputs, committed to version control, and
diffable as plain text. The format is intentionally verbose — every node stores its full
parameter set — so that AI generation and human inspection are both easy.

```json
{
  "version": 1,
  "name": "MysticIslands",
  "world_size_km": 4.0,
  "heightmap_resolution": 2048,
  "seed": 42,
  "output_dir": "res://terrain/",
  "nodes": [
    {
      "id": "n1",
      "type": "FractalNoise",
      "label": "Base terrain noise",
      "position": { "x": 100, "y": 100 },
      "params": {
        "octaves": 6,
        "frequency": 0.8,
        "lacunarity": 2.1,
        "gain": 0.5,
        "offset_x": 0.0,
        "offset_y": 0.0,
        "noise_type": "simplex"
      },
      "outputs": ["height"]
    },
    {
      "id": "n2",
      "type": "ThermalErosion",
      "label": "Erode rock crumbling",
      "position": { "x": 100, "y": 280 },
      "params": {
        "iterations": 50,
        "talus": 0.5,
        "strength": 0.8
      },
      "inputs": {
        "height": "n1.height"
      },
      "outputs": ["height"]
    },
    {
      "id": "n3",
      "type": "TerracedLevels",
      "label": "3-tier MMORPG floors",
      "position": { "x": 100, "y": 460 },
      "params": {
        "num_levels": 3,
        "level_spacing_m": 40.0,
        "terrace_width_m": 200.0,
        "ramp_count": 4,
        "ramp_width_m": 12.0,
        "ramp_slope_max": 28.0
      },
      "inputs": {
        "height": "n2.height"
      },
      "outputs": ["height", "level_mask"]
    },
    {
      "id": "n4",
      "type": "CliffContour",
      "label": "ARK-style mountain edges",
      "position": { "x": 100, "y": 640 },
      "params": {
        "slope_threshold": 0.65,
        "cliff_height_m": 25.0,
        "cliff_noise_scale": 0.3,
        "overhang_probability": 0.1
      },
      "inputs": {
        "height": "n3.height"
      },
      "outputs": ["height", "cliff_mask"]
    },
    {
      "id": "n5",
      "type": "BiomeAssign",
      "label": "Assign biomes",
      "position": { "x": 400, "y": 640 },
      "params": {
        "biomes": ["lowland_forest", "midland_grass", "highland_rock", "cliff_stone"]
      },
      "inputs": {
        "height": "n4.height",
        "slope_mask": "n4.cliff_mask",
        "level_mask": "n3.level_mask"
      },
      "outputs": ["biome_map"]
    },
    {
      "id": "out",
      "type": "TerrainOutput",
      "label": "Final terrain",
      "position": { "x": 250, "y": 820 },
      "params": {},
      "inputs": {
        "height": "n4.height",
        "biome": "n5.biome_map"
      },
      "outputs": []
    }
  ],
  "biomes": {
    "lowland_forest": {
      "height_range": [0.0, 0.35],
      "slope_max": 0.4,
      "splatmap_layers": ["grass", "moss", "mud"],
      "foliage": [
        { "mesh": "res://meshes/trees/oak.uzmesh", "density": 0.08, "max_slope": 0.3 },
        { "mesh": "res://meshes/foliage/fern.uzmesh", "density": 0.5, "max_slope": 0.4 }
      ],
      "ambient_tint": [0.55, 0.75, 0.45]
    },
    "highland_rock": {
      "height_range": [0.6, 1.0],
      "slope_max": 0.8,
      "splatmap_layers": ["rock", "gravel", "snow"],
      "foliage": [
        { "mesh": "res://meshes/rocks/boulder_lg.uzmesh", "density": 0.02, "cluster": true }
      ],
      "ambient_tint": [0.7, 0.7, 0.8]
    }
  }
}
```

### 2.2 Connection Syntax

Node connections use the format `"<source_node_id>.<output_slot_name>"`. A node with multiple
outputs (e.g., `CliffContour` produces both `height` and `cliff_mask`) uses the slot name to
disambiguate. A node with no explicit `inputs` for a required slot uses the implicit default
value defined in the node catalogue (§ 3).

### 2.3 Execution Order

The compiler performs a topological sort (Kahn's algorithm) on the node graph. Cycles are a
validation error. Nodes with no unresolved dependencies are eligible for parallel execution on
separate job threads. Each node operates on full-resolution float32 buffers.

### 2.4 Buffer Types

| Slot Type | C++ Type | Description |
|-----------|----------|-------------|
| `height` | `float32[R×R]` | Normalized 0.0–1.0 heightmap, R = `heightmap_resolution` |
| `mask` | `float32[R×R]` | 0.0–1.0 influence mask |
| `biome_map` | `uint8[R×R]` | Biome ID per texel (0–255 biome indices) |
| `density` | `float32[R×R]` | Foliage density per texel |
| `spawnpoints` | `SpawnPoint[]` | Array of 3D world positions + type tags |

---

## 3. Node Catalogue

### 3.1 Height Generator Nodes

These nodes produce a `height` output buffer from scratch (no required height input).

---

#### `FractalNoise`

Multi-octave Perlin or Simplex fractal noise. The foundational generator for organic terrain.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `noise_type` | `"perlin"\|"simplex"\|"value"` | `"simplex"` | Underlying noise algorithm |
| `octaves` | `int` 1–16 | `6` | Number of noise octaves |
| `frequency` | `float` | `0.8` | Base frequency (cycles per km) |
| `lacunarity` | `float` | `2.1` | Frequency multiplier per octave |
| `gain` | `float` | `0.5` | Amplitude multiplier per octave (persistence) |
| `offset_x` | `float` | `0.0` | World-space X offset for tiling seams |
| `offset_y` | `float` | `0.0` | World-space Y offset for tiling seams |
| `height_min` | `float` 0–1 | `0.0` | Remap output minimum |
| `height_max` | `float` 0–1 | `1.0` | Remap output maximum |

**Outputs**: `height`

---

#### `VoronoiIslands`

Generates island masses using Voronoi cell distance fields. Each cell center becomes an island
peak; the border zones become water. Ideal for archipelago MMORPG worlds.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `num_islands` | `int` 1–64 | `8` | Number of Voronoi seed points |
| `island_radius` | `float` 0.1–1.0 | `0.4` | Island radius relative to world size |
| `falloff` | `"linear"\|"smooth"\|"power"` | `"smooth"` | Edge falloff curve |
| `falloff_power` | `float` 1–4 | `2.0` | Power for "power" falloff mode |
| `water_level` | `float` 0–0.5 | `0.2` | Normalized height below which is sea |
| `merge_nearby` | `bool` | `true` | Blend islands that are within 20% of radius |

**Outputs**: `height`, `island_mask`

---

#### `RadialGradient`

A circular radial gradient — high at center, low at edges. Used as a continent-shape mask or
an island silhouette base. Combine with `FractalNoise` via `HeightBlend` for a
noise-on-gradient result (the standard "continent" technique).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `center_x` | `float` 0–1 | `0.5` | Center X in normalized coordinates |
| `center_y` | `float` 0–1 | `0.5` | Center Y in normalized coordinates |
| `radius` | `float` 0–1 | `0.45` | Gradient radius relative to world size |
| `power` | `float` 0.5–4 | `1.5` | Falloff power (higher = sharper edge) |
| `invert` | `bool` | `false` | Invert output (high at edges, low at center) |

**Outputs**: `height`

---

#### `FlatPlane`

A constant-height plane with optional micro-noise variation. Used as a base for city districts
or as the floor of dungeon zones. Also useful as a `Mask` input to confine generation to a
rectangular sub-region.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `height` | `float` 0–1 | `0.0` | Base constant height |
| `noise_scale` | `float` 0–0.05 | `0.005` | Micro-variation amplitude |
| `noise_frequency` | `float` | `4.0` | Micro-noise frequency |

**Outputs**: `height`

---

#### `HeightmapImport`

Imports an external 16-bit grayscale PNG as a starting heightmap. Useful for importing
hand-crafted concept terrain or real-world DEM data, then applying procedural modifiers on top.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `path` | `string` | `""` | `res://` path to 16-bit PNG |
| `normalize` | `bool` | `true` | Remap pixel range to 0–1 |
| `flip_x` | `bool` | `false` | Mirror horizontally |
| `flip_y` | `bool` | `false` | Mirror vertically |

**Outputs**: `height`

---

### 3.2 MMORPG-Specific Generator Nodes

These nodes are the primary differentiator of the UzEngine WorldGen system. They encode
gameplay-specific terrain constraints that would require hours of manual sculpting without them.

---

#### `Plateau`

Creates flat combat areas at specified height bands. In an MMORPG, spell-casting requires clear
line-of-sight across flat ground; AoE abilities need predictable area coverage; melee-range
fights feel better when the player and target are on the same horizontal plane. The `Plateau`
node snaps terrain within configurable height bands to exact flat levels, with smooth blended
transitions so the transition zones remain walkable.

Algorithm:
1. For each pixel, compute its current height `h`.
2. Find the nearest `target_heights` band `t` where `|h - t| < blend_radius`.
3. Blend from `h` toward `t` with strength = `flatness * (1 - |h - t| / blend_radius)`.
4. Apply a Gaussian smooth pass (radius = `blend_radius / 2`) to transition zone edges.
5. Discard plateau regions smaller than `min_size_m2` (revert those pixels to unmodified `h`).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `target_heights` | `float[]` | `[0.3, 0.6, 0.8]` | Normalized height bands to flatten toward |
| `blend_radius` | `float` 0–0.2 | `0.08` | Transition zone width as fraction of height range |
| `flatness` | `float` 0–1 | `0.92` | How perfectly flat to make the plateau (1.0 = perfectly flat) |
| `min_size_m2` | `float` | `400.0` | Minimum plateau area in m²; smaller patches revert |
| `edge_smooth_passes` | `int` 1–8 | `3` | Gaussian smoothing iterations on plateau edges |

**Inputs**: `height`
**Outputs**: `height`, `plateau_mask` (1.0 where terrain is within a plateau band)

---

#### `TerracedLevels`

Creates N distinct elevation levels with configurable spacing — like MMORPG dungeon floors,
city tiers, or the vertical layering of a plateau fortress. Unlike `Plateau` (which finds and
flattens existing terrain), `TerracedLevels` actively reshapes the entire height distribution
into discrete steps. Critically, it also auto-places walkable access ramps at regular angular
intervals between adjacent levels, ensuring the generated terrain is always navigable without
manual ramp placement.

Algorithm:
1. Quantize the entire heightmap into `num_levels` bands.
2. For each band boundary, carve the steep edge into a cliff or smooth slope.
3. Place `ramp_count` ramps per level transition, evenly spaced angularly around each level.
4. Ramps are spline-carved paths: width = `ramp_width_m`, slope ≤ `ramp_slope_max` degrees.
5. Each ramp path is smoothed against the surrounding terrain using a weighted Gaussian.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `num_levels` | `int` 2–8 | `3` | Number of distinct elevation tiers |
| `level_spacing_m` | `float` | `40.0` | Height difference between adjacent levels in meters |
| `terrace_width_m` | `float` | `150.0` | Approximate width of each flat tier in meters |
| `ramp_count` | `int` 1–12 | `4` | Ramps per level transition (evenly angular-spaced) |
| `ramp_width_m` | `float` 4–30 | `10.0` | Width of each ramp in meters |
| `ramp_slope_max` | `float` 5–35 | `28.0` | Maximum ramp slope in degrees (walkable limit) |
| `ramp_length_m` | `float` | `0.0` | Override ramp length; 0 = auto-compute from spacing and slope |
| `ramp_noise` | `float` 0–0.3 | `0.05` | Adds slight organic variation to ramp path curvature |
| `cliff_at_edges` | `bool` | `true` | Adds cliff geometry at non-ramp level transitions |

**Inputs**: `height`
**Outputs**: `height`, `level_mask` (uint8 per-pixel level index), `ramp_mask`

---

#### `CliffContour`

Wraps vertical cliff geometry around slope edges following the natural terrain contour —
producing the layered mountain silhouettes characteristic of ARK: Survival Evolved, Conan
Exiles, and similar survival-open-world games. Rather than applying cliffs uniformly based on
slope angle alone, `CliffContour` traces iso-contour lines on the slope map and applies cliff
geometry along those contours, producing natural-looking cliff bands that follow the mountain
profile rather than appearing as radially symmetric rings.

Algorithm:
1. Compute slope map: `slope[x,y] = magnitude(gradient(height[x,y]))`.
2. Trace iso-contour lines at slope value `slope_threshold` using the marching squares
   algorithm.
3. For each contour segment, extrude a cliff face: heighten the uphill side by `cliff_height_m`
   and lower the downhill side, creating a sharp step.
4. Apply `cliff_noise_scale` Perlin noise to perturb the cliff edge horizontally (jagged cliffs).
5. For `overhang_probability > 0`, probabilistically extend some cliff segments into overhangs
   by 2–4 meters — these produce dramatic visual silhouettes but require careful physics
   authoring (overhangs are non-walkable collision surfaces).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `slope_threshold` | `float` 0.3–0.9 | `0.65` | Normalized slope value at which cliffs begin |
| `cliff_height_m` | `float` 5–80 | `20.0` | Height of the cliff face in meters |
| `cliff_noise_scale` | `float` 0–1 | `0.25` | Jaggedness of cliff edge (0 = perfectly smooth) |
| `overhang_probability` | `float` 0–0.3 | `0.08` | Fraction of cliff segments that develop overhangs |
| `contour_smooth_passes` | `int` 0–6 | `2` | Smooth contour lines before cliff extrusion |
| `min_cliff_length_m` | `float` | `20.0` | Ignore cliff contour segments shorter than this |

**Inputs**: `height`
**Outputs**: `height`, `cliff_mask` (1.0 on cliff faces, 0.0 elsewhere)

---

#### `AccessRamp`

Procedurally detects elevation transitions (either from `TerracedLevels` level edges or from
arbitrary steep slope regions) and carves smooth walkable ramps through them. Unlike the
embedded ramp generation in `TerracedLevels`, `AccessRamp` operates as a post-process modifier
and can be applied to any heightmap — including hand-imported terrain. It also supports
specifying preferred ramp locations by position array for gameplay-driven placement (e.g., ramps
near quest hubs, dungeon entrances, or town gates).

Algorithm:
1. Detect cliff/edge pixels: `|height[x,y] - height[x+1,y]| > ramp_threshold` (vertical neighbor delta).
2. Cluster adjacent edge pixels into edge segments.
3. For each edge segment longer than `min_edge_length_m`, place one ramp at the segment midpoint
   (or at `positions` if provided).
4. Ramp carve: trace a bezier path from the bottom edge to the top edge with the specified
   slope constraint; apply a Gaussian-profile height carve along the path width.
5. Smooth ramp approach and exit zones (30m on each end) to eliminate sharp transitions.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `ramp_width_m` | `float` 4–30 | `10.0` | Width of carved ramp in meters |
| `ramp_slope_max` | `float` 5–35 | `30.0` | Maximum slope in degrees |
| `smooth_transitions` | `bool` | `true` | Smooth the ramp approach/exit zones |
| `detect_threshold` | `float` | `0.15` | Normalized height delta to classify as a steep edge |
| `min_edge_length_m` | `float` | `30.0` | Minimum edge segment length to receive a ramp |
| `positions` | `[x,z][]` | `[]` | Explicit ramp positions in normalized 0–1 world coords |
| `ramp_noise` | `float` 0–0.2 | `0.03` | Slight path curvature noise for organic ramps |

**Inputs**: `height`, `level_mask` (optional — from `TerracedLevels`)
**Outputs**: `height`, `ramp_mask`

---

#### `CombatArena`

Places flat circular or rectangular combat arenas at specified positions. Designed for
instanced dungeon encounters, world boss spawn points, PvP battleground zones, and arena
districts. The border transitions are configurable: cliff drop (for a defined arena bowl),
smooth slope (for a hill-top arena with natural approaches), or invisible wall marker (border
is flat but a collision wall is placed by the spawn system).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `shape` | `"circle"\|"rectangle"\|"hexagon"` | `"circle"` | Arena footprint shape |
| `radius_m` | `float` 10–500 | `50.0` | Radius (circle/hexagon) or half-extent (rectangle) in meters |
| `positions` | `[x,z][]` or `"auto"` | `"auto"` | Arena centers in normalized coords; "auto" places based on flat areas |
| `auto_count` | `int` | `3` | Number of arenas to auto-place when positions = "auto" |
| `border_type` | `"cliff"\|"slope"\|"wall_marker"` | `"slope"` | Border transition type |
| `border_width_m` | `float` | `15.0` | Width of the border transition zone in meters |
| `flatness` | `float` 0.9–1.0 | `0.98` | Arena floor flatness (1.0 = perfectly flat) |
| `height_target` | `float` or `"nearest_plateau"` | `"nearest_plateau"` | Target height for the arena floor |

**Inputs**: `height`, `plateau_mask` (optional — preferred placement on plateau areas)
**Outputs**: `height`, `arena_mask`

---

### 3.3 Modifier Nodes

Modifier nodes take an existing `height` buffer as input and transform it.

---

#### `ThermalErosion`

Simulates rock crumbling by slope: pixels above the talus angle `t` move material downhill.
Produces natural scree slopes at the base of cliffs and smooths extreme peaks. Run before
`CliffContour` so that the underlying terrain has organic variation but run after
`TerracedLevels` so it does not erode the carefully placed flat tiers (or set `protect_flat`).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `iterations` | `int` 1–500 | `50` | Simulation steps (more = more erosion) |
| `talus` | `float` 0.1–1.0 | `0.5` | Critical slope (normalized) above which material moves |
| `strength` | `float` 0–1 | `0.8` | Fraction of excess material moved per iteration |
| `protect_flat` | `bool` | `false` | Skip pixels within plateau_mask (if connected) |

**Inputs**: `height`
**Outputs**: `height`

---

#### `HydraulicErosion`

Particle-based water erosion simulation: virtual rain droplets pick up sediment on steep slopes
and deposit it in flat areas, cutting valleys and smoothing plains. Computationally expensive
(recommended: 500–2000 iterations for 2048×2048). Run at preview resolution (256×256) first.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `iterations` | `int` 100–10000 | `1000` | Number of simulated droplets |
| `rain_rate` | `float` | `0.01` | Droplets spawned per iteration per unit area |
| `evaporation` | `float` | `0.05` | Water volume lost per step (forces deposition) |
| `sediment_capacity` | `float` | `4.0` | Max sediment a droplet can carry relative to speed |
| `deposit_speed` | `float` | `0.3` | Deposition rate when droplet slows down |
| `erode_speed` | `float` | `0.3` | Erosion rate on steep downhill pixels |
| `min_slope` | `float` | `0.01` | Minimum slope to continue moving |

**Inputs**: `height`
**Outputs**: `height`

---

#### `GaussianSmooth`

Applies a Gaussian kernel blur to the heightmap. Use to soften harsh procedural edges,
smooth the transition zones between biomes, or blur out artifacts from other generators.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `radius` | `float` 1–50 | `5.0` | Gaussian sigma in meters |
| `iterations` | `int` 1–10 | `1` | Number of blur passes |
| `mask` | connected input | `null` | Optional mask: only smooth where mask > 0.5 |

**Inputs**: `height`, `mask` (optional)
**Outputs**: `height`

---

#### `Warp`

Domain warping — displaces the UV coordinates used to sample the height field by a secondary
noise field before sampling. Produces highly organic, non-repeating formations. Classic usage:
`FractalNoise` → `Warp(warp_strength=0.3)` → `ThermalErosion` produces continent-like shapes
with natural river deltas and complex coastlines.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `warp_scale` | `float` | `1.5` | Spatial frequency of the warp noise |
| `warp_strength` | `float` 0–1 | `0.3` | Maximum UV displacement (fraction of world size) |
| `octaves` | `int` 1–6 | `3` | Octaves of warp noise |
| `warp_x_seed` | `int` | `1` | Independent seed for X-axis warp |
| `warp_y_seed` | `int` | `2` | Independent seed for Y-axis warp |

**Inputs**: `height`
**Outputs**: `height`

---

#### `HeightRemap`

Remaps the output range of a heightmap using a linear or custom curve. Use to normalize
different generators before combining, or to push height distribution toward high or low values.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `in_min` | `float` | `0.0` | Input range minimum |
| `in_max` | `float` | `1.0` | Input range maximum |
| `out_min` | `float` | `0.0` | Output range minimum |
| `out_max` | `float` | `1.0` | Output range maximum |
| `curve` | `"linear"\|"smooth"\|"power"\|"spline"` | `"linear"` | Remapping curve type |
| `curve_power` | `float` | `2.0` | Power for "power" curve mode |
| `clamp` | `bool` | `true` | Clamp output to [out_min, out_max] |

**Inputs**: `height`
**Outputs**: `height`

---

#### `Mask`

Restricts modifier effects to a region defined by a mask input. Used to protect plateau areas
from erosion, confine biome splatmaps to island areas, or limit cliff generation to specific
terrain quadrants. The mask can come from any node with a `mask` output slot.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `invert` | `bool` | `false` | Invert the mask (1-x) |
| `threshold` | `float` | `0.5` | Binarize mask at this value (0 = use soft mask) |
| `feather` | `float` | `0.0` | Soften mask edges by this many meters |

**Inputs**: `height`, `mask`
**Outputs**: `height`

---

### 3.4 Combiner Nodes

Combiner nodes merge multiple height buffers into one.

---

#### `HeightBlend`

Blends two height buffers by a scalar weight or a mask buffer.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `weight` | `float` 0–1 | `0.5` | Blend weight: 0 = 100% input_a, 1 = 100% input_b |
| `use_mask` | `bool` | `false` | Use mask input instead of scalar weight |

**Inputs**: `height_a`, `height_b`, `mask` (optional)
**Outputs**: `height`

---

#### `HeightMax`

Per-pixel maximum of two height inputs. Useful for stamping raised features (mountains, mesas)
onto a base terrain without lowering any existing terrain.

**Inputs**: `height_a`, `height_b`
**Outputs**: `height`

---

#### `HeightMin`

Per-pixel minimum of two height inputs. Useful for carving valleys or coastlines into a
higher-frequency base terrain.

**Inputs**: `height_a`, `height_b`
**Outputs**: `height`

---

#### `BiomeSelect`

Selects between two biome outputs (or biome assignment rules) based on combined conditions
(height range + slope range + moisture value). The moisture channel is an optional noise-driven
parameter that adds lateral variation within the same height band — distinguishing a dry
highland grassland from a wet highland forest.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `height_range` | `[min, max]` | `[0.0, 1.0]` | Height band for biome_a |
| `slope_max` | `float` | `1.0` | Max slope for biome_a (steeper → biome_b) |
| `moisture_threshold` | `float` | `0.5` | Moisture above this → biome_a |
| `moisture_noise_scale` | `float` | `0.4` | Spatial frequency of moisture noise |

**Inputs**: `height`, `biome_a`, `biome_b`
**Outputs**: `biome_map`

---

### 3.5 Biome Nodes

---

#### `BiomeAssign`

Assigns biome IDs per texel based on height + slope + optional level index conditions.
Produces a `biome_map` uint8 buffer where each value is an index into the `biomes` dictionary
in the `.uzworldgen` root object.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `biomes` | `string[]` | `[]` | Ordered list of biome names; each biome's conditions are read from the `biomes` dict |
| `fallback_biome` | `string` | `""` | Biome used where no conditions match |
| `height_noise` | `float` | `0.02` | Random variation added to height before biome condition test (breaks straight horizontal bands) |

**Inputs**: `height`, `slope_mask` (optional), `level_mask` (optional), `moisture` (optional)
**Outputs**: `biome_map`

---

#### `BiomeDefinition`

Defines a single biome's visual and gameplay properties. This node is declarative — it does
not transform buffers; it registers a biome entry in the `.uzworldgen` `biomes` dictionary
that `BiomeAssign` references. This node exists so the biome's properties are visible in the
node graph canvas and editable in the Inspector, rather than only in the raw JSON `biomes` dict.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | `string` | `""` | Biome identifier key |
| `height_range` | `[float, float]` | `[0.0, 1.0]` | Normalized height band |
| `slope_max` | `float` | `1.0` | Maximum slope for this biome |
| `splatmap_layers` | `string[]` | `[]` | Up to 4 terrain material layer names (refs `.uzterrain` layer names) |
| `layer_weights` | `float[4]` | `[1,0,0,0]` | Default layer blend weights within this biome |
| `foliage` | `FoliageDef[]` | `[]` | Array of foliage mesh + density + slope rules |
| `ambient_tint` | `[r,g,b]` | `[1,1,1]` | Additive tint applied to scene ambient light in this biome |
| `wind_speed` | `float` | `1.0` | Wind speed multiplier for foliage sway in this biome |

`FoliageDef` fields: `{ "mesh": "res://...", "density": float, "min_height": float, "max_slope": float, "min_scale": float, "max_scale": float, "cluster": bool, "cluster_radius_m": float }`

---

### 3.6 Output Nodes

Output nodes write files to `output_dir` and compose the final `.uzterrain` INI.

---

#### `TerrainOutput`

The required terminal node. Converts the normalized float32 height buffer to a 16-bit
grayscale PNG heightmap, writes it to `{output_dir}{name}.heightmap.png`, and writes the
`.uzterrain` INI file referencing all outputs.

Exactly one `TerrainOutput` node must be present in a valid graph. Additional output nodes
(splatmap, foliage) are optional but recommended.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `height_scale_m` | `float` | `200.0` | World height range in meters (heightmap max value) |
| `base_height_m` | `float` | `-10.0` | World Y of the 0 heightmap value |
| `water_level` | `float` 0–1 | `0.2` | Normalized height below which a water plane is added |
| `water_color_shallow` | `[r,g,b,a]` | — | Shallow water RGBA |
| `water_color_deep` | `[r,g,b,a]` | — | Deep water RGBA |

**Inputs**: `height`, `biome` (optional)
**Outputs**: (writes files) `{name}.heightmap.png`, `{name}.uzterrain`

---

#### `SplatmapOutput`

Converts the `biome_map` buffer into an RGBA splatmap PNG, where each channel corresponds
to one of up to 4 terrain material layers. Biome definitions specify which layers are active
and at what blend weights per biome. The output PNG is 8-bit RGBA.

Transition zones between biomes receive a soft blend by Gaussian-smearing the biome boundary
at the specified `blend_width` before channel assignment.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `blend_width_m` | `float` | `20.0` | Gaussian blend zone width at biome boundaries |
| `slope_rock_override` | `float` 0–1 | `0.7` | Pixels with slope > this value get rock layer boost |
| `rock_layer_index` | `int` 0–3 | `1` | Which splatmap channel receives the slope-rock boost |

**Inputs**: `biome`, `height`, `slope_mask` (optional)
**Outputs**: (writes files) `{name}.splatmap.png`

---

#### `FoliageOutput`

Writes one R8 PNG density map per foliage type defined across all biomes. Each PNG encodes
foliage placement density (0–255 = 0–max_density instances/m²) for one mesh type. The
`.uzterrain` foliage layer entries reference these density maps.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `density_scale` | `float` | `1.0` | Global density multiplier |
| `exclude_slopes` | `float` 0–1 | `0.5` | No foliage above this slope value |
| `exclude_water` | `bool` | `true` | No foliage below water level |
| `randomize_rotations` | `bool` | `true` | Write random Y rotation per instance |

**Inputs**: `biome`, `height`, `slope_mask`, `plateau_mask` (optional), `ramp_mask` (optional)
**Outputs**: (writes files) one `{name}_foliage_{mesh_id}.png` per unique mesh in all biomes

---

#### `SpawnpointOutput`

Auto-places semantic spawn points based on terrain analysis: town centers on flat lowland
areas, dungeon entrances at cliff bases, resource nodes in specific biomes, and player spawn
points on safe flat regions. Outputs a JSON file that the scene system consumes at level load
to instantiate the correct game objects at each spawn.

```json
{
  "spawnpoints": [
    { "id": "town_center_0", "type": "town_center", "pos": [512.3, 42.0, 489.7], "radius_m": 80 },
    { "id": "dungeon_entrance_0", "type": "dungeon_entrance", "pos": [210.5, 18.0, 780.2], "facing": [1,0,0] },
    { "id": "res_iron_0", "type": "resource_node", "resource": "iron_ore", "pos": [350.0, 95.0, 200.0] }
  ]
}
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `town_center_count` | `int` | `2` | Number of town/hub spawns to place on flat lowland |
| `dungeon_entrance_count` | `int` | `3` | Placed at cliff bases (high slope adjacent to flat) |
| `resource_nodes` | `ResourceDef[]` | `[]` | `{ "type": "iron_ore", "biome": "highland_rock", "count": 8 }` |
| `player_spawn_count` | `int` | `5` | Safe flat ground player spawns away from cliffs |
| `min_spawn_spacing_m` | `float` | `100.0` | Minimum distance between same-type spawns |
| `output_file` | `string` | `"{name}_spawns.json"` | Path for spawn point JSON |

**Inputs**: `height`, `biome`, `plateau_mask`, `cliff_mask`, `ramp_mask`, `arena_mask`
**Outputs**: (writes file) `{name}_spawns.json`

---

#### `RoadNetwork`

Generates a procedural road and path network connecting key points (town centers, dungeon
entrances, arenas) following terrain slope. Paths are carved into the heightmap (smoothed flat
surface) and marked in a `road_mask` output. The road network also guides foliage exclusion
(no trees on roads) and splatmap blending (road surfaces use a dedicated gravel/stone layer).

Algorithm:
1. Collect key point positions from connected `SpawnpointOutput` data or `positions` parameter.
2. Compute minimum spanning tree over key points (Prim's algorithm) to find the road graph.
3. For each road edge, trace a spline following A* on the slope map (prefer low-slope paths).
4. Carve a flat corridor of `road_width_m` into the heightmap along each road spline.
5. Apply splatmap channel override along roads (gravel/cobblestone layer).
6. Mark a `road_mask` buffer (1.0 on road corridors, 0.0 elsewhere) for foliage exclusion.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `road_width_m` | `float` 4–30 | `8.0` | Road corridor width in meters |
| `road_smooth_radius` | `float` | `4.0` | Gaussian smooth radius on road edges |
| `positions` | `[x,z][]` | `[]` | Additional key points beyond auto-detected spawns |
| `max_slope_deg` | `float` | `25.0` | Path finder avoids slopes steeper than this |
| `path_style` | `"road"\|"path"\|"bridge"` | `"road"` | Road visual style (affects splatmap layer and carve depth) |
| `splatmap_layer` | `int` 0–3 | `3` | Splatmap channel to paint road surface onto |

**Inputs**: `height`, `spawn_data` (from SpawnpointOutput), `biome`
**Outputs**: `height` (carved roads), `road_mask`

---

## 4. Compilation Pipeline

### 4.1 WorldGenCompiler Binary

The `WorldGenCompiler` is a standalone tool-mode C++ binary (`UzWorldGenCompiler.exe`)
compiled from the engine codebase. It links against the engine's job system, math library,
and image I/O modules but does not initialize a DX12 context — it runs fully headless.

Invocation:
```bash
UzWorldGenCompiler.exe --graph res://worldgen/MysticIslands.uzworldgen --resolution 2048 --output res://terrain/ --preview
```

Flags:
- `--resolution N` — output resolution (256/512/1024/2048/4096)
- `--preview` — emit a 256×256 PNG preview via IPC immediately before full-res pass
- `--seed N` — override the graph seed
- `--dry-run` — validate graph topology without executing nodes

### 4.2 Execution Pipeline

```
1. Parse .uzworldgen JSON
       ↓
2. Validate graph (check for cycles, missing inputs, type mismatches)
       ↓
3. Allocate float32 buffers for all intermediate height/mask slots
   Buffer size: resolution × resolution × 4 bytes (float32)
   At 2048×2048: 16 MB per buffer; max 32 buffers in flight = 512 MB budget
       ↓
4. Topological sort (Kahn's algorithm)
       ↓
5. Execute nodes in topological order using uz_job_system:
   - Independent nodes (same topo-level) execute in parallel on job threads
   - Each node receives pointers to its input and output buffers
   - Nodes signal completion to the job graph; downstream nodes are queued
       ↓
6. If --preview: downsample 256×256 result, encode JPEG, send via IPC
       ↓
7. Execute output nodes (TerrainOutput, SplatmapOutput, FoliageOutput, etc.)
   - TerrainOutput: float32 → uint16 → PNG (stb_image_write)
   - SplatmapOutput: biome_map → RGBA8 PNG
   - FoliageOutput: density floats → R8 PNG per foliage type
   - SpawnpointOutput: analyze buffers → JSON
       ↓
8. Write .uzterrain INI file composing all output references
       ↓
9. IPC: send terrain.hotReload { path: "res://terrain/MysticIslands.uzterrain" }
```

### 4.3 Thread Safety

Each node receives exclusive write access to its own output buffers and shared read access
to its input buffers (which are already complete before the node begins). No two nodes write
to the same buffer. This model requires no mutex locks on buffer access — thread safety is
guaranteed by the DAG execution constraint.

The job system dispatches nodes via `uz_job_system::kick_lambda()`. The compiler uses a
`std::atomic<int>` pending-count per topo-level to track completion and release downstream
nodes.

### 4.4 Progressive Preview

For interactive use from the VSCode WorldGen Panel, the compiler runs in two phases:

**Phase 1 — Preview (< 2 seconds target)**
- Resolution: 256×256
- All nodes execute at low resolution
- Result sent via IPC: `worldgen.preview { heightmap_base64, splatmap_base64 }`
- The Three.js preview mesh in the WorldGen Panel updates immediately

**Phase 2 — Full Resolution (background)**
- Resolution: as configured (default 2048×2048)
- Runs on background thread pool
- Progress reported via IPC: `worldgen.progress { percent, current_node }`
- Completion: `worldgen.complete { terrain_path }`
- The engine hot-reloads the new `.uzterrain` automatically

### 4.5 Memory Budget

| Resolution | float32 buffer size | Max buffers | Peak RAM |
|------------|---------------------|-------------|----------|
| 256 × 256 | 256 KB | 32 | 8 MB |
| 1024 × 1024 | 4 MB | 32 | 128 MB |
| 2048 × 2048 | 16 MB | 32 | 512 MB |
| 4096 × 4096 | 64 MB | 32 | 2 GB |

For 4096×4096 resolution, the compiler uses a streaming tile strategy: process 1024×1024
tiles with overlap margins (128 px) to handle nodes with spatial neighbors (erosion, smooth).

---

## 5. AI Generation (`world_generate` and `world_refine` MCP Tools)

### 5.1 MCP Tool Interface

```typescript
// world_generate — generate a complete world from a text description
world_generate(params: {
  description: string,        // "A mystical highland MMORPG zone with 3 combat plateaus..."
  world_size_km?: number,     // default: 4.0
  style?: WorldGenStyle,      // "mmorpg" | "survival" | "city" | "island" | "dungeon"
  num_plateaus?: number,      // default 3 for mmorpg style
  num_levels?: number,        // vertical tier count
  biomes?: string[],          // ["forest", "highland", "ruins", "swamp"]
  seed?: number,              // if omitted: random
  resolution?: number,        // preview resolution (default 256 for initial response)
  template?: string           // base template key from § 7 (optional override)
}) → {
  graph_path: string,         // res:// path to the written .uzworldgen file
  preview_screenshot: string  // base64 PNG of the engine viewport after terrain hot-reload
}

// world_refine — iteratively adjust an existing world based on text feedback
world_refine(params: {
  graph_path: string,         // existing .uzworldgen to modify
  feedback: string,           // "make the plateaus flatter, add more ramps, cliffs too sharp"
  screenshot_base64: string,  // current engine viewport screenshot (captured by client)
  max_iterations?: number     // default 3
}) → {
  graph_path: string,         // updated .uzworldgen (same path, modified in place)
  changes_summary: string,    // human-readable list of param changes made
  preview_screenshot: string
}

// world_list_templates — returns available templates for reference
world_list_templates() → { templates: TemplateInfo[] }
```

`WorldGenStyle` enum values:
- `"mmorpg"` — default for MMORPGs: plateaus, defined levels, walkable ramps, mixed biomes
- `"survival"` — extreme terrain variation, dense foliage, erosion-heavy, no plateaus
- `"city"` — mostly flat with elevation variety at edges, wall boundaries
- `"island"` — VoronoiIslands base, beach/jungle/highland layers, ruins on high ground
- `"dungeon"` — rocky terrain, dark biome, lava/crater areas, winding paths

### 5.2 AI Generation Pipeline

```
Step 1: Context construction
├── System prompt includes:
│   ├── Full node catalogue (this spec § 3 — all node types and parameters)
│   ├── Closest template graph JSON as reference starting point (§ 7)
│   ├── Style constraints: "mmorpg requires Plateau or TerracedLevels nodes,
│   │   ramp_slope_max must be <= 30 degrees for walkability, always include
│   │   AccessRamp or TerracedLevels ramp_count >= 3"
│   └── MMORPG constraints: "combat arenas must be within 10m height of nearest plateau"
│
├── User prompt: description + extracted parameters
│   Example: "A 4km mystical highland zone. 3 combat plateaus. Pine forests in lowlands.
│              Stone ruins on highlands. Cliffs like ARK. 3 vertical levels."
│
└── Claude API call → .uzworldgen JSON graph

Step 2: Validation
├── Parse generated JSON
├── Check required nodes present (TerrainOutput, at least one generator)
├── Check MMORPG constraints if style = "mmorpg":
│   - TerracedLevels or Plateau node present
│   - ramp_slope_max <= 35 in all AccessRamp/TerracedLevels nodes
│   - HeightScale <= 300m (prevents unwalkable extreme terrain)
└── If validation fails → retry Step 1 with error feedback appended

Step 3: Preview compilation
├── WorldGenCompiler --resolution 256 --preview
├── Progress shown in WorldGen Panel
└── terrain.hotReload IPC → viewport updates

Step 4: Screenshot evaluation
├── engine.screenshot IPC → base64 PNG
│
├── Claude evaluates screenshot with checklist:
│   - "Are plateaus visibly flat? (> 80% of plateau area within 2m height)"
│   - "Are walkable ramps visible at level transitions?"
│   - "Do cliffs follow mountain contours naturally?"
│   - "Does biome distribution match description?"
│   - "Is the terrain walkable at ground level?"
│
└── If criteria not met AND iteration_count < max_iterations:
    → Identify failing criteria
    → Adjust relevant node parameters (e.g., increase flatness, add ramp_count)
    → Goto Step 3 (recompile)

Step 5: Return result
├── Write final .uzworldgen to res://worldgen/{sanitized_name}.uzworldgen
├── Trigger full-resolution compilation in background
└── Return { graph_path, preview_screenshot }
```

### 5.3 `world_refine` Feedback Parsing

When the user provides natural-language feedback, Claude parses it into specific parameter
mutations before adjusting the graph JSON:

| Feedback phrase | Parameter mutation |
|-----------------|-------------------|
| "make plateaus flatter" | Plateau.flatness += 0.05, clamp to 0.99 |
| "more ramps" | TerracedLevels.ramp_count += 2, AccessRamp add node |
| "cliffs too sharp/jagged" | CliffContour.cliff_noise_scale -= 0.1 |
| "cliffs not visible enough" | CliffContour.cliff_height_m *= 1.3 |
| "more erosion / more organic" | ThermalErosion.iterations *= 2; HydraulicErosion add or increase |
| "flatter terrain overall" | FractalNoise.gain -= 0.1 (reduces height variation) |
| "more levels / more floors" | TerracedLevels.num_levels += 1 |
| "biome X too large" | BiomeAssign adjust height_range for biome X |
| "more forest" | Forest BiomeDefinition density increase |
| "less water" | RadialGradient.radius += 0.05, or water_level -= 0.05 |

Claude is given the current graph JSON alongside the screenshot and feedback, so it can reason
about which nodes control which visual features and make targeted changes rather than
regenerating from scratch.

---

## 6. VSCode WorldGen Panel

### 6.1 Panel Architecture

The WorldGen Panel is a VSCode Webview panel (`worldgenPanel.ts`) using the same React +
reactflow stack as the VFX Node Editor. It opens via:
- Command palette: `UzEngine: Open World Generator`
- Scene Tree right-click on a Terrain node: `Open WorldGen Editor`
- Double-click a `.uzworldgen` file in the Asset Browser

```
WorldGenPanel.ts (VSCode extension host)
├── WebviewPanel host
├── IPC bridge → WorldGenCompiler.exe
├── IPC bridge → engine (screenshot, hotReload)
└── MCP bridge → world_generate, world_refine

worldgen-editor/ (React Webview app, bundled with esbuild)
├── App.tsx               — top-level state, message router
├── NodeCanvas.tsx        — reactflow canvas (left 70%)
├── InspectorPanel.tsx    — node parameter editor (right 30%)
├── PreviewPanel.tsx      — Three.js 3D heightmap preview (bottom strip)
├── BiomeEditor.tsx       — visual biome list editor
├── AiGenerateModal.tsx   — text prompt + style + biome selector
├── AiRefinePanel.tsx     — feedback textarea + capture viewport button
└── ToolBar.tsx           — New / Open / Save / Generate / AI buttons
```

### 6.2 Node Canvas

Built on `reactflow` with custom node renderers per node type:

- **Generator nodes** (blue header): `FractalNoise`, `VoronoiIslands`, `RadialGradient`, etc.
- **MMORPG nodes** (gold header): `Plateau`, `TerracedLevels`, `CliffContour`, `AccessRamp`, `CombatArena`
- **Modifier nodes** (green header): erosion, smooth, warp, remap
- **Combiner nodes** (purple header): blend, max, min
- **Biome nodes** (teal header): `BiomeAssign`, `BiomeDefinition`
- **Output nodes** (red header): `TerrainOutput`, `SplatmapOutput`, `FoliageOutput`, `SpawnpointOutput`, `RoadNetwork`

Each node displays:
- Type name and user label
- Input port(s) with type indicator dot (height = blue, mask = grey, biome = teal)
- Output port(s)
- A compact preview thumbnail (64×64 greyscale of that node's output) updated after each compile

Interactions:
- Click node → open in Inspector Panel
- `Ctrl+Space` → node search palette (searchable node type list)
- `Delete` → remove selected node (with confirmation if connected)
- `Ctrl+D` → duplicate node
- `Ctrl+Z` / `Ctrl+Shift+Z` → undo/redo (50-step local history)
- `Ctrl+S` → save graph + trigger recompile
- Drag connection → connect output port to input port

### 6.3 Inspector Panel

Click any node to open its full parameter editor in the Inspector:
- Scalar `float` parameters: number input with min/max range slider
- `int` parameters: stepper input
- `bool` parameters: toggle switch
- `string` enum parameters: dropdown
- Array parameters (`target_heights`, `positions`): dynamic list with + / - row controls
- `[r,g,b]` color parameters: color picker
- `FoliageDef[]` arrays: expandable sub-form with mesh file picker (opens Asset Browser)

Every parameter change immediately dispatches a `compile.preview` message to the extension
host, triggering a new 256×256 compile pass and updating the PreviewPanel. Parameter changes
are batched with 300ms debounce to avoid excessive recompiles during slider drag.

### 6.4 Preview Panel (Three.js)

The bottom strip of the panel shows a real-time 3D preview of the current heightmap:
- A Three.js `PlaneGeometry` (256×256 segments) with Y displacement from the heightmap data
- PBR-style shading using the splatmap channels as vertex colors
- Orbit controls (mouse drag to rotate, scroll to zoom)
- Biome color overlay toggle (shows biome color map instead of splatmap)
- Water plane rendered at `water_level` height

The preview updates whenever the compiler emits a `worldgen.preview` IPC event.

### 6.5 AI Generate Modal

The "AI Generate" button opens a modal dialog:

```
┌─────────────────────────────────────────────────────────────────────┐
│  AI World Generator                                                 │
│                                                                     │
│  Description:                                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ A mystical highland MMORPG zone with 3 combat plateaus,     │   │
│  │ a central cliff mountain, pine forests in the lowlands,     │   │
│  │ and ancient stone ruins on the highlands...                 │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  Style: [MMORPG ▾]   World Size: [4.0 km ▾]   Seed: [42] [🎲]    │
│                                                                     │
│  Plateaus: [3 ▾]   Levels: [3 ▾]   Resolution: [256 preview ▾]    │
│                                                                     │
│  Biomes (select active):                                            │
│  [x] Forest  [x] Highland  [x] Cliff  [ ] Swamp  [ ] Desert        │
│                                                                     │
│  [Generate]                                    [Cancel]            │
└─────────────────────────────────────────────────────────────────────┘
```

During generation, a progress bar shows the current step:
1. "Generating node graph..." (Claude API call)
2. "Compiling preview..." (WorldGenCompiler 256×256)
3. "Evaluating result..." (screenshot + Claude evaluation)
4. "Refining..." (if iteration 2 or 3)
5. "Done" (returns to canvas with new graph loaded)

### 6.6 AI Refine Panel

After generation (or after manually editing the graph), the "Refine with AI" button opens
a slide-in panel on the right side:

```
┌────────────────────────────────────┐
│  Refine with AI                    │
│                                    │
│  [Capture Current Viewport]        │
│                                    │
│  What to change:                   │
│  ┌──────────────────────────────┐  │
│  │ Make the plateaus flatter.   │  │
│  │ Add more ramps between the   │  │
│  │ levels. The cliffs look too  │  │
│  │ sharp near the mountain top. │  │
│  └──────────────────────────────┘  │
│                                    │
│  [Apply Refinement]                │
│                                    │
│  Changes made:                     │
│  • Plateau.flatness: 0.85 → 0.95  │
│  • TerracedLevels.ramp_count: 2→4 │
│  • CliffContour.noise_scale:0.4→0.2│
└────────────────────────────────────┘
```

### 6.7 Biome Editor

The Biome Editor tab shows a visual list of biomes defined in the current graph:

- Each biome displays: color swatch (ambient_tint), name, height range bar, slope indicator
- Click a biome to expand its foliage list (each entry shows mesh name + density slider)
- "Add Biome" button adds a new `BiomeDefinition` node to the graph
- "Paint biome" mode: in the Three.js preview, click to see what biome is assigned at that point
- Export: "Apply to Terrain" writes the biome data to the `.uzterrain` material layer section

### 6.8 Toolbar Controls

| Button | Action |
|--------|--------|
| New | Create empty graph with TerrainOutput node |
| Open | File picker for `.uzworldgen` files |
| Save | Write `.uzworldgen` to disk |
| Generate (F5) | Run full-resolution WorldGenCompiler |
| AI Generate | Open AI Generate modal |
| AI Refine | Open AI Refine panel |
| Resolution | Dropdown: 256 / 512 / 1024 / 2048 / 4096 |
| Seed | Number input + randomize dice button |
| Export to .uzterrain | Opens save dialog for final terrain output |

---

## 7. MMORPG-Specific Templates

Pre-built graph templates are `.uzworldgen` files stored in `res://worldgen/templates/`.
The AI generation system uses the closest-matching template as a starting point.

### `mmorpg_overworld`

The standard MMORPG outdoor zone template. Produces a world with:
- 3-tier elevation using `TerracedLevels` (plains at 0.2, midlands at 0.5, highlands at 0.8)
- Multiple flat combat plateaus via `Plateau` node (flatness: 0.93)
- 4 walkable ramps between each level (slope <= 28°)
- `CliffContour` on mountain edges (cliff_height_m: 22, ARK-style contouring)
- 3 biomes: lowland_forest, midland_highland_grass, highland_rock
- Auto-placed town center (lowland), dungeon entrance (cliff base), player spawns (flat areas)
- Pine trees in lowland, sparse boulders in highland, no vegetation on cliffs or ramps

Default configuration:
```json
{ "world_size_km": 4.0, "heightmap_resolution": 2048, "nodes": [
  "FractalNoise(octaves:6, frequency:0.7)",
  "ThermalErosion(iterations:40)",
  "TerracedLevels(num_levels:3, level_spacing:40m, ramp_count:4, ramp_slope:28°)",
  "Plateau(targets:[0.2,0.5,0.8], flatness:0.93, blend_radius:0.06)",
  "CliffContour(slope_threshold:0.65, cliff_height:22m, noise_scale:0.22)",
  "BiomeAssign(lowland_forest, midland_grass, highland_rock, cliff_stone)",
  "TerrainOutput + SplatmapOutput + FoliageOutput + SpawnpointOutput"
]}
```

### `mmorpg_island`

Archipelago with multiple distinct islands at different sizes:
- `VoronoiIslands(num_islands:5)` base
- `RadialGradient` combined to shape each island
- 3 biomes: beach (coastal), jungle (mid-island), highland (island peaks)
- `CombatArena` on the largest island's highest plateau
- `RoadNetwork` connecting beaches to highland via jungle paths
- Stone ruins spawn points on highland areas

### `mmorpg_dungeon_surface`

Surface approach to a dungeon entrance:
- Dark biome with volcanic rock and lava pool zones
- Crater terrain shape from inverted `RadialGradient`
- `TerracedLevels(num_levels:2)` creating a natural descent into the dungeon area
- Single dungeon entrance spawn at the lowest tier center
- Dense boulder foliage, no trees, ember particle zones (spawn point markers)
- `CliffContour` creates dramatic rock walls around the crater rim

### `mmorpg_city_district`

An urban-terrain hybrid for player cities or NPC settlements:
- `FlatPlane` base with mild noise (micro-variation only)
- `TerracedLevels(num_levels:2, terrace_width:300m)` for city walls and elevated districts
- Wide `AccessRamp` passages (ramp_width:20m) as main gates
- `CombatArena` in the town square area
- Road network in a rough grid pattern between districts
- Minimal foliage (decorative trees only, low density)

### `survival_wilderness`

Extreme terrain for survival games (no plateaus by design):
- `FractalNoise(octaves:8, gain:0.6)` high-variation base
- `HydraulicErosion(iterations:2000)` deep valley carving
- `ThermalErosion(iterations:100)` plus `Warp(strength:0.4)` for extreme organics
- No `Plateau` or `TerracedLevels` nodes
- 5 biomes: arctic_tundra, boreal_forest, temperate_forest, alpine, deep_valley
- Dense foliage throughout

### `floating_islands`

Magical levitating terrain for fantasy worlds:
- Multiple `VoronoiIslands` at different height bands (0.3, 0.6, 0.9)
- `FlatPlane` base at 0.0 (the void / sea below)
- `HeightMax` combiner to stack island layers
- Each island treated as a separate biome
- Bridge spawn markers between islands (RoadNetwork path style "bridge")

---

## 8. Integration with Existing Systems

### 8.1 Terrain System

The WorldGen compiler writes `.uzterrain` files that are valid inputs to the existing
`uz_terrain` plugin. No modifications to the terrain runtime are required. The generated
heightmap PNG is identical in format to hand-authored heightmaps (16-bit grayscale PNG,
same dimension constraints). The generated splatmap PNG uses the same RGBA8 format.

The WorldGen system populates these `.uzterrain` sections automatically:
- `[uzterrain]` header with `world_size_km`, `heightmap_resolution`, and `height_scale`
- All `[material.layerN]` sections from `BiomeDefinition` splatmap_layers
- All `[foliage.X]` sections from `FoliageOutput` density maps
- `[water]` section if `TerrainOutput.water_level > 0`

### 8.2 NavMesh Integration

Generated terrain is immediately compatible with the NavMesh system. The `SpawnpointOutput`
node marks ramp corridors with `navmesh_area = ramp` and arena areas with
`navmesh_area = combat_arena`, enabling NavMesh agents to navigate ramps and recognize arena
boundaries for AI pathfinding. The cliff faces are automatically excluded from walkable area
via their slope angle.

### 8.3 Foliage System

`FoliageOutput` density maps feed directly into `.uzterrain` `[foliage.X]` entries. The
existing `FoliageSystem` reads these density maps at runtime to generate GPU instance buffers
— no changes required to the runtime foliage pipeline.

### 8.4 Physics

The generated heightmap is loaded by the terrain plugin into a `JPH::HeightFieldShape` as
normal. The WorldGen system ensures that ramp slopes are within the character controller's
`max_slope_degrees` limit (configured in `AccessRamp`, `TerracedLevels`). This guarantees
that the `CCharacterController` can traverse all ramps without physics penetration.

### 8.5 AI Native Features

The `world_generate` and `world_refine` MCP tools extend the existing AI native feature set
(`docs/specs/engine/ai-native-features.md`) alongside `scene_generate`, `shader_assist`, and
`script_generate`. They share the same MCP server infrastructure and IPC channel.

### 8.6 VFX Node Editor

The WorldGen Panel Webview is a sibling to the VFX Node Editor Webview. Both use React +
reactflow. The WorldGen Panel can be open simultaneously with the VFX Node Editor in separate
VSCode panel groups. They share the extension host IPC connection but use distinct message
type namespaces (`worldgen.*` vs `vfx.*`).

---

## 9. File Locations

| File | Path |
|------|------|
| World gen graphs | `res://worldgen/*.uzworldgen` |
| Built-in templates | `res://worldgen/templates/*.uzworldgen` |
| Compiler binary | `tools/UzWorldGenCompiler.exe` |
| Compiler source | `tools/worldgen/WorldGenCompiler.cpp` |
| Node implementations | `tools/worldgen/nodes/` |
| Extension panel | `vscode-extension/src/worldgenPanel.ts` |
| Webview React app | `vscode-extension/src/webview/worldgen-editor/` |
| MCP tool handlers | `mcp-server/src/tools/worldGenTools.ts` |
| Generated terrain | `res://terrain/` |
| Spawn point JSON | `res://terrain/{name}_spawns.json` |

---

## 10. IPC Message Reference

```
worldgen.generate      { graph_path, resolution }         → start compilation
worldgen.cancel        {}                                  → cancel current compilation
worldgen.preview       { heightmap_b64, splatmap_b64 }    → compiler → panel
worldgen.progress      { percent, current_node, eta_sec } → compiler → panel
worldgen.complete      { terrain_path, elapsed_ms }       → compiler → panel
worldgen.error         { message, node_id }               → compiler → panel
worldgen.getTemplates  {}                                  → { templates: TemplateInfo[] }
worldgen.openFile      { path }                           → open .uzworldgen in panel
worldgen.saveFile      { path, content }                  → write .uzworldgen JSON
```

---

## 11. Validation Rules

The following constraints are enforced by the compiler before execution:

| Rule | Error |
|------|-------|
| Exactly one `TerrainOutput` node | `WORLDGEN_E001: TerrainOutput required` |
| No cycles in node graph | `WORLDGEN_E002: Cycle detected at node {id}` |
| All input connections resolve | `WORLDGEN_E003: Unresolved input {slot} on node {id}` |
| `TerracedLevels.ramp_slope_max` <= 35 | `WORLDGEN_E004: Ramp slope exceeds walkable limit` |
| `heightmap_resolution` is power of 2 (or 2^N + 1) | `WORLDGEN_E005: Invalid resolution` |
| All `biomes` keys in `BiomeAssign.biomes` exist in `biomes` dict | `WORLDGEN_E006: Undefined biome` |
| Max 4 unique splatmap layers across all biomes | `WORLDGEN_E007: Splatmap layer limit exceeded` |
| Biome `height_range` values in [0, 1] | `WORLDGEN_E008: Height range out of bounds` |

---

## 12. Performance Targets

| Metric | Target |
|--------|--------|
| Preview compilation (256×256) | < 2 seconds |
| Standard generation (2048×2048, 6-node graph) | < 30 seconds |
| Full generation with erosion (2048×2048, 12-node graph) | < 120 seconds |
| AI generation end-to-end (including Claude API) | < 60 seconds |
| AI refinement round-trip | < 30 seconds |
| Node Editor preview update (param change → Three.js update) | < 500 ms |
| Terrain hot-reload after compilation complete | < 3 seconds |

---

## 13. Limits

| Constraint | Value |
|------------|-------|
| Max nodes per graph | 64 |
| Max resolution | 4096×4096 |
| Max biomes | 16 |
| Max foliage types across all biomes | 32 |
| Max splatmap layers | 4 (constrained by terrain system RGBA splatmap) |
| Max TerracedLevels.num_levels | 8 |
| Max spawn points per SpawnpointOutput | 1000 |
| Max road network key points | 32 |
| Max CombatArena instances | 20 |
| Min ramp slope | 5° |
| Max ramp slope | 35° (CCharacterController walkable limit) |
