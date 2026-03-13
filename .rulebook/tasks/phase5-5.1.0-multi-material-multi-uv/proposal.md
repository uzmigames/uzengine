# Multi-Material Submesh & Multi-UV Channel Support

## Motivation

The current rendering system treats each mesh as a monolithic entity: 1 mesh = 1 material = 1 draw call. This design is fundamentally incompatible with real-world 3D assets:

- **glTF 2.0 spec**: Mesh nodes contain multiple *primitives*, each with its own material and index range (submesh boundaries)
- **FBX files**: Polygon groups map to different materials; importing loses this structure
- **Lightmapping**: Lightmaps require a separate UV channel (UV1) distinct from diffuse UVs (UV0)
- **Detail textures**: High-frequency detail maps typically use UV2 with different tiling
- **Decals / Secondary effects**: May require dedicated UV channels for proper blending

Without submesh support, artists cannot import multi-material assets directly or use lightmaps alongside main textures. This milestone adds **submesh (index range) boundaries** and **multi-UV channel support** to enable real asset pipelines.

## What Changes

### Rendering Layer

**DrawCommand** extends with three new fields to enable index/vertex offsets:
```cpp
struct DrawCommand {
    uint32_t indexOffset;      // Start in index buffer
    uint32_t indexCount;       // Number of indices to draw
    uint32_t vertexOffset;     // Base vertex for index lookup
    // ... existing fields
};
```

**MeshAsset** adds submesh tracking:
```cpp
struct Submesh {
    uint32_t indexOffset;      // Into parent mesh's index buffer
    uint32_t indexCount;
    uint32_t vertexOffset;
    uint32_t materialIndex;    // Reference to material in parent
};

struct MeshAsset {
    std::vector<Submesh> submeshes;  // >=1 submesh per mesh
    // ... existing vertex/index buffers
};
```

Drawing changes from "one draw per mesh" to **one draw per submesh**, each with its own material ID and sort key.

### Vertex Format

Introduce a **bitmask system** for vertex layout flexibility:
```cpp
enum class VertexFormatFlags : uint32_t {
    HasUV0     = 1 << 0,  // Always present for texturing
    HasUV1     = 1 << 1,  // Lightmap UVs
    HasUV2     = 1 << 2,  // Detail / secondary channel
    HasTangent = 1 << 3,  // For normal maps
};
```

A single flexible **Vertex struct** holds position, normal, UV0 (required), and UV1/UV2 (optional).

Helper functions:
- `GetVertexStride(VertexFormatFlags)` → byte size per vertex
- `BuildInputLayout(VertexFormatFlags)` → per-backend layout creation

### RHI Backends

Each backend (DX12, Vulkan, Metal) updates:
1. Input layout creation — add TEXCOORD0/1/2 attributes based on flags
2. DrawIndexedInstanced calls — use indexOffset, indexCount, vertexOffset parameters
3. PSO / Pipeline vertex input state — accommodate variable stride

### Shaders

Mesh shaders now receive UV channels conditionally:
```hlsl
float2 uv0 : TEXCOORD0;
float2 uv1 : TEXCOORD1;  // Only if HasUV1 flag set
float2 uv2 : TEXCOORD2;  // Only if HasUV2 flag set
```

Permutations (via #define) control which TEXCOORD attributes are compiled in.

### Import Pipeline

- **glTF importer**: Preserve primitive boundaries as submesh entries; import TEXCOORD_0 and TEXCOORD_1
- **FBX importer** (future): Extract polygon group → material → submesh mapping
- Both importers validate format flags and emit warnings if UV channels are missing

### Forward & Deferred Passes

- **ForwardPBRPass**: Iterate submeshes; emit one draw per submesh with correct material ID
- **GBufferPass**: Similar; preserve lightmap UV if present
- **LightingPass**: Use lightmap UV if mesh has it

## Scope

- **In scope**: DrawCommand, MeshAsset, vertex format system, DX12/Vulkan/Metal backends, forward/deferred passes, glTF importer, shader permutations, tests, documentation
- **Out of scope**: Physics collider generation from submeshes, lightmap baking (editor), FBX importer (deferred)

## Impact

- **Affected systems**: DrawCommand, MeshAsset, all RHI backends, shader compiler, forward/deferred passes, glTF importer
- **Affected code**: `runtime/engine/renderer/`, `runtime/engine/rhi/`, `runtime/shaders/`, import pipeline
- **Breaking change**: YES — DrawCommand and MeshAsset structures change; requires rendering code updates
- **User benefit**: Import complex multi-material assets directly; use lightmaps; support detail textures; ship production-ready glTF/FBX pipelines

## Success Criteria

✅ A mesh with 3 submeshes generates 3 draws per frame (one per submesh)
✅ UV0, UV1, UV2 present in vertex buffer; shader permutations control sampling
✅ glTF with multiple primitives imports as one MeshAsset with N submeshes
✅ Forward PBR and deferred pipelines both render multi-material meshes correctly
✅ All backend input layouts created dynamically based on VertexFormatFlags
✅ 95%+ test coverage for submesh and vertex format logic
