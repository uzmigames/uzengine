# Multi-Material & Multi-UV Channel System

> **Status**: Planned (Phase 3+)
> **Source**: `runtime/engine/renderer/draw_list.h`, `runtime/engine/renderer/mesh.h`
> **Depends on**: Draw List (§04), PBR Material System (§06), RHI (§03), Mesh Import Pipeline
> **References**: glTF primitives, FBX polygon groups, UE5 sections, Godot SurfaceArray

## Purpose

Real-world 3D models frequently contain **multiple materials per mesh** (e.g., a character with skin, eyes, clothing, accessories). They also require **multiple UV coordinate sets** — UV0 for primary textures (albedo, normal, PBR), UV1 for lightmaps, UV2 for detail textures. Currently, the engine's DrawCommand encodes a single mesh with a single material into a single draw call. This document describes the architecture for submesh-based multi-material rendering and support for multiple UV channels.

**Current limitation**: 1 mesh = 1 material = 1 draw call. This forces artists to either split complex models into separate mesh entities (expensive) or bake all variations into a single material (inflexible).

**Planned solution**:
- **Submeshes**: A mesh is divided into indexed ranges (offset + count), each with its own material assignment
- **Multi-UV**: Vertex format supports UV0, UV1, UV2 channels; shaders select the appropriate channel for each texture
- **Per-submesh commands**: Each submesh emits its own DrawCommand with material index, enabling efficient batching and sorting

This enables:
- Import glTF/FBX models with native multi-material hierarchy intact
- Support lightmapped geometry (UV0 for diffuse, UV1 for lightmap)
- Support detail textures and decal projection (UV2 for projected coordinates)
- Reduce draw call overhead by packing related submeshes into single batches

## Submesh / Multi-Material Architecture

### Submesh Concept

A **mesh asset** is divided into **submesh ranges**. Each submesh corresponds to a distinct material slot and occupies a contiguous range in the index buffer.

```
Mesh "Character"
├─ Submesh 0: indices [0, 5000]       → Material: "skin.uzmaterial"
├─ Submesh 1: indices [5000, 6500]    → Material: "eyes.uzmaterial"
├─ Submesh 2: indices [6500, 7200]    → Material: "clothing.uzmaterial"
└─ Submesh 3: indices [7200, 8100]    → Material: "accessories.uzmaterial"

Single vertex buffer shared across all submeshes
Single index buffer with ranges per submesh
```

When rendering, the engine:
1. Iterates over submeshes in the mesh asset
2. For each submesh, creates a separate DrawCommand with the submesh's material index
3. DrawCommand encodes the exact index range to draw (indexOffset, indexCount, vertexOffset)
4. Render pass sorts by material, then executes draws with minimal state changes

### DrawCommand Extension

The DrawCommand structure must encode submesh draw range information:

```cpp
/// Encodes a single draw call (either single mesh or submesh range).
struct DrawCommand {
    /// 64-bit sort key: priority order for batching and PSO selection.
    /// Bits [63:56] = render priority
    /// Bits [55:48] = material ID
    /// Bits [47:0]  = depth or instance ID (for sorting within same material)
    uint64_t sortKey;

    /// World transformation matrix (4x4).
    Mat4 worldMatrix;

    /// Normal matrix (3x3 stored as 4x4 with row 3 = zero). Used for normal transformation.
    Mat4 normalMatrix;

    /// Mesh asset handle (identifies vertex/index buffers and submesh layout).
    uint32_t meshId;

    /// First index in the index buffer for this submesh.
    uint32_t indexOffset;

    /// Number of indices to draw for this submesh.
    uint32_t indexCount;

    /// Base vertex offset (added to all indices when reading vertex buffer).
    /// Allows multiple submeshes to share a single vertex buffer without overlap.
    /// Example: vertexOffset=0 for submesh 0, vertexOffset=5000 for submesh 1, etc.
    int32_t vertexOffset;

    /// PBR texture SRV (Shader Resource View) binding index for bindless rendering.
    /// Bits [7:0]   = albedo map SRV index
    /// Bits [15:8]  = normal map SRV index
    /// Bits [23:16] = roughness map SRV index
    /// Bits [31:24] = metallic map SRV index
    uint32_t srvIndices;

    /// Additional packed data for optional textures (AO, emissive, detail maps).
    uint32_t optionalSrvIndices;
};
```

### Submesh Descriptor

A **Submesh** struct describes the GPU draw parameters for one submesh:

```cpp
/// Describes a contiguous range within a mesh's index buffer.
/// One per material slot in the mesh.
struct Submesh {
    /// First index in the mesh's index buffer for this submesh.
    uint32_t indexOffset;

    /// Number of indices to draw.
    uint32_t indexCount;

    /// Base vertex offset (added to all indices).
    /// Allows efficient packing of multiple submeshes in shared vertex buffer.
    int32_t vertexOffset;

    /// Index into the material buffer for this submesh.
    /// Used to look up PBRMaterial and SRV bindings.
    uint32_t materialIndex;

    /// AABB in local space (for culling).
    /// Updated during import to match geometry bounds.
    AABB localBounds;
};
```

### MeshAsset Storage

A **MeshAsset** owns both the GPU buffers and the submesh layout:

```cpp
/// Mesh asset: vertex/index buffers + submesh hierarchy.
struct MeshAsset {
    /// GPU vertex buffer (contains all submeshes' vertices).
    RHIBufferHandle vertexBuffer;

    /// GPU index buffer (contains all submeshes' indices).
    RHIBufferHandle indexBuffer;

    /// Submesh descriptors (one per material slot).
    std::vector<Submesh> submeshes;

    /// Total vertex count in vertexBuffer.
    uint32_t vertexCount;

    /// Total index count in indexBuffer.
    uint32_t indexCount;

    /// Vertex format flags (determines which UV channels are present).
    /// See VertexFormat flags below.
    uint32_t vertexFormatFlags;

    /// AABB in local space (union of all submesh bounds).
    AABB localBounds;
};
```

### Mesh Generation Example

When a glTF model is imported:

```cpp
// Pseudo-code: import glTF mesh with 4 primitives (submeshes)
std::vector<Submesh> submeshes;
uint32_t indexOffset = 0;

for (const auto& primitive : gltf_mesh.primitives) {
    uint32_t indexCount = primitive.indices.size();

    Submesh submesh{
        .indexOffset = indexOffset,
        .indexCount = indexCount,
        .vertexOffset = 0,  // All share same vertex buffer starting at 0
        .materialIndex = primitive.material_index,
        .localBounds = ComputeBounds(primitive.positions),
    };

    submeshes.push_back(submesh);
    indexOffset += indexCount;
}

mesh_asset.submeshes = submeshes;
mesh_asset.indexCount = indexOffset;
```

## Multi-UV Channel Architecture

### UV Channel Definition

A mesh can encode **up to 4 UV coordinate sets** (though typically only 3 are used):

| Channel | Purpose | Format | Use Case |
|---------|---------|--------|----------|
| **UV0** | Primary texture coordinates | float2 (Vec2) | Albedo, normal, PBR maps (metallic/roughness/AO) |
| **UV1** | Lightmap coordinates | float2 (Vec2) | Baked indirect lighting from lightmap texture |
| **UV2** | Detail / secondary | float2 (Vec2) | Tiled detail textures, decal projection |
| **UV3** | Reserved | float2 (Vec2) | Future: procedural generation, vertex paint, etc. |

**Key properties**:
- **UV0 is mandatory** — all geometry must have UV0 for primary texture mapping
- **UV1 & UV2 are optional** — present only if used by the mesh's materials
- **Non-overlapping** — UV1 for lightmaps is typically unwrapped to [0, 1) with no overlaps, unlike UV0 which may tile
- **Per-vertex** — each vertex stores coordinates for all present channels

### Vertex Layout Impact

The vertex buffer layout changes based on which UV channels are present. This is encoded in **VertexFormat flags**:

```cpp
enum class VertexFormat : uint32_t {
    Position   = 1 << 0,      ///< Vec3, mandatory
    Normal     = 1 << 1,      ///< Vec3, mandatory (for lighting)
    Tangent    = 1 << 2,      ///< Vec4 (xyz = tangent, w = handedness), mandatory (for normal mapping)
    TexCoord0  = 1 << 3,      ///< Vec2, mandatory
    TexCoord1  = 1 << 4,      ///< Vec2, optional (lightmap UV)
    TexCoord2  = 1 << 5,      ///< Vec2, optional (detail UV)
    TexCoord3  = 1 << 6,      ///< Vec2, optional (reserved)
    Color      = 1 << 7,      ///< Vec4, optional (per-vertex color)
};
```

The **vertex struct** matches the flags, packing only present attributes:

```cpp
/// Standard vertex layout with optional UV channels.
struct Vertex {
    /// Position in local space (float3, 12 bytes).
    Vec3 position;

    /// Packed normal (int8 × 4 quantized to [-1, 1], 4 bytes) or float3 (12 bytes).
    /// Quantization saves memory and improves cache efficiency.
    Vec3 normal;

    /// Tangent for normal mapping (float4: xyz = direction, w = handedness ±1, 16 bytes).
    Vec4 tangent;

    /// Primary texture coordinates (float2, 8 bytes).
    /// Used for albedo, normal, PBR maps.
    Vec2 texCoord0;

    // Optional fields (only if VertexFormat flags include them):

    /// Lightmap UV (float2, 8 bytes).
    /// Present only if VertexFormat::TexCoord1 is set.
    /// Non-overlapping, typically [0, 1) range.
    // Vec2 texCoord1;

    /// Detail texture UV (float2, 8 bytes).
    /// Present only if VertexFormat::TexCoord2 is set.
    /// Can tile/repeat for high-frequency detail.
    // Vec2 texCoord2;

    // Total size: 44 + (8 * optional channels)
    // = 44 bytes (no optional)
    // = 52 bytes (with texCoord1)
    // = 60 bytes (with texCoord1 and texCoord2)
};
```

**Packing rules**:
- VertexFormat flags determine which fields are present
- Attributes are always in the same order (position, normal, tangent, texCoord0, texCoord1, ...)
- Vertex stride is computed dynamically based on flags: `stride = 44 + 8 * (num_optional_channels)`
- This allows the renderer to choose the optimal format at import time

### Shader Vertex Input

The HLSL vertex input declaration mirrors the VertexFormat flags:

```hlsl
/// Vertex input structure (matches C++ Vertex layout).
struct VSInput {
    float3 position  : POSITION;      // Always present
    float3 normal    : NORMAL;        // Always present
    float4 tangent   : TANGENT;       // Always present (xyz = tangent, w = handedness)
    float2 uv0       : TEXCOORD0;     // Always present (primary)
    float2 uv1       : TEXCOORD1;     // Present if VertexFormat::TexCoord1 set (lightmap)
    float2 uv2       : TEXCOORD2;     // Present if VertexFormat::TexCoord2 set (detail)
};

float4 main(VSInput input) : SV_POSITION {
    // Construct TBN matrix from normal and tangent
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent.xyz);
    float3 B = cross(N, T) * input.tangent.w;  // Handedness correction

    // Sample textures with appropriate UV channel
    float4 albedo = tex_diffuse.Sample(samplerLinear, input.uv0);
    float4 normalMap = tex_normal.Sample(samplerLinear, input.uv0);

    // If material uses lightmap, sample with UV1
    #if HAS_LIGHTMAP
        float3 lightmap = tex_lightmap.Sample(samplerLinear, input.uv1).rgb;
    #endif

    // If material uses detail texture, sample with UV2 (tiled)
    #if HAS_DETAIL
        float3 detail = tex_detail.Sample(samplerLinear, input.uv2 * detail_scale).rgb;
    #endif

    return float4(albedo.rgb, 1.0);
}
```

## RHI Impact (All Backends)

Each RHI backend must support variable vertex layouts and indexed draw calls with base vertex offsets. This requires changes to pipeline creation and draw encoding.

### DirectX 12

**Input layout descriptor** must declare all used semantic slots:

```cpp
// Define all possible semantic slots (only present ones are used)
D3D12_INPUT_ELEMENT_DESC inputLayoutDesc[] = {
    { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD",  1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // Optional
    { "TEXCOORD",  2, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, // Optional
};

// Create pipeline with matching VertexFormat flags
D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
psoDesc.InputLayout = {
    .pInputElementDescs = inputLayoutDesc,
    .NumElements = (mesh.vertexFormatFlags & VertexFormat::TexCoord2) ? 6 :
                   (mesh.vertexFormatFlags & VertexFormat::TexCoord1) ? 5 : 4,
};
```

**Draw call with base vertex**:

```cpp
// DrawIndexed with submesh range
commandList->DrawIndexedInstanced(
    submesh.indexCount,           // NumIndexCountPerInstance
    1,                            // NumInstances
    submesh.indexOffset,          // StartIndexLocation
    submesh.vertexOffset,         // BaseVertexLocation (D3D12 feature)
    0                             // StartInstanceLocation
);
```

### Vulkan

**VkVertexInputAttributeDescription** array must include all used channels:

```cpp
std::vector<VkVertexInputAttributeDescription> attributeDescs;
attributeDescs.push_back({
    .location = 0,
    .binding = 0,
    .format = VK_FORMAT_R32G32B32_SFLOAT,      // Position
    .offset = 0,
});
attributeDescs.push_back({
    .location = 1,
    .binding = 0,
    .format = VK_FORMAT_R32G32B32_SFLOAT,      // Normal
    .offset = 12,
});
attributeDescs.push_back({
    .location = 2,
    .binding = 0,
    .format = VK_FORMAT_R32G32B32A32_SFLOAT,   // Tangent
    .offset = 24,
});
attributeDescs.push_back({
    .location = 3,
    .binding = 0,
    .format = VK_FORMAT_R32G32_SFLOAT,         // TexCoord0
    .offset = 40,
});
if (mesh.vertexFormatFlags & VertexFormat::TexCoord1) {
    attributeDescs.push_back({
        .location = 4,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,     // TexCoord1
        .offset = 48,
    });
}
if (mesh.vertexFormatFlags & VertexFormat::TexCoord2) {
    attributeDescs.push_back({
        .location = 5,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,     // TexCoord2
        .offset = 56,
    });
}

// Create vertex input state
VkPipelineVertexInputStateCreateInfo vertexInputState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .pVertexBindingDescriptions = &bindingDesc,
    .vertexBindingDescriptionCount = 1,
    .pVertexAttributeDescriptions = attributeDescs.data(),
    .vertexAttributeDescriptionCount = (uint32_t)attributeDescs.size(),
};
```

**Draw call with base vertex**:

```cpp
// vkCmdDrawIndexed with base vertex offset
vkCmdDrawIndexed(
    commandBuffer,
    submesh.indexCount,           // indexCount
    1,                            // instanceCount
    submesh.indexOffset,          // firstIndex
    submesh.vertexOffset,         // vertexOffset (Vulkan feature)
    0                             // firstInstance
);
```

### Metal

**MTLVertexDescriptor** attributes must match all channels:

```cpp
MTLVertexDescriptor *vertexDesc = [MTLVertexDescriptor vertexDescriptor];

// Attribute 0: Position
vertexDesc.attributes[0].format = MTLVertexFormatFloat3;
vertexDesc.attributes[0].offset = 0;
vertexDesc.attributes[0].bufferIndex = 0;

// Attribute 1: Normal
vertexDesc.attributes[1].format = MTLVertexFormatFloat3;
vertexDesc.attributes[1].offset = 12;
vertexDesc.attributes[1].bufferIndex = 0;

// Attribute 2: Tangent
vertexDesc.attributes[2].format = MTLVertexFormatFloat4;
vertexDesc.attributes[2].offset = 24;
vertexDesc.attributes[2].bufferIndex = 0;

// Attribute 3: TexCoord0
vertexDesc.attributes[3].format = MTLVertexFormatFloat2;
vertexDesc.attributes[3].offset = 40;
vertexDesc.attributes[3].bufferIndex = 0;

NSUInteger offset = 48;
if (mesh.vertexFormatFlags & VertexFormat::TexCoord1) {
    vertexDesc.attributes[4].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[4].offset = offset;
    vertexDesc.attributes[4].bufferIndex = 0;
    offset += 8;
}
if (mesh.vertexFormatFlags & VertexFormat::TexCoord2) {
    vertexDesc.attributes[5].format = MTLVertexFormatFloat2;
    vertexDesc.attributes[5].offset = offset;
    vertexDesc.attributes[5].bufferIndex = 0;
}

// Set buffer stride
vertexDesc.layouts[0].stride = offset + ((mesh.vertexFormatFlags & VertexFormat::TexCoord2) ? 8 : 0);
```

**Draw call with base vertex**:

```cpp
// Metal: drawIndexedPrimitives with baseVertex
[renderEncoder
    drawIndexedPrimitives:MTLPrimitiveTypeTriangle
    indexCount:submesh.indexCount
    indexType:MTLIndexTypeUInt32
    indexBuffer:indexBuffer
    indexBufferOffset:submesh.indexOffset * sizeof(uint32_t)
    instanceCount:1
    baseVertex:submesh.vertexOffset
    baseInstance:0
];
```

### Common Requirements

1. **Dynamic stride computation** — Vertex stride must be computed from VertexFormat flags, not hardcoded
2. **Pipeline PSO variants** — Each unique VertexFormat creates a distinct pipeline (DX12 PSO, Vulkan pipeline, Metal render pipeline state)
3. **Base vertex support** — All backends support `baseVertex` or equivalent in indexed draw calls
4. **Input layout reflection** — Input layout must be created to match shader expectations

## Import Pipeline Impact

Mesh import (from glTF, FBX, USDZ) must preserve submesh structure and multi-UV layout.

### glTF Import

glTF **primitives** map directly to submeshes:

```cpp
// glTF mesh with 4 primitives (4 submeshes)
for (const auto& primitive : gltf_mesh.primitives) {
    Submesh submesh{
        .indexOffset = current_index_offset,
        .indexCount = primitive.indices.count(),
        .vertexOffset = 0,
        .materialIndex = primitive.material,
    };
    submeshes.push_back(submesh);
    current_index_offset += primitive.indices.count();
}

// glTF TEXCOORD_0 → VertexFormat::TexCoord0 (mandatory)
// glTF TEXCOORD_1 → VertexFormat::TexCoord1 (optional, e.g., lightmap)
// glTF TEXCOORD_2 → VertexFormat::TexCoord2 (optional, e.g., detail)

uint32_t format_flags = VertexFormat::Position | VertexFormat::Normal | VertexFormat::Tangent | VertexFormat::TexCoord0;
if (primitive.has_texcoord(1)) format_flags |= VertexFormat::TexCoord1;
if (primitive.has_texcoord(2)) format_flags |= VertexFormat::TexCoord2;
mesh_asset.vertexFormatFlags = format_flags;
```

### FBX Import

FBX **material slots** map to submesh ranges:

```cpp
// FBX mesh with 3 material slots → 3 submeshes
for (int slot = 0; slot < fbx_mesh.GetMaterialCount(); ++slot) {
    FBXMaterial* mat = fbx_mesh.GetMaterial(slot);
    std::vector<uint32_t> indices = fbx_mesh.GetIndicesForMaterial(slot);

    Submesh submesh{
        .indexOffset = current_index_offset,
        .indexCount = (uint32_t)indices.size(),
        .vertexOffset = 0,
        .materialIndex = slot,
    };
    submeshes.push_back(submesh);
    current_index_offset += (uint32_t)indices.size();
}

// FBX LAYER 0 → VertexFormat::TexCoord0
// FBX LAYER 1 → VertexFormat::TexCoord1
// FBX LAYER 2 → VertexFormat::TexCoord2
```

### Importer Checklist

1. **Preserve material assignments** — Each submesh must reference the correct material
2. **Preserve UV layout** — If source has TEXCOORD_1 (lightmap), include TexCoord1 in output
3. **Validate ranges** — Ensure submesh index ranges don't overlap and cover the entire index buffer
4. **Compute bounds** — Compute local AABB for each submesh (used for culling)
5. **Detect format flags** — Scan all vertices to determine which optional channels are present
6. **Quantize normals** — Consider quantizing normals (int8 × 4) to save memory and improve cache

## Sort Key Impact

The **64-bit sort key** already encodes material ID in bits [55:48]. With multi-material, the change is **not in sort key design**, but in **how draw commands are generated**:

**Before (single material per mesh)**:
- 1 mesh → 1 DrawCommand with material encoded in sort key

**After (multi-material per mesh)**:
- 1 mesh → N DrawCommands (one per submesh), each with different material in sort key
- Submeshes with the same material may batch together via the renderer's sort and coalesce logic

**Example**:

```
Mesh "character.gltf" with 4 submeshes:
  ├─ Submesh 0 (indices [0–5000])   → Material "skin" (materialId=10)
  ├─ Submesh 1 (indices [5000–6500]) → Material "eyes" (materialId=12)
  ├─ Submesh 2 (indices [6500–7200]) → Material "clothing" (materialId=10)  ← Same as submesh 0
  └─ Submesh 3 (indices [7200–8100]) → Material "accessories" (materialId=15)

DrawCommand 0: materialId=10, indexOffset=0,    indexCount=5000  → Sort key: [...|10|...]
DrawCommand 1: materialId=12, indexOffset=5000, indexCount=1500  → Sort key: [...|12|...]
DrawCommand 2: materialId=10, indexOffset=6500, indexCount=700   → Sort key: [...|10|...]  ← Different index range, same material
DrawCommand 3: materialId=15, indexOffset=7200, indexCount=900   → Sort key: [...|15|...]

Sorted output:
1. DrawCommand 0 (material 10, indices [0–5000])
2. DrawCommand 2 (material 10, indices [6500–7200])   ← Batched with command 0 (same PSO)
3. DrawCommand 1 (material 12, indices [5000–6500])
4. DrawCommand 3 (material 15, indices [7200–8100])
```

No change to sort key structure; submesh ranges allow finer-grained batching.

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine (Planned)** |
|---------|----------|---------|------------------------|
| **Multi-material per mesh** | FStaticMeshSection (per section) | SurfaceArray (per surface) | **Submesh ranges (per material)** |
| **UV channels** | Up to 8 (TEXCOORD0–7) | UV, UV2 | **Up to 4 (UV0–UV3, extensible)** |
| **Lightmap support** | Dedicated TEXCOORD1 | UV2 for lightmaps | **UV1 for lightmaps** |
| **Draw call granularity** | Per-section | Per-surface | **Per-submesh (with batching)** |
| **Vertex layout flexibility** | Flexible (per-mesh declaration) | Flexible (per-surface declaration) | **Flags-based (VertexFormat bits)** |
| **glTF support** | Automatic (primitives → sections) | Automatic (primitives → surfaces) | **Automatic (primitives → submeshes)** |
| **Memory efficiency** | Supports quantized normals | Supports quantization | **Supports quantized normals (planned)** |

## Rules

1. **NEVER draw a full mesh when submeshes exist** — Always iterate submeshes and emit per-submesh DrawCommands. Merging submeshes into a single draw loses material isolation.

2. **ALWAYS preserve submesh boundaries during import** — glTF primitives, FBX material slots, and USDZ partitions map directly to submeshes. Never collapse multiple submeshes into one.

3. **NEVER assume UV1/UV2 exist** — Check VertexFormat flags before accessing optional channels. Always validate shader permutations match the actual vertex format.

4. **ALWAYS pad vertex stride to GPU alignment** — When optional UV channels are absent, pad the vertex struct to a 16-byte boundary to maintain cache efficiency and compatibility across backends.

5. **NEVER hardcode vertex stride** — Derive stride dynamically from VertexFormat flags at pipeline creation time. Never use sizeof(Vertex) — compute from flags instead.

6. **ALWAYS validate index ranges in submeshes** — Ensure submesh.indexOffset + submesh.indexCount ≤ mesh.indexCount. No overlaps, no gaps.

7. **ALWAYS test multi-UV shaders on all backends** — DX12 SM6.6, DX12 SM5.0, Vulkan, Metal. Vertex input layouts must match across all targets.

## Phase Roadmap

| Phase | Milestone | Features |
|-------|-----------|----------|
| **3b-3.0b** | ECS World/Entity | ✅ Entity handles, World container (DONE) |
| **3b-3.1b+** | Forward PBR | ✅ Single-material rendering (DONE) |
| **3c+** | Multi-Material | Multi-submesh, per-submesh materials, DrawCommand range fields |
| **3d+** | Multi-UV | Full UV0/1/2 support, lightmap rendering, detail textures |
| **3e+** | Deferred Pipeline | G-Buffer with full UV support, lightmapped deferred pass |

## Related Docs

- [PBR Material System (§06)](06-pbr-material-system.md) — Material data layout and GPU binding
- [Sort Key Architecture (§07)](07-sort-key-architecture.md) — 64-bit key design for batching
- [Forward PBR Pass (§09)](09-forward-pbr-pass.md) — Draw call submission and PSO binding
- [Draw List (§04)](../04-draw-list/README.md) — DrawCommand collection and sorting
- [Mesh Import Pipeline](../08-asset-system/mesh-import.md) — Submesh preservation during import
