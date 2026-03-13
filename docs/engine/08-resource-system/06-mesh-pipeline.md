# Mesh Pipeline — Import, Optimization, LOD, and GPU Resource Management

> **Status**: Planned (Phase 1.5–2)
> **Source**: `resource_system/mesh/`, `import_pipeline/mesh/`
> **References**: UE5 FStaticMeshLODResources + FSkeletalMeshRenderData, Godot ResourceImporterScene + ArrayMesh, meshoptimizer, fastgltf, ufbx, mikktspace

## Purpose

The Mesh Pipeline ingests raw mesh data (glTF 2.0, FBX, OBJ), validates and optimizes vertex streams, generates LODs, compresses tangents/normals, writes to `.uzmesh` binary format, and manages GPU resource creation. This pipeline is the critical path from digital content to renderable primitives, balancing quality, memory, and performance.

## Architecture

```
Artist Asset (GLTF, FBX, OBJ)
    │
    ├── 1. Parser (fastgltf/ufbx)
    │   └── Extract node hierarchy, meshes, materials, animations
    │
    ├── 2. Validator
    │   ├── Vertex count < 65536 (16-bit index buffer check)
    │   ├── Bone count ≤ 256 (max skeleton influences)
    │   ├── Tangent validity (used for normal mapping)
    │   └── Material slot sanity (no missing references)
    │
    ├── 3. Optimization
    │   ├── meshoptimizer::optimize (vertex cache, overdraw)
    │   ├── meshoptimizer::simplify (automatic LOD decimation)
    │   └── Vertex deduplication (merge identical vertices)
    │
    ├── 4. Tangent Generation (if missing)
    │   └── mikktspace (Mikkelsen tangent space algorithm, gold standard)
    │
    ├── 5. Normal Map Baking (optional)
    │   └── High-poly → low-poly normal transfer via Xatlas + GPU rasterization
    │
    ├── 6. LOD Generation
    │   ├── Compute target triangle count per LOD (e.g., LOD0=100%, LOD1=50%, LOD2=25%)
    │   ├── simplify() with target_count and target_error threshold
    │   └── Per-LOD bounding volume (AABB, sphere)
    │
    ├── 7. Submesh & Skinning Validation
    │   ├── Split meshes by material slot
    │   ├── Normalize bone weights (sum = 1.0, max 4 influences)
    │   └── Validate bone indices < skeleton size
    │
    ├── 8. Compress & Quantize
    │   ├── Normal/Tangent: 10.10.10.2 or 8.8.8.8 packed int
    │   ├── UV: half-float (FP16) if range permits
    │   ├── Colors: uint8 RGBA
    │   └── Bone indices/weights: uint8 (4 influences max)
    │
    ├── 9. Generate Bounding Volumes
    │   ├── AABB (axis-aligned box)
    │   ├── Sphere (center + radius)
    │   ├── OBB (oriented box, rotation + half-extents)
    │   └── Convex hull (for collision, small subset of vertices)
    │
    └── 10. Write .uzmesh Binary
        └── Binary format: header + LOD array + vertex streams + index buffers + submesh metadata
```

## File Format: .uzmesh

Binary format designed for direct GPU upload and 64-byte-aligned streaming.

### Binary Layout

All values are little-endian. All data regions (vertex, index) are 64-byte aligned for direct GPU upload without additional copies.

```
[Header]       64 bytes
  magic:            4 bytes   "UZMS" (0x534D5A55)
  version:          4 bytes   uint32 (1)
  flags:            4 bytes   uint32 (reserved, must be 0)
  lodCount:         4 bytes   uint32
  totalSubmeshCount:4 bytes   uint32
  vertexCount:      4 bytes   uint32
  indexCount:       4 bytes   uint32
  vertexStride:     4 bytes   uint32 (bytes per vertex)
  indexStride:      4 bytes   uint32 (2 = uint16, 4 = uint32)
  vertexDataOffset: 4 bytes   uint32 (byte offset from file start)
  indexDataOffset:  4 bytes   uint32 (byte offset from file start)
  reserved[5]:     20 bytes   (must be 0)

[LODs]         lodCount * sizeof(UzMeshLOD)
[Submeshes]    totalSubmeshCount * sizeof(UzMeshSubmesh)
[Padding]      to 64-byte alignment
[Vertex Data]  vertexCount * vertexStride bytes (64-byte aligned)
[Padding]      to 64-byte alignment
[Index Data]   indexCount * indexStride bytes
```

### Header Structure (64 bytes, packed)

```cpp
#pragma pack(push, 1)
struct UzMeshHeader {
    uint32_t magic = 0x534D5A55;      // Magic: "UZMS"
    uint32_t version = 1;              // Format version
    uint32_t flags = 0;                // Reserved, must be 0
    uint32_t lodCount = 0;             // Number of LOD levels
    uint32_t totalSubmeshCount = 0;    // Total submesh count across all LODs
    uint32_t vertexCount = 0;          // Total vertex count
    uint32_t indexCount = 0;           // Total index count
    uint32_t vertexStride = 0;         // Bytes per vertex
    uint32_t indexStride = 0;          // 2 = uint16, 4 = uint32
    uint32_t vertexDataOffset = 0;     // Byte offset from file start
    uint32_t indexDataOffset = 0;      // Byte offset from file start
    uint32_t reserved[5] = {};         // Reserved for future use
};
// static_assert(sizeof(UzMeshHeader) == 64)
#pragma pack(pop)
```

### LOD Descriptor (16 bytes per LOD, packed)

Each LOD references a contiguous range of submeshes via offset and count into the global submesh array.

```cpp
#pragma pack(push, 1)
struct UzMeshLOD {
    uint32_t lodLevel = 0;              // LOD index (0 = highest detail)
    uint32_t submeshOffset = 0;         // Index of first submesh in the array
    uint32_t submeshCount = 0;          // Number of submeshes in this LOD
    float screenHeightThreshold = 0.0f; // Minimum screen-space height ratio
};
// static_assert(sizeof(UzMeshLOD) == 16)
#pragma pack(pop)
```

### Submesh Descriptor (16 bytes per submesh, packed)

Describes a single submesh (draw call) within a LOD level. Vertex and index offsets are relative to the start of their respective data regions (not file start).

```cpp
#pragma pack(push, 1)
struct UzMeshSubmesh {
    uint32_t vertexOffset = 0;   // First vertex index in the vertex buffer
    uint32_t indexOffset = 0;    // First index in the index buffer
    uint32_t indexCount = 0;     // Number of indices to draw
    uint32_t materialIndex = 0;  // Material slot index
};
// static_assert(sizeof(UzMeshSubmesh) == 16)
#pragma pack(pop)
```

### Vertex Format: VertexPosNormUV (32 bytes per vertex, packed)

Standard interleaved vertex layout suitable for most opaque geometry. Easily extensible for skinning or additional attributes.

```cpp
#pragma pack(push, 1)
struct VertexPosNormUV {
    float position[3];  // World-space position (x, y, z)
    float normal[3];    // Unit normal vector (x, y, z)
    float uv[2];        // Texture coordinates (u, v)
};
// static_assert(sizeof(VertexPosNormUV) == 32)
#pragma pack(pop)
```

### Example: Loading a .uzmesh File

```cpp
// Parse header (first 64 bytes)
UzMeshHeader header;
file.read(reinterpret_cast<char*>(&header), 64);

// Validate
if (header.magic != 0x534D5A55 || header.version != 1) {
    return error("Invalid .uzmesh magic or version");
}

// Parse LODs
std::vector<UzMeshLOD> lods(header.lodCount);
file.read(reinterpret_cast<char*>(lods.data()),
          header.lodCount * sizeof(UzMeshLOD));

// Parse submeshes
std::vector<UzMeshSubmesh> submeshes(header.totalSubmeshCount);
file.read(reinterpret_cast<char*>(submeshes.data()),
          header.totalSubmeshCount * sizeof(UzMeshSubmesh));

// Skip to aligned vertex data
file.seekg(header.vertexDataOffset);
std::vector<uint8_t> vertexData(header.vertexCount * header.vertexStride);
file.read(reinterpret_cast<char*>(vertexData.data()), vertexData.size());

// Skip to aligned index data
file.seekg(header.indexDataOffset);
std::vector<uint8_t> indexData(header.indexCount * header.indexStride);
file.read(reinterpret_cast<char*>(indexData.data()), indexData.size());
```

## Mesh Importer: glTF/GLB to .uzmesh

The built-in `MeshImporter` class converts glTF 2.0 and GLB mesh files into .uzmesh binary format.

### Supported Formats
- `.glb` — Binary glTF 2.0 (recommended, single file)
- `.gltf` — Text glTF 2.0 with external .bin buffers

### Import Pipeline

```
1. Parse glTF with fastgltf
2. Extract first mesh, first primitive (positions, normals, UVs, indices)
3. Generate flat normals if missing (fallback: compute from triangle faces)
4. Apply meshopt_optimizeVertexCache + meshopt_optimizeVertexFetch
5. Write via WriteUzMesh with a single LOD and single submesh
```

### C++ API

```cpp
class MeshImporter : public IAssetImporter {
public:
    /// Returns "mesh" — the unique importer name.
    const char* Name() const override;

    /// Returns current importer version (bump if import logic changes).
    uint32_t Version() const override;

    /// Returns {".glb", ".gltf"} — file extensions this importer handles.
    const std::vector<std::string>& Extensions() const override;

    /// Imports a glTF/GLB file into .uzmesh binary format.
    ///
    /// # Arguments
    /// * sourceData - Raw bytes of the .glb or .gltf file
    /// * sourceName - Filename for format detection and error messages
    /// * options - Ignored (mesh import has no configurable options)
    ///
    /// # Returns
    /// On success: result.data contains the complete .uzmesh binary.
    /// On failure: result.error describes the issue (no meshes, missing positions,
    ///            parse failure, etc.).
    /// Logs Asset::Error on failure, Asset::Info on success (with vertex/index counts).
    AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) override;
};
```

### Output Format

The importer always produces:
- **Single LOD** (LOD0 with full detail)
- **Single Submesh** (material slot 0)
- **VertexPosNormUV format** (position + normal + UV, 32 bytes/vertex)
- **Vertex indices** (uint16 or uint32 depending on vertex count)

Example output for a 1024-vertex cube:

```
UzMeshHeader:
  magic:              0x534D5A55 ("UZMS")
  version:            1
  lodCount:           1
  totalSubmeshCount:  1
  vertexCount:        1024
  indexCount:         6144 (6 faces × 4 verts × 256 triangles)
  vertexStride:       32 (sizeof(VertexPosNormUV))
  indexStride:        2 (uint16)
  vertexDataOffset:   64 + 16 + 16 + padding → 128 (aligned)
  indexDataOffset:    128 + (1024 × 32) + padding → aligned to 64 bytes

UzMeshLOD[0]:
  lodLevel:           0
  submeshOffset:      0
  submeshCount:       1
  screenHeightThreshold: 0.0f

UzMeshSubmesh[0]:
  vertexOffset:       0
  indexOffset:        0
  indexCount:         6144
  materialIndex:      0
```

## Vertex Attribute Streams

### Position (Required)

3 float (12 bytes per vertex). Must be present.

### Normal & Tangent

Normals required for lighting (Phong/PBR). Tangents required for normal mapping.

- **Normal**: 3 floats or packed int (10.10.10.2).
- **Tangent**: 4 components (XYZ direction + W handedness sign ±1).
- **BiTangent**: Computed in shader as `cross(normal, tangent.xyz) * tangent.w`.

### UV Coordinates

UV0 (primary texture coordinates), UV1 (lightmap UVs, optional).

- 2 half-float (4 bytes) or 2 float (8 bytes).
- Range [0, 1] for UV0 (wrapped/clamped per sampler).
- Range [0, 1] lightmap space for UV1 (usually clamped).

### Colors

Vertex color attribute (RGBA, one uint32 per vertex).

- Typically baked lighting or material masks.
- In shader: unpack as `(r, g, b, a) / 255.0f`.

### Bone Weights & Indices (Skinned Meshes Only)

For skeletal animation (Section 19).

- **BoneIndices**: uint8[4] — skeleton bone IDs (0–255).
- **BoneWeights**: uint8[4] — influence weights (normalized: sum = 255).

## Optimization with meshoptimizer

Uses Zeux's meshoptimizer for vertex cache optimization and automatic LOD generation.

```cpp
// Example optimization workflow
const meshopt_VertexFetchStatistics stats_before = meshopt_analyzeVertexFetch(...);

// Reorder vertices for cache efficiency (GPU L1 cache is typically 16 KB)
std::vector<uint32_t> remap(vertex_count);
meshopt_optimizeVertexFetch(remap.data(), indices.data(), index_count,
                            vertices.data(), vertex_count, vertex_size);

// Reorder indices and vertices
meshopt_remapIndexBuffer(indices_opt.data(), indices.data(), index_count, remap.data());
meshopt_remapVertexBuffer(vertices_opt.data(), vertices.data(), vertex_count, vertex_size, remap.data());

// Verify improvement
const meshopt_VertexFetchStatistics stats_after = meshopt_analyzeVertexFetch(...);
assert(stats_after.overfetch < stats_before.overfetch);

// Generate LOD (50% of original triangles)
std::vector<uint32_t> lod_indices(index_count / 2);
size_t lod_count = meshopt_simplify(
    lod_indices.data(),
    indices_opt.data(),
    index_count,
    reinterpret_cast<const float*>(vertices_opt.data()),
    vertex_count,
    vertex_size,
    index_count / 4,          // target_count: 25% of triangles
    0.02f                      // target_error: 2% geometric error
);
lod_indices.resize(lod_count);
```

## Tangent Generation (mikktspace)

Tangent space is critical for normal-mapped surfaces. mikktspace is the industry standard.

```cpp
// mikktspace interface (simplified)
struct IMikkTSpace {
    int (*getNumFaces)(const SMikkTSpaceContext* pContext);
    int (*getNumVerticesOfFace)(const SMikkTSpaceContext* pContext, const int iFace);

    // Position and normal for a vertex
    void (*getPosition)(const SMikkTSpaceContext* pContext, float fOut[],
                       const int iFace, const int iVert);
    void (*getNormal)(const SMikkTSpaceContext* pContext, float fOut[],
                      const int iFace, const int iVert);

    // UV coordinate
    void (*getTexCoord)(const SMikkTSpaceContext* pContext, float fOut[],
                        const int iFace, const int iVert);

    // Output: set tangent and bitangent sign
    void (*setTSpaceBasic)(const SMikkTSpaceContext* pContext,
                           const float fvTangent[], const float fSign,
                           const int iFace, const int iVert);
};

// Usage
genTangSpaceDefault(&context);  // Generates tangent space for all vertices
```

## LOD Selection

LODs are selected based on screen coverage (distance from camera).

```cpp
/// Determine which LOD to use based on projected screen size.
/// lower_bound(screen_size) gives the LOD to render.
uint8_t SelectLOD(float screen_size) {
    // LOD0: screen coverage > 20% → highest detail
    // LOD1: screen coverage 5–20% → medium detail
    // LOD2: screen coverage < 5% → low detail
    if (screen_size > 0.20f) return 0;
    if (screen_size > 0.05f) return 1;
    return 2;
}
```

## Skinned Mesh Validation

For skeletal meshes (Section 19).

```cpp
/// Validate bone influences.
bool ValidateSkinning(const Skeleton& skeleton, const std::vector<Vec4>& weights) {
    for (const auto& w : weights) {
        float sum = w.x + w.y + w.z + w.w;
        if (std::abs(sum - 1.0f) > 0.01f) {
            UZ_LOG(Asset, Warning, "Bone weight sum is {}, should be 1.0", sum);
            return false;
        }
        // Clamp to max 4 influences (already ensured by format)
    }
    return true;
}
```

## GPU Resource Creation

Once .uzmesh is loaded, create GPU buffers for rendering.

```cpp
class Mesh {
private:
    /// Vertex attributes (position, normal, tangent, uv, color).
    std::vector<RHI::Handle<RHI::Buffer>> vertex_buffers_;

    /// Index buffer (16-bit uint).
    RHI::Handle<RHI::Buffer> index_buffer_;

    /// Per-LOD metadata (vertex/index counts, bounds).
    std::vector<MeshLOD> lods_;

    /// Per-material submeshes (index slice, material slot).
    std::vector<Submesh> submeshes_;

public:
    /// Upload mesh data to GPU.
    bool UploadToGPU(RHI::Device& device) {
        // Create vertex buffer with position + normal + tangent + uv
        vertex_buffers_.push_back(device.CreateBuffer({
            .size = vertex_data_.size(),
            .stride = sizeof(Vertex),
            .usage = RHI::BufferUsage::Vertex,
            .initial_data = vertex_data_.data()
        }));

        // Create index buffer
        index_buffer_ = device.CreateBuffer({
            .size = index_data_.size() * sizeof(uint16_t),
            .usage = RHI::BufferUsage::Index,
            .initial_data = index_data_.data()
        });

        return true;
    }
};
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Import formats | FBX, USD, Datasmith | glTF, OBJ (via Assimp) | FBX, Blender via FBX | **glTF 2.0 (fastgltf), FBX (ufbx), OBJ** |
| Binary format | uasset + .ubulk | Resource files | .asset (binary) | **.uzmesh (custom binary)** |
| Tangent generation | MikkTSpace (default) | Computed per-frame | MikkTSpace | **mikktspace** |
| LOD generation | Automatic (Mesh Reduction) | Manual or plugin | LOD Group (manual) | **meshoptimizer::simplify** |
| Optimization | FStaticMeshLODResources (vertex cache) | Limited | Mesh compression | **meshoptimizer (cache + overdraw)** |
| Skinning validation | Automatic (FSkeletalMeshRenderData) | Manual | Built-in | **Normalize weights, max 4 influences** |
| Bounding volumes | AABB + Sphere (automatic) | AABB (per-mesh) | AABB | **AABB + Sphere + OBB + Convex** |
| Convex decomposition | PhysX (separate tool) | CapsuleShape3D | PhysX/Havok | **Optional (via VHACD)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 1.5** | glTF parser (fastgltf), basic mesh validation, vertex deduplication, GPU upload |
| **Phase 2** | meshoptimizer integration, automatic LOD generation, tangent generation (mikktspace), material submesh splitting |
| **Phase 2.5** | FBX support (ufbx), OBJ support, normal map baking, convex hull generation for physics |
| **Phase 3** | Advanced: skeletal mesh import, morph target baking, mesh merging for draw call reduction |
