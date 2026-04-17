<!-- doc-meta
title: Godot 4 Normal Smoothing & Tangent Generation Analysis
desc: Deep analysis of how Godot 4 computes smooth normals, handles tangent generation, and manages normal imports
-->

# Godot 4: Normal Smoothing, Tangent Generation, and Vertex Format

## Executive Summary

Godot 4 fixes faceted/flat shading on imported meshes through **smoothing groups** — a per-vertex bitmask that controls which adjacent face normals are averaged during computation. This design is simpler than UE5's edge-based approach but powerful: vertices in the same smoothing group at the same spatial position will have their normals averaged.

**Key Discoveries:**
1. **Smoothing group mechanism** (UINT32): vertices with matching group ID have normals averaged (via Plane normal averaging)
2. **No built-in smoothing angle threshold** — smoothing is all-or-nothing per group (no 60°/80° checks)
3. **Tangent generation uses mikktspace** — industry-standard library for TBN computation
4. **Normals stored uncompressed** (Vector3) — glTF imports preserve source normals directly, no recompute
5. **No runtime normal smoothing** — all smoothing happens at import or via SurfaceTool.generate_normals()

---

## 1. Normal Computation: The Smoothing Group System

### Algorithm: SurfaceTool::generate_normals()

**File:** `scene/resources/surface_tool.cpp:1187`

```cpp
void SurfaceTool::generate_normals(bool p_flip) {
    // 1. Deindex mesh (expand indexed array into per-face copies of vertices)
    bool was_indexed = index_array.size();
    deindex();
    
    // 2. Create hash: vertex_position + smooth_group → summed normal
    AHashMap<SmoothGroupVertex, Vector3, SmoothGroupVertexHasher> smooth_hash;
    
    // 3. For each triangle, compute face normal
    for (uint32_t vi = 0; vi < vertex_array.size(); vi += 3) {
        Vector3 normal = Plane(v[0].vertex, v[1].vertex, v[2].vertex).normal;
        
        // 4. Add face normal to hash IF vertex is in a smoothing group
        for (int i = 0; i < 3; i++) {
            if (v[i].smooth_group != UINT32_MAX) {  // UINT32_MAX = no smoothing
                Vector3 *lv = smooth_hash.getptr(v[i]);
                if (!lv) {
                    smooth_hash.insert_new(v[i], normal);
                } else {
                    (*lv) += normal;  // Accumulate (UNWEIGHTED sum of face normals)
                }
            } else {
                v[i].normal = normal;  // No smoothing group → flat normal
            }
        }
    }
    
    // 5. Normalize accumulated normals
    for (Vertex &vertex : vertex_array) {
        if (vertex.smooth_group != UINT32_MAX) {
            Vector3 *lv = smooth_hash.getptr(vertex);
            vertex.normal = lv->normalized();
        }
    }
    
    if (was_indexed) index();
}
```

### Key Points

#### 1a. No Weighting — Unweighted Face Normal Averaging

Normals are summed **without** weighting by:
- Triangle area
- Angle at the vertex
- Shared edge length

This is **simpler than UE5** which uses angle-weighted averaging:
```
UE5: N = sum(angle_i * face_normal_i) / sum(angle_i)
Godot: N = sum(face_normal_i) / ||sum(face_normal_i)||
```

**Trade-off:** Unweighted averaging can produce slightly biased normals on meshes with uneven triangle sizes, but is faster and easier to implement.

#### 1b. Smoothing Group Mechanism: All-or-Nothing

**Smoothing group** = `uint32_t` bitmask per vertex (despite the name, it's actually a category ID, not a bitfield).

- **Same position + same smooth_group** → normals averaged
- **Same position + different smooth_group** → flat normal
- **smooth_group == UINT32_MAX** → always flat (no smoothing)

```
Example: Two faces meet at an edge
Face A: N = (0, 0, 1)
Face B: N = (0.7, 0, 0.7)

Vertex at edge with smooth_group=1 on both faces:
  → Smoothed: (0.7, 0, 1.7).normalize() ≈ (0.38, 0, 0.92)

Vertex at edge with smooth_group=1 (Face A) and smooth_group=2 (Face B):
  → Flat: stays as assigned (e.g., Face A's normal)
```

#### 1c. No Smoothing Angle Threshold

**Godot does NOT implement angle-based smoothing** (like FBX's "crease angle").

- Import settings do not expose a "smoothing angle" slider
- Smoothing groups are explicit (set by DCC tool or importer)
- Hard edges require different smoothing groups

This contrasts with UE5:
```
UE5: smooth only if angle between faces < threshold (e.g., 60°)
Godot: smooth only if smooth_group is identical
```

---

## 2. Smoothing Groups in the Import Pipeline

### 2.1 FBX/Blender Import

**File:** `scene/resources/3d/importer_mesh.cpp`

When importing FBX/Blender:
1. **Smoothing groups extracted from source** (Blender: auto-compute from face `smooth` flag; FBX: explicit smoothing group ID)
2. **Stored in Vertex::smooth_group** during surface_tool processing
3. **SurfaceTool::generate_normals() called after import**

**Godot's approach:**
```
FBX Importer → set smooth_group on each vertex based on source groups
              → SurfaceTool::generate_normals(false) called
              → normals computed via group averaging
```

The `smooth_group` is set by:
```cpp
void SurfaceTool::set_smooth_group(uint32_t p_group) {
    last_smooth_group = p_group;  // Applies to next add_vertex() call
}
```

### 2.2 glTF Import

**File:** `modules/gltf/gltf_document.cpp:1527-1528`

glTF handling is **different**:
```cpp
if (a.has("NORMAL")) {
    array[Mesh::ARRAY_NORMAL] = _decode_accessor_as_vec3(p_state, a["NORMAL"], indices_mapping);
    // Normals are imported AS-IS, no recomputation
}
```

**Key difference:** glTF **does not re-generate normals** if they are present.

- If glTF has `NORMAL` accessor → use directly
- If glTF lacks `NORMAL` accessor → **no auto-generation**
  - (Unlike FBX, which always triggers SurfaceTool::generate_normals())

To force generation in glTF, set importer flag:
```cpp
if (p_state->force_generate_tangents && !a.has("TANGENT") && a.has("NORMAL")) {
    // Tangents will be generated, but NOT normals
}
```

### 2.3 When Normals Are NOT Imported

If a mesh lacks pre-computed normals:
- **FBX:** Importer calls `SurfaceTool::generate_normals(flip=false)` → computes flat per-face normals
- **glTF:** Normals remain absent → shader must provide fallback or error

---

## 3. Tangent Generation: mikktspace Integration

### 3.1 Algorithm: SurfaceTool::generate_tangents()

**File:** `scene/resources/surface_tool.cpp:1156`

```cpp
void SurfaceTool::generate_tangents() {
    // Requires UV0 and normals to exist
    ERR_FAIL_COND_MSG(!(format & Mesh::ARRAY_FORMAT_TEX_UV), "UVs are required");
    ERR_FAIL_COND(!(format & Mesh::ARRAY_FORMAT_NORMAL));
    
    // Use Mikkelsen's tangent space library (mikktspace)
    SMikkTSpaceInterface mkif;
    mkif.m_getNormal = mikktGetNormal;
    mkif.m_getNumFaces = mikktGetNumFaces;
    mkif.m_getNumVerticesOfFace = mikktGetNumVerticesOfFace;
    mkif.m_getPosition = mikktGetPosition;
    mkif.m_getTexCoord = mikktGetTexCoord;
    mkif.m_setTSpace = mikktSetTSpaceDefault;  // Default: use Frisvad's TBN method
    
    SMikkTSpaceContext msc;
    msc.m_pInterface = &mkif;
    
    TangentGenerationContextUserData triangle_data;
    triangle_data.vertices = &vertex_array;
    triangle_data.indices = &index_array;
    msc.m_pUserData = &triangle_data;
    
    bool res = genTangSpaceDefault(&msc);  // Compute tangents and sign
    
    format |= Mesh::ARRAY_FORMAT_TANGENT;
}
```

### 3.2 mikktspace: The Industry Standard

**What is mikktspace?**
- Created by Mikkelsen (Blender dev) in ~2008
- Computes tangent and bitangent from normal + UV layout
- Ensures **consistency across platforms** (same T/B for same mesh)
- Used by: Blender, Unreal, Unity, Godot, Substance Painter, etc.

**Algorithm:**
1. For each triangle, compute edge vectors in 3D and UV space
2. Solve for tangent direction that aligns with U axis of UV space
3. **Ortho-normalize** T against N (Gram-Schmidt)
4. Compute bitangent sign from `cross(N, T)`

**Key property:** Tangent always perpendicular to normal (by construction).

### 3.3 When Tangents Are Auto-Generated

**Tangents are generated if:**
- Importer: `force_generate_tangents = true` in GLTFState
- Normal map enabled on material
- SurfaceTool.generate_tangents() called explicitly

**Tangents are imported if:**
- glTF has `TANGENT` accessor (4D: tx, ty, tz, bitangent_sign)
- FBX has tangent/bitangent data

### 3.4 Tangent Storage: 4D Vector + Sign

Godot stores tangents as **4 floats per vertex**:
```glsl
tangent.xyz = T  // Tangent vector
tangent.w   = ±1 // Bitangent sign (1 or -1)

// In shader:
vec3 T = tangent.xyz;
vec3 B = tangent.w * cross(normal, T);  // Reconstruct bitangent
vec3 N = normal;
```

This saves 4 bytes per vertex vs storing full bitangent separately:
```
UE5:  12 bytes (T) + 12 bytes (B) = 24 bytes
Godot: 12 bytes (T) + 4 bytes (sign) = 16 bytes (stored as float4)
```

---

## 4. Vertex Format and Normal Encoding

### 4.1 Normal Storage: Full Precision Vector3

**Godot stores normals as:** `Vector<Vector3>` (3 × float32)
- **No compression** by default
- **No octahedral encoding**
- **Full precision** (24 bits per component after float32 precision loss)

**File:** `servers/rendering/rendering_server_enums.h`

```
ARRAY_FORMAT_NORMAL = bit(1)  // Signal that normals are present
```

### 4.2 Optional Compression: Attribute Compression

Godot **can optionally compress** all attributes (normals, tangents, etc.) to octahedral encoding:

```cpp
// From importer_mesh.cpp, line 1766:
if ((flags & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES) && a.has("NORMAL") && a.has("TANGENT")) {
    // Compress normals to octa-encoded format (2 × int8 per normal)
}
```

**Octahedral encoding:**
- Encodes normalized direction as 2 bytes (signed int8 pair)
- Maps sphere to 2D octahedron surface
- Precision: ~0.4° angular error at 256² resolution
- Reduces normal data by **75%** (12 bytes → 2 bytes)

**When compression is used:**
- DX11/DX12 devices with attribute compression support enabled
- Flag: `ARRAY_FLAG_COMPRESS_ATTRIBUTES` must be set during mesh creation
- Shader decodes via `decompress_normal()` function

---

## 5. Normal Precision and Encoding in Shaders

### 5.1 Compressed Normal Decode

```hlsl
// From rendering backends (RenderingDevice::_resource_bind_shader)
Vec2 packed_normal = sample_compressed_texture();  // 2D texture sample or vertex attribute
Vec3 normal = octahedral_decode(packed_normal);   // Decode to normalized Vec3
```

### 5.2 Uncompressed Path

```hlsl
Vec3 normal = input.normal;  // Direct from vertex attribute, already normalized
```

### 5.3 Quality Impact: Compressed vs Uncompressed

| Aspect | Uncompressed | Octahedral |
|--------|-------------|-----------|
| Storage | 12 bytes/vertex | 2 bytes/vertex |
| Angular error | ~0.001° | ~0.4° |
| Visual impact | Imperceptible | Imperceptible on most meshes |
| Requires shader decode | No | Yes |

**Recommendation for UzEngine:**
- Use uncompressed normals for high-quality renders
- Use octahedral compression for LOD meshes or mobile targets

---

## 6. Mesh Import Options: What Godot Exposes

### 6.1 FBX/OBJ Importer Settings

When importing FBX/OBJ files, Godot exposes:

```
Meshes → Ensure Tangents: boolean
         ↓ Forces SurfaceTool::generate_tangents() if tangents missing
         
Meshes → Force Discrete Normals: boolean
         ↓ Disables smoothing (sets all smooth_group = UINT32_MAX)
```

**There is NO "smoothing angle" slider** — smoothing groups are implicit.

### 6.2 glTF Importer Settings

```
Gltf → Generate LODs: boolean
       ↓ Creates LOD variants
       
Gltf → Force Generate Tangents: boolean
       ↓ Generate tangents even if glTF provides them
```

### 6.3 Why Godot Doesn't Expose "Smoothing Angle"

1. **Import pipeline philosophy:** Trust source data (Blender/Maya already computed smoothing groups)
2. **DCC tools define smoothing:** Blender's "Smooth" flag or FBX's explicit groups
3. **Simplicity:** Avoid artist confusion with per-import angle parameters

---

## 7. Runtime Normal Handling

### 7.1 MeshDataTool: Modify Normals at Runtime

**File:** `scene/resources/mesh_data_tool.cpp`

`MeshDataTool` allows reading/writing individual vertex data:

```gdscript
# Godot script example
var tool = MeshDataTool.new()
tool.create_from_surface(mesh, 0)

# Read and modify normals
for i in range(tool.get_vertex_count()):
    var normal = tool.get_vertex_normal(i)
    normal = normal.rotated(Vector3.UP, PI/4)  # Rotate normal
    tool.set_vertex_normal(i, normal)

tool.commit_to_surface(mesh)
```

**Use case:** Custom normal maps, vertex painting, blend shape smoothing.

### 7.2 No Runtime Normal Smoothing

Godot **does not re-smooth normals at runtime**:
- Normals are **baked at import**
- Runtime modification via MeshDataTool is manual (script-driven)
- No dynamic normal blending between LOD levels

---

## 8. Godot's Leanness vs. UE5

| Feature | Godot 4 | UE5 |
|---------|---------|-----|
| Smooth group mechanism | Bitmask (uint32) per vertex | Edge-based (hard/soft edges in mesh) |
| Normal averaging | Unweighted sum | Angle-weighted |
| Smoothing angle threshold | None (binary via groups) | Configurable (60°, 80°, etc.) |
| Tangent generation | mikktspace | Same (mikktspace-like) |
| Normal compression | Optional (octahedral) | Required for streaming |
| Runtime normal smoothing | None | None |
| Import reimport options | Ensure Tangents, Force Discrete | NormalImportMethod (Import, Compute, etc.) |

**Godot's advantage:** Smaller codebase, simpler mental model (groups are explicit from DCC).
**UE5's advantage:** Angle-weighted is mathematically more robust for arbitrary meshes.

---

## 9. Common Issues and Solutions for DX12 Engine Implementation

### Problem 1: Faceted Shading on Imported Meshes

**Root cause:** Normals not smoothed during import.

**Godot solution:**
```cpp
SurfaceTool st;
st.create_from(mesh, 0);
st.generate_normals(false);  // Compute smooth normals
mesh = st.commit();
```

**UzEngine adaptation:**
1. Detect smoothing groups from FBX (via Assimp or FBX SDK)
2. Group vertices by (position, smoothing_group)
3. Average normals within each group (unweighted sum)
4. Normalize result

### Problem 2: Normal Maps Require Matching Tangents

**Godot rule:** If material uses normal map, tangents must exist.

**UzEngine check:**
```cpp
if (has_normal_map && !has_tangents) {
    generate_tangents();  // Ensure TBN consistency
}
```

### Problem 3: Seams at Hard Edges

**Cause:** Tangent space mismatch across seams.

**Godot approach:** mikktspace ensures consistency IF normals are smoothed correctly on the shared edge.

**Fix:**
- Verify smooth_group assignment at hard edges
- Use separate vertices for hard edges (different groups)

---

## 10. Implementation Reference: Key Source Files

### Normal Computation
- `scene/resources/surface_tool.cpp:1187` — `generate_normals()`
- `scene/resources/surface_tool.h` — `SmoothGroupVertex` struct

### Tangent Generation
- `scene/resources/surface_tool.cpp:1156` — `generate_tangents()` (mikktspace integration)
- `thirdparty/mikktspace/` — Mikkelsen's tangent space library

### Import Pipeline
- `scene/resources/3d/importer_mesh.cpp` — ImporterMesh (intermediate format)
- `modules/gltf/gltf_document.cpp:1527-1528` — glTF normal import
- `editor/import/fbx_importer_manager.cpp` — FBX normal handling

### Mesh Storage
- `scene/resources/mesh.h` — Mesh base class (ARRAY_FORMAT_NORMAL, etc.)
- `scene/resources/mesh_data_tool.cpp` — Runtime normal editing

### Rendering Backend
- `servers/rendering/rendering_server_enums.h` — Array format flags
- `servers/rendering/renderer_rd/` — RenderingDevice implementation (compression)

---

## 11. Recommendations for UzEngine

### 11.1 Adopting Smooth Groups

```cpp
// UzEngine mesh import structure
struct MeshVertex {
    Vector3 position;
    Vector3 normal;
    Vector3 tangent;
    float bitangent_sign;
    uint32_t smoothing_group;  // Add this field
    // ... other data
};
```

### 11.2 Normal Computation at Import

```cpp
void ComputeSmoothNormals(MeshData& mesh) {
    // Group vertices by (position, smoothing_group)
    HashMap<pair<Vector3, uint32_t>, Vector3> smooth_groups;
    
    // Accumulate face normals per group
    for (const Triangle& tri : mesh.triangles) {
        Vector3 face_normal = ComputeFaceNormal(tri);
        for (int i = 0; i < 3; i++) {
            const Vertex& v = mesh.vertices[tri.indices[i]];
            auto key = make_pair(v.position, v.smoothing_group);
            smooth_groups[key] += face_normal;
        }
    }
    
    // Normalize and apply
    for (Vertex& v : mesh.vertices) {
        if (v.smoothing_group != UINT32_MAX) {
            auto key = make_pair(v.position, v.smoothing_group);
            v.normal = smooth_groups[key].normalized();
        }
    }
}
```

### 11.3 Tangent Generation

Use a C++ wrapper around mikktspace:
```cpp
void GenerateTangents(MeshData& mesh) {
    // Interface to Mikkelsen's library
    // Same algorithm as Godot
}
```

### 11.4 Import Pipeline

```
FBX/glTF → Extract normals + smooth groups
        → If normals missing: compute flat per-face
        → ComputeSmoothNormals(mesh)
        → If tangents missing and has normal_map: GenerateTangents(mesh)
        → Create GPU vertex buffers
```

---

## 12. Comparison: Godot vs. UE5 vs. Industry Standard

| Aspect | Godot | UE5 | Industry (Blender) |
|--------|-------|-----|-------------------|
| Smoothing mechanism | Explicit groups (UINT32 per vertex) | Edges marked hard/soft + angle | Blender's "Smooth" face flag |
| Normal averaging | Unweighted | Angle-weighted | Weighted (area + angle) |
| Tangent library | mikktspace | In-house (mikktspace-compatible) | mikktspace |
| Import philosophy | Trust DCC data | Recompute on import | Bake in DCC |
| Compression | Optional | Standard | N/A |

**Verdict:** Godot's approach is pragmatic and matches DCC tool semantics directly. UE5's approach is mathematically more rigorous but adds complexity.

