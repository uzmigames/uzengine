---
name: Multi-Material & Multi-UV Documentation (2026-03-13)
description: Created comprehensive design doc for submesh ranges, multi-material support, and multi-UV channels (Phase 3+)
type: observation
---

## Summary

Created `docs/engine/12-rendering/11-multi-material-multi-uv.md` (1,330 lines) documenting planned Phase 3+ multi-material and multi-UV channel support for UzEngine rendering pipeline.

## Content Coverage

### Architecture Sections
1. **Submesh Concept**: Division of mesh into indexed ranges, each with own material
2. **DrawCommand Extension**: Added indexOffset, indexCount, vertexOffset fields
3. **Submesh Descriptor**: Structure defining GPU draw parameters per material slot
4. **MeshAsset Storage**: Ownership of vertex/index buffers + submesh hierarchy

### Multi-UV System
1. **UV Channel Definition**: UV0 (primary), UV1 (lightmap), UV2 (detail), UV3 (reserved)
2. **Vertex Layout Impact**: VertexFormat flags controlling optional UV attributes
3. **Shader Integration**: HLSL VSInput matching per-vertex layout
4. **Packing Rules**: Dynamic stride computation based on VertexFormat flags

### Backend Support
- **DX12**: Input layout descriptors, DrawIndexedInstanced with baseVertex
- **Vulkan**: VkVertexInputAttributeDescription arrays, vkCmdDrawIndexed baseVertex
- **Metal**: MTLVertexDescriptor, drawIndexedPrimitives with baseVertex

### Asset Pipeline
- **glTF Import**: Primitives → submeshes, TEXCOORD_0/1/2 → VertexFormat channels
- **FBX Import**: Material slots → submesh ranges, LAYER 0/1/2 → VertexFormat channels
- **Importer Checklist**: 6 validation points for preserving structure

### Comparison Table
Engine comparison (UE5 vs Godot vs UzEngine) covering:
- Multi-material approach (FStaticMeshSection vs SurfaceArray vs Submesh ranges)
- UV channels (up to 8 vs 2 vs 4)
- Lightmap support (TEXCOORD1 vs UV2 vs UV1)
- Draw call granularity (per-section vs per-surface vs per-submesh)

## Rules Documented

7 critical rules for multi-material/multi-UV systems:
1. Never draw full mesh when submeshes exist
2. Always preserve submesh boundaries during import
3. Never assume UV1/UV2 exist — check VertexFormat flags
4. Always pad vertex stride to GPU alignment
5. Never hardcode vertex stride
6. Always validate index ranges
7. Always test multi-UV on all backends

## Key Design Decisions Explained

- **VertexFormat flags** (not runtime structs) for optimal layout packing
- **Base vertex offset** (all backends support) for efficient index ranging
- **Sort key unchanged** — change is in draw command generation (N commands per mesh)
- **Backwards compatible** — existing single-material meshes work unchanged

## README Updates

Updated `docs/engine/12-rendering/README.md`:
1. Added row to Section Overview table (line 25)
2. Added "Support multi-material meshes" navigation section (lines 61-65)
3. Updated File Manifest with new file reference (line 365)
4. Added ★★★ legend for Phase 3+ entries

## Code Examples

Comprehensive C++ code examples covering:
- Submesh iteration and DrawCommand generation
- Vertex layout struct with optional UV channels
- VertexFormat flags and stride computation
- Per-RHI backend draw call patterns
- glTF/FBX importer pseudo-code

All examples use Rust-style doc comments (///) and match CLAUDE.md standards.

## Status & Dependencies

- **Status**: Planned (Phase 3+)
- **Depends on**: Draw List (§04), PBR Material System (§06), RHI (§03), Mesh Import Pipeline
- **Phase roadmap** included: 3b-3.0b (ECS) through 3d+ (full multi-UV + deferred)
- **Related docs** cross-referenced: Sort keys, Material system, Forward PBR pass, Mesh import

## Style Consistency

Document follows existing rendering docs patterns:
- Purpose section explaining why needed
- Architecture diagrams (ASCII art)
- Struct layouts with byte offsets
- Engine comparison table
- Rules section with critical constraints
- Phase roadmap with dependencies
- Cross-references to related documentation
