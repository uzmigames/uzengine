# Proposal: glTF Import & Mesh Primitives (Phase 3A)

## Why
The 0.1 POC loaded glTF/glB models and had built-in primitive generators (sphere, cube, cone, cylinder, plane, torus, quad). Current 0.2 only has a hardcoded cube in MeshBuilder and loads pre-baked .uzmesh files. There's no way to import standard 3D model formats or procedurally generate common shapes. glTF is the industry-standard interchange format (Khronos) and supporting it is essential for any content pipeline. Primitive generators enable rapid prototyping and testing without external assets.

## What Changes

### New Systems
1. **glTF importer** — Import glTF 2.0 / glB binary using fastgltf library, converting meshes (positions, normals, UVs, tangents, indices) to .uzmesh format with automatic tangent generation via MikkTSpace when missing
2. **Mesh primitive generators** — Procedural generators for sphere (UV sphere), cone, cylinder, plane, torus, and quad with correct normals, UVs, and tangents
3. **Multi-submesh support** — A single glTF file can contain multiple meshes/primitives with different materials, stored as submeshes in .uzmesh
4. **Material extraction** — Extract PBR material parameters (baseColor, metallic, roughness, normal, AO, emissive) from glTF materials and create corresponding .uzmat references

### Refactoring
- MeshBuilder expanded with new primitive types beyond CreateCube
- Asset importer registry gains glTF importer alongside texture importer

## Impact
- Affected specs: `docs/engine/08-resource-system/`
- Affected code: `runtime/engine/asset/importers/`, `runtime/engine/renderer/mesh.h`
- Breaking change: NO — additive, existing .uzmesh loading unchanged
- User benefit: Load standard 3D models, rapid prototyping with built-in shapes
- Enables: Complex scenes with real-world models, material showcase demos
