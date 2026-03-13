# Terrain — Heightmap-Based Landscape & Sculpting

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/terrain/`, `engine/subsystem/terrain_world.h`
> **Depends on**: Rendering (§12), Physics (§16), Streaming (§18), Material System (§33)
> **References**: UE5 `ALandscapeProxy` + `ALandscapeStreamingProxy`, Godot community terrain plugins

## Purpose

Large-scale heightmap-based terrain rendering and editing. Terrain decouples mesh generation from artists' sculpting brushes — GPU-driven indirect rendering handles 10+ km² maps with LOD, while the editor provides brush-based tools (raise, lower, smooth, flatten, erode). Material blending via painted weight layers and triplanar projection enable cliff-to-grass transitions without UV seams. Virtual texturing streams high-resolution detail textures from disk on demand.

## Design Overview

Terrain is **not** a monolithic mesh — it's split into:

1. **CLandscape** — owns heightmap texture (R16 or R32F), material layers, sculpting state
2. **CTerrainCollider** — physics representation (Jolt heightfield collider per chunk)
3. **CLandscapeComponent** — render proxy (clipmap LOD mesh + material)
4. **Foliage spawning** — integrated density maps per layer (grass, trees)

All rendering is **GPU-driven**: compute shader generates mesh on-the-fly, indirect draw submits clustered geometry. No static mesh asset — terrain is procedural.

## Heightmap Format

```cpp
/// Heightmap resolution per component: 513x513 vertex grid (512x512 texels).
/// Each component is ~2km² at 4m grid spacing.
constexpr uint32_t kTerrainVertexCountPerComponent = 513;

/// Height values: 16-bit (±327.67 m precision) or 32-bit (full float).
/// Stored in GPU texture with per-component offset for precision.
enum class HeightmapFormat {
    Height16,  // int16: ±327.67 m with 0.01 m precision
    Height32,  // float32: full IEEE precision
};

/// Texture array: layer 0 = height, layer 1-N = material weights (R8 per layer)
/// Resolution: (ComponentCountX * 512) × (ComponentCountY * 512) texels
/// Stored as `res://terrain.uztexture` (compressed at cook time, uncompressed in GPU vram)
```

Heightmap is **persistent** — sculpt changes are immediately reflected in the live texture and saved to the project when the user saves.

## Clipmap LOD — Seamless Concentric Rings

Instead of pre-baking LOD meshes, terrain uses a **clipmap** approach: concentric rectangular rings, each coarser than the inner. A 5-ring clipmap uses 1:1 triangles for ~50m radius, then 1:2, 1:4, 1:8, 1:16 from there to 5 km.

```
Ring 0 (1:1)   ┌─────────────────┐
               │   512x512       │  World Space
               │   (4m spacing)  │  ~2 km radius
               └─────────────────┘

Ring 1 (1:2)   ┌─────────────────────────────┐
               │   256x256                   │  Every other vertex
               │   (8m spacing)              │  ~4 km radius
               └─────────────────────────────┘

Ring 4 (1:16)  ┌─────────────────────────────────────────┐
               │   64x64                                 │  ~16 km radius
               │   (64m spacing)                         │  Closes horizon
               └─────────────────────────────────────────┘
```

The clipmap is **shifted** to follow the camera. Each frame, compute shader:
1. Tests which ring needs update (camera distance > update threshold)
2. Samples heightmap, interpolates to ring's LOD, writes to ring mesh buffer
3. GPU mesh shader stitches ring boundaries (no cracks)

**Result**: Seamless mesh at 60 FPS with <1 ms GPU cost for entire planet.

## GPU-Driven Rendering

No traditional submeshes — rendering is **fully compute-driven**:

```cpp
struct MeshletCluster {
    uint32_t vertex_offset;    // into clipmap vertex buffer
    uint32_t index_offset;     // into clipmap index buffer
    uint32_t lod_level;        // which ring
    Sphere   bounds;           // world-space for culling
};

// Per frame:
// 1. Compute shader: frustum + occlusion cull clusters
// 2. Append visible clusters to indirect draw buffer
// 3. Single DrawIndexedIndirect(terrain_clusters_indirect_buffer)
```

All terrain drawcalls are merged into **one** indirect draw. Material blending happens in pixel shader, reading from weight textures and heightmap normal derivatives.

## Material Blending — Triplanar + Weight Maps

Each terrain has **4-8 paintable material layers** (grass, dirt, rock, sand, etc.):

```
Weight Map (one per layer):
├─ Layer 0 (Grass):     weight_0 texture (grayscale)
├─ Layer 1 (Dirt):      weight_1 texture
├─ Layer 2 (Rock):      weight_2 texture
└─ Layer 3 (Sand):      weight_3 texture

Pixel Shader:
1. Sample 4 weight maps at pixel UV
2. Normalize weights (sum to 1)
3. For each layer:
   a. Triplanar sample base color (X, Y, Z projections)
   b. Blend with height-based steepness (cliffs = rock, slopes = grass)
4. Output: blended color
```

**Triplanar blending** avoids UV stretching on steep slopes — samples are projected from three directions and blended by surface normal. Cliffs automatically show rock because steep normals favor vertical projection.

**Weight painting tool** (editor): brush-based painting that immediately updates weight textures on GPU. Painters can see live preview on the 3D viewport.

## Virtual Texturing — Megatexture Streaming

Terrain detail textures are too large to fit in VRAM (e.g., 16k×16k diffuse + normal + roughness = 2 GB). Instead, use **virtual texturing**:

```cpp
struct VirtualTexturePool {
    // Physical cache: 4×4 grid of 256×256 tiles = 1024×1024 physical texture
    ResourceHandle<Texture> physical_diffuse;
    ResourceHandle<Texture> physical_normal;
    ResourceHandle<Texture> physical_roughness;

    // Sparse virtual layer: 16k×16k virtual space
    // Each pixel maps to a tile on disk (tiled PNG/BC7)
    // Pixel shader requests tile at (virtual_uv), GPU fetches from disk async
};

// Pixel Shader Pseudocode:
// virtual_uv = world_pos * megatexture_scale  // e.g., 16k×16k
// physical_uv = VT_Sample(physical_diffuse, virtual_uv)  // indirect lookup
// color = textureLod(physical_diffuse, physical_uv, 0)
```

Resident tiles (visible in frame) are prioritized; off-screen tiles stream at lower priority. Feedback buffer tracks which tiles were sampled, GPU schedules loads for next frame.

## Sculpting Tools

Editor provides brush-based deformation:

- **Raise / Lower**: Increase or decrease height at brush center, falloff to edge
- **Smooth**: Gaussian blur heightmap under brush
- **Flatten**: Set all pixels under brush to target height
- **Erosion**: Simulate water flow (heightfield flow simulation)
- **Noise**: Add Perlin noise for natural variation
- **Ramp**: Create stairs or slope between two heights

All edits are **GPU-accelerated** — compute shader applies brush stamp to heightmap texture in <1 ms. Changes are visible immediately.

## Foliage Integration

Terrain owns **foliage density maps** (one per material layer):

```cpp
// CLandscape::foliage_layers[4]:
// Layer 0 (Grass) → density map → spawn 1M grass blades
// Layer 1 (Trees) → density map → spawn 10k trees (physics-enabled)
// Layer 2 (Rock) → density map → spawn boulders (sparse, physical)
```

Density map is a **grayscale texture** (same resolution as heightmap). Values 0-255 indicate spawn probability per cell. Editor painting tool adjusts density in real-time; compute shader respawns foliage on dirty regions.

See [Foliage § Instancing](../38-foliage/01-overview.md) for rendering details.

## Terrain Holes

For cave entrances, tunnels, bridges — carve holes in the terrain mesh:

```cpp
struct TerrainHole {
    Vec3   center_world;
    float  radius;
    bool   is_tunnel;      // hole or cave entry?
};

// Vertex Shader:
// if (distance(vertex_world, hole.center) < hole.radius) {
//     discard_vertex();  // skip this vertex
// }
```

Holes disable collision too — raycasts and physics bodies pass through carved regions. Designed for artist workflow: paint hole regions, collision is automatic.

## Physics Integration

`CTerrainCollider` provides physics representation to Jolt:

```cpp
struct CTerrainCollider {
    ResourceHandle<Landscape> terrain;     // owner
    Mat4                       local_to_world;
    HeightfieldShapeId         shape_id;    // Jolt heightfield
};
```

Jolt's heightfield collider reads from the GPU heightmap texture via a **callback function**:

```cpp
JPH::HeightFieldShape shape(
    /* vertices_x= */ 513,
    /* vertices_z= */ 513,
    /* get_height */ [terrain](uint32_t x, uint32_t z) {
        return terrain->GetHeightmapValue(x, z);  // texture read
    }
);
```

Physics updates are **lazy** — heightfield shape is rebuilt only when heightmap changes (sculpt tools). Terrain holes automatically exclude collision (vertices with height = INVALID_HEIGHT).

## Streaming Architecture

Terrain components are streamed with the world:

```cpp
struct TerrainStreamingProxy {
    AABB                      bounds;     // world-space component bounds
    Vec2Int                   grid_coord; // which component is this?
    ResourceHandle<Landscape> terrain;
    bool                       is_loaded;
};

// World Streaming System:
// 1. Camera moves → streaming grid updates
// 2. Unload components > 5 km away
// 3. Load components < 3 km away (hysteresis prevents thrashing)
// 4. GPU updates clipmap rings as components load
```

Each component's heightmap chunk is a separate `.uzheightmap` asset (16 MB uncompressed for R32F, 8 MB for R16). Cooked terrain archives store tiles compressed with zstd.

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Clipmap LOD rendering, GPU-driven mesh gen, heightmap editing (raise/lower/smooth), 4-layer material blending, basic triplanar. |
| **Phase 4** | Virtual texturing, foliage spawning integration, sculpting tools (flatten/erode/noise), streaming, terrain holes, editor brush UI. |

## Rules

1. **NEVER** store heightmap as static mesh — always GPU-driven with compute shaders
2. **ALWAYS** use clipmap for LOD — no pre-baked LOD meshes, no runtime mesh simplification
3. **ALWAYS** normalize weights before sampling materials — undefined behavior if weights don't sum to 1
4. **NEVER** enable physics collision on individual vertices — use Jolt's heightfield shape (built-in culling)
5. **ALWAYS** validate brush bounds during sculpting — clamp to heightmap extents, never overflow
6. **ALWAYS** invalidate clipmap rings after any heightmap edit — lazy rebuild on next frame
7. **NEVER** use mip filtering on weight maps — sample at full resolution or nearest LOD (mips cause seams)
