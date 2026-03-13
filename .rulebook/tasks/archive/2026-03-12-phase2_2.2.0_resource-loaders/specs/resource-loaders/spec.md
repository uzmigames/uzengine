# Resource Loaders — Spec Delta

## ADDED Requirements

### Requirement: MeshImporter glTF Parsing
The MeshImporter SHALL parse glTF 2.0 files (.glb, .gltf) using fastgltf. It MUST extract vertex positions (Vec3), normals (Vec3), UV coordinates (Vec2), and index buffers. Missing normals SHALL be auto-generated via flat shading. Missing UVs SHALL default to (0,0).

#### Scenario: Import valid glTF mesh
Given a valid cube.glb file with 8 vertices and 36 indices
When MeshImporter::Import() is called
Then it produces a .uzmesh file containing 8 vertices and 36 indices
And the import result reports success with zero warnings

#### Scenario: Missing normals auto-generated
Given a .glb file with positions and indices but no normals
When MeshImporter processes the mesh
Then flat-shading normals are generated from triangle faces
And a warning is logged indicating normals were generated

#### Scenario: Invalid glTF rejected
Given a file that is not valid glTF
When MeshImporter::Import() is called
Then it returns success=false with a descriptive error message

### Requirement: Mesh Vertex Cache Optimization
The MeshImporter MUST apply meshoptimizer vertex cache optimization (`meshopt_optimizeVertexCache` + `meshopt_optimizeVertexFetch`) to all imported meshes. This SHALL reduce GPU vertex fetch overfetch.

#### Scenario: Optimized mesh has better cache statistics
Given a raw mesh with unoptimized vertex ordering
When the mesh is imported with optimize_cache=true
Then meshopt_analyzeVertexFetch reports lower overfetch than the input

### Requirement: UzMesh Binary Format
The .uzmesh format SHALL use magic number 'UZMS' (0x554D5A53) and version field (uint32). The layout MUST be: UzMeshHeader + UzMeshLOD[] + UzMeshSubmesh[] + vertex data + index data. All data MUST be 64-byte aligned for direct GPU upload.

#### Scenario: Valid .uzmesh file structure
Given a mesh was imported successfully
When the .uzmesh file is read
Then the first 4 bytes equal 'UZMS'
And the version field is 1
And vertex data begins at a 64-byte aligned offset

#### Scenario: Wrong magic number rejected
Given a file with magic number 'XXXX'
When the .uzmesh loader attempts to read it
Then it SHALL return an error indicating invalid format

### Requirement: TextureImporter Image Decoding
The TextureImporter SHALL decode PNG, JPG, and TGA files using stb_image to RGBA8 format. It MUST support 1-4 channel inputs, converting all to RGBA8. Alpha-less formats SHALL have alpha set to 255.

#### Scenario: Import PNG to RGBA8
Given a 256x256 RGB PNG file
When TextureImporter::Import() is called
Then it produces a .uztexture file with RGBA8 data (4 bytes per pixel)
And alpha channel is set to 255 for all pixels

#### Scenario: Import 1-channel grayscale
Given a grayscale PNG (1 channel)
When TextureImporter processes it
Then R=G=B=grayscale value and A=255 for each pixel

### Requirement: Mipmap Chain Generation
The TextureImporter MUST generate a complete mipmap chain from base resolution down to 1x1 using box-filter downsampling. Each mip level MUST be exactly half the dimensions of the previous (minimum 1).

#### Scenario: Correct mip count
Given a 256x256 base texture
When mipmaps are generated
Then 9 mip levels are produced (256, 128, 64, 32, 16, 8, 4, 2, 1)

#### Scenario: Non-power-of-two texture
Given a 300x200 texture
When mipmaps are generated
Then each mip is floor(prev/2) clamped to minimum 1

### Requirement: UzTexture Binary Format
The .uztexture format SHALL use magic number 'UZTX' (0x5554585A) and version field. Layout MUST be: UzTextureHeader + MipLevel[] (offsets and sizes) + mip data blobs. The format field MUST distinguish Uncompressed_RGBA8 from compressed formats (BC1-BC7) for future use.

#### Scenario: Valid .uztexture file structure
Given a texture was imported successfully
When the .uztexture file is read
Then the magic is 'UZTX' and version is 1
And mip level offsets point to valid data regions

### Requirement: Runtime Mesh Loader GPU Upload
The .uzmesh runtime loader SHALL read the binary file, validate magic and version, and upload vertex buffer + index buffer to GPU via RHI. It MUST return a Mesh handle containing GPU buffer references and LOD/submesh metadata.

#### Scenario: Mesh loaded and renderable
Given a valid .uzmesh file on disk
When the runtime loader processes it
Then GPU vertex and index buffers are created
And the returned Mesh handle contains correct vertex/index counts

### Requirement: Runtime Texture Loader GPU Upload
The .uztexture runtime loader SHALL read the binary file, validate magic and version, create a D3D12 texture with correct dimensions and mip count, upload RGBA8 data, and register a bindless SRV descriptor.

#### Scenario: Texture loaded and sampleable
Given a valid .uztexture file with 256x256 base and 9 mips
When the runtime loader processes it
Then a GPU texture is created with 9 mip levels
And a bindless SRV descriptor index is returned

### Requirement: Asset Cache Integration
Both importers MUST integrate with the existing AssetCache (xxHash64 content-addressed). Importing the same unchanged source file twice SHALL result in a cache hit — no reimport occurs on the second call.

#### Scenario: Cache hit skips reimport
Given a .glb file was previously imported and the source is unchanged
When uzcook processes it again
Then the .uzimport sidecar hash matches and import is skipped
And the cached .uzmesh output is reused

### Requirement: uzcook Registration
Both MeshImporter and TextureImporter SHALL be registered in the uzcook tool's AssetImporterRegistry. Running `uzcook --source assets/` MUST discover and process all .glb, .gltf, .png, .jpg, .tga files.

#### Scenario: Batch import via uzcook
Given an assets/ directory with 3 .glb files and 5 .png files
When `uzcook --source assets/` is executed
Then 3 .uzmesh files and 5 .uztexture files are produced in .cache/
And .uzimport sidecars are written for all 8 source files
