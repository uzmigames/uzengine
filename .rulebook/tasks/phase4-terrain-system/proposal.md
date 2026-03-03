# Proposal: phase4-terrain-system

## Why

The engine has no large-scale terrain capability. Open-world and outdoor games require heightmap-based terrain with material blending, foliage, and efficient LOD — none of which exist in the current renderer. Representing terrain as a static mesh is impractical: a 1 km x 1 km terrain at 1 m resolution requires 1 million triangles in a single draw call with no LOD. Without a dedicated terrain system featuring chunk-based LOD, a Jolt HeightFieldShape collider, and a splatmap material blend, open-world games cannot be built with UzEngine.

## What Changes

- **`.uzterrain` file format** — plain-text INI file referencing a 16-bit PNG heightmap, an RGBA splatmap PNG, four layer material paths, chunk size (default 32 m), and world scale (meters per heightmap texel); parsed by the `uz_terrain` plugin at load time
- **Chunk LOD system** — terrain divided into 32x32 m chunks; each chunk has 4 LOD levels (full, half, quarter, eighth resolution); LOD selected per-chunk per-frame by camera distance; geomorphing blend in the vertex shader between adjacent LOD levels to prevent popping; `TerrainChunkSystem` updates LOD assignments on the render thread
- **Splatmap material blending** — RGBA splatmap channels control blend weights for 4 terrain layers; each layer has albedo, normal, roughness, and AO textures; blended in the terrain HLSL pixel shader using the same G-Buffer output as the PBR renderer
- **GPU-instanced foliage** — density map PNG drives grass/rock/tree placement; `FoliageSystem` generates `StructuredBuffer<float4x4>` instance data per foliage type per visible chunk; rendered via the existing `INSTANCED` shader variant; distance-based density fade and wind animation via a simple sine-wave vertex offset
- **Jolt `HeightFieldShape` collider** — heightmap data uploaded to Jolt at load time as a `JPH::HeightFieldShapeSettings`; provides O(1) point-height queries used by CCharacterController and vehicle wheels; `PhysicsWorld` registers the terrain body as a static non-movable body with the Jolt `BroadPhase`
- **Editor sculpt/paint tools** — four tools accessible from the terrain toolbar in the VSCode editor panel: Raise/Lower (Gaussian falloff brush), Smooth (averaging kernel), Flatten (target height stamp), Paint Layer (splatmap channel brush); all tools write to the heightmap/splatmap PNG in-place and trigger a partial terrain rebuild for the affected chunks
- **NavMesh integration** — `TerrainChunkSystem` exposes geometry to `NavMeshSystem` via the `INavMeshGeometrySource` interface so Recast can voxelize terrain during the cook step; `navmesh_area` metadata per terrain configures walk cost

## Impact

- Affected specs: `docs/specs/terrain-system.md`, `docs/specs/physics-system.md`, `docs/specs/renderer.md`, `docs/specs/gpu-instancing.md`, `docs/specs/navmesh.md`
- Affected code: `plugins/uz_terrain/terrain_system.cpp`, `plugins/uz_terrain/terrain_chunk.cpp`, `plugins/uz_terrain/foliage_system.cpp`, `shaders/terrain.hlsl`, `src/physics/physics_world.cpp`, `src/editor/terrain_tools.cpp`, `vscode-extension/src/panels/terrainPanel.ts`
- Breaking change: NO — terrain is delivered as the optional `uz_terrain` plugin; scenes without a `.uzterrain` reference are completely unaffected
- User benefit: Open-world and outdoor game developers can author large terrains directly in the VSCode editor using sculpt and paint tools, import heightmaps from external tools, and get accurate physics collision and navmesh baking automatically without any manual mesh authoring or collision setup.
