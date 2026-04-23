# Mesh Particle Depth Sort

## Why

Sprite particles already use back-to-front sorting via the bitonic sort pass, but `MeshRenderModule` does not — mesh particles with transparent materials render in insertion order, producing visibly wrong blending (closer meshes drawn behind farther ones). Production-critical for transparent glass shards, energy cores, jelly debris.

Niagara's mesh renderer supports "Sort by View Depth" out of the box with one checkbox.

## What Changes

- `MeshRenderModule`: add `sortMode` enum { None, ViewDepth, CustomAttribute }, default None (current behavior).
- When `ViewDepth`: reuse the existing bitonic sort infrastructure to produce a sorted-indices buffer for the mesh draw.
- Mesh draw path: replace linear instance index with sortedIndices[i] in the VS / or issue `DrawIndexedInstancedIndirect` with pre-shuffled instance buffer.
- When `CustomAttribute`: sort by a user-custom-attribute (depends on phase16_vfx-user-custom-attributes — implement None+ViewDepth first; CustomAttribute gated behind that).

## Impact

- Affected specs: VFX sort pass generalization, mesh render module config
- Affected code: `mesh_render_module.h/cpp`, `vfx_sort_pass.h`, mesh VS, `frame_particles.cpp`
- Breaking change: NO (default None preserves current behavior)
- User benefit: correct alpha blending for transparent mesh particles
