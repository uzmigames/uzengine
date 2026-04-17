---
title: UzEngine Animation Bug — Root Cause and Fix Plan
status: diagnosed
last_updated: 2026-04-17
---

# UzEngine Animation Bug — Root Cause and Fix Plan

## Symptom

The skinned mushroom mesh renders correctly at bind pose but explodes into
splayed-out spikes the moment any non-identity pose is applied. The deformation
is **identical on the CPU `SkinCpu` path and the GPU compute `gpu_skinning_cs`
path**. Switching the palette buffer between Upload heap (persistently mapped)
and Default heap (per-frame staging copy) does not change the deformation.
Hardcoding the shader's skin matrix to identity, or uploading literal
`Mat4::Identity()` into the palette buffer at run time, produces the correct
bind-pose render in both paths.

## Root cause: TRS-vs-matrix composition mismatch

UzEngine has **two different world-transform composition pipelines** that share
the same input data but compute different results when any bone has non-uniform
scale (which is common in FBX rigs after axis conversion / pre-/post-rotation
baking).

### Pipeline A — FBX importer (TRS composition)

`runtime/engine/asset/importers/fbx_importer.cpp:269-294` — `ComputeLocalFromWorldTRS`:

```cpp
[[nodiscard]] BoneTransform ComputeLocalFromWorldTRS(
    const BoneTransform& world,
    const BoneTransform& parent) {
    BoneTransform local;
    local.scale = Vec3{
        world.scale.x / parent.scale.x,
        world.scale.y / parent.scale.y,
        world.scale.z / parent.scale.z};
    const Quat parentInv = parent.rotation.inverse();
    local.rotation = (parentInv * world.rotation).normalize();
    const Vec3 delta = world.position - parent.position;
    const Vec3 invRotDelta = parentInv * delta;
    local.position = Vec3{
        invRotDelta.x / parent.scale.x,
        invRotDelta.y / parent.scale.y,
        invRotDelta.z / parent.scale.z};
    return local;
}
```

This is the inverse of `BoneTransform::ComposeWith` (defined at
`runtime/engine/animation/bone_transform.h:117-123`):

```cpp
[[nodiscard]] BoneTransform ComposeWith(const BoneTransform& parent) const {
    BoneTransform result;
    result.scale = parent.scale * scale;
    result.rotation = (parent.rotation * rotation).normalize();
    result.position = parent.position + parent.rotation * (parent.scale * position);
    return result;
}
```

Both functions are **TRS composition**: scale is a componentwise product,
rotation is a quaternion product, translation respects the parent's full
rotation+scale stack. This is what UE5 does in `FTransform`
(`Engine/Source/Runtime/Core/Public/Math/TransformNonVectorized.h`).

The FBX importer takes per-bone world TRS (decomposed from
`ufbx_skin_cluster::bind_to_world`) and computes the local TRS using this
inverse composition. Likewise, the animation extractor (line 695,
`ExtractAnimations`) calls the same `ComputeLocalFromWorldTRS` for every
keyframe.

The stored **local TRS values are designed to recompose into the original
world TRS via TRS composition** (`ComposeWith`), not via matrix multiplication.

### Pipeline B — Runtime palette pass (matrix composition)

`runtime/engine/animation/skeleton.cpp:67-99` — `Skeleton::ComputeInverseBindMatrices`:

```cpp
const Mat4 localBind = Mat4::TRS(bone.bindTranslation, bone.bindRotation, bone.bindScale);
if (bone.parentIndex == kInvalidBoneIndex) {
    worldBindMatrices[i] = localBind;
} else {
    worldBindMatrices[i] = localBind * worldBindMatrices[parent];
}
bone.inverseBindWorld = worldBindMatrices[i].inverse();
```

`runtime/engine/animation/skeletal_mesh_instance.cpp:89-101` — `UpdatePaletteFromPose`:

```cpp
for (uint32_t i = 0; i < boneCount; ++i) {
    const BoneInfo& bone = m_skeleton->GetBone(i);
    const Mat4 local = pose.localTransforms[i].ToMatrix();
    if (bone.parentIndex < 0) {
        worldMatrices[i] = local;
    } else {
        worldMatrices[i] = local * worldMatrices[parent];
    }
    m_palette[i] = bone.inverseBindWorld * worldMatrices[i];
}
```

Both functions convert each bone's local TRS to a 4×4 matrix
(`Mat4::TRS = Scaling * Rotation * Translation`, row-vector convention) and
then compose the world by **matrix multiplication**.

### Why bind pose works

Inside `ComputeInverseBindMatrices`, the world matrix is computed via the
matrix path AND then immediately inverted to make `inverseBindWorld`. At
runtime, when the animation player has not been instructed to play any clip,
`UpdatePaletteFromPose` sees `pose.localTransforms[i] == bone.GetBindTransform()`
(the local TRS read straight back from the importer). The matrix path then
recomputes the same `worldBindMatrices[i]` value that `ComputeInverseBindMatrices`
already inverted. Their product collapses to identity.

This *always* holds, regardless of whether the underlying TRS values are valid
TRS or whether the matrix-composed world equals the FBX-evaluated world. As
long as both sides walk the same matrix-composition tree from the same TRS
inputs, identity is the result. **That is why the user verified
`max element-wise deviation ≤ 1.5e-5` on every palette slot at bind pose** —
it is a tautology of the algebra.

### Why animated poses break

When `Mushroom_Idle` plays, the per-bone keyframes are sampled to produce a
`pose.localTransforms[i]` value that **differs from `bone.bind*`**. Those
keyframe values were stored by the importer as the *TRS-style local
decomposition* of the FBX-evaluated *animated world* TRS at sample time. They
are guaranteed to recompose, **via TRS composition**, into the original animated
world TRS.

But the runtime composes them via **matrix multiplication**. Whenever the
hierarchy contains a bone with non-uniform scale (FBX rigs frequently bake
non-trivial scale into pre-/post-rotation matrices after axis conversion), the
matrix product is **not equal** to the TRS-composed world. The discrepancy is
a sheared, non-orthogonal block in the upper-left 3×3 of the world matrix.

`m_palette[i] = inverseBindWorld[i] * worldMatrices_anim[i]` then evaluates to
a non-identity, non-rigid skin matrix per bone, and the per-vertex linear blend
of those distorted matrices is the visible explosion of spikes.

**The bind-pose vs animated-pose tautology mismatch is the smoking gun.** The
bind pose cancels the bug structurally; nothing else can.

## How UE5 avoids this

`Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:2683-2701`
— `FAnimationRuntime::FillUpComponentSpaceTransforms`:

```cpp
for (int Index = 0; Index < ComponentSpaceTransforms.Num(); ++Index) {
    int32 ParentIndex = RefSkeleton.GetParentIndex(Index);
    if (ParentIndex != INDEX_NONE) {
        ComponentSpaceTransforms[Index] =
            BoneSpaceTransforms[Index] * ComponentSpaceTransforms[ParentIndex];
    } else {
        ComponentSpaceTransforms[Index] = BoneSpaceTransforms[Index];
    }
}
```

The `*` here is `FTransform::operator*`, which composes
position/rotation/scale **in TRS space**, never as 4×4 matrices. UE5 only
flattens to `FMatrix44f` once per bone, immediately before GPU upload, in
`UpdateRefToLocalMatricesInner` at
`Engine/Source/Runtime/Engine/Private/SkeletalRender.cpp:417-438`:

```cpp
ReferenceToLocal[ThisBoneIndex] =
    (FMatrix44f)ComponentTransform[ThisBoneIndex].ToMatrixWithScale();
```

— and even there, the matrix is built directly from the TRS, not from a chain
of matrix products. The inverse-bind multiply
(`SkeletalRender.cpp:460-463`) is also done once per bone on already-composed
matrices, not as part of the hierarchy walk.

## Fix plan

### Option 1 — **Recommended.** Compose poses as `BoneTransform`, flatten to `Mat4` once

Change `UpdatePaletteFromPose` to walk the hierarchy with
`BoneTransform::ComposeWith` (or its parent-relative inverse) and only call
`ToMatrix()` on the final per-bone world. Same change for
`Skeleton::ComputeInverseBindMatrices`. This matches UE5 exactly and works for
any rig regardless of scale.

Sketch of the fix to `skeletal_mesh_instance.cpp`:

```cpp
std::vector<BoneTransform> worldTransforms(boneCount);
for (uint32_t i = 0; i < boneCount; ++i) {
    const BoneInfo& bone = m_skeleton->GetBone(i);
    const BoneTransform& localTRS = pose.localTransforms[i];

    if (bone.parentIndex < 0) {
        worldTransforms[i] = localTRS;
    } else {
        worldTransforms[i] = localTRS.ComposeWith(
            worldTransforms[static_cast<size_t>(bone.parentIndex)]);
    }

    const Mat4 worldMatrix = worldTransforms[i].ToMatrix();
    m_palette[i] = bone.inverseBindWorld * worldMatrix;
}
```

And the matching fix to `skeleton.cpp::ComputeInverseBindMatrices`:

```cpp
std::vector<BoneTransform> worldBindTRS(boneCount);
for (uint32_t i = 0; i < boneCount; ++i) {
    auto& bone = m_bones[i];
    const BoneTransform localBind = bone.GetBindTransform();
    if (bone.parentIndex == kInvalidBoneIndex) {
        worldBindTRS[i] = localBind;
    } else {
        worldBindTRS[i] = localBind.ComposeWith(
            worldBindTRS[static_cast<size_t>(bone.parentIndex)]);
    }
    bone.inverseBindWorld = worldBindTRS[i].ToMatrix().inverse();
}
```

Both paths now use the same TRS composition the FBX importer assumed when it
ran `ComputeLocalFromWorldTRS`. Bind pose still cancels (because the same
algorithm is on both sides), and animated poses now compose into the correct
world TRS that matches what the FBX evaluator produced at import time.

### Option 2 — Fall back to matrix decomposition on import (NOT recommended)

Change the importer to compute local *matrices* via matrix-product inverse
(`local = world * inverse(parentWorld)`) and store the **matrix** in the
skeleton/clip instead of a TRS triplet. This preserves any shear that the FBX
hierarchy already had. The downsides: the asset format becomes 64 bytes per
bone keyframe instead of 40, blending and additive layering become much harder
(can't slerp a sheared matrix), and we diverge further from the UE5 reference.
Listed for completeness only.

### Option 3 — Strip non-uniform scale at import (NOT recommended)

Force `ComputeLocalFromWorldTRS` to produce only uniform scales, swallowing
any shear silently. This silently breaks rigs that legitimately use anisotropic
scaling (e.g., mirrored bones, exaggerated weapon swings). Listed for
completeness only.

## Verification plan after the fix

1. Re-build the runtime with Option 1 applied to both
   `skeletal_mesh_instance.cpp` and `skeleton.cpp`.
2. Run `uzengine.exe --project sample/projects/fbx_test.uzproject --editor`
   with the existing `mushroom_controller.lua` script (which forces
   `Mushroom_Idle`).
3. Capture a screenshot at frame 60 and verify the mushroom holds an
   anatomically plausible idle pose (not splayed spikes).
4. Confirm bind pose is unchanged by re-running with the script disabled.
5. Press the locomotion / attack keys (W, Space, etc.) and confirm the
   transitions look correct.
6. If anything is still wrong, the next likely suspect is the per-frame
   `ufbx_evaluate_scene` decomposition of `node_to_world`
   (`fbx_importer.cpp:683`) — `ufbx_matrix_to_transform` may itself project
   non-trivial shear out into TRS in a way that loses information. In that case
   the fix would move to the importer to use UE-style TRS composition during
   keyframe extraction too.

## Consequences

- The signature of `BoneTransform::ComposeWith` (and the inverse
  `ComputeLocalFromWorldTRS`) becomes the **canonical** UzEngine bone
  composition rule.
- The legacy `Mat4::TRS` + `Mat4 *` path remains valid for static (non-
  animated) usages but **must not** be the hierarchy-composition primitive for
  the skeletal animation path.
- Future render features that need the per-bone world matrix (IK, ragdoll,
  socket attachments) should derive their matrix from
  `worldTransforms[i].ToMatrix()` after the new TRS walk, not from the legacy
  `worldMatrices` vector.
- The `inverseBindWorld` field name in `BoneInfo` is misleading — it is the
  inverse of the bind-pose **component-space** matrix (no scene world transform
  applied). Consider renaming to `inverseBindComponent` in a follow-up
  cosmetic pass.

## Files to edit (order)

1. `runtime/engine/animation/skeletal_mesh_instance.cpp` —
   replace the `UpdatePaletteFromPose` matrix walk with the
   `BoneTransform::ComposeWith` walk.
2. `runtime/engine/animation/skeleton.cpp` —
   replace the `ComputeInverseBindMatrices` matrix walk likewise.
3. (Optional / cosmetic) Rename `BoneInfo::inverseBindWorld` to
   `inverseBindComponent` and update doc comment in `skeleton.h`.
4. (Optional) Add a unit test under `runtime/tests/animation/` that builds a
   2-bone skeleton with a non-identity rotation on the parent and a non-
   uniform scale on the child, plays a synthetic 1-keyframe clip with a
   non-zero rotation on the child, and asserts that
   `UpdatePaletteFromPose` produces the same vertex output as a reference
   FTransform-composition implementation (both should match the FBX evaluator
   exactly).

## References

- `runtime/engine/asset/importers/fbx_importer.cpp:240-294` — TRS importer
  helpers (`DecomposeUfbxMatrix`, `ComputeLocalFromWorldTRS`).
- `runtime/engine/asset/importers/fbx_importer.cpp:413-440` — bind-pose local
  TRS construction (per bone).
- `runtime/engine/asset/importers/fbx_importer.cpp:687-703` — animation
  extraction (per frame, per bone).
- `runtime/engine/animation/bone_transform.h:117-123` — `ComposeWith` (TRS).
- `runtime/engine/animation/skeleton.cpp:67-99` — `ComputeInverseBindMatrices`
  (matrix path, **bug site #1**).
- `runtime/engine/animation/skeletal_mesh_instance.cpp:64-103` —
  `UpdatePaletteFromPose` (matrix path, **bug site #2**).
- `Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:2683-2701`
  — UE5 reference (`FillUpComponentSpaceTransforms`, FTransform composition).
- `Engine/Source/Runtime/Engine/Private/SkeletalRender.cpp:417-463` — UE5
  reference (`UpdateRefToLocalMatricesInner`, where TRS becomes matrix).
