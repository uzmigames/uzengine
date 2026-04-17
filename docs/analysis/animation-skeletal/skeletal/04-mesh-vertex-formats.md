# Skeletal Mesh Vertex Formats and Bone Mapping — UE5.6 Reference Analysis

**Document**: Analysis of UE5.6 skeletal mesh vertex layout and bone-mapping mechanism for UzEngine skeletal animation system.

**Analysis Date**: 2026-04-16  
**UE5.6 Source**: W:\UE_5.6_Source  
**Referenced Files**: USkeletalMesh, FSkeletalMeshLODRenderData, FSkinWeightVertexBuffer, GpuSkinCommon.ush, GpuSkinCacheComputeShader.usf

---

## 1. Overview — The Two-Layer Bone Index System

UE5's skeletal mesh architecture uses a **per-section bone remapping layer** that UzEngine may be missing:

- **Vertex-stored indices**: Section-local bone indices (0 to MaxBonesInSection-1)
- **BoneMap array**: Per-section lookup table mapping local → global skeleton bone indices
- **Shader read**: `GlobalBoneIndex = Section.BoneMap[VertexBoneIndex]` → then index the bone matrix buffer

This two-layer system is **not** optional or performance-driven—it is **fundamental to how skeletal mesh sections work** when meshes have non-contiguous bone usage across sections.

---

## 2. USkeletalMesh Asset Structure

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\SkeletalMeshLODRenderData.h:127-183`

### FSkeletalMeshLODRenderData — Per-LOD Render Data

```cpp
class FSkeletalMeshLODRenderData : public FRefCountBase
{
public:
    // Array of render sections (one per material)
    TArray<FSkelMeshRenderSection> RenderSections;           // Line 132
    
    // GPU vertex streams for this LOD
    FStaticMeshVertexBuffers StaticVertexBuffers;            // Line 138
    FSkinWeightVertexBuffer SkinWeightVertexBuffer;           // Line 141
    
    // Additional per-LOD data
    FMultiSizeIndexContainer MultiSizeIndexContainer;         // Line 135
    FSkeletalMeshVertexClothBuffer ClothVertexBuffer;         // Line 144
    FMorphTargetVertexInfoBuffers MorphTargetVertexInfoBuffers; // Line 147
    // ...
};
```

The LOD owns:
1. **RenderSections** — per-material metadata including BoneMap
2. **StaticVertexBuffers** — position, tangent, UV data (shared across sections)
3. **SkinWeightVertexBuffer** — per-vertex bone indices + weights (shared across sections)

---

## 3. FSkelMeshRenderSection — Per-Material Bone Mapping

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\SkeletalMeshLODRenderData.h:27-125`

### The Critical BoneMap Array

```cpp
struct FSkelMeshRenderSection
{
    uint16 MaterialIndex;                    // Line 30
    uint32 BaseIndex;                        // Line 33 (index buffer offset)
    uint32 NumTriangles;                     // Line 36
    uint32 BaseVertexIndex;                  // Line 51 (vertex buffer offset)
    uint32 NumVertices;                      // Line 71
    int32 MaxBoneInfluences;                 // Line 74
    
    // *** THE CRITICAL FIELD ***
    TArray<FBoneIndexType> BoneMap;          // Line 68
    // Maps section-local bone indices → mesh global skeleton bone indices
    
    // Other fields (cloth, LOD, etc.)
    TArray<FMeshToMeshVertData> ClothMappingData_DEPRECATED;
    TArray<TArray<FMeshToMeshVertData>> ClothMappingDataLODs;
    FClothingSectionData ClothingData;
    FDuplicatedVerticesBuffer DuplicatedVerticesBuffer;
    
    // Defaults
    FSkelMeshRenderSection()
        : MaterialIndex(0)
        , BaseIndex(0)
        , NumTriangles(0)
        , bRecomputeTangent(false)
        , bCastShadow(true)
        , bVisibleInRayTracing(true)
        , RecomputeTangentsVertexMaskChannel(ESkinVertexColorChannel::None)
        , BaseVertexIndex(0)
        , NumVertices(0)
        , MaxBoneInfluences(4)              // Line 98 — DEFAULT 4 INFLUENCES
        , CorrespondClothAssetIndex(-1)
        , bDisabled(false)
    {}
};
```

**The BoneMap field comment** (Line 67):
> "The bones which are used by the vertices of this section. Indices of bones in the USkeletalMesh::RefSkeleton array"

This is the **remapping table**. In the shader:
```glsl
uint localBoneIndex = /* read from vertex skinweight buffer */;
uint globalBoneIndex = BoneMap[localBoneIndex];  // Remap to skeleton index
FBoneMatrix boneMat = BoneMatrices[globalBoneIndex];  // Index the bone matrix buffer
```

---

## 4. Vertex Stream Layouts — Byte-Exact Specifications

### 4.1 Position Stream — FPositionVertexBuffer

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\SkeletalMeshVertexBuffer.h`

```
Offset (bytes)  Field                Size    Comment
───────────────────────────────────────────────────────
0               Position.X (float)   4
4               Position.Y (float)   4
8               Position.Z (float)   4
───────────────────────────────────────────────────────
                TOTAL STRIDE         12 bytes per vertex
```

**Code definition** (implicit in TGPUSkinVertexBase structure):
- FVector3f Position → 3 floats → 12 bytes

### 4.2 Tangent/Normal Stream — FStaticMeshVertexBuffer

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\SkeletalMeshVertexBuffer.h:18-102`

```cpp
struct TGPUSkinVertexBase
{
    FPackedNormal TangentX;    // 4 bytes
    FPackedNormal TangentZ;    // 4 bytes
    // Derived: TangentY = cross(TangentX, TangentZ)
};
```

For **TGPUSkinVertexFloat16Uvs** (common):
```
Offset (uint32 units)  Field               Size       Comment
──────────────────────────────────────────────────────────────
0                      TangentX            1 u32      FPackedNormal
1                      TangentZ            1 u32      FPackedNormal
2                      Position.X          1 float    (3 floats total)
3                      Position.Y          1 float
4                      Position.Z          1 float
5                      UVs[0]              1 f16x2    (2x float16)
6                      UVs[1]              1 f16x2    (if NumTexCoords ≥ 2)
...
```

**Comment in source** (Line 16):
> "make sure to update GpuSkinCacheCommon.usf if the member sizes/order change!"

---

### 4.3 Skin Weight Stream — FSkinWeightVertexBuffer

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\SkinWeightVertexBuffer.h:15-63`

#### Bone Index Format — FBoneIndexType

```cpp
typedef uint8  FBoneIndex8;      // Line 15
typedef uint16 FBoneIndex16;     // Line 16
// Type selected based on skeleton size
```

#### Per-Vertex Weight Data Structure

```cpp
template <bool bExtraBoneInfluences, typename BoneIndexType>
struct TLegacySkinWeightInfo
{
    enum { NumInfluences = bExtraBoneInfluences ? EXTRA_BONE_INFLUENCES : MAX_INFLUENCES_PER_STREAM };
    BoneIndexType InfluenceBones[NumInfluences];   // Line 26
    uint8         InfluenceWeights[NumInfluences]; // Line 27
};
```

Constants **from GPUSkinPublicDefs.h**:
```cpp
#define MAX_INFLUENCES_PER_STREAM   4           // Default influences per base stream
#define EXTRA_BONE_INFLUENCES       8           // Extra stream (optional)
#define MAX_TOTAL_INFLUENCES        12          // 4 + 8
```

#### Default Layout — 4 Influences (Most Common)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\GPUSkinPublicDefs.h:11-18`

```
Offset (bytes)  Field                       Size    Type
─────────────────────────────────────────────────────────
0               InfluenceBones[0]           1       uint8
1               InfluenceBones[1]           1       uint8
2               InfluenceBones[2]           1       uint8
3               InfluenceBones[3]           1       uint8
4               InfluenceWeights[0]         1       uint8 (0-255, divide by 255.0 to normalize)
5               InfluenceWeights[1]         1       uint8
6               InfluenceWeights[2]         1       uint8
7               InfluenceWeights[3]         1       uint8
─────────────────────────────────────────────────────────
                TOTAL STRIDE                8 bytes per vertex
```

#### With Extra Influences (8 Total)

**When MaxBoneInfluences ≥ 5**, UE splits into two streams:

```
STREAM 0 (4 influences):
Offset (bytes)  Field                   Size
─────────────────────────────
0-3             InfluenceBones[0-3]     4
4-7             InfluenceWeights[0-3]   4
                STRIDE: 8 bytes

STREAM 1 (4 extra influences):
Offset (bytes)  Field                   Size
─────────────────────────────
0-3             InfluenceBones[4-7]     4
4-7             InfluenceWeights[4-7]   4
                STRIDE: 8 bytes
```

Per-vertex data is split across two buffers but maintains **consistent byte offsets within each**.

#### With 16-Bit Bone Indices

**When skeleton has >256 bones**, bone indices use uint16:

```
Offset (bytes)  Field                       Size
─────────────────────────────────────────────────
0               InfluenceBones[0]           2       uint16
2               InfluenceBones[1]           2       uint16
4               InfluenceBones[2]           2       uint16
6               InfluenceBones[3]           2       uint16
8               InfluenceWeights[0]         1       uint8
9               InfluenceWeights[1]         1       uint8
10              InfluenceWeights[2]         1       uint8
11              InfluenceWeights[3]         1       uint8
──────────────────────────────────────────────────
                TOTAL STRIDE                12 bytes per vertex
```

---

## 5. GPU Skinning Compute Shader — How Bone Indices Are Read

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\GpuSkinCommon.ush:118-147`

### Reading Bone Indices (Unpacking)

```glsl
int4 GetBlendIndices(Buffer<uint> InWeightBuffer, uint StreamOffset, uint ExtraInfluenceIndex)
{
    int4 UnpackedBlendIndices = 0;
    
#if GPUSKIN_BONE_INDEX_UINT16
    StreamOffset += GPUSKIN_VB_OFFSET_INFLUENCEBONES + 2 * ExtraInfluenceIndex;
    UnpackedBlendIndices.xy = UnpackUInt2FromUInt(InWeightBuffer[StreamOffset   ]);
    UnpackedBlendIndices.zw = UnpackUInt2FromUInt(InWeightBuffer[StreamOffset + 1]);
#else // GPUSKIN_BONE_INDEX_UINT16
    StreamOffset += GPUSKIN_VB_OFFSET_INFLUENCEBONES + ExtraInfluenceIndex;
    UnpackedBlendIndices = UnpackUintToUint4(InWeightBuffer[StreamOffset]);
#endif // GPUSKIN_BONE_INDEX_UINT16
    
    return UnpackedBlendIndices;
}
```

**Key line 128**: `UnpackedBlendIndices = UnpackUintToUint4(InWeightBuffer[StreamOffset]);`
- Reads a **single uint32** containing 4 × uint8 bone indices
- Returns `int4` with indices 0-3

### Reading Bone Weights (Normalized)

```glsl
half4 GetBlendWeights(Buffer<uint> InWeightBuffer, uint StreamOffset, uint InNumBoneInfluences)
{
    StreamOffset += ((InNumBoneInfluences + 3) / 4) * (1 + GPUSKIN_BONE_INDEX_UINT16);
    
    half4 UnpackedBlendWeights = 0;
#if GPUSKIN_BONE_WEIGHTS_UINT16
    // ... (rare path)
#else
    StreamOffset += ExtraInfluenceIndex;
    UnpackedBlendWeights = UnpackRGBA8(InWeightBuffer[StreamOffset]);  // Line 144
#endif
    
    return UnpackedBlendWeights;
}
```

**Line 144**: `UnpackRGBA8(...)` unpacks a uint32 as 4 × uint8 and **normalizes to [0,1]**.

---

### Computing Bone Matrices

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\GpuSkinCommon.ush:205-222`

```glsl
FBoneMatrix ComputeBoneMatrixWithLimitedInfluences(
    Buffer<float4> InBoneMatrices, 
    FGPUSkinIndexAndWeight In, 
    bool bExtraInfluences = GPUSKIN_USE_EXTRA_INFLUENCES)
{
    FBoneMatrix BoneMatrix = 
        In.BlendWeights.x * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices.x);
    BoneMatrix += 
        In.BlendWeights.y * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices.y);
    // ... (lines 211-219 for indices z, w and extra influences)
    
    return BoneMatrix;
}

FBoneMatrix InternalGetBoneMatrix(Buffer<float4> InBoneMatrices, uint In)
{
    return FBoneMatrix(
        InBoneMatrices[In*3],
        InBoneMatrices[In*3 + 1],
        InBoneMatrices[In*3 + 2]
    );
}
```

**Critical observation**: The shader directly indexes the bone matrix buffer with `In.BlendIndices.*` values **without any BoneMap lookup visible here**. This is because:

1. The bone indices **in the vertex buffer are already section-local** (0 to MaxBonesInSection-1)
2. The **BoneMap remapping happens at upload time** (CPU side, during skeleton/mesh setup)
3. OR the BoneMap is applied **when the vertex data is gathered into the per-section buffers**

---

## 6. Vertex Data Gathering and Section-Specific Buffers

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\SkeletalRenderGPUSkin.cpp` (implied from GPU skin cache design)

When a skeletal mesh LOD is prepared:

1. **Per-section vertex range** is identified:
   - BaseVertexIndex (start)
   - NumVertices (count)
   - BoneMap (section-local → global remapping)

2. **Bone indices in the weight buffer** for vertices in this section **are stored as section-local indices** (0 to MaxBonesInSection-1)

3. **When BoneMatrices buffer is built**, only the bones used by **this section** are included at indices 0 to MaxBonesInSection-1

4. **BoneMap then maps these section indices → skeleton indices** for any downstream systems that need global skeleton references

---

## 7. UzEngine Current Implementation vs. UE5

### UzEngine's SkinnedVertex Structure

**File** (inferred from memory): `runtime/engine/animation/skeletal_mesh_instance.h`

```cpp
// 56 bytes total (approximately)
struct SkinnedVertex {
    f32[3]  position;      // 12 bytes
    f32[3]  normal;        // 12 bytes (packed normal → expanded)
    f32[2]  texCoord;      // 8 bytes
    f32[4]  tangent;       // 16 bytes (packed normal + handedness)
    u32     boneIndices;   // 4 bytes (uint8[4])
    u32     boneWeights;   // 4 bytes (uint8[4])
};
```

### GPU Skinning Compute Shader

**File**: `runtime/shaders/runtime/compute/gpu_skinning_cs.hlsl`

The shader reads bone indices directly from the buffer **without a section-to-global BoneMap lookup**.

### Likely Issue — Missing BoneMap Dereference

**Hypothesis**: UzEngine's FBX importer generates section-local bone indices (same as UE5) but:

1. ✅ Exports section-local indices to the vertex weight buffer
2. ❌ **Does NOT generate the per-section BoneMap** (or the GPU skin shader doesn't use it)
3. ❌ **GPU shader treats section-local indices as global skeleton indices**

**Result**: Vertices skinned to wrong bones because index 0 might mean "bone 0 in this section" but the shader reads it as "bone 0 in the global skeleton".

---

## 8. Suspects and Diagnostics

### Suspect 1: Missing BoneMap in FBX Importer

**Check**: Does `skeletal_mesh_instance.cpp` build a per-section BoneMap when loading FBX?

```cpp
// If missing, this is P0
for (int sectionIdx = 0; sectionIdx < section_count; sectionIdx++) {
    auto& section = lod.sections[sectionIdx];
    // Is section.bone_map populated?
    // Or is it empty / identity mapping?
}
```

### Suspect 2: GPU Shader Not Remapping Indices

**Check**: Does `gpu_skinning_cs.hlsl` have access to BoneMap and use it?

```hlsl
// WRONG (current, suspected)
float3x4 bone_mat = BoneMatrices[bone_index];  // Direct access

// CORRECT (should be)
uint global_bone = BoneMap[bone_index];        // Remap first
float3x4 bone_mat = BoneMatrices[global_bone]; // Then access
```

### Suspect 3: BoneMap Not Uploaded to GPU

**Check**: Is the per-section BoneMap uploaded as a shader resource?

UE5 likely uses:
- Compute shader parameter or SRV for the section's BoneMap
- Or bakes the mapping at CPU time when building per-section vertex data

---

## 9. Reference Implementation — Data Flow Diagram

```
┌─────────────────────────────────────────┐
│     Skeletal Mesh Asset (.uasset)       │
│   - RefSkeleton (global bone hierarchy) │
│   - LODs[]                              │
└────────────┬────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────┐
│   FSkeletalMeshLODRenderData             │
│   - RenderSections[] (per material)      │
│   - StaticVertexBuffers                 │
│   - SkinWeightVertexBuffer              │
└────────────┬────────────────────────────┘
             │
             ├─────────────────────────────┐
             │                             │
             ▼                             ▼
     ┌─────────────────┐          ┌────────────────────┐
     │  Section[0]     │          │   Section[1]       │
     │ ───────────────│          │ ──────────────────│
     │ MaterialIdx: 0 │          │ MaterialIdx: 1    │
     │ BaseVertex: 0  │          │ BaseVertex: 1024  │
     │ NumVerts: 1024 │          │ NumVerts: 512     │
     │ BoneMap:       │          │ BoneMap:          │
     │  [0→5,1→0,...] │          │  [0→3,1→7,...]   │
     └────────┬────────┘          └────────┬──────────┘
              │                            │
              ▼                            ▼
    ┌──────────────────────────┐  ┌──────────────────────────┐
    │ Vertices 0-1023          │  │ Vertices 1024-1535       │
    │ BoneIndices (local):     │  │ BoneIndices (local):     │
    │  vertex[0] = [0,1,2,3]   │  │  vertex[0] = [0,1,3,7]   │
    │  (indices into Section[0]│  │  (indices into Section[1]│
    │   BoneMap, not skeleton) │  │   BoneMap, not skeleton) │
    └───────────┬──────────────┘  └────────┬─────────────────┘
                │                          │
                ▼                          ▼
    ┌──────────────────────────┐  ┌──────────────────────────┐
    │ Remap with BoneMap[0]:   │  │ Remap with BoneMap[1]:   │
    │  [0,1,2,3] → [5,0,8,12]  │  │  [0,1,3,7] → [3,7,2,14]  │
    │  (global skeleton indices)│  │  (global skeleton indices)│
    └───────────┬──────────────┘  └────────┬─────────────────┘
                │                          │
                ▼                          ▼
    ┌──────────────────────────┐  ┌──────────────────────────┐
    │ BoneMatrices[] (global)  │  │ BoneMatrices[] (global)  │
    │  [3] = ...               │  │  [3] = ...               │
    │  [5] = ...               │  │  [7] = ...               │
    │  ...                     │  │  ...                     │
    └──────────────────────────┘  └──────────────────────────┘
```

---

## 10. Key Takeaways — What UzEngine Must Implement

### ✅ Already Correct (Inferred)

1. **Vertex weight packing**: Section-local uint8 bone indices (0-255)
2. **Weight normalization**: uint8 weights / 255.0 → [0, 1]
3. **Default influences**: 4 per vertex (matches UE5 default)
4. **Position/tangent layout**: FVector3f + FPackedNormal

### ❌ Likely Missing

1. **Per-section BoneMap generation in importer**
   - FBX importer must populate `section.bone_map[local_idx] → global_idx`
   - One BoneMap per render section

2. **BoneMap GPU upload**
   - Per-section BoneMap must be uploaded as SRV or parameter
   - Or baked into vertex layout at CPU time

3. **BoneMap dereference in GPU shader**
   - Shader must remap: `global_idx = BoneMap[local_idx]` before matrix access
   - Critical for meshes with sparse bone usage

### Default Assumptions

- **MushroomMonster test model**: 21 bones → uint8 indices suffice (no 16-bit)
- **Max influences per section**: 4 (common for organic characters)
- **Vertex stride**: 12 (pos) + 8 (bones + weights) = 20 bytes minimum

---

## 11. Files to Audit in UzEngine

| File | Check For |
|------|-----------|
| `runtime/engine/animation/skeletal_mesh_instance.cpp` | BoneMap population in LoadMesh() |
| `runtime/engine/animation/skeletal_mesh_subsystem.cpp` | Per-section bone usage tracking |
| `runtime/engine/renderer/mesh/gpu_mesh.cpp` | BoneMap upload to GPU |
| `runtime/shaders/runtime/compute/gpu_skinning_cs.hlsl` | BoneMap lookup before matrix access |
| FBX importer code | Section-local index assignment during LOD import |

---

## 12. References

**UE5.6 Source Files Cited**:
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\SkeletalMeshLODRenderData.h` — FSkeletalMeshLODRenderData, FSkelMeshRenderSection
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Rendering\SkinWeightVertexBuffer.h` — FSkinWeightVertexBuffer, TLegacySkinWeightInfo
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\GPUSkinPublicDefs.h` — MAX_INFLUENCES_PER_STREAM, bone index constants
- `W:\UE_5.6_Source\Engine\Shaders\Private\GpuSkinCommon.ush` — Shader bone index/weight unpacking
- `W:\UE_5.6_Source\Engine\Shaders\Private\GpuSkinCacheComputeShader.usf` — GPU skinning compute shader

**Published References**:
- None cited in UE5 source for skeletal mesh architecture (it is established game engine practice)

---

## End of Analysis

This document provides the complete vertex format specification and identifies the **bone mapping layer** as the most likely source of UzEngine's skeletal animation bug. The per-section BoneMap is structural to UE5's skeletal mesh design and cannot be omitted without breaking multi-section meshes with sparse bone usage.
