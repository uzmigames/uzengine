## Mesh & Draw Command Layer
- [x] Add `Submesh` struct (indexOffset, indexCount, vertexOffset, materialIndex). Added SubMesh struct to mesh.h with startIndex, indexCount, materialIndex fields. Also submeshes vector in Mesh struct.
- [ ] Add `MeshAsset` with `std::vector<Submesh>` submesh storage
- [ ] Extend `DrawCommand` with indexOffset, indexCount, vertexOffset fields
- [ ] Update draw command generation to emit N commands per mesh (one per submesh)
- [ ] Update SortKey usage — each submesh gets its own sort key with its material

## Vertex Format & Multi-UV
- [ ] Define `VertexFormatFlags` bitmask (HasUV0, HasUV1, HasUV2, HasTangent)
- [ ] Define flexible `Vertex` struct with UV0 (required), UV1/UV2 (optional)
- [ ] Create `GetVertexStride(VertexFormatFlags)` helper
- [ ] Create `BuildInputLayout(VertexFormatFlags)` per-backend helper

## RHI Backend — DX12
- [ ] Update DX12 input layout to support TEXCOORD0/1/2
- [ ] Update DX12 DrawIndexedInstanced calls to use indexOffset/indexCount/vertexOffset
- [ ] Update DX12 PSO creation for variable vertex stride
- [ ] Add DX12 unit tests for multi-UV vertex layout

## RHI Backend — Vulkan
- [ ] Update VkVertexInputAttributeDescription for TEXCOORD0/1/2
- [ ] Update vkCmdDrawIndexed calls with offset/count parameters
- [ ] Update Vulkan pipeline vertex input state for variable stride
- [ ] Add Vulkan unit tests for multi-UV vertex layout

## RHI Backend — Metal
- [ ] Update MTLVertexDescriptor for UV0/UV1/UV2 attributes
- [ ] Update Metal drawIndexedPrimitives calls with offset/count
- [ ] Update Metal pipeline vertex format for variable stride
- [ ] Add Metal unit tests for multi-UV vertex layout

## Shader Updates
- [ ] Update mesh_vs.hlsl to pass UV0/UV1/UV2 through VSInput/PSInput
- [ ] Update mesh_ps.hlsl to sample textures using appropriate UV channel
- [ ] Update gbuffer_ps.hlsl for deferred path UV support
- [ ] Add shader permutation flag for UV1/UV2 presence
- [ ] Update fullscreen_vs.hlsl if needed for lightmap sampling

## Import Pipeline
- [ ] Update glTF importer to preserve submesh (primitive) boundaries
- [ ] Update glTF importer to import TEXCOORD_0, TEXCOORD_1 UV channels
- [ ] Update FBX importer (when available) for multi-material polygon groups
- [ ] Add importer unit tests for multi-submesh and multi-UV meshes

## Forward PBR Pass
- [ ] Update ForwardPBRPass to draw per-submesh instead of per-mesh
- [ ] Pass UV channel info to shader via per-object constant buffer

## Deferred Pipeline
- [ ] Update GBufferPass for per-submesh drawing
- [ ] Update GBuffer shader to write UV1 lightmap data if present
- [ ] Update LightingPass to read lightmap UV if available

## Documentation
- [ ] Write docs/engine/12-rendering/11-multi-material-multi-uv.md
- [ ] Update docs/engine/12-rendering/06-pbr-material-system.md with submesh references
- [ ] Update docs/engine/12-rendering/07-sort-key-architecture.md with multi-material notes

## Tests
- [ ] Unit test: Submesh struct validation
- [ ] Unit test: DrawCommand with submesh fields
- [ ] Unit test: VertexFormatFlags stride calculation
- [ ] Integration test: multi-material mesh rendering (forward path)
- [ ] Integration test: multi-material mesh rendering (deferred path)
- [ ] Integration test: multi-UV channel data flow
