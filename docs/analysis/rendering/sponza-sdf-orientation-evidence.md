# Sponza SDF orientation evidence

CPU audit performed 2026-09-06 in the shared UzEngine worktree. Runtime files were read only during this audit. No GPU execution was used.

## Inputs and method

- Command: `node` with an inline JavaScript here-document, using built-in `fs` and `path` modules only.
- Scene: `sample/content/scenes/sponza.uzscene`; mesh list and identity child transforms: `sample/content/meshes/sponza/sponza.uzprefab`.
- Read all 138 unique meshes referenced by the prefab, not all 145 mesh files in the directory. All use UInt32 indices and the Standard 48-byte vertex layout.
- Decode mesh header fields, vertex positions at byte offset 0 and normals at offset 12, and LOD0 indices. The decoded source floats are IEEE float32; CPU calculations use JavaScript float64.
- Compute each triangle geometric normal as cross(b-a,c-a). Count its dot product with the sum of finite, nonzero vertex normals. Sign audit threshold is +/-1e-8 for count classification.
- For each specified world point and each mesh whose expanded cubic SDF volume contains it, calculate the exact closest triangle point using the same Ericson region tests as `mesh_sdf_bake_cs.hlsl`. Ties retain the first triangle, matching the shader strict less-than comparison.
- Before sign: sign(dot(point-closest, cross)). After sign: reverse cross only when dot(cross, sum(valid vertex normals))<0, equivalent to production `OrientSdfFaceNormal` in `runtime/engine/renderer/gi/mesh_sdf_baker.cpp`.
- Cubic bake volume: center of mesh AABB; half extent is max over axes of (axis size/2 + max(axis size*0.1,1 cm)). Resolution is 32. Prefab mesh transforms are identity.
- The table reports exact per-mesh nearest-face distances before texture sampling. It does not include fp16 quantization, trilinear interpolation, GDF discretization, or exterior-volume distance extension, and is not a GPU image-quality measurement.

## Winding evidence

Triangles examined: **3,747,286**. Opposed to vertex normals: **3,739,444**. Aligned: **7,842**.

Concrete floor triangle: `walls_1stfloor_01_floor_01_0.uzmesh`, LOD0 triangle 356, indices [690,689,818]. Vertex normals are approximately +Z; normalized geometric cross is [-0.00000117944,0,-0.9999999999993]. Camera A is 159 cm above that floor.

## Exact nearest-face before/after

| Point (cm) | Mesh volumes containing point | Negative meshes before | Negative meshes after | Minimum before (cm) | Minimum after (cm) | Minimum mesh after |
|---|---:|---:|---:|---:|---:|---|
| 86, 881, 159 | 62 | 51 | 11 | -1467.368446 | -1547.895208 | exterior_stones_01_tile_0.uzmesh |
| -211, -270, 135 | 66 | 55 | 11 | -2235.350725 | -1557.279160 | roof_floor_03_01_brickwall_01_0.uzmesh |
| -236, 224, 74 | 61 | 50 | 11 | -2013.959807 | -1584.132966 | exterior_stones_01_tile_0.uzmesh |
| 0, 0, 150 | 66 | 56 | 10 | -1999.888922 | -1674.866147 | roof_floor_03_01_brickwall_01_0.uzmesh |
| 0, 0, 600 | 77 | 67 | 10 | -2082.559892 | -1305.229863 | roof_floor_03_01_brickwall_01_0.uzmesh |
| 0, 0, 1200 | 78 | 68 | 10 | -2327.456917 | -1036.707545 | walls_1stfloor_01_brickwall_02_0.uzmesh |

## Floor sanity check

The floor has correct geometric distance but inverted imported winding. Applying the explicit normal orientation changes only the sign.

| Point (cm) | Floor LOD0 triangle | Before (cm) | After (cm) |
|---|---:|---:|---:|
| 86, 881, 159 | 356 | -159.000099 | 159.000099 |
| -211, -270, 135 | 438 | -135.000092 | 135.000092 |
| -236, 224, 74 | 443 | -74.000092 | 74.000092 |
| 0, 0, 150 | 394 | -150.567388 | 150.567388 |
| 0, 0, 600 | 394 | -600.142167 | 600.142167 |
| 0, 0, 1200 | 394 | -1200.071136 | 1200.071136 |

## Remaining minimum partitions

| Point (cm) | Minimum mesh after | LOD0 triangle | Bake voxel size (cm) |
|---|---|---:|---:|
| 86, 881, 159 | exterior_stones_01_tile_0.uzmesh | 16 | 135.925800 |
| -211, -270, 135 | roof_floor_03_01_brickwall_01_0.uzmesh | 410 | 102.823251 |
| -236, 224, 74 | exterior_stones_01_tile_0.uzmesh | 41 | 135.925800 |
| 0, 0, 150 | roof_floor_03_01_brickwall_01_0.uzmesh | 402 | 102.823251 |
| 0, 0, 600 | roof_floor_03_01_brickwall_01_0.uzmesh | 434 | 102.823251 |
| 0, 0, 1200 | walls_1stfloor_01_brickwall_02_0.uzmesh | 840 | 128.773146 |

## Remaining-partition topology

Weld vertices by identical decoded float32 positions, ignoring UV/normal seam duplication, and count undirected LOD0 edge incidence. One incident triangle means a boundary edge; more than two means non-manifold. Material names below are the basename of each prefab material path, under `materials/sponza/`.

| Mesh | Material | Triangles | Welded positions | Boundary edges | Non-manifold edges |
|---|---|---:|---:|---:|---:|
| exterior_stones_01_tile_0.uzmesh | stones_01_tile.uzmat | 88 | 65 | 42 | 0 |
| roof_floor_03_01_brickwall_01_0.uzmesh | brickwall_01.uzmat | 436 | 306 | 142 | 0 |
| roof_floor_03_01_stones_01_tile_0.uzmesh | stones_01_tile.uzmat | 106 | 118 | 104 | 0 |
| walls_1stfloor_01_brickwall_02_0.uzmesh | brickwall_02.uzmat | 1821 | 1101 | 361 | 0 |
| walls_1stfloor_01_floor_01_0.uzmesh | floor_01.uzmat | 1460 | 760 | 52 | 0 |

## Interpretation

The orientation fix removes the global winding inversion and corrects the floor at all six sample points. It is insufficient for the scene union: 10 or 11 other mesh fields remain negative at each point. The explicit boundary-edge counts confirm that the worst remaining partitions are open surfaces, so a nearest-face sign is not a reliable inside/outside test for those independent material meshes. Taking their signed minimum still incorrectly classifies the tested free-space points as interior.

A follow-up must distinguish open/two-sided surface distance from closed-volume signed distance; changing winding again cannot solve this remaining topology problem. GPU readback and final scene validation remain necessary because this audit measures exact CPU nearest-face distances before volume discretization.
