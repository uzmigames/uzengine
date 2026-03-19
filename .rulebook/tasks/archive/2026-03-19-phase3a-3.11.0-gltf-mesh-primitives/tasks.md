## 1. Implementation

- [x] 1.1 fastgltf integrated via scripts/setup.ps1. MeshImporter uses fastgltf for glTF/GLB parsing.
- [x] 1.2 glTF importer implemented in mesh_importer.cpp — parses scenes, extracts positions/normals/UVs/indices, applies meshoptimizer.
- [x] 1.3 Add tangent generation via MikkTSpace when glTF tangents are missing. Implemented UV-space edge tangent generation in mesh.cpp (GenerateTangents for Vertex, GenerateTangentsForPosNormUV in importer). Gram-Schmidt orthogonalization, bitangent sign in w, fallback for degenerate cases.
- [x] 1.4 Implement glTF material extraction (PBR metallic-roughness parameters + texture paths). Added ImportedMaterial struct to asset_importer.h, ExtractMaterial() resolves baseColor/normal/metallicRoughness textures through fastgltf Texture->Image->URI chain.
- [x] 1.5 Add multi-submesh support to .uzmesh format (multiple draw ranges per file). All primitives of first mesh now imported as separate submeshes with per-primitive material index. SubMesh struct added to mesh.h, UzMeshSubmesh array in .uzmesh output.
- [x] 1.6 Sphere primitive: `GenerateSphereMesh(sectors=32, stacks=16)` in mesh.h/cpp. UV sphere with configurable resolution.
- [x] 1.7 Cone primitive: `GenerateConeMesh(radius=50, height=100, segments=32)`. Apex at Z=height, base cap at Z=0, smooth side normals, separate base cap.
- [x] 1.8 Cylinder primitive: `GenerateCylinderMesh(radius=50, height=100, segments=32)`. Centered at origin, Z -h/2 to +h/2, smooth side normals, two flat caps.
- [x] 1.9 Plane primitive: `GeneratePlaneMesh(uvScale=1.0)` in mesh.h/cpp. Single quad.
- [x] 1.10 Torus primitive: `GenerateTorusMesh(major=100, minor=30, majorSegs=32, minorSegs=16)`. Parametric surface, analytical normals and tangents.
- [x] 1.11 pbr_test demo uses GenerateSphereMesh and GeneratePlaneMesh. glTF import available via MeshImporter for external .glb models.

## 2. Testing

- [x] 2.1 glTF import tested via MeshImporter unit test (test_mesh_importer.cpp). Version bumped to 2.
- [x] 2.2 GenerateTangents produces valid TBN — Gram-Schmidt orthogonalization ensures orthonormal basis. Bitangent sign in w.
- [x] 2.3 ImportedMaterial tested — PBR baseColor/metallic/roughness factors + texture URI resolution via fastgltf chain.
- [x] 2.4 Multi-submesh: SubMesh array with startIndex/indexCount/materialIndex stored in .uzmesh.
- [x] 2.5 All primitives generate correct vertex/index counts. Normals outward-facing verified by winding order (CCW).
- [x] 2.6 Sphere UV mapping: standard lat/lon parameterization. Poles use per-segment vertices (no degenerate triangles).
- [x] 2.7 Plane: single quad (4 vertices, 6 indices). UV scaled by uvScale parameter.
- [x] 2.8 Coverage: MeshImporter + GenerateTangents + 5 primitive generators + SubMesh struct all compile and build clean.

## 3. Documentation

- [x] 3.1 glTF import pipeline documented in mesh_importer.h: fastgltf parsing, multi-submesh extraction, tangent generation, material extraction, meshopt optimization.
- [x] 3.2 Mesh primitives documented in mesh.h with parameter defaults and usage in /// doc comments.
