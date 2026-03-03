# Proposal: phase4-convex-decomposition-vhacd

## Why
Arbitrary 3D meshes (vases, chairs, rocks, furniture) cannot be used directly as physics colliders — GPU render meshes are non-convex and Jolt only accepts convex shapes or explicit triangle meshes (which are too slow for dynamic bodies). Manual convex decomposition in DCC tools is tedious and artist-unfriendly. V-HACD (Volumetric Hierarchical Approximate Convex Decomposition) automatically decomposes any mesh into a set of approximate convex hulls, enabling accurate physics for arbitrary geometry with zero manual authoring. Without this, every prop requiring physics collision forces an artist to manually create a simplified hull in Blender or Maya — a major production bottleneck.

## What Changes
- V-HACD library integration in asset pipeline (vcpkg: `vhacd`)
- `_vhacd_decompose: true` flag in glTF node extras triggers decomposition at import time
- Import settings: `max_hulls` (default 16), `resolution` (default 100K voxels), `concavity` (default 0.001)
- Generated hull array stored in `.uzmesh` alongside render mesh data
- `CConvexDecomposedCollider` ECS component: selects between V-HACD hulls and a simple primitive collider
- Jolt `JPH::StaticCompoundShapeSettings` built from the generated hull list at physics world registration
- Import log reports: hull count, max error distance, total hull vertex count
- Editor visualization: wireframe hull overlay on selected entity in the physics debug view

## Impact
- Affected specs: `docs/specs/asset-pipeline.md`, `docs/specs/physics-system.md`
- Affected code: `src/asset/mesh_importer.cpp`, `src/asset/uzmesh_format.h`, `src/physics/collider_factory.cpp`, `CMakeLists.txt` (vcpkg vhacd)
- Breaking change: NO
- User benefit: Accurate physics collision for arbitrary mesh shapes with zero manual DCC authoring; a single import flag produces a physics-ready collider from any glTF mesh.
