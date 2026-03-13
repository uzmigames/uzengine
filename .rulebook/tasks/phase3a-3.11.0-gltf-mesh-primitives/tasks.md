## 1. Implementation

- [ ] 1.1 Integrate fastgltf library into build system (vcpkg or manual)
- [ ] 1.2 Implement glTF importer (parse scenes, extract mesh data: positions, normals, UVs, indices)
- [ ] 1.3 Add tangent generation via MikkTSpace when glTF tangents are missing
- [ ] 1.4 Implement glTF material extraction (PBR metallic-roughness parameters + texture paths)
- [ ] 1.5 Add multi-submesh support to .uzmesh format (multiple draw ranges per file)
- [ ] 1.6 Implement sphere primitive generator (UV sphere with configurable segments/rings)
- [ ] 1.7 Implement cone primitive generator (configurable radius, height, segments)
- [ ] 1.8 Implement cylinder primitive generator (configurable radius, height, segments)
- [ ] 1.9 Implement plane primitive generator (configurable width, depth, subdivisions)
- [ ] 1.10 Implement torus primitive generator (configurable major/minor radius, segments)
- [ ] 1.11 Create glTF import demo (load external .glb model, display with PBR materials)

## 2. Testing

- [ ] 2.1 Test glTF import of standard test models (Box, Sphere, DamagedHelmet)
- [ ] 2.2 Test tangent generation produces valid TBN basis
- [ ] 2.3 Test material extraction maps to correct PBR parameters
- [ ] 2.4 Test multi-submesh .uzmesh roundtrip (write + read back)
- [ ] 2.5 Test each primitive generator (correct vertex count, index count, normals point outward)
- [ ] 2.6 Test sphere UV mapping (no degenerate triangles at poles)
- [ ] 2.7 Test plane subdivisions (correct grid topology)
- [ ] 2.8 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Update `docs/engine/08-resource-system/` with glTF import pipeline
- [ ] 3.2 Add mesh primitives API reference (parameters, usage examples)
