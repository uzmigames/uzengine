# Terrain & World Building Editor

> **Tool Category**: World Authoring
> **Phase**: 4
> **Depends on**: terrain-system, scene-system, physics-system, navmesh, editor-panels, undo-redo, editor-gizmos
> **File Formats**: `.uzterrain`, `.heightmap`, `.splatmap`, `.uzfoliage`, `.uzworld`

---

## 1. Overview & Goals

The Terrain & World Building Editor is a professional open-world authoring suite embedded inside VSCode as a dedicated editor panel. It provides AAA-quality terrain creation workflows — sculpting, texture painting, procedural foliage, world composition, and atmospheric control — without leaving the development environment.

### Design Goals

| Goal | Description |
|------|-------------|
| **Non-destructive workflow** | All edits stored as compressed deltas; full undo/redo stack |
| **AI-native authoring** | Every operation is also an MCP tool call; AI can co-author terrain |
| **Physics-synchronized** | Jolt `HeightFieldShape` rebuilt automatically (debounced 500 ms) after sculpt |
| **NavMesh-aware** | Terrain edits invalidate baked navmesh; editor prompts to rebake |
| **Large world capable** | Tile-streaming architecture up to 64 km × 64 km with `.uzworld` manifest |
| **Format transparency** | All data in plain-text INI + standard PNG — readable by AI without parsing |

### Target Users

- **World builders**: Large-scale terrain generation, elevation, rivers, mountains
- **Environment artists**: Texture layering, foliage placement, detail painting
- **Level designers**: Object scattering, pathable surface definition, navmesh areas
- **Technical artists**: LOD configuration, streaming budgets, performance profiling

---

## 2. Editor Layout

The Terrain Editor opens as a dedicated VSCode WebView panel with a full-screen DX12 viewport and surrounding tooling panels.

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  TOOLBAR: [Sculpt] [Paint] [Foliage] [Place] [Atmosphere] | [Undo] [Redo] [Save]│
├────────┬────────────────────────────────────────────────────┬───────────────────┤
│        │                                                    │  LAYER STACK      │
│  TOOL  │                                                    │ ┌───────────────┐ │
│  PANEL │                                                    │ │ [=] Snow       │ │
│        │                                                    │ │ [=] Rock       │ │
│ ┌────┐ │           3D VIEWPORT (DX12)                       │ │ [=] Dirt       │ │
│ │Raise│ │                                                    │ │ [=] Grass  [*]│ │
│ │Lower│ │      [brush ring projected on terrain surface]     │ └───────────────┘ │
│ │Smth │ │                                                    │                   │
│ │Flat │ │                                                    │  HEIGHTMAP PREV.  │
│ │Ramp │ │                                                    │ ┌───────────────┐ │
│ │Eros │ │                                                    │ │   [grayscale  │ │
│ │Noise│ │                                                    │ │    thumbnail] │ │
│ └────┘ │                                                    │ └───────────────┘ │
│        │                                                    │                   │
│ Radius │                                                    │  ASSET BROWSER    │
│ [====] │                                                    │ ┌───────────────┐ │
│  32.0m │                                                    │ │ Foliage       │ │
│ Str    │                                                    │ │ > Grass       │ │
│ [===]  │                                                    │ │ > Bush        │ │
│  0.35  │                                                    │ │ > Pine Tree   │ │
│ Falloff│                                                    │ │ Rocks         │ │
│ [Smth] │                                                    │ │ Decals        │ │
│        │                                                    │ └───────────────┘ │
├────────┴────────────────────────────────────────────────────┴───────────────────┤
│  WORLD MAP                    │  STATUS: Tile (2,3) | FPS: 60 | Instances: 48k  │
│ ┌──────────────────────────┐  │  [LOD Vis] [Overdraw] [Cull Stats] [GPU Timing] │
│ │  [tile mosaic mini-map]  │  │                                                  │
│ └──────────────────────────┘  │                                                  │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Panel Descriptions

| Panel | Location | Content |
|-------|----------|---------|
| **3D Viewport** | Center (primary) | Full DX12 terrain render; brush ring indicator |
| **Tool Panel** | Left | Context-sensitive controls for current mode |
| **Layer Stack** | Right-top | Splatmap layers with thumbnail swatches |
| **Heightmap Preview** | Right-middle | Live 16-bit grayscale thumbnail (256×256 display) |
| **Asset Browser** | Right-bottom | Foliage assets, rock meshes, decals; drag-to-place |
| **World Map** | Bottom-left | Mosaic of all world tiles with camera indicator |
| **Status Bar** | Bottom-right | Performance stats, current tile, GPU timing |

### Mode Toolbar

The top toolbar switches the active editing mode. Each mode reconfigures the Tool Panel:

```
[Sculpt Mode]  [Paint Mode]  [Foliage Mode]  [Place Mode]  [Atmosphere Mode]
     F1              F2            F3              F4              F5
```

---

## 3. Sculpting Tools

### Tool Panel — Sculpt Mode

```
┌─────────────────────┐
│  SCULPT TOOLS       │
├─────────────────────┤
│ (•) Raise           │
│ ( ) Lower           │
│ ( ) Smooth          │
│ ( ) Flatten         │
│ ( ) Ramp            │
│ ( ) Erosion         │
│ ( ) Noise           │
├─────────────────────┤
│ BRUSH               │
│ Radius  [====] 32m  │
│ Strength[===]  0.35 │
│ Falloff         ▼   │
│  (•) Smooth         │
│  ( ) Linear         │
│  ( ) Constant       │
├─────────────────────┤
│ SYMMETRY            │
│ [ ] X-Axis          │
│ [ ] Z-Axis          │
│ [ ] Radial  N: 4    │
├─────────────────────┤
│ HEIGHTMAP           │
│ [Import PNG]        │
│ [Export PNG]        │
│ [Export RAW]        │
└─────────────────────┘
```

### Sculpt Tool Reference

| Tool | Shortcut | Description | Algorithm |
|------|----------|-------------|-----------|
| **Raise** | R | Increase height within brush | Add gaussian-weighted delta per pixel |
| **Lower** | F | Decrease height within brush | Subtract gaussian-weighted delta |
| **Smooth** | S | Gaussian blur on heightmap region | 5×5 Gaussian kernel (sigma configurable) |
| **Flatten** | G | Set all pixels to a target height | Lerp toward sampled height at brush center |
| **Ramp** | T | Linear height ramp between two clicked points | Bilinear interpolation along click-drag segment |
| **Erosion** | E | Hydraulic or thermal erosion simulation | See Section 9 (Procedural Generation) |
| **Noise** | N | Apply procedural noise offset | Simplex / Perlin / Voronoi (selectable) |

### Brush Properties

| Property | Range | Default | Description |
|----------|-------|---------|-------------|
| `radius` | 0.5 – 512 m | 32 m | Brush circle radius in world units |
| `strength` | 0.01 – 1.0 | 0.35 | Scale of height change per stroke tick |
| `falloff` | smooth / linear / constant | smooth | How strength decreases toward brush edge |
| `rotation` | 0° – 360° | 0° | Brush rotation (for stamp brushes) |
| `spacing` | 0.1 – 1.0 | 0.25 | Distance between paint ticks as fraction of radius |

### Falloff Curves

```
Smooth (cosine):         Linear:             Constant:
    │  ╭──╮                │ ╱                  │ ╔══╗
str │ ╱    ╲               │╱                   │ ║  ║
    │╱      ╲              │                    │ ║  ║
    └────────             └────────             └────────
     0      r             0      r              0      r
```

### Symmetric Sculpting

Symmetry is applied in world space around the terrain center or around a user-defined pivot point. Available modes:

- **X-Axis**: Mirror across the XZ=0 plane; every brush stroke is duplicated on the opposite X side.
- **Z-Axis**: Mirror across the ZX=0 plane.
- **Radial**: N-way radial symmetry (N = 2, 3, 4, 6, 8); brush is copied at N equidistant angles.

### Heightmap Import / Export

```
Supported import formats:
  - 16-bit grayscale PNG (preferred; lossless)
  - 8-bit grayscale PNG (auto-upscaled to 16-bit)
  - 16-bit RAW (little-endian, width × height × 2 bytes)
  - R16_UNORM DDS

Export formats:
  - 16-bit grayscale PNG  (saved as .heightmap companion file)
  - 16-bit RAW little-endian
```

On import, the editor validates that dimensions satisfy `(N × chunk_size_pixels) + 1` and prompts to resize if they do not.

### Sculpt Undo

Each sculpt stroke produces a **heightmap delta snapshot** of the modified region only. Snapshots are LZ4-compressed before being pushed to the `UndoStack`. A full 1025×1025 heightmap (2 MB) compressed to ~200 KB; typical per-stroke deltas are 5–30 KB depending on brush size.

---

## 4. Texture Painting (Splatmap)

### Layer Stack Panel

```
┌─────────────────────────────┐
│  LAYER STACK         [+ Add] │
├─────────────────────────────┤
│ 3 [eye] [↑] [↓]  Snow       │
│   [████] albedo: snow.png    │
│          tiling: 5.0  rot:0° │
├─────────────────────────────┤
│ 2 [eye] [↑] [↓]  Rock       │
│   [████] albedo: rock.png    │
│          tiling: 6.0  rot:0° │
├─────────────────────────────┤
│ 1 [eye] [↑] [↓]  Dirt       │
│   [████] albedo: dirt.png    │
│          tiling: 8.0  rot:0° │
├─────────────────────────────┤
│ 0 [eye] [↑] [↓]  Grass  [*] │  ← active layer (painted to)
│   [████] albedo: grass.png   │
│          tiling: 8.0  rot:0° │
├─────────────────────────────┤
│ Splatmap: world_splat.png    │
│ [Export Splatmap PNG]        │
└─────────────────────────────┘
```

### Layer Properties

Each texture layer supports up to three texture maps:

| Property | Type | Description |
|----------|------|-------------|
| `name` | string | Display name (e.g., "Grass", "Rock") |
| `albedo` | texture path | Diffuse/albedo texture |
| `normal` | texture path | Tangent-space normal map |
| `roughness_map` | texture path | Roughness/metallic packed map (optional) |
| `roughness` | float 0–1 | Constant roughness (used if no map) |
| `tiling` | float | World-space UV tiling (texels/meter) |
| `rotation` | float 0–360° | UV rotation of this layer |
| `blend_mode` | enum | `weight` (accumulate) or `direct` (replace) |
| `visible` | bool | Toggle layer visibility for inspection |

**Maximum layers**: 8 per terrain. Layers 0–3 are encoded in the primary splatmap RGBA. Layers 4–7 are encoded in a secondary splatmap (`world_splat2.png`) with the same RGBA packing.

### Paint Tool Panel

```
┌─────────────────────┐
│  PAINT TOOLS        │
├─────────────────────┤
│ (•) Paint Layer     │
│ ( ) Erase Layer     │
│ ( ) Smooth Weights  │
│ ( ) Select Layer    │
│ ( ) Auto-Paint      │
├─────────────────────┤
│ TARGET LAYER        │
│ [ Grass (0)      ▼] │
├─────────────────────┤
│ BRUSH               │
│ Radius  [====] 20m  │
│ Opacity [===]  0.5  │
│ Flow    [==]   0.3  │
│ Hardness[=]    0.1  │
│ Mode:               │
│ (•) Accumulate      │
│ ( ) Replace         │
├─────────────────────┤
│ AUTO-PAINT RULES    │
│ [Configure Rules...] │
└─────────────────────┘
```

### Auto-Paint Rules

Auto-paint applies texture weights procedurally based on terrain properties. Rules are evaluated in layer order (last rule wins):

| Rule Trigger | Parameter | Example |
|--------------|-----------|---------|
| Slope range | `min_slope` – `max_slope` degrees | 30°–90° → Rock layer |
| Altitude range | `min_height` – `max_height` meters | 150–200 m → Snow layer |
| Curvature | concave / convex | Concave → Dirt (sediment) |
| Layer adjacency | blend into adjacent layer N | Smooth Grass-to-Rock transition |
| Random noise | strength (adds variation) | Breaks up tiling artifacts |

Auto-paint rules are stored inside the `.uzterrain` INI under `[auto_paint]` sections and can be re-applied non-destructively at any time:

```ini
[auto_paint.rule0]
layer       = 1          ; Rock
trigger     = slope
min_slope   = 35.0
max_slope   = 90.0
strength    = 1.0

[auto_paint.rule1]
layer       = 3          ; Snow
trigger     = altitude
min_height  = 160.0
max_height  = 200.0
strength    = 0.9
```

### Splatmap Channel Encoding

```
Primary splatmap (world_splat.png — RGBA):
  R = layer 0 weight (Grass)
  G = layer 1 weight (Rock)
  B = layer 2 weight (Dirt)
  A = layer 3 weight (Sand)

Secondary splatmap (world_splat2.png — RGBA):
  R = layer 4 weight
  G = layer 5 weight
  B = layer 6 weight
  A = layer 7 weight
```

The terrain shader normalizes weights per-pixel so channels do not need to sum to 1.0. This allows artists to paint freely without manually balancing weights.

---

## 5. Foliage Editor

### Foliage Asset Types

| Type | Example Assets | Default LOD Levels | Physics |
|------|---------------|-------------------|---------|
| **Grass** | grass_clump, fern, flower | 2 (mesh + billboard) | None |
| **Bush** | shrub_01, holly, fern_large | 3 | None |
| **Tree** | pine_01, oak_large, palm | 4 + impostor | Capsule collider (trunk only) |
| **Rock** | rock_small, boulder, pebble | 3 | Convex mesh collider |
| **Decal** | leaf_pile, moss_patch | 1 | None |

### Foliage Tool Panel

```
┌─────────────────────────────┐
│  FOLIAGE TOOLS              │
├─────────────────────────────┤
│ (•) Paint                   │
│ ( ) Erase                   │
│ ( ) Select                  │
│ ( ) Density Map             │
├─────────────────────────────┤
│ ACTIVE ASSET                │
│ [Pine Tree (pine_01)     ▼] │
│ [Preview thumbnail]         │
├─────────────────────────────┤
│ BRUSH                       │
│ Radius  [====] 15m          │
│ Density [===]  0.05/m²      │
│ Jitter  [===]  0.8          │
├─────────────────────────────┤
│ INSTANCE PROPERTIES         │
│ Scale   min: 0.7  max: 1.8  │
│ Rot Y   [x] Random          │
│ Align to Normal: [x]        │
│ Cull dist: 500m             │
│ Wind sway: 0.3 amp          │
├─────────────────────────────┤
│ PLACEMENT RULES             │
│ Max slope:   25°            │
│ Min height:  5m             │
│ Max height:  180m           │
│ Layer mask:  [Grass] [x]    │
├─────────────────────────────┤
│ LOD DISTANCES               │
│ LOD0: 0–50m                 │
│ LOD1: 50–150m               │
│ LOD2: 150–300m              │
│ Impostor: 300–500m          │
└─────────────────────────────┘
```

### Foliage Density Reference Table

| Foliage Type | Dense (instances/m²) | Medium | Sparse | GPU Budget (1km² terrain) |
|-------------|---------------------|--------|--------|---------------------------|
| Grass clumps | 8.0 | 4.0 | 1.0 | ~8M / ~4M / ~1M |
| Ferns | 2.0 | 0.5 | 0.1 | ~2M / ~500K / ~100K |
| Bushes | 0.5 | 0.1 | 0.02 | ~500K / ~100K / ~20K |
| Trees (small) | 0.1 | 0.05 | 0.01 | ~100K / ~50K / ~10K |
| Trees (large) | 0.02 | 0.01 | 0.002 | ~20K / ~10K / ~2K |
| Rocks | 0.3 | 0.1 | 0.02 | ~300K / ~100K / ~20K |

> Note: GPU instance buffer is 1M instances per terrain tile. Practical foliage counts are determined by cull distances — most instances are culled by the GPU culling compute shader before drawing.

### Foliage Instance Properties

| Property | Type | Description |
|----------|------|-------------|
| `density` | float (instances/m²) | Target density within painted areas |
| `min_scale` | float | Minimum uniform scale multiplier |
| `max_scale` | float | Maximum uniform scale multiplier |
| `random_yaw` | bool | Apply random Y-axis rotation per instance |
| `align_to_normal` | bool | Rotate instance to match terrain surface normal |
| `cull_distance` | float (meters) | GPU culling distance |
| `lod_distances` | float[4] | Distance thresholds for LOD 0–3 |
| `wind_sway_amp` | float | Wind sway amplitude (vertex shader param) |
| `wind_sway_freq` | float | Wind sway frequency (Hz) |
| `cast_shadow` | bool | Whether foliage casts shadows |
| `receive_shadow` | bool | Whether foliage receives shadows |

### Procedural Placement Rules

Placement rules constrain where foliage is scattered procedurally. Rules are AND-combined:

```ini
[foliage.pine_trees]
mesh             = "res://meshes/foliage/pine_01.glb"
density          = 0.05
min_scale        = 0.7
max_scale        = 1.8
random_rotation  = true
align_to_normal  = true
max_slope        = 25.0         ; degrees — no trees on steep slopes
min_height       = 5.0          ; world Y meters
max_height       = 180.0
layer_mask       = 0x01         ; only on splatmap layer 0 (Grass)
cull_distance    = 500.0
wind_sway        = 0.15
lod_distances    = [50, 150, 300, 500]
cast_shadow      = true
```

### Foliage GPU Rendering Pipeline

```
CPU: FoliagePainter → .uzfoliage (binary instance data per tile)
         ↓
Scene Load: Upload FoliageInstance[] → GPU StructuredBuffer (per foliage type)
         ↓
Per Frame: Compute shader frustum cull → IndirectArguments buffer
         ↓
DrawIndexedInstancedIndirect (ExecuteIndirect) — zero CPU readback
         ↓
Foliage vertex shader: read StructuredBuffer[SV_InstanceID]
                       apply wind sway displacement
                       apply LOD morph factor
```

### Foliage Physics

| Type | Collider | Jolt Shape |
|------|----------|------------|
| Grass | None | — |
| Bush | None | — |
| Tree (trunk ≥ 0.3m radius) | Capsule | `CapsuleShapeSettings` (r=trunk_radius, h=2m) |
| Rock (all) | Convex mesh | `ConvexHullShapeSettings` (auto-cooked from LOD2 mesh) |

Tree colliders are only created within a configurable `collision_distance` (default: 50 m) from any active character to keep the Jolt body count bounded.

---

## 6. Object Placement Mode

The Place mode allows scattering or individually placing `StaticMesh` nodes on the terrain surface.

### Place Tool Panel

```
┌─────────────────────────────┐
│  PLACE TOOLS                │
├─────────────────────────────┤
│ (•) Scatter Brush           │
│ ( ) Single Place            │
│ ( ) Physical Drop           │
│ ( ) Select / Move           │
├─────────────────────────────┤
│ ACTIVE MESH                 │
│ [rock_boulder_01.glb     ▼] │
│ [Preview]                   │
├─────────────────────────────┤
│ SCATTER BRUSH               │
│ Radius  [====] 20m          │
│ Count   [===]  5/stroke     │
│ Jitter  [===]  0.9          │
├─────────────────────────────┤
│ ALIGNMENT                   │
│ [x] Align to surface        │
│ [ ] Lock Y rotation         │
│ Grid snap: [ ] 0.5m         │
├─────────────────────────────┤
│ TRANSFORM VARIANCE          │
│ Scale   min:0.8  max:1.2    │
│ Rot Y   min: 0°  max:360°   │
└─────────────────────────────┘
```

### Placement Modes

| Mode | Description |
|------|-------------|
| **Scatter Brush** | Paint-stroke places N random instances per tick within brush radius |
| **Single Place** | Click on terrain surface to place one instance; shows transform gizmo |
| **Physical Drop** | Instance spawns above surface and falls with Jolt physics; settles in place |
| **Select / Move** | Click to select placed instances; uses standard transform gizmo |

### Physical Drop Settlement

Physical Drop creates a temporary dynamic Jolt body for the mesh, simulates for up to 5 seconds, waits for the body to go to sleep (velocity < 0.1 m/s), then freezes it as a static `StaticMesh` node. The undo action teleports the instance back to the pre-drop position.

### Grid Snapping

When grid snap is enabled, instance positions are snapped to a user-defined grid in XZ (independent of Y). Grid size presets: 0.25 m, 0.5 m, 1.0 m, 2.0 m, custom.

---

## 7. World Composition (Large Worlds)

### Architecture Overview

Large worlds exceed the practical size of a single terrain (max 4096 × 4096 m per Jolt `HeightFieldShape`). The World Composition system divides the world into **tiles** managed by a `.uzworld` manifest.

```
┌──────────────────────────────────────────────────────────┐
│  .uzworld manifest                                       │
│  ┌─────┬─────┬─────┬─────┬─────┐                        │
│  │(0,0)│(1,0)│(2,0)│(3,0)│(4,0)│  ← tile row 0         │
│  ├─────┼─────┼─────┼─────┼─────┤                        │
│  │(0,1)│(1,1)│(2,1)│(3,1)│(4,1)│  ← tile row 1         │
│  ├─────┼─────┼─────┼─────┼─────┤                        │
│  │(0,2)│(1,2)│[cam]│(3,2)│(4,2)│  ← current tile       │
│  └─────┴─────┴─────┴─────┴─────┘                        │
│                                                          │
│  Each tile = 1 km × 1 km  (configurable)                │
│  Each tile = 1 .uzterrain + N .uzscene objects           │
└──────────────────────────────────────────────────────────┘
```

### World Map Panel

```
┌─────────────────────────────────────────┐
│  WORLD MAP                     [+ Tile] │
├─────────────────────────────────────────┤
│                                         │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┐     │
│  │   │   │   │   │   │   │   │   │     │
│  ├───┼───┼───┼───┼───┼───┼───┼───┤     │
│  │   │   │ ░░│░░░│   │   │   │   │     │
│  ├───┼───┼─░░┼░░─┼───┼───┼───┼───┤     │
│  │   │   │░░░│░  │   │   │   │   │     │
│  ├───┼───┼───┼───┼───┼───┼───┼───┤     │
│  │   │   │   │ ▲ │   │   │   │   │     │ ← camera pos
│  └───┴───┴───┴───┴───┴───┴───┴───┘     │
│                                         │
│  [x] Show tile names                   │
│  [ ] Show navmesh coverage             │
│  [ ] Show streaming radius             │
│  [Merge Tiles...] [Split Tile...]      │
└─────────────────────────────────────────┘
```

### Tile Streaming

| Parameter | Default | Description |
|-----------|---------|-------------|
| `tile_size` | 1024 m | Tile size in meters (XZ) |
| `stream_radius` | 2 tiles (2048 m) | Load tiles within this radius |
| `unload_radius` | 3 tiles (3072 m) | Unload tiles beyond this radius |
| `lod_impostor_dist` | 1500 m | Tiles beyond this distance use billboard impostor |

Streaming operates on a background IO thread. Priority queue sorts tiles by camera proximity; closest tiles load first. Unloading releases GPU memory (vertex buffers, textures, foliage GPU buffers).

### World Origin Rebasing

At large distances (> 50 km from world origin), floating-point precision degrades. The engine applies **world origin rebasing** when the camera exceeds a threshold:

1. Camera's world position is subtracted from all active node transforms.
2. The new logical world origin matches the camera's current tile center.
3. All physics bodies are shifted by the same offset.
4. The tile streaming system re-centers its load radius.

Rebasing is transparent to Lua scripts (position values are always relative to current world origin).

### Per-Tile Data

Each tile stores:

```
terrain/tiles/tile_X_Y/
├── tile.uzterrain         — terrain definition (INI)
├── tile.heightmap         — 16-bit PNG heightmap
├── tile_splat.png         — RGBA splatmap (layers 0–3)
├── tile_splat2.png        — RGBA splatmap (layers 4–7, if used)
├── tile_foliage.uzfoliage — binary foliage instance positions
├── tile_objects.uzscene   — placed static mesh nodes for this tile
└── tile_thumb.png         — 256×256 thumbnail for World Map mosaic
```

### Tile Operations

| Operation | Description |
|-----------|-------------|
| **Add Tile** | Creates a new flat tile adjacent to the world; generates default terrain |
| **Remove Tile** | Deletes all tile data (requires confirmation) |
| **Merge Tiles** | Combines two adjacent tiles into one tile (doubles resolution, resamples heightmap) |
| **Split Tile** | Splits one tile into four 0.5× tiles (halves resolution per tile) |
| **Rebake All NavMesh** | Triggers NavMesh bake across all loaded tiles |
| **Export Thumbnail** | Regenerates all tile thumbnails for the World Map mosaic |

---

## 8. Atmosphere & Sky System

### Atmosphere Tool Panel

```
┌─────────────────────────────┐
│  ATMOSPHERE                 │
├─────────────────────────────┤
│ PRESETS                     │
│ [Clear] [Overcast] [Rain]   │
│ [Storm] [Snow] [Fog]        │
├─────────────────────────────┤
│ TIME OF DAY                 │
│ Time  [========] 14:30      │
│ Sun dir: X:-35° Y:210°      │
│ Animate: [ ]  [Keyframes]   │
├─────────────────────────────┤
│ SUN                         │
│ Color  [████████] #FFF5E0   │
│ Intensity  [====] 1.5 lux   │
│ Disk size  [==]   0.5°      │
├─────────────────────────────┤
│ SKY                         │
│ Sky top    [████] #1A3A6E   │
│ Sky horiz  [████] #C0D8F0   │
│ Rayleigh   [==]   0.0025    │
│ Mie        [=]    0.003     │
├─────────────────────────────┤
│ FOG                         │
│ Enabled: [x]                │
│ Color  [████████] #C0C8D0   │
│ Density  [===]   0.005      │
│ Start    [===]   50m        │
│ Height falloff [==] 0.3     │
├─────────────────────────────┤
│ CLOUDS                      │
│ Enabled: [x]                │
│ Coverage [====]  0.4        │
│ Density  [===]   0.7        │
│ Speed    [==]    0.02 m/s   │
│ Direction: 45°              │
└─────────────────────────────┘
```

### Sky Parameters

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `time_of_day` | float | 0.0–24.0 | Hour of day (drives sun direction) |
| `sun_direction` | vec2 | pitch/yaw | Explicit override (if not time-driven) |
| `sun_color` | RGB | — | Sun disc and primary shadow color |
| `sun_intensity` | float | 0–10 lux | Directional light illuminance |
| `sky_top_color` | RGB | — | Zenith sky color |
| `sky_horizon_color` | RGB | — | Horizon sky color |
| `rayleigh` | float | 0.0–0.01 | Rayleigh scattering coefficient (blue sky) |
| `mie` | float | 0.0–0.02 | Mie scattering coefficient (sun halo) |

### Fog Parameters

UzEngine uses exponential height fog:

```
density_at_height(y) = fog_density * exp(-fog_height_falloff * y)
fog_factor = 1 - exp(-integrated_density_along_ray)
```

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `fog_enabled` | bool | false | Enable volumetric fog |
| `fog_color` | RGB | #C0C8D0 | Fog scatter color |
| `fog_density` | float | 0.005 | Global fog density coefficient |
| `fog_start` | float | 50 m | Distance at which fog begins |
| `fog_height_falloff` | float | 0.3 | Rate fog thins with altitude |

### Cloud Layer

Procedural cloud layer is generated by a tileable cloud noise texture scrolled over time:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `cloud_enabled` | bool | false | Enable cloud layer |
| `cloud_coverage` | float 0–1 | 0.4 | How much of sky is covered |
| `cloud_density` | float 0–1 | 0.7 | Opacity of cloud layer |
| `cloud_altitude` | float (m) | 1500 m | Cloud layer altitude |
| `cloud_speed` | float | 0.02 m/s | Cloud scroll speed |
| `cloud_direction` | float | 45° | Cloud wind direction (degrees) |
| `cloud_shadow` | bool | true | Clouds cast shadows on terrain |

### Weather Presets

| Preset | Time | Sun | Fog | Clouds | Rain |
|--------|------|-----|-----|--------|------|
| **Clear** | 12:00 | 1.5 lux, #FFF5E0 | off | 0.1 coverage | off |
| **Overcast** | 10:00 | 0.4 lux, #D0D8E0 | density 0.002 | 0.85 coverage | off |
| **Rain** | 8:00 | 0.2 lux, #A0A8B0 | density 0.015 | 0.95 coverage | particles |
| **Storm** | 20:00 | 0.05 lux, #606068 | density 0.05 | 1.0 coverage | heavy + lightning |
| **Snow** | 9:00 | 0.5 lux, #E0E8F8 | density 0.008 | 0.7 coverage | snow particles |
| **Fog** | 6:00 | 0.3 lux, #D8D8D8 | density 0.04, start 5m | 0.3 coverage | off |

### Animated Time of Day

Time-of-day animation is defined as a keyframe curve on the `time_of_day` property. The `DirectionalLight` node's rotation tracks the sun direction computed from the time value.

```ini
[atmosphere.tod_keyframes]
; time_of_day → sun_intensity, sun_color, sky_top_color
0.0  = { intensity=0.0, sun_color=[0.1,0.1,0.3], sky_top=[0.05,0.05,0.2] }
6.0  = { intensity=0.2, sun_color=[1.0,0.5,0.1], sky_top=[0.5,0.3,0.1] }  ; sunrise
12.0 = { intensity=1.5, sun_color=[1.0,0.98,0.9], sky_top=[0.1,0.2,0.6] } ; noon
18.0 = { intensity=0.3, sun_color=[1.0,0.4,0.1], sky_top=[0.5,0.2,0.1] }  ; sunset
22.0 = { intensity=0.0, sun_color=[0.05,0.05,0.2], sky_top=[0.02,0.02,0.1] }
```

---

## 9. Procedural Generation Integration

### Heightmap Generation Presets

| Preset | Noise Type | Parameters |
|--------|-----------|------------|
| **Mountains** | Ridged multifractal | octaves=8, lacunarity=2.0, gain=0.5 |
| **Plains** | Simplex + low-frequency billowing | max_height=20m, smoothness=0.9 |
| **Coast** | Simplex + sea-level clamp | sea_level=10m, beach_width=50m |
| **Canyon** | Domain-warped simplex + erosion | canyon_depth=80m, ridges=true |
| **Volcanic Island** | Radial falloff + cone shape | caldera_radius=200m, lava_channels=true |
| **Desert Dunes** | Wind-oriented noise | dune_height=15m, orientation=45° |

### Erosion Parameters

| Type | Iterations | Parameters | Effect |
|------|-----------|------------|--------|
| **Hydraulic** | 10–100K | `inertia`, `capacity`, `deposition`, `evaporation` | Rivers, gullies, sediment plains |
| **Thermal** | 100–10K | `talus_angle`, `strength` | Scree slopes, cliff softening |

Erosion runs on a background thread; progress is displayed in the status bar. Erosion output can be previewed in grayscale before committing to the heightmap.

### AI Prompt Integration (MCP)

The Terrain Editor exposes MCP tool endpoints that accept natural language prompts:

```json
terrain.generate {
  "prompt": "volcanic island with a caldera lake, black sand beaches, lava flow channels",
  "size_m": 2048,
  "seed": 42
}
```

The MCP server interprets the prompt, selects noise presets and erosion passes, and calls:

```
terrain.applyNoisePreset { preset: "volcanic_island", seed: 42 }
terrain.runErosion       { type: "hydraulic", iterations: 50000 }
terrain.runErosion       { type: "thermal", iterations: 5000 }
terrain.autoLayerPaint   { rules: ["altitude_snow", "slope_rock", "flat_black_sand"] }
terrain.autoBiomeFoliage { biome: "tropical_sparse" }
```

All generated operations are recorded as individual undo commands so the artist can step back through any AI-generated change.

### Biome Foliage Auto-Population

After procedural heightmap generation, the biome map drives automatic foliage placement:

| Biome | Foliage |
|-------|---------|
| `alpine` | Sparse pine, rock piles, snowgrass |
| `temperate_forest` | Dense oak/pine mix, ferns, undergrowth |
| `tropical` | Palms, broad-leaf trees, tall grass |
| `desert` | Cacti, dry scrub, rock clusters |
| `tundra` | Dwarf shrubs, moss patches, boulders |
| `coastal` | Beach grass, driftwood, seabirds (particles) |

---

## 10. Performance Tools

### Visualization Modes

The viewport overlay toolbar provides these diagnostic views (toggle per mode):

| Mode | Shortcut | Visualization |
|------|----------|---------------|
| **LOD Colors** | Shift+1 | Terrain chunks color-coded: LOD0=green, LOD1=yellow, LOD2=orange, LOD3=red |
| **Wireframe** | Shift+2 | Full terrain wireframe showing chunk boundaries and triangle density |
| **Overdraw** | Shift+3 | Heat map of foliage overdraw (blue=low, red=high) |
| **Normals** | Shift+4 | Per-vertex normals displayed as blue lines |
| **Splatmap** | Shift+5 | Raw RGBA splatmap shown on terrain (no texture, just color weights) |
| **NavMesh** | Shift+6 | Recast/Detour navmesh overlay with area color coding |

### Foliage Cull Statistics

The status bar displays per-type foliage statistics:

```
Foliage: 482,110 total | 31,440 visible | 98 draw calls | 0.8 ms GPU
   Pine Tree:  12,200 total | 8,400 vis | 12 DC | 0.3 ms
   Grass:     450,000 total | 20,000 vis |  4 DC | 0.2 ms
   Rocks:      19,910 total |  3,040 vis | 82 DC | 0.3 ms
```

### GPU Timing

GPU timestamps are inserted around each terrain render pass. Timings shown in the status bar panel:

| Pass | Typical Budget |
|------|---------------|
| Terrain geometry (all LODs) | 0.5–2.0 ms |
| Terrain shadow map | 0.3–1.0 ms |
| Foliage GPU cull compute | 0.1–0.3 ms |
| Foliage geometry | 0.5–3.0 ms |
| Water surface | 0.2–0.5 ms |
| Atmosphere / sky | 0.1–0.3 ms |
| **Total terrain budget** | **2–7 ms** (target: < 4 ms at 1080p) |

### Streaming Budget Indicator

```
STREAMING  [ ████████░░░░░░░ ] 53%   GPU: 214 / 400 MB
  Loaded tiles: 9 of 25  |  Pending IO: 2 tiles  |  Unloading: 1 tile
```

---

## 11. File Formats

### `.uzterrain` — Terrain Tile Definition

```ini
[uzterrain version=1 name="WorldTerrain_2_3"]

; Dimensions
heightmap        = "res://terrain/tiles/tile_2_3/tile.heightmap"
width_meters     = 1024.0
depth_meters     = 1024.0
height_scale     = 200.0
base_height      = -10.0

; Chunk system
chunk_size       = 32.0       ; meters per chunk (32×32 grid for 1024m terrain)

; Splatmap
splatmap         = "res://terrain/tiles/tile_2_3/tile_splat.png"
splatmap2        = "res://terrain/tiles/tile_2_3/tile_splat2.png"  ; layers 4–7

; Material layers (up to 8)
[material.layer0]
name             = "Grass"
albedo           = "res://textures/terrain/grass_albedo.png"
normal           = "res://textures/terrain/grass_normal.png"
roughness        = 0.9
tiling           = 8.0
uv_rotation      = 0.0

[material.layer1]
name             = "Rock"
albedo           = "res://textures/terrain/rock_albedo.png"
normal           = "res://textures/terrain/rock_normal.png"
roughness        = 0.7
tiling           = 6.0

[material.layer2]
name             = "Dirt"
albedo           = "res://textures/terrain/dirt_albedo.png"
normal           = "res://textures/terrain/dirt_normal.png"
roughness        = 0.95
tiling           = 10.0

[material.layer3]
name             = "Snow"
albedo           = "res://textures/terrain/snow_albedo.png"
normal           = "res://textures/terrain/snow_normal.png"
roughness        = 0.45
tiling           = 5.0

; Foliage layers
[foliage.grass_clumps]
mesh             = "res://meshes/foliage/grass_clump.glb"
density          = 4.0
min_scale        = 0.8
max_scale        = 1.4
random_rotation  = true
align_to_normal  = true
max_slope        = 30.0
min_height       = 0.0
max_height       = 150.0
wind_sway        = 0.2
layer_mask       = 0x01
cull_distance    = 60.0
lod_distances    = [15, 40, 60]

[foliage.pine_trees]
mesh             = "res://meshes/foliage/pine_01.glb"
density          = 0.05
min_scale        = 0.7
max_scale        = 1.8
random_rotation  = true
align_to_normal  = true
max_slope        = 25.0
min_height       = 5.0
max_height       = 180.0
wind_sway        = 0.15
layer_mask       = 0x01
cull_distance    = 500.0
lod_distances    = [50, 150, 300, 500]
physics_capsule  = true
physics_radius   = 0.25
physics_height   = 6.0

; Auto-paint rules
[auto_paint.rule0]
layer       = 1
trigger     = slope
min_slope   = 35.0
max_slope   = 90.0
strength    = 1.0

[auto_paint.rule1]
layer       = 3
trigger     = altitude
min_height  = 160.0
max_height  = 200.0
strength    = 0.9

; Water
[water]
enabled          = true
height           = 0.0
normal_map       = "res://textures/water_normal.png"
normal_scroll_speed = [0.02, 0.015]
shallow_color    = [0.1, 0.6, 0.5, 0.7]
deep_color       = [0.0, 0.2, 0.4, 1.0]
fresnel_strength = 1.0
foam_depth       = 0.5
```

### `.heightmap` — 16-bit PNG Grayscale

Standard 16-bit grayscale PNG. No custom headers or metadata beyond standard PNG chunks.

```
Resolution: (N × chunk_count) + 1 pixels per side
Default:    1025 × 1025 pixels (32 chunks × 32 pixels + 1 shared edge)
Encoding:   uint16 big-endian (standard PNG 16-bit)
Value 0     → world Y = base_height
Value 65535 → world Y = base_height + height_scale
File ext:   .heightmap (treated as standard PNG by all image tools)
```

### `.splatmap` — RGBA PNG Layer Weights

Standard 8-bit RGBA PNG. One file per 4 layers.

```
Resolution: Same as heightmap, typically 1025×1025 or 2049×2049 for detail
Encoding:   8-bit per channel (0–255 → weight 0.0–1.0)
R channel:  Layer 0 weight
G channel:  Layer 1 weight
B channel:  Layer 2 weight
A channel:  Layer 3 weight
Note:       Weights are shader-normalized; channels need not sum to 255
```

### `.uzfoliage` — Binary Foliage Instance Data

Compact binary format for foliage instance positions. One file per terrain tile per foliage type group.

```
Header (16 bytes):
  magic          uint32  = 'UZFL'
  version        uint32  = 1
  foliage_type   uint32  = index into .uzterrain foliage[] array
  instance_count uint32

Per Instance (32 bytes):
  position       float32[3]   x, y, z world position
  yaw            float32      Y-axis rotation (radians)
  scale          float32      uniform scale multiplier
  layer_weight   float32      splatmap weight at this position (for runtime blending)
  _pad           float32[2]   reserved for future use (wind_phase, etc.)

Total: 16 + instance_count × 32 bytes
```

### `.uzworld` — World Composition Manifest

```ini
[uzworld version=1 name="OpenWorld"]

; Global world settings
tile_size        = 1024.0       ; meters per tile (XZ)
tile_height_max  = 4096.0       ; maximum Y extent across all tiles
stream_radius    = 2048.0       ; load tiles within this distance (meters)
unload_radius    = 3072.0
origin_rebase_threshold = 50000.0  ; meters from origin before rebasing

; World-scale atmosphere (overridden per-tile if tile has its own atmosphere block)
[atmosphere]
time_of_day      = 12.0
sun_intensity    = 1.5
fog_enabled      = true
fog_density      = 0.003

; Tile registry
[tile.2.3]
path             = "res://terrain/tiles/tile_2_3/tile.uzterrain"
world_offset     = [2048.0, 0.0, 3072.0]   ; tile origin in world space
objects          = "res://terrain/tiles/tile_2_3/tile_objects.uzscene"
navmesh          = ".import/navmesh/tile_2_3.uznavmesh"
thumbnail        = "res://terrain/tiles/tile_2_3/tile_thumb.png"
enabled          = true

[tile.2.4]
path             = "res://terrain/tiles/tile_2_4/tile.uzterrain"
world_offset     = [2048.0, 0.0, 4096.0]
objects          = "res://terrain/tiles/tile_2_4/tile_objects.uzscene"
navmesh          = ".import/navmesh/tile_2_4.uznavmesh"
thumbnail        = "res://terrain/tiles/tile_2_4/tile_thumb.png"
enabled          = true

; Additional tiles ...
```

---

## 12. Implementation Notes

### C++ Class Hierarchy

```
src/engine/terrain/
├── terrain_system.h/cpp          — TerrainSystem: per-frame update, registration
├── terrain_chunk.h/cpp           — TerrainChunk: GPU buffers, LOD state
├── terrain_lod.h/cpp             — LOD distance computation, geomorphing
├── heightmap_sculptor.h/cpp      — HeightmapSculptor: CPU brush operations
├── splatmap_painter.h/cpp        — SplatmapPainter: CPU layer weight painting
├── foliage_painter.h/cpp         — FoliagePainter: instance placement + .uzfoliage I/O
├── foliage_renderer.h/cpp        — GPU instancing, ExecuteIndirect, GPU cull compute
├── terrain_collision.h/cpp       — Jolt HeightFieldShape create/update, debounced rebuild
├── water_system.h/cpp            — Water mesh, shader parameters
├── world_composition.h/cpp       — WorldComposition: tile streaming, origin rebase
├── atmosphere_system.h/cpp       — AtmosphereSystem: sky, fog, cloud parameters
└── terrain_editor.h/cpp          — TerrainEditor: top-level editor coordinator
                                    bridges all subsystems to VSCode IPC

src/engine/terrain/shaders/
├── terrain.hlsl                  — Terrain vertex + pixel shader (splatmap PBR)
├── foliage.hlsl                  — Foliage vertex + pixel shader (wind sway, LOD morph)
├── foliage_cull.hlsl             — GPU compute: frustum + distance cull → IndirectArgs
├── water.hlsl                    — Water surface shader (normal scroll, foam, fresnel)
└── atmosphere.hlsl               — Sky dome + fog integration shader
```

### Key Class Interfaces

```cpp
// terrain_editor.h

class TerrainEditor {
public:
    // Lifecycle
    void on_attach(TerrainNode* node);          // called when terrain node selected
    void on_detach();
    void on_update(float dt);
    void on_render_3d(ID3D12GraphicsCommandList* cmd);   // brush ring overlay

    // Mode switching
    void set_mode(TerrainEditMode mode);         // Sculpt/Paint/Foliage/Place/Atmosphere
    TerrainEditMode get_mode() const;

    // IPC command dispatch (from VSCode panel messages)
    bool handle_ipc(const std::string& cmd, const JsonValue& args, JsonValue& out);

private:
    HeightmapSculptor   m_sculptor;
    SplatmapPainter     m_painter;
    FoliagePainter      m_foliage;
    AtmosphereSystem    m_atmosphere;
    UndoStack*          m_undo;          // shared with EditorSystem
    TerrainNode*        m_target;
};

// heightmap_sculptor.h

class HeightmapSculptor {
public:
    // Apply a sculpt operation to the in-memory heightmap buffer
    void sculpt(SculptTool tool, glm::vec2 center_xz, const BrushParams& brush,
                std::vector<uint16_t>& heightmap, int width, int height,
                float terrain_width_m, float terrain_depth_m);

    // Erosion simulations (run on worker thread; progress callback)
    void run_hydraulic_erosion(std::vector<uint16_t>& hm, int size,
                               const HydraulicParams& p,
                               std::function<void(float)> progress_cb);

    void run_thermal_erosion(std::vector<uint16_t>& hm, int size,
                             const ThermalParams& p,
                             std::function<void(float)> progress_cb);

    // Noise generation
    void apply_noise(std::vector<uint16_t>& hm, int w, int h,
                     NoiseType type, const NoiseParams& p);

    // Import / export
    bool import_png(const std::string& path, std::vector<uint16_t>& hm,
                    int& out_width, int& out_height);
    bool export_png(const std::string& path, const std::vector<uint16_t>& hm,
                    int width, int height);

private:
    // Brush kernel generators
    std::vector<float> make_gaussian_kernel(float radius_pixels, float sigma) const;
    std::vector<float> make_linear_falloff(float radius_pixels) const;
};

// foliage_painter.h

class FoliagePainter {
public:
    struct FoliageInstance {
        glm::vec3 position;
        float     yaw;
        float     scale;
        float     layer_weight;
        float     _pad[2];
    };

    // Add instances within brush area
    void paint(const FoliageLayerDef& def, glm::vec2 center_xz,
               float radius, float density_multiplier,
               const std::vector<uint16_t>& heightmap,
               const std::vector<uint8_t>& splatmap,
               std::vector<FoliageInstance>& instances);

    // Remove instances within brush area
    void erase(glm::vec2 center_xz, float radius,
               std::vector<FoliageInstance>& instances);

    // Procedural scatter from rules (called on terrain cook)
    void scatter_procedural(const FoliageLayerDef& def,
                             const std::vector<uint16_t>& heightmap,
                             const std::vector<uint8_t>& splatmap,
                             int hm_width, int hm_height,
                             float terrain_width_m, float terrain_depth_m,
                             uint32_t seed,
                             std::vector<FoliageInstance>& out_instances);

    // Serialization
    bool save_uzfoliage(const std::string& path, uint32_t foliage_type_idx,
                        const std::vector<FoliageInstance>& instances);
    bool load_uzfoliage(const std::string& path,
                        std::vector<FoliageInstance>& out_instances);
};

// terrain_collision.h

class TerrainCollision {
public:
    // Create or recreate the Jolt HeightFieldShape from heightmap data
    void rebuild(const std::vector<uint16_t>& heightmap,
                 int samples_per_side,
                 float cell_size_m,
                 float height_scale,
                 float base_height,
                 JPH::PhysicsSystem& jph);

    // Debounced rebuild — called on each sculpt tick; fires rebuild after 500ms quiet
    void notify_sculpted(const std::vector<uint16_t>& heightmap, /* ... */);

    void update(float dt);   // processes debounce timer

private:
    JPH::BodyID       m_body_id;
    float             m_rebuild_timer  = 0.0f;
    bool              m_rebuild_pending = false;
    static constexpr float REBUILD_DEBOUNCE_S = 0.5f;
};

// world_composition.h

class WorldComposition {
public:
    bool load_manifest(const std::string& uzworld_path);
    void update(const glm::vec3& camera_world_pos);
    void unload_all();

    // World origin rebasing
    glm::vec3 get_world_offset() const;   // subtract from all node positions
    void apply_rebase_if_needed(const glm::vec3& camera_pos,
                                SceneManager& scene,
                                JPH::PhysicsSystem& jph);

private:
    struct TileEntry {
        std::string path;
        glm::vec3   world_offset;
        std::string objects_path;
        std::string navmesh_path;
        std::string thumbnail_path;
        bool        loaded    = false;
        bool        requested = false;
    };

    std::unordered_map<std::string, TileEntry> m_tiles;
    float  m_stream_radius;
    float  m_unload_radius;
    float  m_rebase_threshold;
    glm::vec3 m_current_world_offset = {0, 0, 0};

    void load_tile(TileEntry& tile, Arena& arena);
    void unload_tile(TileEntry& tile);
};
```

### DX12 Rendering Pipeline

```
Frame Begin
│
├── Terrain Pass
│   ├── SetPipelineState(terrain_pso)
│   ├── SetDescriptorTable(heightmap, splatmap, layer textures)
│   ├── Per chunk: frustum cull → DrawIndexedInstanced (LOD selected)
│   └── Geomorphing blend factor via root constants
│
├── Foliage Cull Pass (Compute)
│   ├── SetPipelineState(foliage_cull_cso)
│   ├── Dispatch: thread per foliage type per instance
│   └── Output: IndirectArguments buffer per foliage type
│
├── Foliage Render Pass
│   ├── SetPipelineState(foliage_pso)
│   └── Per foliage type: ExecuteIndirect(foliage_ib, indirect_args)
│
├── Water Pass
│   ├── SetPipelineState(water_pso)
│   ├── Bind depth buffer for edge foam + refraction
│   └── Draw water quad grid
│
└── Sky / Atmosphere Pass
    ├── SetPipelineState(sky_pso)
    └── Draw fullscreen sky dome (depth = 1.0, drawn last)
```

### Jolt Physics Integration

Sculpt operations update the in-memory `std::vector<uint16_t>` heightmap immediately for visual feedback. The Jolt `HeightFieldShape` rebuild is debounced: a 500 ms timer resets on every sculpt tick. When the timer fires (user stopped sculpting), the terrain collision is rebuilt on a background thread via:

```cpp
void TerrainCollision::notify_sculpted(const std::vector<uint16_t>& hm, ...) {
    m_pending_heightmap = hm;    // copy
    m_rebuild_timer     = REBUILD_DEBOUNCE_S;
    m_rebuild_pending   = true;
}

void TerrainCollision::update(float dt) {
    if (!m_rebuild_pending) return;
    m_rebuild_timer -= dt;
    if (m_rebuild_timer <= 0.0f) {
        m_rebuild_pending = false;
        // Enqueue background task
        g_worker_pool.enqueue([this] {
            rebuild(m_pending_heightmap, ...);
        });
    }
}
```

NavMesh invalidation is triggered after physics rebuild completes. The editor shows a status bar warning: "NavMesh outdated — click to rebake" with a one-click rebake action.

### VSCode WebView Integration

The Terrain Editor VSCode extension contributes:

1. **Terrain Editor Panel** (`uzengine.terrain.editor`): Full-screen WebView hosting the tool panels, layer stack, world map, and status bar. Communicates via IPC to `UzEngine.exe --editor`.
2. **Layer Stack WebView**: HTML list of splatmap layers with thumbnail swatches (updated via `terrain.getLayerThumbnails` IPC response).
3. **World Map WebView**: HTML canvas mosaic of tile thumbnails; click navigates the DX12 camera to that tile.
4. **Heightmap Preview WebView**: 256×256 canvas updated via `terrain.getHeightmapThumb` (returns 256×256 grayscale PNG as base64).

### IPC Handlers — Full Reference

```
; Sculpting
terrain.sculpt           { tool, center:[x,z], radius, strength, falloff, symmetry }
terrain.importHeightmap  { path }                 → { ok }
terrain.exportHeightmap  { path, format }         → { ok }
terrain.getHeightmapThumb {}                      → { png_base64 }
terrain.runErosion       { type, iterations, params } → streams progress events

; Painting
terrain.paintLayer       { layer, center:[x,z], radius, opacity, mode }
terrain.eraseLayer       { layer, center:[x,z], radius, opacity }
terrain.smoothWeights    { center:[x,z], radius, strength }
terrain.autoLayerPaint   { rules }                → { ok }
terrain.getLayerThumbnails {}                     → { thumbnails:[{layer,png_base64}] }

; Foliage
terrain.foliagePaint     { foliage_type, center:[x,z], radius, density_mult }
terrain.foliageErase     { foliage_type, center:[x,z], radius }
terrain.addFoliageLayer  { ... }
terrain.removeFoliageLayer { name }
terrain.getFoliageStats  {}                       → { per_type:[{name,total,visible}] }

; Object placement
terrain.placeObject      { mesh, position:[x,y,z], rotation:[x,y,z], scale }
terrain.scatterObjects   { mesh, center:[x,z], radius, count, variance }
terrain.physicalDrop     { mesh, position:[x,y,z] }

; World composition
world.loadManifest       { path }
world.addTile            { tile_x, tile_z, size_m }
world.removeTile         { tile_x, tile_z }
world.getTileList        {}                       → { tiles:[...] }
world.getTileThumbnails  {}                       → { thumbnails:[{tile, png_base64}] }
world.mergeTiles         { tile_a, tile_b }
world.splitTile          { tile_x, tile_z }
world.rebakeAllNavmesh   {}                       → streams progress events

; Atmosphere
atmosphere.setParams     { time_of_day, sun_intensity, fog_density, ... }
atmosphere.setPreset     { preset }
atmosphere.addTodKeyframe { time, params }
atmosphere.removeTodKeyframe { time }

; Queries
terrain.getHeightAt      { x, z }                → { height }
terrain.getNormalAt      { x, z }                → { normal:[x,y,z] }
terrain.getWeightsAt     { x, z }                → { r, g, b, a }
terrain.rebuildCollision  {}
terrain.bakeNavmesh      {}                       → streams progress events
```

---

## 13. System Limits

| Constraint | Value | Notes |
|------------|-------|-------|
| Max terrain size per tile | 4096 × 4096 m | Jolt HeightFieldShape limit (4097 samples) |
| Default tile size | 1024 × 1024 m | 1025×1025 heightmap |
| Max splatmap layers | 8 | 2 RGBA splatmaps |
| Max foliage layer types per tile | 16 | `.uzterrain` foliage[] array |
| Max GPU foliage instances per tile | 1,000,000 | ~64 MB StructuredBuffer |
| Max LOD levels per terrain chunk | 4 | LOD0–LOD3 + geomorphing |
| Max world tiles | 4096 (64×64 grid) | `.uzworld` manifest limit |
| Max world extents (64×64 tiles × 1km) | 64 km × 64 km | Float rebasing required beyond 50 km |
| Undo stack depth | 100 entries | LZ4-compressed heightmap deltas |
| Erosion max iterations | 500,000 | Background thread; ~30s on modern CPU |
| Physics rebuild debounce | 500 ms | After last sculpt tick |
| Foliage physics collision distance | 50 m | Tree capsule colliders only within this range |
| Water planes per tile | 1 | Multiple water volumes: Phase 5 |

---

## 14. Related Documentation

- [specs/terrain-system.md](../specs/terrain-system.md) — Terrain runtime: chunk system, LOD, shader, collision
- [specs/scene-system.md](../specs/scene-system.md) — Node types, `.uzscene` format, `Terrain` node
- [specs/physics-system.md](../specs/physics-system.md) — Jolt integration, `HeightFieldShape`, `CColliderTerrain`
- [specs/navmesh.md](../specs/navmesh.md) — Recast/Detour navmesh baking over terrain geometry
- [specs/editor-panels.md](../specs/editor-panels.md) — Editor panel architecture, IPC protocol
- [specs/undo-redo.md](../specs/undo-redo.md) — `UndoStack`, `EditorCommand` pattern (heightmap deltas)
- [specs/editor-gizmos.md](../specs/editor-gizmos.md) — Transform gizmo used in Object Placement mode
- [specs/gpu-instancing.md](../specs/gpu-instancing.md) — `StructuredBuffer<float4x4>`, `ExecuteIndirect` foliage
- [specs/shader-library.md](../specs/shader-library.md) — `GlobalParams` cbuffer at b15, terrain.hlsl conventions
- [specs/ai-native-features.md](../specs/ai-native-features.md) — MCP `terrain.generate` tool integration
