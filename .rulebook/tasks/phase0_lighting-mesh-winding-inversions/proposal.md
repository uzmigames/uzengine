# Meshes exported counter-clockwise render inside-out

## Why

Block B.5b found the chrome sphere of `chrome_test` rendering as the inside of its far hemisphere: the
dump's visible positions were the far half's and the outward normals faced away from the camera
(N·V −0.70). The engine renders clockwise-front (the D3D default; `scripts/test_decal_lod_normals.ps1`
documents the convention), and `sample/content/meshes/sphere.uzmesh` is wound counter-clockwise
(100 % of its triangles disagree with its outward normals), so back-face culling keeps the far side.
An audit of every sample mesh (`scripts/repair_mesh_winding.py`, 254 meshes) found the same whole-mesh
inversion in seven Sponza meshes (`arch_stones_01_stone_trims_01_0`, the two
`column_2ndfloor_19_column_head_2ndfloor_0x_0`, `lamps_2ndflloor_01_metal_door_0`,
`ornament_door_2ndfloor_15_metal_door_0`, `ornament_doors_09_metal_door_0`,
`roof_floor_03_01_roof_tiles_01_0`; the first was already listed in known-issues as "a whole-mesh
winding inversion that still needs the treatment the decal got"), in the `test_formats` meshes
(bunny, teapot, suzanne at 97.8 %) and in `test_shard`. Every other mesh (the cube, the DamagedHelmet,
137 Sponza meshes) is clockwise. The user asked for the rendering to be fixed (2026-10-08).

## What changes

- `scripts/repair_mesh_winding.py` (new): audits every `.uzmesh` against the clockwise-front rule
  (LOD0 and the LOD table) and, with `--repair`, flips every triangle of every LOD of a whole-mesh
  inversion (the last two indices swapped; vertex data untouched); it refuses meshes below the
  inversion threshold (99 %, suzanne at 95 % explicitly) so partial inconsistencies stay with the LOD
  tool.
- The shipped assets repaired in place: the sphere, the seven Sponza meshes (their residual LOD
  triangles then cleaned with `test_decal_lod_normals.ps1 -Repair`), bunny, teapot, suzanne and
  `test_shard`.
- Evidence: `scripts/test_mesh_winding.py` (W1-W4): the audit, the chrome sphere's visible hemisphere
  and its specular against the PMREM (block B.5b's original E4), Sponza's direct light unchanged
  outside the repaired meshes.

## Gates (pre-registered)

W1 the audit reads no inverted mesh (LOD0 ≥ 99 % inconsistent) among the sample meshes; the mixed
class (simplifier residue ≤ 15 %) is reported. W2 `chrome_test`: the sphere's visible positions are
the near hemisphere's (x median > 0 with the camera at x = 400) and N·V > 0 at the median (before
−0.70). W3 the chrome sphere against F × the dumped PMREM environment along the reflected ray: median
relative error ≤ 0.2 (block B.5b's E4 as first written; before 0.99). W4 Sponza Camera C: the direct
light differs from the capture of the build before only where the repaired meshes or their shadows
can reach (reported: changed pixels, their share inside the repaired meshes' screen bounds); positions
identical elsewhere.

## Impact
- Affected specs: mesh-winding (new)
- Affected code: `scripts/repair_mesh_winding.py`, `scripts/test_mesh_winding.py`; assets:
  `sample/content/meshes/sphere.uzmesh`, seven `sample/content/meshes/sponza/*.uzmesh`,
  `sample/content/meshes/test_formats/{bunny,teapot,suzanne}.uzmesh`, `test_shard.uzmesh`
- Breaking change: NO for correct content (the repaired meshes render their outside; scenes that
  showed their inside change)
- User benefit: the sample sphere and seven Sponza pieces render their visible side.

## Out of scope

The importer that produced counter-clockwise meshes (a mirrored node whose winding was not flipped
when the transform was baked, or a source already inverted) is not changed: every current sample mesh
is now consistent and the audit script guards future imports; `ornament_doors_09_metal_door_0_0`
(3.7 % at LOD0, 12 % at LOD3) is the simplifier class the LOD tool owns.
