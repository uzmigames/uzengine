# Lighting side item — sample meshes exported counter-clockwise rendered inside-out

Rulebook task `phase0_lighting-mesh-winding-inversions` (proposal, design, spec `mesh-winding`). Branch
`feat/sponza-gi-parity`, 2026-10-08. Block B.5b's finding (the chrome sphere of `chrome_test` as the inside
of its far hemisphere), taken at the user's request ("conserte essa renderização").

## Before

- The engine renders clockwise-front (the D3D default: `RHICullMode::Back` with the default front face;
  `scripts/test_decal_lod_normals.ps1` documents the rule `dot(-cross(b - a, c - a), avg(normals)) > 0`).
  `sphere.uzmesh` (UZMH v1, 2145 vertices, 3968 triangles) is wound counter-clockwise: 100 % of its
  triangles disagree with its outward normals, so back-face culling keeps the far side and the camera
  sees its inside, lit by normals facing away (block B.5b measured positions x median −51.6 cm with the
  camera at x = 400 and N·V −0.70).
- The audit of every sample mesh (`scripts/repair_mesh_winding.py`, 254 meshes) found 11 whole-mesh
  inversions (LOD0 ≥ 99 % inconsistent): the sphere, seven Sponza meshes (`arch_stones_01_stone_trims_01_0`,
  the two `column_2ndfloor_19_column_head_2ndfloor_0x_0`, `lamps_2ndflloor_01_metal_door_0`,
  `ornament_door_2ndfloor_15_metal_door_0`, `ornament_doors_09_metal_door_0`,
  `roof_floor_03_01_roof_tiles_01_0`), bunny, teapot and `test_shard`; suzanne at 97.8 %; one mixed
  mesh (`ornament_doors_09_metal_door_0_0`, 3.7 % at LOD0, the simplifier class). The cube, the
  DamagedHelmet and the other 137 Sponza meshes are clockwise. `arch_stones_01_stone_trims_01_0` had been
  listed in known-issues since 2026-09-08 as "a whole-mesh winding inversion that still needs the
  treatment the decal got".

## What changed

- `scripts/repair_mesh_winding.py`: the audit over LOD0 and the LOD table, the classes ok / mixed /
  INVERTED, and `--repair`, which swaps the last two indices of every triangle of every LOD of an
  inverted mesh (vertex data, bounds and header untouched); meshes below the inversion threshold
  (99 %; suzanne at 95 % explicitly) are refused so partial inconsistencies stay with the LOD tool.
- The assets repaired in place: the sphere (3968 triangles), the seven Sponza meshes (18 771 to 180 217
  triangles each), bunny, teapot, suzanne and `test_shard`; the Sponza meshes' residual per-triangle
  inconsistencies the inversion had hidden (55 to 1675 triangles on the higher LODs) then cleaned with
  `test_decal_lod_normals.ps1 -Repair`. The re-audit reads two mixed meshes (the simplifier class) and no
  inversion.
- `scripts/test_mesh_winding.py` (W1-W4).

## Gates (`scripts/test_mesh_winding.py`)

| Gate | Criterion | Result |
|---|---|---|
| W1 | no sample mesh is a whole-mesh inversion (LOD0 >= 99 % inconsistent with clockwise-front) | PASS: 253 meshes, inverted [], mixed [('sample\\content\\meshes\\sponza\\ornament_doors_09_metal_door_0_0.uzmesh', 0.037), ('sample\\content\\meshes\\test_formats\\suzanne.uzmesh', 0.022)] |
| W2 | chrome_test: the sphere shows its near hemisphere (positions x median > 0 with the camera at x = 400) and N.V > 0 at the median | PASS: 366005 px, x median 72.18612639485798, N.V median 0.697388036278292 (p10 0.30762136507728177) |
| W3 | the chrome sphere against F x the dumped PMREM along the reflected ray (median relative error <= 0.2) | PASS: median error 0.035 (ratio 0.995) over 91498 px; upward rays 0.022163654796533865 |
| W4 | Sponza Camera C: positions identical outside the repaired meshes' screen bounds; the direct-light changes reported with their share inside those bounds | PASS: positions changed 0 px, 0 outside the bounds; direct changed 0 px, 0.0 % inside |

**Numbers (after, 1080p).** Audit: 253 meshes, inverted [], mixed [('sample\\content\\meshes\\sponza\\ornament_doors_09_metal_door_0_0.uzmesh', 0.037), ('sample\\content\\meshes\\test_formats\\suzanne.uzmesh', 0.022)]. The chrome
sphere (366005 px): positions x median 72.186 cm, N·V median 0.697 (p10 0.308); against F × the PMREM along the
reflected ray, median relative error 0.035 (ratio 0.995) over 91498 px, upward rays 0.022: block
B.5b's E4 as first written now passes. Sponza Camera C against block B.5c's capture: positions changed 0 px, direct light changed
0 px, colour changed 3971 px (the loading-state noise): none of the seven repaired meshes is visible from Camera C.
Harness 4/4.

## Still open

- The importer that produced counter-clockwise meshes is not changed (the FBX importer flips winding
  for right-handed sources since its v17; the Sponza glTF import's path for these seven meshes was not
  traced): the audit guards the shipped content and future imports can be checked with it.
- `ornament_doors_09_metal_door_0_0` (3.7 % at LOD0, 12 % at LOD3) and suzanne's residue are the
  simplifier class the LOD tool owns.
- The repaired Sponza meshes' visibility from Camera A is measured in the mesh-decal item (its before
  capture follows this repair).
