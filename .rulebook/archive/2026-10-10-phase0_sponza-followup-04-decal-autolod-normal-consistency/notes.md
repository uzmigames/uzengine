# Notes — phase0_sponza-followup-04-decal-autolod-normal-consistency

2026-09-08. Exe 12:40 (generator change is header-only, `auto_lod.h`, so it rebuilds every importer).

## Measurement

The LODs are **baked into the asset** — `ApplyAutoLODs` concatenates LOD1-3 index buffers after LOD0 in the `.uzmesh` and fills `header.lods[]` — so the artifact can be measured straight from the file, no simplifier run needed.

`scripts/rendering/test_decal_lod_normals.ps1` walks each LOD's index buffer and counts triangles whose winding disagrees with their vertices' normals. Convention: the engine renders clockwise-front (D3D default), so a triangle is consistent when `dot(-cross(b-a, c-a), avg(vertexNormals)) > 0`. That convention is confirmed by the data itself — 99 of 145 Sponza meshes have **zero** inconsistent triangles at LOD0; under the opposite convention essentially every triangle would be flagged.

`decals_1st_floor1_dirt_decal_0.uzmesh`, before:

| LOD | triangles | inconsistent |
|---|---|---|
| LOD0 | 8138 | **0** |
| LOD1 | 4068 | **0** (1 degenerate) |
| LOD2 | 2035 | **23** (1.13 %) |
| LOD3 | 1072 | **26** (2.43 %) |

Exactly the SponzaTest handoff's claim ("correct LOD0/1 orientation; some LOD2/3 triangles remain inconsistent"), now quantified.

## Cause

`GenerateAutoLODs` (`runtime/engine/renderer/mesh/auto_lod.h`) called `meshopt_simplify` with **positions only**. The simplifier therefore had no reason to preserve normal discontinuities, and a collapse could leave a triangle whose winding no longer matched its vertex normals. Thin, near-coplanar geometry — a floor decal — is the worst case.

## Fix

1. **Generator**: `meshopt_simplify` → `meshopt_simplifyWithAttributes`, passing the vertex normals as three attributes with weight 0.5 each. Every import path (`uzfbx`, `uzgltf`, `mesh_importer`, `fbx_importer`) goes through this header, so future imports keep normals consistent. Targets, error bound and vertex-cache optimisation are unchanged.
2. **Shipped assets**: `test_decal_lod_normals.ps1 -Repair` swaps the last two indices of each inconsistent triangle, flipping its winding to agree with the vertex normals. Vertex data is never touched. `-All` applies this across `sample/content/meshes/sponza`, restricted to meshes whose **LOD0 is already consistent** — the safe subset.

## Scope of the defect across Sponza

`test_decal_lod_normals.ps1 -All` over 145 meshes:

| Class | Count | Meaning |
|---|---|---|
| clean at every LOD | 56 | nothing to do |
| **LOD0 clean, higher LODs dirty** | **43** | the simplifier artifact — repaired |
| LOD0 itself inconsistent | 46 | authored/smoothed normals legitimately disagree with some faces (one mesh, `arch_stones_01_stone_trims_01_0`, is inconsistent at 100 % — a whole-mesh winding inversion, the same class as the decal's original problem) — **left alone**, it is not a LOD artifact |

Typical magnitudes in the repaired set are 0.1–5 % of a LOD's triangles (e.g. `column_2ndfloor_19_stone_trims_01_0`: 0 / 31 / 32 / 37 over 50344 / 25172 / 12586 / 6293 triangles).

## Verification

| Check | Result |
|---|---|
| Decal audit after repair | LOD0 0, LOD1 0, LOD2 **0**, LOD3 **0**; script exits 0 |
| Bulk audit before repair | 56 clean / 43 artifact / 46 authored |
| `--cvar r.LOD.Bias=2.0` Camera A (forces coarser LODs) | 53.2 / 34.4 % before |
| `--cvar r.LOD.Bias=2.0` Camera C | 87.2 / 15.2 % before |
| `zig build` after the generator change | clean |

After the bulk repair (`-All -Repair`, **8123 triangles across 43 meshes**):

| Check | Result |
|---|---|
| Full re-classification | **99 clean, 0 simplifier-artifact, 46 authored-inconsistent** |
| Decal audit | 0 / 0 / 0 / 0, exits 0 |
| `r.LOD.Bias=2.0` Camera A | 53.2 / 34.4 % → **53.2 / 34.4 %** |
| `r.LOD.Bias=2.0` Camera C | 87.2 / 15.2 % → **87.3 / 15.2 %** |
| LOD0 frame 640 | Camera A 53.2 / 34.0 %, Camera C 87.7 / 15.3 % — unchanged |
| `test_mesh_sdf_bvh.ps1` | PASS (3082 exact distance/sign comparisons) |
| `test_sdf_diffuse_gpu.ps1` | PASS (max_abs_error 0.000195) |
| `test_sdf_surface_origin.ps1` | FAIL at 0.009328 — the pre-existing expected-open failure, unchanged |

The visual delta is ~0.1 % because 8123 triangles at coarse LODs are a small fraction of the frame; the value is correctness at distance, not a visible shift.

## Build caveat found while landing this

The generator change is **header-only**, and `zig build` did not recompile: an `#error` planted in `auto_lod.h` still built clean and `uzmesh.exe` kept its old timestamp. Changing `uzmesh.cpp` by content forced the rebuild (13:30), after which the same `#error` probe correctly failed. Any header-only edit in this repo needs a timestamp check on the dependent binary.

## Notes for later

- The 46 "authored-inconsistent" meshes deserve their own look: `arch_stones_01_stone_trims_01_0` being 100 % inconsistent at LOD0 means its winding is inverted relative to its normals for the whole mesh, exactly what `repair_sponza_decal_normals` fixed for the decal. Not touched here — it needs the same careful treatment the decal got, including a look at how it renders.
- `-All` is slow (PowerShell walks every triangle of 145 meshes; minutes, not seconds). It is a diagnostic, not something to run per-build. The single-mesh mode used by the regression check is fast.
- The generator fix only affects **new** imports. Assets already in the repo carry their baked LODs; that is why the repair pass exists.
