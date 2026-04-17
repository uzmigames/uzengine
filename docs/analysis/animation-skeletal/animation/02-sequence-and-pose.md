# UE5 Animation Sequence & Pose Evaluation Analysis

**Document ID:** `02-animation-sequence-and-pose`  
**Version:** 1.0  
**Date:** 2026-04-16  
**Context:** UzEngine skeletal mesh deformation bug — bind pose works, non-identity animated poses produce heavy mesh deformation. This analysis documents UE5's complete pose evaluation pipeline to identify root cause.

---

## Executive Summary

UE5's animation system evaluates poses through a rigorous pipeline:

1. **UAnimSequence** — compressed per-bone keyframe tracks (translation/rotation/scale)
2. **FRawAnimSequenceTrack** — raw uncompressed tracks (PosKeys[], RotKeys[], ScaleKeys[])
3. **Decompression** → **FCompactPose** — sampled pose in local-to-parent bone space
4. **Additive evaluation** — if the clip is marked additive, applies base pose delta
5. **Component-space conversion** — local transforms composed hierarchically
6. **Palette composition** — each bone: `palette[i] = inverseBind[i] × world[i]`

**Critical constraint:** All animation keys are **ALWAYS in LOCAL-TO-PARENT bone space**. UE never stores or evaluates in component space. Misunderstanding this single fact causes the exact deformation seen in UzEngine.

---

## 1. UAnimSequence — Compressed Keyframe Storage

### 1.1 Header Fields and Metadata

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Animation\AnimSequence.h:201-300`

```cpp
UCLASS(config=Engine, hidecategories=(UObject, Length), BlueprintType, MinimalAPI)
class UAnimSequence : public UAnimSequenceBase
{
    // Per-sequence metadata
    UPROPERTY(Category = Compression, EditAnywhere, meta = (ForceShowEngineContent))
    TObjectPtr<class UAnimBoneCompressionSettings> BoneCompressionSettings;

    UPROPERTY(Category = Compression, EditAnywhere, meta = (ForceShowEngineContent))
    TObjectPtr<class UAnimCurveCompressionSettings> CurveCompressionSettings;

    // Additive animation metadata
    UPROPERTY(EditAnywhere, Category=AdditiveSettings, AssetRegistrySearchable)
    TEnumAsByte<enum EAdditiveAnimationType> AdditiveAnimType;

    /* Additive reference pose type. Refer above enum type */
    UPROPERTY(EditAnywhere, Category=AdditiveSettings, meta=(DisplayName = "Base Pose Type"))
    TEnumAsByte<enum EAdditiveBasePoseType> RefPoseType;

    /* Additive reference frame if RefPoseType == AnimFrame **/
    UPROPERTY(EditAnywhere, Category = AdditiveSettings)
    int32 RefFrameIndex;
    
    /* Additive reference animation if it's relevant - i.e. AnimScaled or AnimFrame **/
    UPROPERTY(EditAnywhere, Category=AdditiveSettings, meta=(DisplayName = "Base Pose Animation"))
    TObjectPtr<class UAnimSequence> RefPoseSeq;
};
```

**Key properties:**
- `BoneCompressionSettings` — algorithm used to compress bone tracks (varies per sequence)
- `CurveCompressionSettings` — morphtarget/scalar curve compression
- `AdditiveAnimType` — one of `AAT_None`, `AAT_LocalSpaceBase`, `AAT_RotationOffsetMeshSpace`
- `RefPoseType` — how to compute the additive base pose (`ABPT_RefPose`, `ABPT_AnimScaled`, `ABPT_AnimFrame`, `ABPT_LocalAnimFrame`)
- `RefPoseSeq` — optional reference animation for additive blending

### 1.2 Compressed vs. Uncompressed Data

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Animation\AnimCompressionTypes.h:223-260`

**Compressed storage** — used at runtime:
```cpp
struct FCompressedAnimSequence {
    // Platform-specific compressed track data (varies by platform)
    FCompressedAnimData CompressedDataStructure;
    
    // Maps from compressed track index to skeleton bone index
    TArray<FTrackToSkeletonMap> CompressedTrackToSkeletonMapTable;
    
    // Compressed curve names and values
    TArray<FAnimCompressedCurveIndexedName> IndexedCurveNames;
};
```

**Raw storage** — used in editor:
```cpp
struct FRawAnimSequenceTrack {
    UPROPERTY()
    TArray<FVector3f> PosKeys;      // Translation keys
    
    UPROPERTY()
    TArray<FQuat4f> RotKeys;        // Rotation keys
    
    UPROPERTY()
    TArray<FVector3f> ScaleKeys;    // Scale keys
    
    // Times are stored per-track, not per-key (shared among all tracks)
};
```

**CRITICAL:** Both compressed and uncompressed tracks store **LOCAL-TO-PARENT bone space** transforms. UE does NOT store component-space keys.

### 1.3 Additive Animation Types

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Animation\AnimTypes.h:586-595`

```cpp
enum EAdditiveAnimationType : int
{
    /** No additive. */
    AAT_None  UMETA(DisplayName="No additive"),
    
    /* Create Additive from LocalSpace Base. */
    AAT_LocalSpaceBase UMETA(DisplayName="Local Space"),
    
    /* Create Additive from MeshSpace Rotation Only, Translation still will be LocalSpace. */
    AAT_RotationOffsetMeshSpace UMETA(DisplayName="Mesh Space"),
    
    AAT_MAX,
};
```

**Key point:** Even `AAT_RotationOffsetMeshSpace` stores **translation in local space**; only rotation is converted to mesh (component) space.

---

## 2. FRawAnimSequenceTrack — Per-Bone Keyframe Streams

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\Animation\AnimTypes.h:864-937`

```cpp
struct FRawAnimSequenceTrack
{
    GENERATED_USTRUCT_BODY()

    /** Position keys. */
    UPROPERTY()
    TArray<FVector3f> PosKeys;      // Translation in LOCAL bone space

    /** Rotation keys. */
    UPROPERTY()
    TArray<FQuat4f> RotKeys;        // Rotation in LOCAL bone space

    /** Scale keys. */
    UPROPERTY()
    TArray<FVector3f> ScaleKeys;    // Scale in LOCAL bone space

    // Serializer + validation code
    friend FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrack& T);
    bool ContainsNaN() const;
    bool Serialize(FArchive& Ar);
};
```

**Documentation comment (implicit):** There are NO explicit comments in the struct, but UE's animation pipeline treats all keys as **LOCAL-TO-PARENT**. This is the **#1 undocumented assumption** in the codebase — every codec, every decompression function, and every pose evaluation assumes this without stating it.

**Why LOCAL space?** 
- Allows retargeting without recompressing (scale skeletons without per-frame delta changes)
- Supports humanoid characters with varied proportions
- Enables LOD-based bone culling (skinned weights handle missing bones)

---

## 3. Pose Evaluation — FCompactPose & Index Spaces

### 3.1 Three Bone Index Types

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\AnimationCore\Public\BoneIndices.h:62-83`

```cpp
// This represents a compact pose bone index. A compact pose is held by a bone 
// container and can have a different ordering than either the skeleton or skeletal mesh.
struct FCompactPoseBoneIndex : public FBoneIndexBase
{
    explicit FCompactPoseBoneIndex(int32 InBoneIndex) { BoneIndex = InBoneIndex; }
    UE_BONE_INDEX_OPERATORS(FCompactPoseBoneIndex)
};

// This represents a skeletal mesh bone index which may differ from the 
// skeleton bone index it corresponds to.
struct FMeshPoseBoneIndex : public FBoneIndexBase
{
    explicit FMeshPoseBoneIndex(int32 InBoneIndex) { BoneIndex = InBoneIndex; }
    UE_BONE_INDEX_OPERATORS(FMeshPoseBoneIndex)
};

// This represents a skeleton bone index which may differ from the skeletal 
// mesh bone index it corresponds to.
struct FSkeletonPoseBoneIndex : public FBoneIndexBase
{
    explicit FSkeletonPoseBoneIndex(int32 InBoneIndex) { BoneIndex = InBoneIndex; }
    UE_BONE_INDEX_OPERATORS(FSkeletonPoseBoneIndex)
};
```

**Three index spaces exist:**

| Index Type | Used For | Remapping |
|---|---|---|
| `FSkeletonPoseBoneIndex` | Animation clip metadata (track-to-bone map) | Skeleton defines the canonical order |
| `FMeshPoseBoneIndex` | GPU skinning weights | SkeletalMesh LOD reordering |
| `FCompactPoseBoneIndex` | Runtime pose evaluation | Runtime bone filter (only required bones) |

The **BoneContainer** (`FBoneContainer`) maps between these three spaces at runtime.

### 3.2 FCompactPose Structure

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Public\BonePose.h:102-173`

```cpp
template <typename InAllocator>
struct FBaseCompactPose : FBasePose<FCompactPoseBoneIndex, InAllocator>
{
public:
    FBaseCompactPose() : BoneContainer(nullptr) {}

    typedef FCompactPoseBoneIndex BoneIndexType;
    
    // Access the bone container (holds remapping tables)
    const FBoneContainer& GetBoneContainer() const 
    {
        checkSlow(BoneContainer && BoneContainer->IsValid());
        return *BoneContainer;
    }

    void SetBoneContainer(const FBoneContainer* InBoneContainer)
    {
        check(InBoneContainer && InBoneContainer->IsValid());
        BoneContainer = InBoneContainer;
        this->InitBones(BoneContainer->GetBoneIndicesArray().Num());
    }

protected:
    const FBoneContainer* BoneContainer;
};

// Actual pose used at runtime
struct FCompactPose : FBaseCompactPose<FDefaultAllocator>
{
    // Inherits bone array indexed by FCompactPoseBoneIndex
};
```

**Key property:** FCompactPose stores **local-to-parent transforms**. Index `[0]` is always the root bone. The array is densely packed (only required bones), not sparse.

---

## 4. GetBonePose — Pose Extraction Pipeline

### 4.1 Main Entry Point

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Animation\AnimSequence.cpp:1731-1871`

```cpp
void UAnimSequence::GetBonePose(
    FAnimationPoseData& OutAnimationPoseData, 
    const FAnimExtractContext& ExtractionContext, 
    bool bForceUseRawData /*= false*/) const
{
    SCOPE_CYCLE_COUNTER(STAT_AnimSeq_GetBonePose);
    
    FCompactPose& OutPose = OutAnimationPoseData.GetPose();
    FBoneContainer& RequiredBones = OutPose.GetBoneContainer();

    // Initialize to ref pose (or additive identity if additive animation)
    const bool bIsBakedAdditive = !bUseRawDataForPoseExtraction && IsValidAdditive();
    
    if (bIsBakedAdditive)
    {
        OutPose.ResetToAdditiveIdentity();  // All bones = (rot=identity, trans=0, scale=1)
    }
    else
    {
        OutPose.ResetToRefPose();  // Initialize to skeleton's bind pose
    }

    // ─── Sample animated tracks ────────────────────────────────────────────
    
    const int32 NumTracks = PlatformCompressedData.CompressedTrackToSkeletonMapTable.Num();

    if (NumTracks != 0)
    {
        // Create decompression context
        FAnimSequenceDecompressionContext DecompContext(
            PlatformTargetFrameRate.Default, 
            PlatformTargetFrameRate.Default.AsFrameTime(GetPlayLength()).RoundToFrame().Value,
            InterpolationType,
            GetRetargetTransformsSourceName(),
            *PlatformCompressedData.CompressedDataStructure,
            GetSkeleton()->GetRefLocalPoses(),
            PlatformCompressedData.CompressedTrackToSkeletonMapTable,
            GetSkeleton(),
            IsValidAdditive(),
            AdditiveAnimType);

        // Decompress and sample pose
        UE::Anim::Decompression::DecompressPose(
            OutPose,
            PlatformCompressedData,
            ExtractionContext,
            DecompContext,
            GetRetargetTransforms(),
            RootMotionReset);
    }

    // ─── Evaluate curves (morph targets, scalar values) ────────────────────
    
    EvaluateCurveData_Lockless(OutAnimationPoseData.GetCurve(), ExtractionContext, bUseRawDataForPoseExtraction);
    
    // ─── Evaluate animation attributes ───────────────────────────────────
    
    EvaluateAttributes(OutAnimationPoseData, ExtractionContext, false);
}
```

**Algorithm:**
1. **Initialize** pose to ref pose (identity relative to bind pose)
2. **Decompress/sample** animated bone tracks into the pose
3. **Evaluate** curves and attributes
4. **(Optional) Apply additive** if the sequence is marked additive

**CRITICAL:** The pose returned contains **LOCAL-TO-PARENT transforms**. The palette composition step (in SkeletalMeshInstance) converts these to component space.

### 4.2 Additive Pose Extraction

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Animation\AnimSequence.cpp:1873-1905`

```cpp
void UAnimSequence::GetBonePose_Additive(
    FAnimationPoseData& OutAnimationPoseData, 
    const FAnimExtractContext& ExtractionContext) const
{
    FCompactPose& OutPose = OutAnimationPoseData.GetPose();
    FBlendedCurve& OutCurve = OutAnimationPoseData.GetCurve();

    if (!IsValidAdditive())
    {
        OutPose.ResetToAdditiveIdentity();
        return;
    }

    // Extract target pose (the full animated pose)
    GetBonePose(OutAnimationPoseData, ExtractionContext);

    // Extract base pose (reference for delta)
    FCompactPose BasePose;
    FBlendedCurve BaseCurve;
    BasePose.SetBoneContainer(&OutPose.GetBoneContainer());
    BaseCurve.InitFrom(OutCurve);

    FAnimationPoseData BasePoseData(BasePose, BaseCurve);
    GetAdditiveBasePose(BasePoseData, ExtractionContext);

    // Convert target - base = additive delta
    FAnimationRuntime::ConvertPoseToAdditive(OutPose, BasePose);
    OutCurve.ConvertToAdditive(BaseCurve);
}

void UAnimSequence::GetAdditiveBasePose(
    FAnimationPoseData& OutAnimationPoseData, 
    const FAnimExtractContext& ExtractionContext) const
{
    // Four possible base poses:
    // 1. ABPT_RefPose: skeleton's bind pose (default)
    // 2. ABPT_AnimScaled: reference animation, time-normalized
    // 3. ABPT_AnimFrame: reference animation at specific frame
    // 4. ABPT_LocalAnimFrame: this animation at specific frame
    
    switch (RefPoseType)
    {
        case ABPT_AnimScaled:
        {
            // Normalize time: map [0, CurrentSeqLength] to [0, RefSeqLength]
            double Fraction = FMath::Clamp<double>(
                ExtractionContext.CurrentTime / (double)GetPlayLength(), 0.0, 1.0);
            double BasePoseTime = (double)RefPoseSeq->GetPlayLength() * Fraction;
            
            FAnimExtractContext BasePoseContext(ExtractionContext);
            BasePoseContext.CurrentTime = BasePoseTime;
            RefPoseSeq->GetBonePose(OutAnimationPoseData, BasePoseContext);
            break;
        }
        case ABPT_AnimFrame:
        {
            GetSequencePose(OutAnimationPoseData, ExtractionContext, *RefPoseSeq, RefFrameIndex);
            break;
        }
        case ABPT_LocalAnimFrame:
        {
            GetSequencePose(OutAnimationPoseData, ExtractionContext, *this, RefFrameIndex);
            break;
        }
        case ABPT_RefPose:
        default:
            OutAnimationPoseData.GetPose().ResetToRefPose();
            break;
    }
}
```

**Critical control flow for additive animations:**
1. Sample the target animation clip at time `t`
2. Sample the base animation (either reference clip or this clip at frame N)
3. **Subtract base from target** using `ConvertPoseToAdditive`

---

## 5. Transform Composition — FTransform::Multiply

### 5.1 FTransform Structure

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\TransformNonVectorized.h:37-54`

```cpp
template<typename T>
struct alignas(alignof(TQuat<T>)) TTransform
{
protected:
    /** Rotation of this transformation, as a quaternion. */
    TQuat<T>    Rotation;
    
    /** Translation of this transformation, as a vector. */
    TVector<T>  Translation;
    
    /** 3D scale (always applied in local space) as a vector. */
    TVector<T>  Scale3D;
};
```

**Layout:** Rotation (16 B) + Translation (12 B) + Scale3D (12 B) = 40 B.

### 5.2 Transform Composition (A * B)

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\TransformNonVectorized.h:1311-1348`

```cpp
FORCEINLINE void TTransform<T>::Multiply(
    TTransform<T>* OutTransform, 
    const TTransform<T>* A, 
    const TTransform<T>* B)
{
    //  QST(A) = Q(A)*S(A)*P*-Q(A) + T(A)
    //  QST(AxB) = Q(B)*S(B)*QST(A)*-Q(B) + T(B)
    //
    //  Expanding:
    //  QST(AxB) = Q(B)*S(B)*Q(A)*S(A)*P*-Q(A)*-Q(B) + Q(B)*S(B)*T(A)*-Q(B) + T(B)
    //
    //  Simplified:
    //  Q(AxB) = Q(B) * Q(A)
    //  S(AxB) = S(A) * S(B)
    //  T(AxB) = Q(B) * S(B) * T(A) * -Q(B) + T(B)

    if (AnyHasNegativeScale(A->Scale3D, B->Scale3D))
    {
        MultiplyUsingMatrixWithScale(OutTransform, A, B);
    }
    else
    {
        OutTransform->Rotation = B->Rotation * A->Rotation;
        OutTransform->Scale3D = A->Scale3D * B->Scale3D;
        OutTransform->Translation = B->Rotation * (B->Scale3D * A->Translation) + B->Translation;
    }
}
```

**Order semantics:** `C = A * B` means "first apply A, then apply B".
- `Rotation(C) = B.Rotation × A.Rotation` (quaternion order)
- `Translation(C) = B.Rotation × (B.Scale × A.Translation) + B.Translation`

**Hierarchical composition:** If `LocalChild = local-to-parent transform` and `ParentWorld = parent-to-world`, then:
```cpp
ChildWorld = LocalChild * ParentWorld
```

This is the **row-vector convention** (transforms are applied right-to-left in composition, but semantically "first to last").

### 5.3 Application in Palette Computation

**File:** `e:\UzEngine\runtime\engine\animation\skeletal_mesh_instance.cpp:64-103`

```cpp
void SkeletalMeshInstance::UpdatePaletteFromPose() {
    // ... 
    
    // Compute world matrix of each bone in topological order
    std::vector<Mat4> worldMatrices(boneCount, Mat4::Identity());

    for (uint32_t i = 0; i < boneCount; ++i) {
        const BoneInfo& bone = m_skeleton->GetBone(i);
        const Mat4 local = pose.localTransforms[i].ToMatrix();

        if (bone.parentIndex < 0) {
            worldMatrices[i] = local;
        } else {
            // CRITICAL: Row-vector convention
            // world[i] = local[i] * world[parent[i]]
            worldMatrices[i] = local * worldMatrices[static_cast<size_t>(bone.parentIndex)];
        }

        // palette[i] = inverseBind[i] * world[i]
        m_palette[i] = bone.inverseBindWorld * worldMatrices[i];
    }
}
```

**Algorithm:**
1. Walk bones in topological order (parents before children)
2. For each bone: `world[i] = local[i] × world[parent[i]]`
3. Compose with inverse bind: `palette[i] = inverseBind[i] × world[i]`

---

## 6. Local-to-Component Space Conversion

**UE5 does NOT have an explicit "convert to component space" step in GetBonePose.**

Instead:
- GetBonePose returns **LOCAL-to-PARENT transforms** in the FCompactPose
- The palette composition (in SkeletalMeshInstance) **implicitly** converts to component space by walking the hierarchy
- Each bone's world transform = local × parent's world

**This is the #1 critical difference from UzEngine's current implementation.**

---

## 7. UzEngine Mapping

### 7.1 Conceptual Mapping

| UE5 Concept | UzEngine Equivalent | File |
|---|---|---|
| `UAnimSequence` | `AnimationClip` | `runtime/engine/animation/animation_clip.h` |
| `FRawAnimSequenceTrack` | Per-bone keyframe stream (in-memory clip data) | (embedded in AnimationClip) |
| `FCompactPose` | `Pose` (local transforms) | `runtime/engine/animation/pose.h` |
| `FBoneContainer` | `Skeleton` (hierarchy + bind pose) | `runtime/engine/animation/skeleton.h` |
| `FCompactPoseBoneIndex` | `uint32_t` bone index | (implicit) |
| `FTransform::Multiply` | `BoneTransform` composition | `runtime/engine/animation/bone_transform.h` |
| `UpdatePaletteFromPose` | `SkeletalMeshInstance::UpdatePaletteFromPose` | `runtime/engine/animation/skeletal_mesh_instance.cpp:64-103` |

### 7.2 Key Differences

**UE5 vs. UzEngine:**

| Aspect | UE5 | UzEngine | Impact |
|---|---|---|---|
| **Pose space** | Always LOCAL-to-PARENT | Stored as LOCAL (correct) | ✓ Matches UE5 |
| **Palette composition** | `palette[i] = inverseBind[i] × world[i]`<br>`world[i] = local[i] × world[parent[i]]` | Same formula (line 101) | ✓ Matches UE5 |
| **Additive handling** | `target - base` (delta in local space) | ??? (no additive support yet) | ??? Unknown |
| **Index spaces** | Three (CompactPose, Mesh, Skeleton) | One (implicit uint32_t) | ✓ Simpler, acceptable |
| **Decompression** | Codec-based (varies by compression algo) | Per-frame keyframe lerp | ⚠️ Different algorithm, but same output |
| **Reference frame application** | Hierarchical retargeting via FSkeletonRemapping | ??? (unknown) | ??? Unknown |

---

## 8. Additive Animation Deep Dive

### 8.1 ConvertPoseToAdditive Algorithm

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Animation\AnimationRuntime.cpp:1075-1114`

```cpp
void FAnimationRuntime::ConvertTransformToAdditive(
    FTransform& TargetTransform, 
    const FTransform& BaseTransform)
{
    // Additive rotation: target_rot × base_rot^(-1)
    TargetTransform.SetRotation(
        TargetTransform.GetRotation() * BaseTransform.GetRotation().Inverse());
    
    // Additive translation: target_trans - base_trans
    TargetTransform.SetTranslation(
        TargetTransform.GetTranslation() - BaseTransform.GetTranslation());
    
    // Additive scale: (target_scale / base_scale) - 1
    // This allows blending with other additive scale values
    TargetTransform.SetScale3D(
        TargetTransform.GetScale3D() * BaseTransform.GetSafeScaleReciprocal(BaseTransform.GetScale3D()) - 1.f);
    
    TargetTransform.NormalizeRotation();
}

void FAnimationRuntime::ConvertPoseToAdditive(
    FCompactPose& TargetPose, 
    const FCompactPose& BasePose)
{
    for (FCompactPoseBoneIndex BoneIndex : BasePose.ForEachBoneIndex())
    {
        FTransform& TargetTransform = TargetPose[BoneIndex];
        const FTransform& BaseTransform = BasePose[BoneIndex];
        ConvertTransformToAdditive(TargetTransform, BaseTransform);
    }
}
```

**Semantics:**
- **Rotation delta:** `rot_delta = target_rot × base_rot^(-1)` (local rotation difference)
- **Translation delta:** `trans_delta = target_trans - base_trans` (local translation offset)
- **Scale delta:** `scale_delta = (target_scale ÷ base_scale) - 1` (scale ratio - 1, for blending)

**Why this works:** When applied to a base pose via `pose_final = base + (target - base) = target`, the deltas compose correctly in local space.

### 8.2 Additive Type: AAT_LocalSpaceBase

**Default additive type.** The entire animation (translation, rotation, scale) is stored as deltas in LOCAL bone space.

**Application:**
1. Sample target clip at time `t` → full transform
2. Sample base clip (reference animation or bind pose) at time `t` → base transform
3. Compute delta: `delta = target - base` (in local space)
4. **When playing:** blend delta with other poses: `result = base_pose + (weight × delta)`

### 8.3 Additive Type: AAT_RotationOffsetMeshSpace

**Hybrid additive.** Rotation is stored as delta in **MESH (component) space**; translation stays in LOCAL space.

**Why separate?** Allows rotation-only animation layers (like upperbody idle sway) to stack cleanly on locomotion.

**Application:**
1. Sample target clip → full transform (trans in local, rot in mesh)
2. Sample base clip → base transform
3. Convert rotation to local delta (mesh-space rot must be un-composed to local)
4. Application is more complex due to space mismatch

---

## 9. Skeleton Retargeting

**Not a pose evaluation concern** (handled elsewhere), but documented for completeness:

- `FSkeletonRemapping` maps bones between different skeletons (humanoid → different rig)
- Applied **before** palette composition if target skeleton differs from source
- Uses `GetRetargetTransforms()` to look up remapped reference frames

---

## 10. Top 3 Suspects for UzEngine's Animation Bug

### Suspect 1: **Additive animation wrongly applied (HIGHEST PROBABILITY)**

**Evidence:**
- Bind pose works (local transforms are identity, palette composition is identity)
- Non-identity animated poses produce deformation (suggests transform composition or space mismatch)
- MushroomMonster_Idle clip **may be imported/flagged as additive but applied as non-additive** (or vice versa)

**Investigation:**
1. Check if the clip is marked `AdditiveAnimType != AAT_None`
2. If additive, verify the base pose is being correctly extracted
3. Compare UE5's `ConvertPoseToAdditive` (lines 1075–1085) with any additive code in UzEngine
4. Test: Disable additive handling in UE5 export → reimport → does UzEngine work?

**Root cause:** If a clip is **baked additive** in UE5 (stored as delta from bind pose), but UzEngine treats it as absolute, the pose will be `delta + identity = delta`, which is a small offset, causing localized deformation.

### Suspect 2: **Local-to-world hierarchy composition is wrong (HIGH PROBABILITY)**

**Evidence:**
- Line `worldMatrices[i] = local * worldMatrices[parent[i]]` is the **ONLY** place UE5 converts local → world
- If UzEngine is doing something different (e.g., `local + parent_trans` instead of composition), the hierarchy breaks
- Non-leaf bones deform more than leaf bones (parent error propagates to children)

**Investigation:**
1. Print UE5's world matrices at each bone (use PIX capture of a frame)
2. Compute UzEngine's world matrices from the same pose
3. Compare element-by-element
4. Check for: wrong multiplication order, missing scale application, quaternion vs. matrix bugs

**Test case:** Single-bone animation (no children) should have zero error if this is the bug.

### Suspect 3: **Bone-space convention mismatch in compressed tracks (MEDIUM PROBABILITY)**

**Evidence:**
- UE5 **guarantees** all tracks are LOCAL-to-PARENT
- If UzEngine's decompression reads them as **component-space** or **model-space**, the compose-hierarchy step will apply them wrong
- Decompression must respect the "apply times and interpolate" logic, not reinterpret the values

**Investigation:**
1. Export a simple humanoid (Armature: Root, Root→Spine, Spine→Arm)
2. Sample a pose at time 0.5s in UE5
3. Call `GetBonePose()` and read `OutPose[0]`, `OutPose[1]`, `OutPose[2]`
4. Decompress the same frame in UzEngine (line-by-line through AnimationClip::Sample)
5. Compare the local transforms — they **must** match exactly (within float32 precision)

**Critical check:**
```cpp
// UE5 pseudocode (correct)
for frame in 0..N:
    localTransform = Lerp(key[i], key[i+1], alpha);  // LOCAL space
    
// UzEngine pseudocode (verify it does the same)
for bone in skeleton:
    localTransform = ...  // Must be LOCAL, not component
```

---

## References

### UE5 Source Files Cited

| File | Lines | Content |
|---|---|---|
| `AnimSequence.h` | 201–300 | UAnimSequence header, metadata |
| `AnimSequence.cpp` | 1731–1871 | GetBonePose main function |
| `AnimSequence.cpp` | 1873–1952 | GetBonePose_Additive, GetAdditiveBasePose |
| `AnimTypes.h` | 586–595 | EAdditiveAnimationType enum |
| `AnimTypes.h` | 864–937 | FRawAnimSequenceTrack struct |
| `AnimCompressionTypes.h` | 223–260 | FCompressedAnimSequence |
| `BonePose.h` | 102–173 | FBaseCompactPose structure |
| `BoneIndices.h` | 62–83 | Three index types (CompactPose, Mesh, Skeleton) |
| `AnimationRuntime.cpp` | 1075–1114 | ConvertPoseToAdditive, ConvertTransformToAdditive |
| `TransformNonVectorized.h` | 1311–1348 | FTransform::Multiply composition logic |
| `AnimSequenceDecompressionContext.h` | 52–75 | Decompression context (metadata for codec) |

### UzEngine Files for Comparison

| File | Purpose |
|---|---|
| `runtime/engine/animation/animation_clip.h` | Equivalent to UAnimSequence |
| `runtime/engine/animation/pose.h` | Equivalent to FCompactPose (local transforms) |
| `runtime/engine/animation/skeleton.h` | Equivalent to USkeleton + FBoneContainer |
| `runtime/engine/animation/skeletal_mesh_instance.cpp:64–103` | UpdatePaletteFromPose (critical function) |
| `runtime/engine/animation/bone_transform.h` | Equivalent to FTransform |

---

## Summary

UE5's pose evaluation pipeline is:

1. **Sample** per-bone keyframe tracks (LOCAL-to-PARENT space)
2. **Optionally subtract base pose** if the clip is marked additive
3. **Walk skeleton hierarchy** in topological order, composing local-to-world using `world[i] = local[i] × world[parent[i]]`
4. **Compose with inverse bind:** `palette[i] = inverseBind[i] × world[i]`

**The #1 undocumented constraint:** All animation keys, compressed or uncompressed, are **ALWAYS in LOCAL-TO-PARENT bone space.** No exceptions. UzEngine must ensure its decompression respects this, and that its palette composition correctly hierarchically composes local→world.

If UzEngine's deformation occurs for non-identity poses but not for bind pose, the bug is **definitely** in the space convention or hierarchy composition, not in interpolation/sampling.

---

**Status:** Ready for implementation analysis. Recommend reading skeleton.h and animation_clip.cpp next to understand UzEngine's current decompression and composition logic.
