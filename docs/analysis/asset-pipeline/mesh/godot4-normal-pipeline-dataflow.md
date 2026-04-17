<!-- doc-meta
title: Godot 4 Normal Pipeline Data Flow
desc: Visual walkthrough of how normals flow through the import, computation, and rendering pipeline
-->

# Godot 4 Normal Pipeline: Complete Data Flow

## End-to-End: FBX File → Rendered Mesh

```
FBX File (Blender export)
│
├─ Each face has: smooth flag (boolean)
└─ Each vertex has: position, UV, color, etc.

      ↓ [FBX Importer]

Importer reads:
├─ smooth=true  → smooth_group = (face's group ID)
├─ smooth=false → smooth_group = UINT32_MAX (flat)
└─ vertex position + face group → SurfaceTool::Vertex

      ↓ [Surface Tool Processing]

SurfaceTool::create_from(fbx_mesh, surface_index):
├─ Populate vertex_array[] with all vertices
├─ Set smooth_group on each vertex
└─ Format |= ARRAY_FORMAT_NORMAL (normal will be generated)

      ↓ [Normal Generation]

SurfaceTool::generate_normals():
├─ Deindex mesh (expand indexed triangles)
├─ For each triangle:
│   ├─ Compute face normal = Plane(v0, v1, v2).normal
│   └─ For each vertex in triangle:
│       └─ If smooth_group != UINT32_MAX:
│           └─ smooth_hash[(position, smooth_group)] += face_normal
├─ For each vertex:
│   └─ If smooth_group != UINT32_MAX:
│       └─ vertex.normal = smooth_hash[(position, smooth_group)].normalized()
├─ If was_indexed: reindex()
└─ Format |= ARRAY_FORMAT_NORMAL

      ↓ [Tangent Generation]

SurfaceTool::generate_tangents() [OPTIONAL]:
├─ Requires: ARRAY_FORMAT_TEX_UV + ARRAY_FORMAT_NORMAL
├─ Call genTangSpaceDefault() from mikktspace
├─ For each triangle:
│   ├─ Compute edge vectors (3D and UV)
│   ├─ Solve for tangent direction
│   └─ Gram-Schmidt orthonormalization against normal
├─ Store as: float4(tx, ty, tz, bitangent_sign)
└─ Format |= ARRAY_FORMAT_TANGENT

      ↓ [Mesh Creation]

SurfaceTool::commit(existing_mesh, flags):
├─ commit_to_arrays() → Array a[ARRAY_MAX]
├─ Create GPU mesh:
│   ├─ vertices[] → GPU vertex buffer
│   ├─ normals[] → GPU normal buffer (or compressed)
│   ├─ tangents[] → GPU tangent buffer
│   ├─ indices[] → GPU index buffer
│   └─ Other attributes (UV, color, bones)
└─ Return Mesh resource

      ↓ [Runtime: Mesh Instance 3D]

MeshInstance3D node:
├─ mesh = imported mesh resource
├─ material = StandardMaterial3D (or custom)
└─ transform = global position/rotation

      ↓ [Render Frame]

RenderingServer processes:
├─ GBuffer pass:
│   ├─ VS: transform position to view-space
│   ├─ Bind normal buffer
│   ├─ Bind tangent buffer
│   ├─ Output normal to G-Buffer
│   └─ Output depth, albedo, roughness, metallic, etc.
└─ Deferred lighting pass:
    ├─ Read G-Buffer normal
    ├─ Compute lighting using normal
    └─ Output lit color

      ↓ [Frame Output]

Screen with smooth normals → no faceting
```

---

## Detailed: Smoothing Group Computation

### Input State

```
Three faces meeting at a corner:

Face A (smooth_group=1):
  v0: pos=(0,0,0), smooth_group=1
  v1: pos=(1,0,0), smooth_group=1
  v2: pos=(0,1,0), smooth_group=1
  normal = (0, 0, 1)

Face B (smooth_group=1):  [Shares edge v0-v1]
  v3: pos=(0,0,0), smooth_group=1  [Same position as v0]
  v4: pos=(1,0,0), smooth_group=1  [Same position as v1]
  v5: pos=(1,1,0), smooth_group=1
  normal = (0.707, 0, 0.707)

Face C (smooth_group=2):  [Shares edge v0-v2 with Face A, different group]
  v6: pos=(0,0,0), smooth_group=2  [Same position as v0, different group]
  v7: pos=(0,1,0), smooth_group=2  [Same position as v2, different group]
  v8: pos=(-1,0,0), smooth_group=2
  normal = (-1, 0, 0)
```

### Processing Steps

#### Step 1: Deindex

```
input: 9 vertices (indexed 0-8)
deindex():
  ├─ Expand index buffer into full vertex array
  └─ Result: 9 vertices in order (no index buffer)
```

#### Step 2: Build Smooth Hash

```
Iterate through all triangles (vertices 0-2, 3-5, 6-8):

Face A (vertices 0-2):
  v[0]: smooth_hash[(pos=(0,0,0), group=1)] = (0, 0, 1)
  v[1]: smooth_hash[(pos=(1,0,0), group=1)] = (0, 0, 1)
  v[2]: smooth_hash[(pos=(0,1,0), group=1)] = (0, 0, 1)

Face B (vertices 3-5):
  v[3]: smooth_hash[(pos=(0,0,0), group=1)] += (0.707, 0, 0.707)
        → smooth_hash[(pos=(0,0,0), group=1)] = (0.707, 0, 1.707)
  v[4]: smooth_hash[(pos=(1,0,0), group=1)] += (0.707, 0, 0.707)
        → smooth_hash[(pos=(1,0,0), group=1)] = (0.707, 0, 1.707)
  v[5]: smooth_hash[(pos=(1,1,0), group=1)] = (0.707, 0, 0.707)

Face C (vertices 6-8):
  v[6]: smooth_hash[(pos=(0,0,0), group=2)] = (-1, 0, 0)  [Different group!]
  v[7]: smooth_hash[(pos=(0,1,0), group=2)] = (-1, 0, 0)  [Different group!]
  v[8]: smooth_hash[(pos=(-1,0,0), group=2)] = (-1, 0, 0)
```

#### Step 3: Normalize and Apply

```
For each vertex:

v[0]: pos=(0,0,0), group=1
  → smooth_hash[(pos=(0,0,0), group=1)] = (0.707, 0, 1.707)
  → normal = (0.707, 0, 1.707).normalized()
  → normal ≈ (0.381, 0, 0.925)  [Blend of Face A and B]

v[1]: pos=(1,0,0), group=1
  → normal ≈ (0.381, 0, 0.925)  [Same position and group as v4]

v[2]: pos=(0,1,0), group=1
  → smooth_hash[(pos=(0,1,0), group=1)] = (0, 0, 1)
  → normal = (0, 0, 1)  [Only Face A touches this position]

v[3]: pos=(0,0,0), group=1
  → Same as v[0]: normal ≈ (0.381, 0, 0.925)

v[4]: pos=(1,0,0), group=1
  → Same as v[1]: normal ≈ (0.381, 0, 0.925)

v[5]: pos=(1,1,0), group=1
  → smooth_hash[(pos=(1,1,0), group=1)] = (0.707, 0, 0.707)
  → normal = (0.707, 0, 0.707).normalized()
  → normal ≈ (0.707, 0, 0.707)

v[6]: pos=(0,0,0), group=2
  → smooth_hash[(pos=(0,0,0), group=2)] = (-1, 0, 0)
  → normal = (-1, 0, 0)  [FLAT, separate from v[0] due to group mismatch]

v[7]: pos=(0,1,0), group=2
  → smooth_hash[(pos=(0,1,0), group=2)] = (-1, 0, 0)
  → normal = (-1, 0, 0)  [FLAT, separate from v[2] due to group mismatch]

v[8]: pos=(-1,0,0), group=2
  → smooth_hash[(pos=(-1,0,0), group=2)] = (-1, 0, 0)
  → normal = (-1, 0, 0)
```

### Result

```
Edge between Face A and Face B (both group=1):
  → Vertices share BOTH position AND group
  → Normals are AVERAGED
  → Smooth shading

Edge between Face A and Face C (group=1 vs group=2):
  → Vertices share position but NOT group
  → Normals are SEPARATE
  → Hard edge (faceted appearance)

This mechanism allows artists to:
  ✓ Hard edge by assigning different groups to same-position vertices
  ✓ Smooth edge by assigning same group
  ✗ Cannot control angle threshold per-edge (binary choice only)
```

---

## Tangent Generation: mikktspace Algorithm

### Input

```
Vertex data:
  position: Vector3
  normal: Vector3 (already computed/imported)
  uv: Vector2

Triangle: v0, v1, v2
```

### Algorithm Steps

```
1. Compute edge vectors in 3D space:
   edge1 = v1.position - v0.position
   edge2 = v2.position - v0.position

2. Compute edge vectors in UV space:
   duv1 = v1.uv - v0.uv
   duv2 = v2.uv - v0.uv

3. Build 2×3 matrix:
   [duv1.x  duv1.y] [tangent.x tangent.y tangent.z]     [edge1.x edge1.y edge1.z]
   [duv2.x  duv2.y] [bitangent]                      =   [edge2.x edge2.y edge2.z]
   
   Solve for [tangent, bitangent] using least-squares

4. Gram-Schmidt orthonormalization against normal:
   tangent = (tangent - (tangent · normal) * normal).normalized()
   
5. Compute bitangent sign:
   bitangent_direction = cross(normal, tangent)
   bitangent_sign = (dot(bitangent_direction, computed_bitangent) < 0) ? -1 : 1

6. Store as:
   tangent_attribute.xyz = tangent
   tangent_attribute.w = bitangent_sign
```

### Why Orthonormalization?

```
Normal space constraint: T ⊥ N (tangent perpendicular to normal)

Without Gram-Schmidt:
  Tangent might not be perfectly perpendicular
  → Artifacts when computing B = N × T in shader

With Gram-Schmidt:
  T' = T - (T · N) * N
  T_final = T'.normalized()
  → Guaranteed perpendicular, high precision
```

### Shader Reconstruction

```hlsl
// Vertex Shader
VS_Output vs(VertexInput input) {
    VS_Output output;
    
    // Read from vertex buffer
    float3 normal = input.normal;
    float4 tangent = input.tangent;  // (tx, ty, tz, sign)
    
    // Reconstruct TBN matrix
    float3 T = normalize(tangent.xyz);
    float3 N = normalize(normal);
    float3 B = tangent.w * cross(N, T);  // Bitangent
    
    // Transform to world space
    output.TBN = float3x3(T, B, N) * World;
    
    return output;
}

// Pixel Shader
PS_Output ps(VS_Output input) {
    // Sample normal map (expects normal in tangent space)
    float3 normal_tangent = SampleNormalMap();
    
    // Transform to world space
    float3 normal_world = normalize(input.TBN * normal_tangent);
    
    // Use normal_world for lighting calculations
    return LitColor;
}
```

---

## glTF Import: Different Path

### When glTF Has Normals

```
glTF file:
  ├─ POSITION accessor → vertices[]
  ├─ NORMAL accessor → normals[]  [Already computed in DCC]
  ├─ TANGENT accessor → tangents[] [Computed by mikktspace in DCC]
  └─ TEXCOORD_0 accessor → uvs[]

ImporterMesh::add_surface():
  a[Mesh::ARRAY_VERTEX] = vertices (from POSITION)
  a[Mesh::ARRAY_NORMAL] = normals  (from NORMAL, AS-IS)
  a[Mesh::ARRAY_TANGENT] = tangents (from TANGENT, AS-IS)
  a[Mesh::ARRAY_TEX_UV] = uvs (from TEXCOORD_0)
  
Result: No recomputation, trust glTF data
```

### When glTF Lacks Normals

```
glTF file:
  ├─ POSITION accessor → vertices[]
  ├─ NORMAL accessor → MISSING
  ├─ TANGENT accessor → MISSING
  └─ TEXCOORD_0 accessor → uvs[]

gltf_document.cpp:
  if (a.has("NORMAL")) {
      array[Mesh::ARRAY_NORMAL] = normals_from_glTF;
  } else {
      // ⚠️ NO AUTO-GENERATION
      // Normals remain absent unless explicitly computed
  }

Difference from FBX:
  FBX: Always generates normals if missing
  glTF: Assumes normals are always present or not needed
```

---

## Comparison: Godot vs UE5 vs DCC

### Data Flow

```
GODOT:
  FBX/Blender
    ├─ Face "smooth" flag
    └─ Smoothing group ID
        ↓
  ImporterMesh (smooth_group per vertex)
    ↓
  SurfaceTool::generate_normals()
    ├─ Group vertices by (position, smooth_group)
    └─ Average face normals within group
        ↓
  Mesh (with pre-computed smooth normals)
    ↓
  GPU (use directly, no shader recomputation)

UE5:
  FBX/DCC
    ├─ Normals (if present)
    └─ Hard/soft edge flags
        ↓
  StaticMesh (intermediate)
    ↓
  FBXMeshUtilities::GenerateTangents()
    ├─ Recompute normals if missing
    ├─ Option: angle-weighted if needed
    └─ Compute tangents via mikktspace
        ↓
  StaticMeshResource (with computed normals)
    ↓
  GPU

DCC (Blender):
  Model
    ├─ Face "smooth" flag (boolean)
    ├─ Shade smooth / Shade flat
    └─ Auto-compute normals based on threshold
        ↓
  Export to FBX/glTF
    ├─ Embed computed normals
    ├─ Embed smoothing groups
    └─ Embed tangents (via mikktspace)
        ↓
  File (ready to import)
```

### Computational Cost

```
Godot:
  Import: Hash grouping + normal accumulation + normalization
  Cost: O(vertices + faces)
  Shader: None (use pre-computed normals)

UE5:
  Import: Recomputation if missing
  Cost: Higher (recompute + weighted averaging)
  Shader: None (use pre-computed normals)

DCC (Blender):
  Authoring: Compute normals + assign groups
  Cost: Done once, baked in file
  Import: Trivial (just read data)
  Shader: None
```

---

## Summary: Smooth Normals in Action

| Stage | Godot | Data | Cost |
|-------|-------|------|------|
| DCC | Artist sets "smooth" | Per-face flag | Free |
| Export | Blender computes groups | UINT32 per vertex | Free |
| Import | Read FBX | smooth_group array | O(n) |
| Generation | Hash + average | Group → normal | O(v + f) |
| GPU Upload | Store normal buffer | Vector3[] | 12 B/vertex |
| Render | Fetch normal | Direct from buffer | 1 texture fetch |
| Shader | Use directly | No TBN recalc | 0 ALU |

**Key win:** All smoothing done at import → zero runtime cost in shader.

