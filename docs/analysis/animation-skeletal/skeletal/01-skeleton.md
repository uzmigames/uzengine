# UE5 Skeleton Reference Analysis

## Executive Summary

UE5's skeleton system stores bone hierarchies in **topologically-ordered flat arrays** with **parent indices** always pointing to earlier bones. Each bone has a **local-space bind-pose transform** (relative to parent), which is composed up the parent chain to produce **component-space (model-local) bind matrices**. These are inverted and precomputed into `RefBasesInvMatrix` for GPU skinning. The skinning pipeline multiplies vertices by `RefBasesInvMatrix[bone] * currentComponentSpaceBone[bone]` — the inverse bind matrix transforms vertices from component space into bone-local space, then the current bone matrix moves them to the animated position. UzEngine's `Skeleton` struct correctly replicates the hierarchy structure but must verify that **inverse bind matrix computation matches UE5's exact formula and matrix math conventions** (column-vector multiplication with row-major storage).

---

## 1. FReferenceSkeleton — The Authoritative Bone Hierarchy

**Files:** `Engine/Source/Runtime/Engine/Public/ReferenceSkeleton.h:99–244`

### Storage Layout

UE5 maintains **two parallel bone hierarchies**: RawRefBoneInfo (original asset bones) and FinalRefBoneInfo (raw + virtual bones). Both follow the same structure:

```cpp
// ReferenceSkeleton.h:108-116
private:
    TArray<FMeshBoneInfo> RawRefBoneInfo;   // Bones from original asset
    TArray<FTransform>    RawRefBonePose;   // Per-bone local-space bind pose
    
    TArray<FMeshBoneInfo> FinalRefBoneInfo; // Raw bones + virtual bones added by USkeleton
    TArray<FTransform>    FinalRefBonePose; // Per-bone local-space bind pose (final)
```

Each `FMeshBoneInfo` (defined in `ReferenceSkeleton.h:13–42`) contains:

```cpp
struct FMeshBoneInfo {
    FName Name;                    // Bone name (e.g., "Armature|Spine")
    int32 ParentIndex;             // INDEX_NONE for root bones, else index of parent
};
```

**Critical invariant** (ReferenceSkeleton.h:185–186):
- Parent index must either be `INDEX_NONE` (root) **or a valid index strictly less than the current bone's index**.
- This guarantees **topological ordering**: parents are always added before children.

### Space of RefBonePose — LOCAL SPACE

The `RawRefBonePose` and `FinalRefBonePose` arrays store **bone-local transforms** (relative to parent), NOT component space:

- **Root bone**: Transform is relative to the component (skeletal mesh) origin.
- **Child bones**: Transform is relative to their parent's local space.

**Evidence:** `ReferenceSkeleton.h:263–266` exposes const access to ref bone poses:

```cpp
const TArray<FTransform> & GetRefBonePose() const {
    return FinalRefBonePose;  // Local space per bone
}
```

And the documentation comment at `ReferenceSkeleton.h:110–111` labels it:
```cpp
/** Reference bone transform **/
TArray<FTransform> RawRefBonePose;
```

No "ComponentSpace" qualifier — this is **local transform per bone, relative to parent**.

---

## 2. USkeleton — The Shared Skeleton Asset

**Files:** `Engine/Source/Runtime/Engine/Classes/Animation/Skeleton.h:1–250` (partial read above)

### Relationship to FReferenceSkeleton

`USkeleton` is the **shared asset** containing a `FReferenceSkeleton` member:

- **One `USkeleton`** can be referenced by **multiple `USkeletalMesh` assets**.
- Each mesh has its own `RefSkeleton` (loaded from the mesh asset), but animations refer to the shared `USkeleton`.
- The mapping between mesh RefSkeleton and skeleton RefSkeleton is done via **bone name matching**, not index.

**Key data from `Skeleton.h:46–67`:**
```cpp
USTRUCT()
struct FSkeletonToMeshLinkup {
    UPROPERTY()
    TArray<int32> SkeletonToMeshTable;  // Skeleton bone index → Mesh bone index
    
    UPROPERTY()
    TArray<int32> MeshToSkeletonTable;  // Mesh bone index → Skeleton bone index
};
```

This allows multiple meshes to share one skeleton even if they have **different bone counts or ordering** — the mapping tables handle the translation.

---

## 3. RefBasesInvMatrix — The Inverse-Bind-Matrix Array (CRITICAL)

**Files:** 
- `Engine/Source/Runtime/Engine/Classes/Engine/SkeletalMesh.h:80–138` (enum flag)
- `Engine/Source/Runtime/Engine/Private/SkeletalMesh.cpp:2288–2331`

### Storage Location

`RefBasesInvMatrix` is stored in `USkeletalMesh` (NOT in `USkeleton`):

```cpp
// SkeletalMesh.h — the enum shows it's an async property of USkeletalMesh
enum class ESkeletalMeshAsyncProperties : uint64 {
    RefBasesInvMatrix = 1 << 4,  // Line 81
    // ...
};
```

The array is accessed via `GetRefBasesInvMatrix()` which returns `TArray<FMatrix44f>&`.

### Computation Algorithm

**Source: `SkeletalMesh.cpp:2288–2331`**

```cpp
void USkeletalMesh::CalculateInvRefMatrices() {
    const int32 NumRealBones = GetRefSkeleton().GetRawBoneNum();
    TArray<FMatrix>& ComposedRefPoseMatrices = GetCachedComposedRefPoseMatrices();
    
    // Allocate or reallocate
    if(GetRefBasesInvMatrix().Num() != NumRealBones) {
        GetRefBasesInvMatrix().Empty(NumRealBones);
        GetRefBasesInvMatrix().AddUninitialized(NumRealBones);
        ComposedRefPoseMatrices.Empty(NumRealBones);
        ComposedRefPoseMatrices.AddUninitialized(NumRealBones);
        
        // For each bone:
        for(int32 b = 0; b < NumRealBones; b++) {
            // Step 1: Convert local-space bone transform to matrix
            ComposedRefPoseMatrices[b] = GetRefPoseMatrix(b);  // Local matrix
            
            // Step 2: Compose up the parent chain to get component-space matrix
            if(b > 0) {
                int32 Parent = GetRefSkeleton().GetRawParentIndex(b);
                ComposedRefPoseMatrices[b] = ComposedRefPoseMatrices[b] 
                                            * ComposedRefPoseMatrices[Parent];
            }
            
            // Step 3: Invert the component-space matrix
            GetRefBasesInvMatrix()[b] = FMatrix44f(ComposedRefPoseMatrices[b].Inverse());
        }
    }
}
```

**KEY INSIGHT:** The multiplication order on line 2313 is:
```
ComposedRefPoseMatrices[b] = ComposedRefPoseMatrices[b] * ComposedRefPoseMatrices[Parent]
```

In UE5's **column-vector math convention** (where vectors are transformed as `v' = M * v`), this means:
- **First apply the child's local matrix** (leftmost in multiplication)
- **Then apply the parent's component-space matrix** (rightmost in multiplication)
- Result: component-space matrix for the child

### GetRefPoseMatrix — Local-to-Matrix Conversion

**Source: `SkeletalMesh.cpp:4934–4941`**

```cpp
FMatrix USkeletalMesh::GetRefPoseMatrix(int32 BoneIndex) const {
    check(BoneIndex >= 0 && BoneIndex < GetRefSkeleton().GetRawBoneNum());
    FTransform BoneTransform = GetRefSkeleton().GetRawRefBonePose()[BoneIndex];
    BoneTransform.NormalizeRotation();
    return BoneTransform.ToMatrixWithScale();
}
```

This retrieves the **local-space FTransform** and converts it to an `FMatrix` using `ToMatrixWithScale()`.

---

## 4. Bind-Pose Chain: Local TRS → Component Space → Inverse Bind

**Files:**
- `Engine/Source/Runtime/Core/Public/Math/TransformNonVectorized.h:241–290` (ToMatrixWithScale)
- `Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:2683–2701` (FillUpComponentSpaceTransforms)

### FTransform::ToMatrixWithScale — TRS to Matrix

**Source: `TransformNonVectorized.h:241–290`**

Converts a **local-space** `FTransform` (Translation, Rotation, Scale) to an `FMatrix`:

```cpp
FORCEINLINE TMatrix<T> ToMatrixWithScale() const {
    TMatrix<T> OutMatrix;
    
    // Translation goes into row 3 (M[3][0], M[3][1], M[3][2])
    OutMatrix.M[3][0] = Translation.X;
    OutMatrix.M[3][1] = Translation.Y;
    OutMatrix.M[3][2] = Translation.Z;
    
    // Rotation + Scale computed from quaternion
    // (27 lines of rotation matrix algebra omitted for brevity)
    // Final matrix: M[i][j] where i=row, j=column
    
    OutMatrix.M[0][3] = 0.0f;
    OutMatrix.M[1][3] = 0.0f;
    OutMatrix.M[2][3] = 0.0f;
    OutMatrix.M[3][3] = 1.0f;
    return OutMatrix;
}
```

**Key layout** (row-major storage, column-vector math):
- **Rows 0–2, Cols 0–2**: Rotation * Scale (3x3 part)
- **Row 3, Cols 0–2**: Translation (stored in last row)
- **Col 3, Rows 0–2**: 0 (no perspective)
- **M[3][3]**: 1 (homogeneous)

**Math convention**: When multiplied with a column vector `v = [x, y, z, 1]ᵀ`, the result is `M * v` (matrix on left, vector on right), which applies transformations in this order:
1. Rotate (matrix rows 0–2, cols 0–2)
2. Scale (baked into rotation)
3. Translate (last row)

### FillUpComponentSpaceTransforms — Composing the Hierarchy

**Source: `AnimationRuntime.cpp:2683–2701`**

```cpp
void FAnimationRuntime::FillUpComponentSpaceTransforms(
    const FReferenceSkeleton& RefSkeleton,
    const TArrayView<const FTransform> &BoneSpaceTransforms,  // LOCAL
    TArray<FTransform> &ComponentSpaceTransforms)             // OUTPUT
{
    ComponentSpaceTransforms.Empty(BoneSpaceTransforms.Num());
    ComponentSpaceTransforms.AddUninitialized(BoneSpaceTransforms.Num());
    
    for (int Index = 0; Index < ComponentSpaceTransforms.Num(); ++Index) {
        int32 ParentIndex = RefSkeleton.GetParentIndex(Index);
        if (ParentIndex != INDEX_NONE) {
            // Transform composition: child * parent in FTransform space
            ComponentSpaceTransforms[Index] = 
                BoneSpaceTransforms[Index] * ComponentSpaceTransforms[ParentIndex];
        } else {
            ComponentSpaceTransforms[Index] = BoneSpaceTransforms[Index];
        }
    }
}
```

**Critical:** In UE5's `FTransform` **operator\***, the semantics are:
```
(T1 * T2).TransformPosition(P) == T2.TransformPosition(T1.TransformPosition(P))
```

This means: **first apply left-side transform, then right-side transform**.

So: `BoneSpaceTransforms[Index] * ComponentSpaceTransforms[ParentIndex]` means:
1. First: Apply parent's component-space transform
2. Then: Apply child's bone-local transform

This produces the **component-space transform for the child**.

---

## 5. The Skinning Pipeline: Bind Vertex → Animated Vertex

**Key Formula (from SkeletalMesh.cpp:2313, 2328):**

```
1. ComposedRefPoseMatrices[b] = GetRefPoseMatrix(b)  // Local matrix
2. if (b > 0):
     ComposedRefPoseMatrices[b] *= ComposedRefPoseMatrices[Parent]  // Compose to component space
3. RefBasesInvMatrix[b] = ComposedRefPoseMatrices[b].Inverse()     // Invert for skinning
```

### Vertex Transformation (GPU Skinning)

Vertices in the skeletal mesh are stored in **component space (mesh-local space)** at bind pose:

```
BoundVertex_ComponentSpace = BoundVertex at rest (in skeletal mesh space)
```

To move this vertex when animated:

```cpp
AnimatedVertex = CurrentBonePalette[boneIndex] * InverseBindPalette[boneIndex] 
                 * BoundVertex_ComponentSpace

// Expanded:
AnimatedVertex = CurrentBoneComponentSpace[boneIndex] 
               * RefBasesInvMatrix[boneIndex]
               * BoundVertex_ComponentSpace
```

**Step-by-step:**
1. `RefBasesInvMatrix[boneIndex] * BoundVertex_ComponentSpace`
   - Transforms vertex from component space → bone-local space at bind pose
2. `CurrentBoneComponentSpace[boneIndex] * (result of step 1)`
   - Transforms bone-local vertex → component space at current animated pose

---

## 6. Matrix Math Convention in UE5

**Files:** `Math/Matrix.h:27–34`

UE5 uses **column-vector math** with **row-major storage**:

```cpp
// From Matrix.h:27-34
/**
 * 4x4 matrix of floating point values.
 * 
 * Note that C = A * B applies A first, then B:
 *   (A*B).TransformPosition(Pt) == B.TransformPosition(A.TransformPosition(Pt))
 * 
 * Matrix elements are accessed with M[RowIndex][ColumnIndex].
 */
```

This means:
- **Storage**: Row-major (`M[row][col]`)
- **Math**: Column vector multiplication (`M * v` where v is a column)
- **Composition**: `A * B` = first apply A, then apply B (left-to-right)

### Consequence for SkeletalMesh.cpp:2313

```cpp
ComposedRefPoseMatrices[b] = ComposedRefPoseMatrices[b] * ComposedRefPoseMatrices[Parent]
```

The child's component-space matrix is: **child_local × parent_component-space**
- First: rotate/scale/translate by child's local transform
- Then: apply parent's component-space transform
- Result: component-space for child

---

## 7. UzEngine Mapping

| UE5 Type/Concept | UzEngine Equivalent | Location | Notes |
|---|---|---|---|
| `FReferenceSkeleton` | `uz::Skeleton` | `runtime/engine/animation/skeleton.h:90–242` | Flat bone array, topological order, local transforms |
| `FMeshBoneInfo` | `uz::BoneInfo` (partial) | `skeleton.h:54–84` | Name, parent index, bind transform |
| `RawRefBonePose[i]` | `BoneInfo::bindTranslation/bindRotation/bindScale` | `skeleton.h:63–69` | Local-space bind transform per bone |
| `RefBasesInvMatrix[i]` | `BoneInfo::inverseBindWorld` | `skeleton.h:71–78` | Precomputed inverse of component-space bind matrix |
| `FReferenceSkeleton::RawRefBoneInfo` | `Skeleton::m_bones` | `skeleton.h:232` | Vector of BoneInfo structs |
| `FReferenceSkeleton::RawNameToIndexMap` | `Skeleton::m_nameIndex` | `skeleton.h:235` | Name-to-index lookup |
| `FAnimationRuntime::FillUpComponentSpaceTransforms` | **NOT YET IMPLEMENTED** | N/A | Must compute component-space poses from local poses |
| `CalculateInvRefMatrices()` | `Skeleton::ComputeInverseBindMatrices()` | `skeleton.h:162` | Compute inverse bind matrices |

---

## Root Cause: UzEngine's inverseBindWorld Computation

**Current Implementation** (`skeleton.cpp` or wherever `ComputeInverseBindMatrices()` is defined):

UzEngine likely either:
1. **Skips component-space composition** — computes `inverseBindWorld` from local bone matrices directly (WRONG)
2. **Uses incorrect matrix order** — applies parent-then-child instead of child-then-parent (WRONG)
3. **Doesn't invert properly** — stores the bind matrix instead of its inverse (WRONG)

The skeletal animation WIP memo notes: *"bind-pose mesh still deformed. Fix first next session."* This indicates the bind pose itself is wrong, not the animation. The deformation on bind pose + any animation suggests the inverse bind matrices are incorrect.

**What to check:**
1. Does `ComputeInverseBindMatrices()` compose local transforms up the parent chain? (line 2312 in SkeletalMesh.cpp)
2. Does it use the correct multiplication order: `child * parent` not `parent * child`?
3. Does it invert the final component-space matrix?
4. Are the inverted matrices stored in `BoneInfo::inverseBindWorld` (or equivalent)?

If any of these is wrong, bind-pose vertices will be incorrectly transformed, producing deformation even at bind pose.

---

## References

- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\ReferenceSkeleton.h` — Skeleton hierarchy structure
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Animation\Skeleton.h` — Shared skeleton asset
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\SkeletalMesh.h` — RefBasesInvMatrix declaration
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\SkeletalMesh.cpp:2288–2331` — CalculateInvRefMatrices implementation
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\SkeletalMesh.cpp:4934–4941` — GetRefPoseMatrix
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Animation\AnimationRuntime.cpp:2683–2701` — FillUpComponentSpaceTransforms
- `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\TransformNonVectorized.h:241–290` — FTransform::ToMatrixWithScale
- `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\Matrix.h:27–34` — Matrix math convention documentation
