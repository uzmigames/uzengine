<!-- doc-meta
title: Implementing Normal Smoothing in UzEngine
description: Step-by-step guide to add UE5-style normal smoothing and tangent generation to UzEngine's mesh import pipeline.
keywords: mesh-import, normals, tangents, implementation, FBX, glTF
tags: guide, rendering, import-pipeline
-->

# Implementing Normal Smoothing in UzEngine

## Quick Reference

**Status: IMPLEMENTED** (v0.7.0). UzEngine now computes smooth normals via area+angle weighted averaging and MikkTSpace tangents during FBX/glTF import.

### Implementation Summary

```
Current behavior (v0.7.0+):
- FBX: ComputeSmoothNormals() with area×angle weighting + 60° hard edge threshold
- glTF: preserves imported normals (from Blender/Maya), computes only when missing
- Tangents: MikkTSpace (industry standard, matches UE5/Unity/Blender)
- CVars: r.Mesh.NormalImportMode, r.Mesh.SmoothingAngle, r.Mesh.TangentMode

Key files:
- engine/asset/mesh/normal_smoother.h/.cpp — smooth normal algorithm
- engine/asset/mesh/mikktspace_tangents.h/.cpp — MikkTSpace wrapper
- engine/asset/mesh/packed_normal.h — PackedNormal/PackedTangent types (int8x4)

Required behavior:
- Vertices at same position → merged for normal calculation
- Vertex normal = weighted average of adjacent triangles
- Soft edges (similar face angles) → normals smoothed
- Hard edges (sharp angles) → normals separate
- Result: smooth surfaces except at intentional hard edges
```

### Visual Example

**Before (faceted):**
```
Sphere with flat-shaded triangles, each triangle clearly visible
```

**After (smoothed):**
```
Sphere appears smooth, light gradients smoothly across surface
```

---

## Architecture Overview

UzEngine's mesh system (current state, 2026-04-04):

```
FBX/glTF file
    ↓
[Import] → Raw vertex/index data (FBX SDK or assimp)
    ↓
[?] ← Currently: nothing. Normals = face normals
    ↓
Vertex buffer (Position, Normal, Tangent, UV)
    ↓
GPU render
```

**After implementing normal smoothing:**

```
FBX/glTF file
    ↓
[Import] → Raw vertex/index data
    ↓
[Mesh Builder Pipeline]
  - Compute triangle normals (area-weighted)
  - Detect hard edges (angle threshold + smoothing groups)
  - Average normals per vertex (weighted by area × angle)
  - Generate tangents (MikkTSpace or built-in)
  - Pack normals/tangents
    ↓
Vertex buffer (Position, Normal_packed, Tangent_packed, BinormalSign, UV)
    ↓
GPU render (unpack and decode)
```

---

## Implementation Plan

### Phase 1: Normal Averaging (Minimum Viable)

**Scope:** Basic per-vertex normal smoothing without weighting

**Time:** 2-3 hours

**Files to create/modify:**
- `runtime/src/engine/mesh/mesh_builder.h` — main API
- `runtime/src/engine/mesh/mesh_builder.cpp` — implementation
- `runtime/tests/test_mesh_builder.cpp` — unit tests

**Algorithm:**
```cpp
struct Vertex {
    Vec3 position;
    Vec3 normal;
};

struct MeshBuilder {
    // Input: raw vertices and indices
    void BuildNormals(
        const span<const uint32_t> indices,
        span<Vertex> vertices)
    {
        // 1. Compute per-face normals
        vector<Vec3> face_normals(indices.size() / 3);
        for (size_t i = 0; i < indices.size(); i += 3) {
            uint32_t a = indices[i], b = indices[i+1], c = indices[i+2];
            Vec3 edge1 = vertices[b].position - vertices[a].position;
            Vec3 edge2 = vertices[c].position - vertices[a].position;
            face_normals[i/3] = cross(edge1, edge2).normalize();
        }
        
        // 2. Clear vertex normals
        for (auto& v : vertices) v.normal = Vec3::zero();
        
        // 3. Accumulate face normals per vertex
        for (size_t i = 0; i < indices.size(); i += 3) {
            uint32_t a = indices[i], b = indices[i+1], c = indices[i+2];
            Vec3 face_normal = face_normals[i/3];
            vertices[a].normal += face_normal;
            vertices[b].normal += face_normal;
            vertices[c].normal += face_normal;
        }
        
        // 4. Normalize
        for (auto& v : vertices) v.normal = v.normal.normalize();
    }
};
```

**Testing:**
```cpp
TEST(MeshBuilder, NormalSmoothing) {
    // Create a simple pyramid (4 triangles sharing 1 apex)
    vector<Vertex> vertices = {
        // Apex (shared by 4 faces)
        {Vec3(0, 1, 0), Vec3::zero()},
        // Base vertices
        {Vec3(1, 0, 0), Vec3::zero()},
        {Vec3(0, 0, 1), Vec3::zero()},
        {Vec3(-1, 0, 0), Vec3::zero()},
        {Vec3(0, 0, -1), Vec3::zero()},
    };
    
    vector<uint32_t> indices = {
        // 4 triangles, all sharing vertex 0
        0, 1, 2,
        0, 2, 3,
        0, 3, 4,
        0, 4, 1,
    };
    
    MeshBuilder builder;
    builder.BuildNormals(indices, vertices);
    
    // Apex normal should be average of 4 face normals
    Vec3 apex_normal = vertices[0].normal;
    EXPECT_NEAR(apex_normal.length(), 1.0f, 0.01f);  // Normalized
    // Direction should point roughly up-ish (blend of 4 face normals)
}
```

### Phase 2: Area-Weighted and Corner-Angle-Weighted Averaging

**Scope:** Add weighting for higher quality smoothing

**Time:** 1-2 hours (build on Phase 1)

**Code changes:**
```cpp
// In MeshBuilder::BuildNormals
for (size_t i = 0; i < indices.size(); i += 3) {
    uint32_t a = indices[i], b = indices[i+1], c = indices[i+2];
    Vec3 p0 = vertices[a].position;
    Vec3 p1 = vertices[b].position;
    Vec3 p2 = vertices[c].position;
    
    Vec3 edge1 = p1 - p0;
    Vec3 edge2 = p2 - p0;
    Vec3 face_normal = cross(edge1, edge2);
    
    // Weight by area (magnitude of cross product ∝ area)
    float area = face_normal.length() * 0.5f;
    face_normal = face_normal.normalize();
    
    // Compute corner angles
    float angle_a = ComputeCornerAngle(p0, p1, p2);
    float angle_b = ComputeCornerAngle(p1, p2, p0);
    float angle_c = ComputeCornerAngle(p2, p0, p1);
    
    // Accumulate with weights
    vertices[a].normal += face_normal * (area * angle_a);
    vertices[b].normal += face_normal * (area * angle_b);
    vertices[c].normal += face_normal * (area * angle_c);
}

// Helper
float ComputeCornerAngle(Vec3 vertex, Vec3 edge1_endpoint, Vec3 edge2_endpoint) {
    Vec3 edge1 = (edge1_endpoint - vertex).normalize();
    Vec3 edge2 = (edge2_endpoint - vertex).normalize();
    float dot_product = clamp(dot(edge1, edge2), -1.0f, 1.0f);
    return acos(dot_product);  // Returns angle in radians [0, π]
}
```

### Phase 3: Hard Edge Detection

**Scope:** Mark edges as "hard" so normals don't smooth across them

**Time:** 1-2 hours

**Algorithm:**
```cpp
struct Edge {
    uint32_t vertex_a, vertex_b;
    vector<uint32_t> adjacent_triangles;
    bool is_hard = false;
};

void BuildHardEdges(
    const span<const uint32_t> indices,
    span<Vertex> vertices,
    float angle_threshold = 60.0f)  // Degrees
{
    // 1. Build edge → triangles map
    map<pair<uint32_t, uint32_t>, vector<uint32_t>> edges;
    
    for (size_t tri_idx = 0; tri_idx < indices.size(); tri_idx += 3) {
        uint32_t a = indices[tri_idx];
        uint32_t b = indices[tri_idx + 1];
        uint32_t c = indices[tri_idx + 2];
        
        // Normalize edge (min, max) to avoid duplicate keys
        auto add_edge = [&](uint32_t v1, uint32_t v2) {
            if (v1 > v2) swap(v1, v2);
            edges[{v1, v2}].push_back(tri_idx / 3);
        };
        
        add_edge(a, b);
        add_edge(b, c);
        add_edge(c, a);
    }
    
    // 2. Compute per-face normals (from Phase 2)
    vector<Vec3> face_normals = ComputeFaceNormals(indices, vertices);
    
    // 3. Check each edge
    float cos_threshold = cos(radians(angle_threshold));
    
    for (const auto& [edge_key, adjacent_tris] : edges) {
        if (adjacent_tris.size() != 2) {
            // Boundary edge → always hard
            hard_edges.insert(edge_key);
            continue;
        }
        
        Vec3 normal_a = face_normals[adjacent_tris[0]];
        Vec3 normal_b = face_normals[adjacent_tris[1]];
        
        float dot_product = dot(normal_a, normal_b);
        
        if (dot_product < cos_threshold) {
            // Angle > threshold → hard edge
            hard_edges.insert(edge_key);
        }
    }
}
```

**Testing:**
```cpp
TEST(MeshBuilder, HardEdgeDetection) {
    // Create two triangles at 90° angle (hard edge)
    vector<Vertex> vertices = {
        {Vec3(0, 0, 0), Vec3::zero()},
        {Vec3(1, 0, 0), Vec3::zero()},
        {Vec3(0, 1, 0), Vec3::zero()},
        {Vec3(0, 0, 1), Vec3::zero()},
    };
    
    vector<uint32_t> indices = {
        0, 1, 2,  // Triangle in XY plane
        0, 1, 3,  // Triangle in XZ plane (90° angle)
    };
    
    set<pair<uint32_t, uint32_t>> hard_edges;
    BuildHardEdges(indices, vertices, 45.0f);  // 45° threshold
    
    // Edge (0, 1) should be hard (90° > 45°)
    EXPECT_TRUE(hard_edges.count({0, 1}));
}
```

### Phase 4: Tangent Generation (MikkTSpace)

**Scope:** Integrate MikkTSpace library for production-quality tangent generation

**Time:** 3-4 hours (includes dependency setup)

**Setup:**
```bash
# 1. Add MikkTSpace to build.zig
const mikktspace = b.dependency("mikktspace", .{
    .target = target,
    .optimize = optimize,
});
exe.linkLibrary(mikktspace);
exe.root_module.addImport("mikktspace", mikktspace.module("mikktspace"));
```

**Integration:**
```cpp
#include "mikktspace.h"

struct MikkTSpaceContext {
    const span<const uint32_t> indices;
    const span<const Vertex> vertices;
    span<Vec3> out_tangents;
    span<float> out_bitangent_signs;
};

// MikkTSpace callbacks
static int GetNumFaces(const SMikkTSpaceContext* pContext) {
    MikkTSpaceContext* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
    return ctx->indices.size() / 3;
}

static int GetNumVerticesOfFace(const SMikkTSpaceContext* pContext, int iFace) {
    return 3;  // Always triangles
}

static void GetPosition(const SMikkTSpaceContext* pContext, float fPos[], int iFace, int iVert) {
    MikkTSpaceContext* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
    uint32_t vertex_idx = ctx->indices[iFace * 3 + iVert];
    Vec3 pos = ctx->vertices[vertex_idx].position;
    fPos[0] = pos.x;
    fPos[1] = pos.y;
    fPos[2] = pos.z;
}

static void GetNormal(const SMikkTSpaceContext* pContext, float fNorm[], int iFace, int iVert) {
    MikkTSpaceContext* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
    uint32_t vertex_idx = ctx->indices[iFace * 3 + iVert];
    Vec3 norm = ctx->vertices[vertex_idx].normal;
    fNorm[0] = norm.x;
    fNorm[1] = norm.y;
    fNorm[2] = norm.z;
}

static void GetTexCoord(const SMikkTSpaceContext* pContext, float fUV[], int iFace, int iVert) {
    MikkTSpaceContext* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
    uint32_t vertex_idx = ctx->indices[iFace * 3 + iVert];
    Vec2 uv = ctx->vertices[vertex_idx].uv;  // Assume UV is in Vertex
    fUV[0] = uv.x;
    fUV[1] = uv.y;
}

static void SetTSpaceBasic(const SMikkTSpaceContext* pContext,
                          const float fTangent[3], float fSign,
                          int iFace, int iVert) {
    MikkTSpaceContext* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
    uint32_t vertex_idx = ctx->indices[iFace * 3 + iVert];
    ctx->out_tangents[vertex_idx] = Vec3(fTangent[0], fTangent[1], fTangent[2]);
    ctx->out_bitangent_signs[vertex_idx] = -fSign;  // UE5 negates the sign
}

void ComputeTangentsMikkTSpace(
    const span<const uint32_t> indices,
    span<Vertex> vertices)
{
    vector<Vec3> tangents(vertices.size());
    vector<float> bitangent_signs(vertices.size());
    
    MikkTSpaceContext ctx = {
        .indices = indices,
        .vertices = vertices,
        .out_tangents = tangents,
        .out_bitangent_signs = bitangent_signs,
    };
    
    SMikkTSpaceInterface interface = {
        .m_getNumFaces = GetNumFaces,
        .m_getNumVerticesOfFace = GetNumVerticesOfFace,
        .m_getPosition = GetPosition,
        .m_getNormal = GetNormal,
        .m_getTexCoord = GetTexCoord,
        .m_setTSpaceBasic = SetTSpaceBasic,
        .m_setTSpace = nullptr,
    };
    
    SMikkTSpaceContext mikktspace_context = {
        .m_pInterface = &interface,
        .m_pUserData = &ctx,
        .m_bIgnoreDegenerates = true,
    };
    
    genTangSpaceDefault(&mikktspace_context);
    
    // Copy tangents back to vertices
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].tangent = tangents[i];
        vertices[i].bitangent_sign = bitangent_signs[i];
    }
}
```

### Phase 5: Integration with FBX Importer

**Scope:** Wire normal smoothing into the existing FBX import code

**Time:** 2-3 hours

**Current importer location:** (search UzEngine codebase for FBX loader)

**Integration point:**
```cpp
// After FBX SDK loads raw mesh data, before packing into GPU vertex buffer
void ImportFBX(const char* filename, Mesh& out_mesh) {
    // ... existing FBX SDK code ...
    
    vector<uint32_t> indices = /* from FBX */;
    vector<Vertex> vertices = /* from FBX */;
    
    // NEW: Build normals and tangents
    MeshBuilder builder;
    builder.BuildNormals(indices, vertices);  // Phase 1
    builder.AddAreaAndAngleWeighting(indices, vertices);  // Phase 2
    builder.DetectHardEdges(indices, vertices);  // Phase 3
    builder.ComputeTangentsMikkTSpace(indices, vertices);  // Phase 4
    
    // ... pack into GPU vertex buffer ...
}
```

### Phase 6: GPU Decoding and Shader Integration

**Scope:** Unpack normals and tangents in shaders

**Time:** 1-2 hours

**Vertex layout change:**
```cpp
// Before (WRONG):
struct VertexData {
    Vec3 position;
    Vec3 normal;        // ← 12 bytes
    Vec3 tangent;       // ← 12 bytes
    Vec2 uv;            // ← 8 bytes
};

// After (CORRECT):
struct VertexData {
    Vec3 position;           // 12 bytes
    FPackedNormal normal;    // 4 bytes (int8×4)
    FPackedNormal tangent;   // 4 bytes (int8×4)
    float bitangent_sign;    // 4 bytes
    Vec2 uv;                 // 8 bytes
};  // Total: 32 bytes (was 44 bytes) — 27% smaller!
```

**Shader decoding:**
```hlsl
// Input to pixel shader
struct FSInput {
    float3 position : SV_Position;
    uint normal_packed : NORMAL0;       // FPackedNormal (uint32)
    uint tangent_packed : TANGENT0;     // FPackedNormal (uint32)
    float bitangent_sign : TANGENT1;
    float2 uv : TEXCOORD0;
};

// Unpack function
float3 UnpackNormal(uint packed) {
    // Extract as signed int8
    int4 packed_int = int4(
        (packed >> 0) & 0xFF,
        (packed >> 8) & 0xFF,
        (packed >> 16) & 0xFF,
        (packed >> 24) & 0xFF
    );
    
    // Sign-extend (int8 → int32)
    int4 signed_int = int4(
        (packed_int.x << 24) >> 24,
        (packed_int.y << 24) >> 24,
        (packed_int.z << 24) >> 24,
        (packed_int.w << 24) >> 24
    );
    
    // Normalize to [-1, 1]
    return float3(signed_int.xyz) / 127.0;
}

// Main shader function
float4 main(FSInput input) : SV_Target {
    float3 normal = UnpackNormal(input.normal_packed);
    float3 tangent = UnpackNormal(input.tangent_packed);
    float3 bitangent = cross(normal, tangent) * input.bitangent_sign;
    
    // TBN matrix for normal mapping
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    
    // ... use tbn for normal mapping ...
}
```

---

## Testing Strategy

### Unit Tests (Phase 1-4)

```cpp
// Test file: runtime/tests/test_mesh_builder.cpp
TEST_SUITE("MeshBuilder") {
    TEST("NormalSmoothing_Sphere") {
        // Create an icosphere (smooth surface)
        auto vertices = CreateIcosphere(subdivisions=2);
        
        MeshBuilder builder;
        builder.BuildNormals(vertices.indices, vertices.data);
        
        // Check that all vertex normals point outward from center
        for (const auto& v : vertices.data) {
            Vec3 to_center = -v.position.normalize();
            EXPECT_GT(dot(v.normal, v.position.normalize()), 0.9f);
            // Normals should point roughly outward
        }
    }
    
    TEST("AreaWeighting_SpikyTriangle") {
        // A large triangle + a tiny triangle sharing one edge
        // The large triangle should dominate the shared vertex normal
        vector<Vertex> vertices = { /* ... */ };
        
        MeshBuilder builder;
        builder.BuildNormals(...);
        
        // Shared vertex normal should align more with large triangle
        // than with tiny triangle
    }
    
    TEST("HardEdges_Cube") {
        // A cube has 90° angles between faces → all edges hard
        auto cube = CreateCube();
        
        set<pair<uint32_t, uint32_t>> hard_edges;
        BuildHardEdges(cube.indices, cube.vertices, angle_threshold=45.0f);
        
        // Should find 12 hard edges (one per cube edge)
        EXPECT_EQ(hard_edges.size(), 12);
        
        // Vertex normals should not smooth across hard edges
        // (Each vertex has multiple normals, one per face)
    }
    
    TEST("Tangents_MikkTSpace_UVSeam") {
        // A box with UVs that seam across one edge
        // MikkTSpace should align tangents to UV direction, not geometry
        auto box = CreateBox();
        
        ComputeTangentsMikkTSpace(...);
        
        // Tangents on seamed edge should point along UV0, not geometry
    }
}
```

### Visual Tests (Phase 5-6)

```bash
# 1. Import a test mesh (e.g., sphere, skull)
uzengine --import test_sphere.fbx

# 2. Render with before/after
uzengine --screenshot before.png  # With old faceted normals
uzengine --screenshot after.png   # With smoothed normals

# 3. PSNR comparison (should be similar, just smoother)
compare_images before.png after.png  # Visual inspection
```

---

## Migration Checklist

- [ ] **Phase 1:** Implement `MeshBuilder::BuildNormals()`
- [ ] **Phase 1:** Write unit tests for normal averaging
- [ ] **Phase 2:** Add area and angle weighting
- [ ] **Phase 2:** Update unit tests with weighting checks
- [ ] **Phase 3:** Implement hard edge detection
- [ ] **Phase 3:** Unit test hard edges on cube, sphere with hard edges
- [ ] **Phase 4:** Integrate MikkTSpace library (add to build.zig)
- [ ] **Phase 4:** Implement MikkTSpace callbacks
- [ ] **Phase 4:** Unit test tangent alignment with UV0
- [ ] **Phase 5:** Wire into FBX importer entry point
- [ ] **Phase 5:** Update FBX import UI options (if applicable)
- [ ] **Phase 6:** Change vertex layout to use FPackedNormal
- [ ] **Phase 6:** Implement shader unpacking function
- [ ] **Phase 6:** Update all shader code to use new vertex format
- [ ] **Phase 6:** Test visual output (sphere, cube, complex mesh)
- [ ] **Documentation:** Write "Normal Import" guide for users
- [ ] **Documentation:** Document import options

---

## Performance Considerations

| Phase | CPU Time (1M tris) | GPU Memory Saved | Notes |
|-------|-------------------|------------------|-------|
| Current (no smoothing) | ~10ms | 44 bytes/vertex | Fast but ugly |
| Phase 1 (basic averaging) | ~50ms | 44 bytes/vertex | Simple, good quality |
| Phase 2 (weighted) | ~80ms | 44 bytes/vertex | Small overhead |
| Phase 3 (hard edges) | ~120ms | 44 bytes/vertex | Negligible perf cost |
| Phase 4 (MikkTSpace) | ~200ms | 44 bytes/vertex | Slowest phase |
| Phase 6 (packed normals) | ~200ms | 32 bytes/vertex | 27% smaller! |

**Recommendation:** All phases are acceptable. Import is one-time cost (not frame-critical). GPU memory savings from Phase 6 worth the effort.

---

## References

- **UE5 source:** `Engine/Source/Runtime/StaticMeshDescription/Private/StaticMeshOperations.cpp`
- **MikkTSpace:** https://github.com/mmikk/MikkTSpace
- **Paper:** "Mikktspace" by Morten Mikkelsen
- **Guide in this repo:** `docs/analysis/mesh/ue5-normal-smoothing-and-tangent-generation.md`
