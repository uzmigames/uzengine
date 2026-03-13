# Submesh & Multi-UV Vertex Format Specification

## Overview

This specification defines the behavior of submesh drawing (multiple materials per mesh) and multi-UV channel support (lightmap UVs, detail texture UVs) across all RHI backends.

## Structure

### Submesh Definition

```cpp
struct Submesh {
    /// Index into mesh's index buffer where this submesh's indices begin.
    uint32_t indexOffset;

    /// Number of indices in this submesh.
    uint32_t indexCount;

    /// Base vertex offset for index lookups (applies to all indices in this submesh).
    uint32_t vertexOffset;

    /// Index into the parent mesh's material array.
    uint32_t materialIndex;
};
```

### VertexFormatFlags

```cpp
enum class VertexFormatFlags : uint32_t {
    HasUV0     = 1 << 0,  // UV channel 0 (diffuse/main texture UVs) — MUST be present
    HasUV1     = 1 << 1,  // UV channel 1 (lightmap UVs)
    HasUV2     = 1 << 2,  // UV channel 2 (detail/secondary texture UVs)
    HasTangent = 1 << 3,  // Tangent vector for normal mapping
};
```

### Vertex Layout

All vertices in a mesh have the same layout determined by VertexFormatFlags:

```cpp
struct Vertex {
    // Always present
    float position[3];
    float normal[3];
    float uv0[2];           // TEXCOORD0

    // Optional, based on flags
    float uv1[2];           // TEXCOORD1 (if HasUV1)
    float uv2[2];           // TEXCOORD2 (if HasUV2)
    float tangent[3];       // (if HasTangent)
};
```

The vertex stride depends on which channels are present. Helper function `GetVertexStride(VertexFormatFlags flags)` returns the exact byte size.

## Given/When/Then Scenarios

### Scenario 1: Single-Material Mesh (Backward Compatibility)

**Given** a mesh with one submesh covering all indices (e.g., indexOffset=0, indexCount=10000, vertexOffset=0)

**When** the mesh is submitted for rendering via the scene

**Then**
- Exactly 1 DrawCommand is emitted with indexOffset=0, indexCount=10000, vertexOffset=0
- The SortKey contains the single material ID
- The mesh draws as a single unit (no change to existing behavior)

### Scenario 2: Multi-Material Mesh

**Given** a mesh with 3 submeshes:
- Submesh A: indexOffset=0, indexCount=1000, materialIndex=0 (wood)
- Submesh B: indexOffset=1000, indexCount=2000, materialIndex=1 (metal)
- Submesh C: indexOffset=3000, indexCount=1000, materialIndex=2 (glass)

**When** the mesh is submitted for rendering

**Then**
- 3 DrawCommands are emitted (one per submesh)
- Each DrawCommand has the correct indexOffset/indexCount/vertexOffset
- Each DrawCommand's SortKey contains its respective materialIndex (0, 1, or 2)
- All 3 draws are batched together in the render list (same mesh, different materials)
- When submitted to GPU: 3 separate DrawIndexedInstanced calls execute (DX12) or 3 vkCmdDrawIndexed calls (Vulkan) or 3 Metal drawIndexedPrimitives calls

### Scenario 3: UV1 Lightmap Channel Present

**Given** a vertex buffer with VertexFormatFlags = HasUV0 | HasUV1 (lightmap channel enabled)

**When** the DX12/Vulkan/Metal input layout is created via `BuildInputLayout(flags)`

**Then**
- D3D12: D3D12_INPUT_ELEMENT_DESC includes both TEXCOORD0 and TEXCOORD1
- Vulkan: VkVertexInputAttributeDescription includes binding for TEXCOORD1
- Metal: MTLVertexDescriptor includes an attribute for TEXCOORD1
- The vertex shader receives both `float2 uv0` and `float2 uv1`
- Stride is adjusted to include the extra 8 bytes per vertex

### Scenario 4: UV2 Absent (Not in Format)

**Given** a vertex buffer with VertexFormatFlags = HasUV0 (UV2 NOT set)

**When** the shader compiles

**Then**
- Input layout does NOT include TEXCOORD2 attribute
- Vertex shader has NO `float2 uv2` input
- If pixel shader attempts to use UV2, it receives (0,0) via permutation or shader logic
- The input layout stride does NOT include 8 bytes for uv2

### Scenario 5: glTF Import with Multiple Primitives

**Given** a glTF 2.0 mesh with 3 primitives:
- Primitive 0: 500 indices, material index 2 (red plastic), has TEXCOORD_0, no TEXCOORD_1
- Primitive 1: 800 indices, material index 5 (metal), has TEXCOORD_0 and TEXCOORD_1
- Primitive 2: 600 indices, material index 2 (red plastic), has TEXCOORD_0, no TEXCOORD_1

**When** the mesh is imported

**Then**
- MeshAsset.submeshes has 3 entries:
  - Submesh[0]: indexOffset=0, indexCount=500, materialIndex=2
  - Submesh[1]: indexOffset=500, indexCount=800, materialIndex=5
  - Submesh[2]: indexOffset=1300, indexCount=600, materialIndex=2
- VertexFormatFlags = HasUV0 | HasUV1 (because at least one primitive has TEXCOORD_1)
- Vertices for Submesh[1] use their imported TEXCOORD_1 data
- Vertices for Submesh[0] and [2] have UV1 set to (0,0) as padding (layout must be uniform)
- All vertices share the same stride based on combined format flags

### Scenario 6: DrawIndexed with Offsets — Cross-Backend Consistency

**Given** a DrawCommand with:
- indexOffset = 1000 (start at index 1000)
- indexCount = 500 (draw 500 indices)
- vertexOffset = 200 (base vertex is 200)

**When** executed on DX12

**Then** `DrawIndexedInstanced(500, 1, 1000, 200, 0)` is called
- indexCount (500) → first parameter
- vertexOffset (200) → fourth parameter (BaseVertexLocation)
- indexOffset (1000) → third parameter (StartIndexLocation)

**When** executed on Vulkan

**Then** `vkCmdDrawIndexed(500, 1, 1000, 200, 0)` is called
- indexCount (500) → first parameter
- indexOffset (1000) → third parameter (firstIndex)
- vertexOffset (200) → fourth parameter (vertexOffset)

**When** executed on Metal

**Then** `drawIndexedPrimitives()` is called with:
- `indexCount: 500`
- `indexBufferOffset: indexOffset * sizeof(uint32_t)` = 1000 * 4 = 4000 bytes
- `baseVertex: 200`

**Constraint**: All backends produce identical results: the same 500 indices, adjusted by baseVertex 200, are rasterized.

### Scenario 7: Backward Compatibility — Existing Single-Material Meshes

**Given** an existing mesh asset (pre-submesh) with 5000 vertices and 7500 indices (all using the same material)

**When** loaded by the new system

**Then**
- It is automatically wrapped as a single-submesh asset:
  - submeshes[0] = { indexOffset: 0, indexCount: 7500, vertexOffset: 0, materialIndex: 0 }
- VertexFormatFlags defaults to HasUV0 (no lightmap, no detail, no tangent)
- One DrawCommand is emitted (same as before)
- No performance change; system is backward compatible

### Scenario 8: Shader UV Channel Permutations

**Given** a mesh with VertexFormatFlags = HasUV0 | HasUV2 (NO UV1)

**When** mesh_ps.hlsl is compiled

**Then**
- The shader receives `float2 uv0` and `float2 uv2` in its input
- The shader does NOT receive `float2 uv1` (not in input layout)
- If pixel shader references `uv1`, linker error or permutation guards it out
- If mesh_ps.hlsl samples a lightmap, it must guard the access: `#if defined(HAS_UV1) // texture(samplerLightmap, uv1) #endif`

### Scenario 9: DrawCommand Sort Key with Submesh Material

**Given** a mesh with 2 submeshes from materials {wood=0x1234, glass=0x5678} being sorted

**When** draw calls are sorted for depth/material coherence

**Then**
- DrawCommand from submesh 0 has SortKey with material 0x1234
- DrawCommand from submesh 1 has SortKey with material 0x5678
- Draws may be reordered by depth but remain grouped by material
- Same-material submeshes (e.g., Submesh A and C both material 0, different meshes) are sorted together

## Constraints & Notes

1. **Uniform Stride**: All vertices in a mesh share the same stride (determined by VertexFormatFlags). Primitives with different UV channel presence are padded or promoted.
2. **Index Offsets**: indexOffset is in **index units** (not bytes). For uint32 indices, byte offset = indexOffset * 4.
3. **Vertex Offsets**: vertexOffset is **per-vertex** (not bytes).
4. **Material Index Locality**: materialIndex references the parent mesh's material array. A submesh may share the same material as other submeshes or have a unique one.
5. **Import Validation**: Importers must ensure all submesh index/vertex ranges are non-overlapping and within bounds.
6. **Deferred Rendering**: GBuffer passes write submesh-specific data; each submesh gets its own SortKey for deferred material bindings.
7. **Performance**: Drawing N submeshes = N DrawIndexedInstanced / vkCmdDrawIndexed calls. For < 10 submeshes per mesh, performance is equivalent to N separate meshes; for very high submesh counts (>50), consider LOD or mesh consolidation.

## Testing Requirements

### Unit Tests

1. **Submesh Struct Validation**
   - Create submesh with various offset/count combinations
   - Verify no overlaps for [0, 1000], [1000, 2000], [2000, 3000]
   - Verify bounds checking: reject indexOffset + indexCount > totalIndices

2. **VertexFormatFlags & Stride**
   - HasUV0 alone → stride = 32 (pos 12 + normal 12 + uv0 8)
   - HasUV0 | HasUV1 → stride = 40
   - HasUV0 | HasUV1 | HasUV2 → stride = 48
   - Tangent flag adds 12 more bytes

3. **Input Layout Creation**
   - DX12: Verify D3D12_INPUT_ELEMENT_DESC array length and AlignedByteOffsets
   - Vulkan: Verify VkVertexInputAttributeDescription count
   - Metal: Verify MTLVertexAttribute count in descriptor

### Integration Tests

1. **Multi-Material Mesh Rendering (Forward Path)**
   - Load a 3-submesh mesh, each with different material
   - Render in forward path
   - Verify 3 draw calls per frame
   - Verify correct material properties applied to each submesh

2. **Multi-Material Mesh Rendering (Deferred Path)**
   - Render same 3-submesh mesh via deferred (GBuffer + Lighting)
   - Verify GBuffer writes 3 distinct material IDs
   - Verify lighting reads correct material per pixel

3. **Multi-UV Channel Data Flow**
   - Create mesh with UV0 (main) and UV1 (lightmap)
   - Verify vertex buffer contains both channels
   - Verify shader receives both texcoords
   - Sample different textures from UV0 and UV1; verify visual correctness

4. **glTF Multi-Primitive Import**
   - Import glTF with 3 primitives, 2 with different materials, 1 sharing material 0
   - Verify submeshes created with correct ranges
   - Verify materials assigned to submeshes
   - Render and compare visual output to glTF spec

5. **Backward Compatibility**
   - Load existing single-material mesh (pre-submesh format)
   - Verify wrapped as 1-submesh asset
   - Render identical to original version
   - Verify no performance regression

## Success Criteria

- ✅ All 6 given/when/then scenarios execute correctly
- ✅ All unit tests pass (stride, bounds, layout generation)
- ✅ All integration tests pass (forward, deferred, multi-UV, glTF, backward compat)
- ✅ Cross-backend consistency: DX12, Vulkan, Metal produce identical output for same draw commands
- ✅ 95%+ coverage of submesh and vertex format code paths
