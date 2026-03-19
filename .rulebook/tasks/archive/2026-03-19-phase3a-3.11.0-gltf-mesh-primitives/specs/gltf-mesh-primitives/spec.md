# glTF Import & Mesh Primitives — Spec Delta

## ADDED Requirements

### Requirement: glTF 2.0 Import
The asset importer MUST support glTF 2.0 (.gltf + .bin) and glB binary format, extracting mesh geometry (positions, normals, UVs, indices) and converting to .uzmesh format.

#### Scenario: Simple Box Import
Given a standard glTF Box model
When imported via the glTF importer
Then a valid .uzmesh SHALL be produced with 24 vertices and 36 indices

#### Scenario: Missing Tangents
Given a glTF model without tangent attributes
When imported
Then tangents SHALL be auto-generated via MikkTSpace
And the resulting TBN basis SHALL be orthonormal

### Requirement: Mesh Primitive Generators
The MeshBuilder MUST provide procedural generators for sphere, cone, cylinder, plane, torus, and quad with correct positions, normals, UVs, and tangents.

#### Scenario: Sphere Generation
Given MeshBuilder::CreateSphere(radius=1.0, segments=32, rings=16) is called
When the mesh is generated
Then all vertex normals SHALL point radially outward
And UV mapping SHALL cover the full [0,1] range without degenerate triangles

#### Scenario: Plane Generation
Given MeshBuilder::CreatePlane(width=10, depth=10, subdivisions=4) is called
When the mesh is generated
Then the mesh SHALL have (4+1)^2 = 25 vertices in a regular grid
And all normals SHALL point in the +Z direction

### Requirement: Multi-Submesh Support
The .uzmesh format MUST support multiple submeshes per file, each with its own index range and material reference, to represent multi-material glTF meshes.

#### Scenario: Multi-Material Model
Given a glTF model with 3 primitives using different materials
When imported
Then the .uzmesh SHALL contain 3 submesh entries
And each submesh SHALL reference its own material index
