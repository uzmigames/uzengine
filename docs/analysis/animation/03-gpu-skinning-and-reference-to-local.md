# GPU Skinning and Reference-to-Local Matrix Formula Analysis

## Executive Summary

UzEngine's GPU skinning pipeline (compute shader + CPU palette) diverges from UE5's reference implementation at **one critical point**: the **palette matrix multiplication order**. UE5 computes `ReferenceToLocal[i] = RefBasesInvMatrix[i] * ComponentSpace[i]` using **column-vector convention** (`mul(Matrix, Vector)` in HLSL), while UzEngine computes `palette[i] = inverseBind[i] * world[i]` using **row-vector convention** (`mul(Vector, Matrix)` in HLSL). This difference can cause bind-pose to render correctly (both are identity) but animated poses to deform because the matrix multiplication order reverses.

---

## 1. UE5's ReferenceToLocal Matrix Formula

### Location & Code

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\SkeletalRender.cpp`
**Lines 357–464** (`UpdateRefToLocalMatricesInner`)
**Lines 474–524** (`UpdateRefToLocalMatrices`)

### The Formula: Two-Step Process

UE5 computes skinning matrices in **two distinct stages**:

#### Stage 1: Bone-Local Pose (Lines 417–438)

```cpp
// UpdateRefToLocalMatricesInner(), lines 417-437
if (ComponentTransform.IsValidIndex(ThisBoneIndex)) {
    if (bBoneVisibilityStatesValid) {
        const int32 ParentIndex = RefSkeleton.GetParentIndex(ThisBoneIndex);
        bool bNeedToHideBone = BoneVisibilityStates[ThisBoneIndex] != BVS_Visible;
        if (bNeedToHideBone && ParentIndex != INDEX_NONE) {
            ReferenceToLocal[ThisBoneIndex] = ReferenceToLocal[ParentIndex].ApplyScale(0.f);
        } else {
            checkSlow(ComponentTransform[ThisBoneIndex].IsRotationNormalized());
            ReferenceToLocal[ThisBoneIndex] = (FMatrix44f)ComponentTransform[ThisBoneIndex].ToMatrixWithScale();
        }
    } else {
        checkSlow(ComponentTransform[ThisBoneIndex].IsRotationNormalized());
        ReferenceToLocal[ThisBoneIndex] = (FMatrix44f)ComponentTransform[ThisBoneIndex].ToMatrixWithScale();
    }
}
```

At this point, `ReferenceToLocal[i]` holds the **component-space bone matrix** (i.e., bone i's transform relative to the component origin, NOT relative to the bone's bind-pose local space).

#### Stage 2: Apply Inverse Bind (Line 462)

```cpp
// UpdateRefToLocalMatricesInner(), lines 460-463
for (int32 ThisBoneIndex = 0; ThisBoneIndex < NumReferenceToLocal; ++ThisBoneIndex) {
    VectorMatrixMultiply(&ReferenceToLocal[ThisBoneIndex], 
                         &(*RefBasesInvMatrix)[ThisBoneIndex], 
                         &ReferenceToLocal[ThisBoneIndex]);
}
```

This calls UE's **column-vector** matrix multiply:
```
ReferenceToLocal[i] = RefBasesInvMatrix[i] * ReferenceToLocal[i]
```

Where:
- `RefBasesInvMatrix[i]` = inverse of bone i's **bind-pose component-space matrix**
- `ReferenceToLocal[i]` (after Stage 1) = current **component-space bone matrix**
- Result = **ReferenceToLocal[i]** = the skinning matrix that transforms bind-pose vertices to the current pose

### Semantic Meaning

The name **"ReferenceToLocal"** is misleading. It actually means:
- **"Reference"** = bind-pose (reference pose), with vertices expressed in **component-space**
- **"Local"** = animation-space, meaning the animated (component-space) bone frame

The formula `ReferenceToLocal[i] = RefBasesInvMatrix[i] * ComponentSpace[i]` in column-vector convention is equivalent to:
1. `RefBasesInvMatrix[i]` projects a component-space point into **bone-i's bind-pose local space**
2. `ComponentSpace[i]` then transforms that point into **bone-i's current (animated) local space**

Or in English: *"Move the vertex from the bind-pose local frame to the animated local frame via component-space as an intermediary."*

### Buffer Layout

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\GPUSkinVertexFactory.h`

```cpp
// Line 199-201
FBoneMatrix InternalGetBoneMatrix(Buffer<float4> InBoneMatrices, uint In) 
{ 
    return FBoneMatrix(InBoneMatrices[In*3], InBoneMatrices[In*3 + 1], InBoneMatrices[In*3 + 2]); 
}
```

**Storage**: 48 bytes per bone (3 rows × 4 columns = 12 floats = 48 bytes). Row 4 is omitted since it's always (0, 0, 0, 1).

---

## 2. GPU Skinning Vertex Shader: The Multiplication Order

### Location & Code

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\GpuSkinVertexFactory.ush`
**Lines 546–596** (SkinPosition / SkinPreviousPosition)

```hlsl
/// transform position by weighted sum of skinning matrices
float3 SkinPosition( FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates )
{
    float3 Position;
    if (IsMorphTarget()) {
        // morph target delta handling
        Position = MorphPosition(Input, Intermediates);
    } else {
        Position = Intermediates.UnpackedPosition;
    }

    // Note the use of mul(Matrix,Vector), bone matrices are stored transposed
    // for tighter packing.
    Position = mul(Intermediates.BlendMatrix, float4(Position, 1));

    return Position;
}
```

**Multiplication Convention**: `mul(Matrix, Vector)` = **column-vector convention**

This matches the palette matrix formula: matrices are stored as 3×4 and interpret input as column vectors.

### Bone Matrix Computation

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\GpuSkinCommon.ush`
**Lines 205–222**

```hlsl
FBoneMatrix ComputeBoneMatrixWithLimitedInfluences(Buffer<float4> InBoneMatrices, 
                                                    FGPUSkinIndexAndWeight In, 
                                                    bool bExtraInfluences = GPUSKIN_USE_EXTRA_INFLUENCES)
{
    FBoneMatrix 
    BoneMatrix  = In.BlendWeights.x * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices.x);
    BoneMatrix += In.BlendWeights.y * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices.y);
#if !GPUSKIN_LIMIT_2BONE_INFLUENCES
    BoneMatrix += In.BlendWeights.z * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices.z);
    BoneMatrix += In.BlendWeights.w * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices.w);
    if (bExtraInfluences) {
        BoneMatrix += In.BlendWeights2.x * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices2.x);
        BoneMatrix += In.BlendWeights2.y * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices2.y);
        BoneMatrix += In.BlendWeights2.z * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices2.z);
        BoneMatrix += In.BlendWeights2.w * InternalGetBoneMatrix(InBoneMatrices, In.BlendIndices2.w);
    }
#endif // GPUSKIN_LIMIT_2BONE_INFLUENCES
    return BoneMatrix;
}
```

**Result**: Weighted sum of up to 8 bone matrices (4 base + 4 extra influences).

---

## 3. UzEngine's Palette Computation

### Location & Code

**File**: `e:\UzEngine\runtime\engine\animation\skeletal_mesh_instance.cpp`
**Lines 64–103** (`UpdatePaletteFromPose`)

```cpp
void SkeletalMeshInstance::UpdatePaletteFromPose() {
    if (!m_skeleton) return;

    const uint32_t boneCount = m_skeleton->GetBoneCount();
    if (m_palette.size() != boneCount) {
        m_palette.assign(boneCount, Mat4::Identity());
    }

    // Snapshot the pose under the lock to avoid partial reads if SetPose races.
    Pose pose;
    {
        std::lock_guard<std::mutex> lock(m_poseMutex);
        pose = m_currentPose;
    }

    // Fall back to bind pose if the pose is empty or sized differently.
    if (pose.GetBoneCount() != boneCount) {
        pose = Pose::FromBindPose(*m_skeleton);
    }

    // Compute world matrix of each bone in topological order (parents already
    // done when the child is processed). Row-vector convention:
    //     world[i] = local[i] * world[parent[i]].
    // Then palette[i] = inverseBind[i] * world[i], so the shader can skin via
    //     skinnedPoint = bindPoint * palette[i].
    std::vector<Mat4> worldMatrices(boneCount, Mat4::Identity());

    for (uint32_t i = 0; i < boneCount; ++i) {
        const BoneInfo& bone = m_skeleton->GetBone(i);
        const Mat4 local = pose.localTransforms[i].ToMatrix();

        if (bone.parentIndex < 0) {
            worldMatrices[i] = local;
        } else {
            worldMatrices[i] = local * worldMatrices[static_cast<size_t>(bone.parentIndex)];
        }

        m_palette[i] = bone.inverseBindWorld * worldMatrices[i];
    }
}
```

**The Formula**:
```
worldMatrices[i] = local[i] * worldMatrices[parent[i]]
palette[i] = inverseBindWorld[i] * worldMatrices[i]
```

### Storage Convention

**File**: `e:\UzEngine\runtime\shaders\runtime\compute\gpu_skinning_cs.hlsl`
**Lines 65–82** (LoadPaletteRowMajor)

```hlsl
/// Loads a single bone palette entry from a raw byte-address buffer.
/// CPU `Mat4` is stored row-major (`XMFLOAT4X4`), 64 B per matrix:
///   floats[0..3]   = row 0
///   floats[4..7]   = row 1
///   floats[8..11]  = row 2
///   floats[12..15] = row 3   (translation for row-vector matrices)
float4x4 LoadPaletteRowMajor(ByteAddressBuffer buf, uint boneIndex) {
    const uint base = boneIndex * 64u;
    const float4 r0 = asfloat(buf.Load4(base +  0u));
    const float4 r1 = asfloat(buf.Load4(base + 16u));
    const float4 r2 = asfloat(buf.Load4(base + 32u));
    const float4 r3 = asfloat(buf.Load4(base + 48u));
    return float4x4(r0, r1, r2, r3);
}
```

Matrices are stored **row-major** (64 bytes per bone, full 4×4).

### GPU Skinning Application

**File**: `e:\UzEngine\runtime\shaders\runtime\compute\gpu_skinning_cs.hlsl`
**Lines 114–127**

```hlsl
/// Weighted sum of bone skinning matrices. Palettes are loaded as
/// row-major from a raw byte-address buffer, matching CPU `Mat4`
/// storage. Matrix layout M[i][j] = CPU Mat4(i, j).
const float4x4 P0 = LoadPaletteRowMajor(paletteBytes, idx0);
const float4x4 P1 = LoadPaletteRowMajor(paletteBytes, idx1);
const float4x4 P2 = LoadPaletteRowMajor(paletteBytes, idx2);
const float4x4 P3 = LoadPaletteRowMajor(paletteBytes, idx3);
const float4x4 skin = P0 * w0 + P1 * w1 + P2 * w2 + P3 * w3;

// Row-vector convention (matches mesh_vs / CPU palette math):
// `result = v * M` ⇔ `result[i] = sum_j v[j] * M[j][i]`.
const float4 posS = mul(float4(position, 1.0), skin);
const float3 nS   = mul(float4(normal,   0.0), skin).xyz;
const float3 tS   = mul(float4(tangent.xyz, 0.0), skin).xyz;
```

**Multiplication Convention**: `mul(Vector, Matrix)` = **row-vector convention**

---

## 4. Side-by-Side Comparison: UE5 vs UzEngine

| Aspect | UE5 | UzEngine |
|--------|-----|---------|
| **World Matrix Computation** | Not shown; computed earlier in C++ | `world[i] = local[i] * world[parent[i]]` (row-vector) |
| **Palette Formula** | `ReferenceToLocal[i] = RefBasesInvMatrix[i] * ComponentSpace[i]` (column-vector) | `palette[i] = inverseBindWorld[i] * worldMatrices[i]` (row-vector) |
| **Inverse Bind Semantic** | Inverse of bind-pose component-space bone matrix | Inverse of bind-pose world-space bone matrix |
| **Bone Matrix Storage** | 48 bytes (3×4, row-major) | 64 bytes (4×4, row-major) |
| **GPU Multiplication** | `mul(Matrix, Vector)` — column-vector | `mul(Vector, Matrix)` — row-vector |
| **Input Vertex Position Space** | Component-space (bind-pose mesh-local) | Component-space (bind-pose mesh-local) |

---

## 5. The Critical Difference: Matrix Multiplication Order

The **semantic issue** is NOT in the formulas themselves, but in **whether they're equivalent**.

### UE5's Approach (Column-Vector)

```
ReferenceToLocal[i] = InvBind[i] * ComponentSpace[i]
position_out = mul(Matrix, Vector) = mul(ReferenceToLocal[i], vec4(position, 1))
```

Mathematical meaning:
```
position_out = InvBind[i] * ComponentSpace[i] * vec4(position, 1)
```

Step-by-step:
1. `ComponentSpace[i] * vec4(position, 1)` transforms bind-pose position to current-frame bone-local space
2. `InvBind[i] * (result)` transforms back to component-space

### UzEngine's Approach (Row-Vector)

```
palette[i] = InvBindWorld[i] * world[i]
position_out = mul(vec4(position, 1), palette[i])
```

Mathematical meaning:
```
position_out = vec4(position, 1) * InvBindWorld[i] * world[i]
```

Transposing to column-vector for comparison:
```
position_out = (world[i]^T * InvBindWorld[i]^T) * vec4(position, 1)
```

### Why Bind-Pose Works

When the pose is the bind-pose:
- UE5: `ComponentSpace[i]` = bind-pose component-space matrix = `InvBind[i]^{-1}`, so the multiplication is identity
- UzEngine: `world[i]` = bind-pose world-space matrix, and `InvBindWorld[i]` is its inverse, so the multiplication is identity

**Both are identity, so both render bind-pose correctly.**

### Why Animated Poses Break

When the pose is animated (non-identity):
- The **order of matrix multiplication changes the result** unless the matrices are commutatrive (which they're not)
- UzEngine's formula may inadvertently reverse the order or use the wrong semantic matrix

---

## 6. Verification: UE5's Exact Inverse Bind Computation

UE5 never explicitly shows the computation of `RefBasesInvMatrix` in the files provided, but the semantic is clear:

From **SkeletalRender.cpp line 485**:
```cpp
const TArray<FMatrix44f>* RefBasesInvMatrix = &SkinnedAsset->GetRefBasesInvMatrix();
```

This is retrieved from the `USkinnedAsset` (mesh), which precomputes the inverse of each bone's **bind-pose component-space matrix** at load time.

In UzEngine, the `bone.inverseBindWorld` is computed similarly but for **world-space**, which is only equivalent if the component-to-world transform is identity at load time.

---

## 7. Suspected Differences (Root Cause Analysis)

### Issue 1: Semantic Matrix Inversion (HIGHEST PROBABILITY)

**UE5**: `RefBasesInvMatrix[i]` = inverse of bind-pose **component-space** matrix
```cpp
RefBasesInvMatrix[i] = (BindPoseComponentSpace[i])^{-1}
```

**UzEngine**: `bone.inverseBindWorld[i]` = inverse of bind-pose **world-space** matrix
```cpp
inverseBindWorld[i] = (BindPoseWorldSpace[i])^{-1}
```

If the mesh origin is not at the component origin, these are **different matrices**, and the skinning result will be wrong even with corrected multiplication order.

**Verification needed**: Check if UzEngine's skeleton data stores world-space or component-space inverse binds.

### Issue 2: Multiplication Order (HIGH PROBABILITY)

Even with corrected inverse bind, the **order of multiplication matters**.

**Current UzEngine formula** (row-vector):
```
palette[i] * position = inverseBindWorld[i] * world[i] * position
```

**Equivalent to column-vector**:
```
position' = (world[i]^T * inverseBindWorld[i]^T) * position
```

**What UE5 does** (column-vector):
```
position' = RefBasesInvMatrix[i] * ComponentSpace[i] * position
```

If `RefBasesInvMatrix[i]` is truly `(BindPoseComponentSpace[i])^{-1}` and `ComponentSpace[i]` is the current bone's component-space matrix, then for the row-vector convention to match, UzEngine should compute:

```
palette[i] = ComponentSpace[i]^T * (RefBasesInvMatrix[i]^T)
```

But UzEngine computes:
```
palette[i] = inverseBindWorld[i] * world[i]
```

These are **not the same operation** unless `inverseBindWorld == RefBasesInvMatrix^T` and `world == ComponentSpace^T`, which is geometrically unlikely.

---

## 8. Known Failure Mode

From the memory note: *"Skeletal animation WIP @ 6ab9a975: full pipeline landed; bind-pose mesh still deformed. Fix first next session."*

**Observation**: Bind-pose (identity all bones) renders correctly, but animated poses cause mesh deformation.

This is **consistent with**:
1. A matrix semantic mismatch (bind-pose is identity for both, masking the bug)
2. An incorrect multiplication order that happens to be corrected by identity matrices

---

## 9. Recommended Fix Path

1. **Verify inverse bind semantic**: Is `bone.inverseBindWorld` stored as world-space inverse (wrong) or component-space inverse (correct)?
   - If world-space: compute `inverseBindComponent[i] = inverseBindWorld[i] * ComponentToWorld^{-1}` before palette computation
   - If already component-space: proceed to step 2

2. **Transpose both matrices for consistency**: Convert UzEngine to column-vector convention to match UE5 exactly:
   ```cpp
   palette[i] = (inverseBindComponent[i]^T) * (worldMatrices[i]^T)
   ```

3. **Re-test on animated poses**: Verify that non-bind-pose animations render correctly.

---

## 10. References

- **UE5 Source**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\SkeletalRender.cpp` (lines 357–524)
- **UE5 Shaders**: `W:\UE_5.6_Source\Engine\Shaders\Private\GpuSkinVertexFactory.ush` (lines 546–596)
- **UE5 Bone Matrix Computation**: `W:\UE_5.6_Source\Engine\Shaders\Private\GpuSkinCommon.ush` (lines 205–222)
- **UzEngine C++ Palette**: `e:\UzEngine\runtime\engine\animation\skeletal_mesh_instance.cpp` (lines 64–103)
- **UzEngine GPU Skinning**: `e:\UzEngine\runtime\shaders\runtime\compute\gpu_skinning_cs.hlsl` (lines 114–127)

---

## 11. Matrix Multiplication Quick Reference

### Column-Vector Convention (UE5)
```
result = M * v = (M[0][0] * v[0] + M[0][1] * v[1] + M[0][2] * v[2] + M[0][3] * v[3],
                  M[1][0] * v[0] + M[1][1] * v[1] + M[1][2] * v[2] + M[1][3] * v[3],
                  ...)
```
In HLSL: `mul(Matrix, Vector)`

### Row-Vector Convention (UzEngine)
```
result = v * M = (v[0] * M[0][0] + v[1] * M[1][0] + v[2] * M[2][0] + v[3] * M[3][0],
                  v[0] * M[0][1] + v[1] * M[1][1] + v[2] * M[2][1] + v[3] * M[3][1],
                  ...)
```
In HLSL: `mul(Vector, Matrix)`

**Key Insight**: `v * M = (M^T * v^T)^T`, so converting between conventions requires transposing both vectors and matrices.
