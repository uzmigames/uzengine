## 1. Implementation

### Format Stabilization
- [ ] 1.1 Finalize .uzmesh binary format (UzMeshHeader, UzMeshLOD, UzMeshSubmesh, VertexAttribute enum)
- [ ] 1.2 Finalize .uztexture binary format (UzTextureHeader, CompressionFormat enum, MipLevel)
- [ ] 1.3 Add fastgltf (0.7.1+) as thirdparty dependency via vcpkg
- [ ] 1.4 Add meshoptimizer as thirdparty dependency via vcpkg

### MeshImporter v1
- [ ] 1.5 Implement glTF parser (fastgltf): extract positions, normals, UVs, indices from .glb/.gltf
- [ ] 1.6 Implement vertex cache optimization (meshopt_optimizeVertexCache + meshopt_optimizeVertexFetch)
- [ ] 1.7 Implement .uzmesh binary writer (header + vertex streams + index buffer)
- [ ] 1.8 Register MeshImporter with AssetImporterRegistry (extensions: .glb, .gltf)

### TextureImporter v1
- [ ] 1.9 Implement image decoder (stb_image: PNG, JPG, TGA → RGBA8)
- [ ] 1.10 Implement box-filter mipmap chain generation
- [ ] 1.11 Implement .uztexture binary writer (header + mip offsets + RGBA8 data)
- [ ] 1.12 Register TextureImporter with AssetImporterRegistry (extensions: .png, .jpg, .tga)

### Runtime Loaders
- [ ] 1.13 Implement .uzmesh runtime loader (read + validate + GPU upload VB/IB via RHI)
- [ ] 1.14 Implement .uztexture runtime loader (read + validate + create texture + upload + bindless SRV)

### Integration
- [ ] 1.15 Register both importers in uzcook main
- [ ] 1.16 Add test assets (cube.glb, test_texture.png) to assets/ directory

## 2. Testing

- [ ] 2.1 Test MeshImporter: parse cube.glb, verify vertex/index counts, validate .uzmesh output bytes
- [ ] 2.2 Test MeshImporter: missing file, invalid glTF, zero-vertex mesh error handling
- [ ] 2.3 Test TextureImporter: import PNG, verify dimensions, mip count, RGBA8 data
- [ ] 2.4 Test TextureImporter: missing file, corrupt PNG, 1x1 texture edge case
- [ ] 2.5 Test .uzmesh loader: read back written file, verify magic/version, data integrity
- [ ] 2.6 Test .uztexture loader: read back written file, verify header, mip chain sizes
- [ ] 2.7 Test format validation: wrong magic number rejected, wrong version rejected
- [ ] 2.8 Test cache integration: import same file twice, verify cache hit (no reimport)
- [ ] 2.9 Test uzcook: batch process assets/ directory with both importers
- [ ] 2.10 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Update `docs/engine/08-resource-system/06-mesh-pipeline.md` with final .uzmesh format
- [ ] 3.2 Update `docs/engine/08-resource-system/07-texture-pipeline.md` with final .uztexture format
- [ ] 3.3 Add asset authoring guide (supported formats, import options, uzcook usage)
