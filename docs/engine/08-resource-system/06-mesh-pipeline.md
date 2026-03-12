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

Binary format designed for direct GPU upload and cache-friendly streaming.

```cpp
/// Header identifies file and version.
struct UzMeshHeader {
    uint32_t magic = 'UZMS';    // Magic number (0x5A5B4D53)
    uint32_t version = 1;        // Format version (bump if incompatible)
    uint32_t lod_count;          // Number of LOD levels
    uint32_t submesh_count;      // Number of submeshes (material slots)
    uint32_t flags;              // Is skinned? Has normals? Has tangents?
};

/// Per-LOD level: vertex count, index buffer, bounds.
struct UzMeshLOD {
    uint32_t vertex_count;
    uint32_t index_count;
    AABB aabb;                   // Bounding box in local space
    Sphere sphere;               // Bounding sphere
    float screen_size;           // LOD selection: screen coverage threshold
};

/// Submesh: slice of vertex/index buffers for one material slot.
struct UzMeshSubmesh {
    uint32_t index_start;
    uint32_t index_count;
    uint32_t material_slot;      // Index into material array
    AABB aabb;
};

/// Vertex attribute stream definitions.
/// Data is interleaved (one vertex per position + normal + UV + color, etc.)
enum class VertexAttribute : uint8_t {
    Position,                    // Vec3 (required)
    Normal,                      // 10.10.10.2 packed uint
    Tangent,                     // 10.10.10.2 packed uint (handedness in sign bit)
    BiTangent,                   // Computed from Normal × Tangent
    UV0, UV1,                    // Vec2 half-float
    Color,                       // uint32 RGBA
    BoneWeights,                 // uint8[4]
    BoneIndices,                 // uint8[4]
};
```

## Import Pipeline API

```cpp
namespace uz::asset {
    /// Options controlling mesh import.
    struct MeshImportConfig {
        /// Enable automatic tangent generation (mikktspace).
        bool generate_tangents = true;

        /// Enable automatic LOD generation via meshoptimizer::simplify.
        bool generate_lods = true;

        /// LOD decimation targets (0.0–1.0 triangle count ratio).
        std::vector<float> lod_targets = {1.0f, 0.5f, 0.25f};

        /// Recalculate normals (ignore source data).
        bool recalculate_normals = false;

        /// Optimize vertex cache and overdraw order.
        bool optimize_cache = true;

        /// Maximum bones per vertex (clamped to 4 for most engines).
        uint8_t max_influences = 4;

        /// Scale mesh by this factor (useful for unit conversions).
        float scale = 1.0f;
    };

    /// High-level import function.
    /// Parses asset, validates, optimizes, writes .uzmesh.
    struct ImportResult {
        bool success;
        std::string error_message;
        std::vector<std::string> warnings;
        size_t output_bytes;  // Size of .uzmesh written
    };

    ImportResult ImportMesh(
        StringView input_path,          // e.g., "res://models/character.glb"
        StringView output_path,         // e.g., ".cache/models/character.uzmesh"
        const MeshImportConfig& config
    );
}
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
