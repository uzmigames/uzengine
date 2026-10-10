# Design: mesh winding inversions

## Before
`sphere.uzmesh` (UZMH v1, 2145 vertices, 3968 triangles) and twelve other sample meshes are wound
counter-clockwise relative to their outward normals; the GBuffer pipelines cull back faces with the
D3D default front face (clockwise), so the near side is culled and the far side's inside is drawn
with normals facing away from the camera: dark, wrongly lit, and for the chrome sphere a black disc
whatever the environment term does.

## After
- `scripts/rendering/repair_mesh_winding.py`: reads the UZMH header (vertex stride at 12, counts at 16 / 24,
  index format at 20, vertex / index offsets at 56 / 64, the LOD table at 76 + 8·l), evaluates per
  triangle `dot(-cross(b - a, c - a), avg(normals))` (the rule of `test_decal_lod_normals.ps1`) for
  LOD0 and every LOD, classifies meshes as ok / mixed / INVERTED (LOD0 ≥ 99 %), and with `--repair`
  swaps the last two indices of every triangle of every LOD of an inverted mesh. Vertex data, bounds
  and the header are untouched; the file keeps its version.
- The repaired meshes' residual per-triangle inconsistencies (simplifier artifacts the inversion had
  hidden: 0.1-8 % on the higher LODs) were then cleaned with `test_decal_lod_normals.ps1 -Repair`.
- The audit is W1 of `scripts/rendering/test_mesh_winding.py`; the importer is unchanged (out of scope).

## Harness
`scripts/rendering/test_mesh_winding.py`: W1 the audit over every sample mesh; W2 `chrome_test`'s sphere in
the dump (positions on the near hemisphere, N·V > 0); W3 the sphere's specular against F × the dumped
PMREM along the reflected ray (block B.5b's E4 as first written); W4 Sponza Camera C (frame 120)
against block B.5c's capture: positions identical outside the repaired meshes' screen bounds, the
direct-light changes reported.
