---
title: Skeletal Animation — UE5 Reference Analysis
status: in-progress
last_updated: 2026-04-17
---

# Skeletal Animation — UE5.6 Reference Analysis

This directory contains a layer-by-layer reference analysis of Unreal Engine 5.6's
skeletal animation pipeline (`W:\UE_5.6_Source`) as a baseline to compare against
UzEngine's implementation.

The trigger for this analysis was a long-running bug:

- UzEngine's GPU skinning compute pipeline was suspected as the source of mesh
  deformation when an animation was played.
- After bisection (CPU vs GPU paths produce identical visuals; bind-pose works
  on BOTH paths; the deformation is identical in BOTH paths) the bug was
  isolated to the **palette computation from the per-frame pose**, NOT to the
  GPU dispatch.
- This analysis documents UE5's exact pipeline so we can map each UzEngine
  layer against a verified reference implementation.

## Reading order

| File | Layer | Status |
|------|-------|--------|
| [01-skeleton.md](01-skeleton.md) | `FReferenceSkeleton`, `RefBasesInvMatrix`, bind-pose chain | DELIVERED |
| [02-animation-sequence-and-pose.md](02-animation-sequence-and-pose.md) | `UAnimSequence`, `FCompactPose`, local→component-space, additive poses | DELIVERED |
| [03-gpu-skinning-and-reference-to-local.md](03-gpu-skinning-and-reference-to-local.md) | `UpdateRefToLocalMatricesInner`, GPU shader, multiplication conventions | DELIVERED |
| [04-skeletal-mesh-and-vertex-formats.md](04-skeletal-mesh-and-vertex-formats.md) | `USkeletalMesh`, vertex streams, per-section `BoneMap` | DELIVERED |
| [99-uzengine-bug-root-cause.md](99-uzengine-bug-root-cause.md) | Synthesis: root cause + fix plan | DELIVERED |

## Quick conclusion (TL;DR)

The bug is a **TRS-vs-matrix composition convention mismatch** between the FBX
importer and the runtime:

- The **FBX importer** at `runtime/engine/asset/importers/fbx_importer.cpp:269`
  (`ComputeLocalFromWorldTRS`) decomposes world transforms back to local using
  TRS-style inverse composition (`local.scale = world.scale / parent.scale`,
  componentwise; `local.rotation = parentInv * world.rotation`;
  `local.position = parentInv * (world.position - parent.position) / parent.scale`).

- The **runtime palette pass** at
  `runtime/engine/animation/skeletal_mesh_instance.cpp:91` and
  `runtime/engine/animation/skeleton.cpp:91` recomposes the world matrix via
  `Mat4 * Mat4`. This **bakes non-uniform scale into a non-orthogonal
  rotation/scale block** that does not equal the TRS-style world matrix the
  importer decomposed from.

These two paths agree iff every bone has **uniform scale** (or every bone has
identity rotation). In a real FBX rig — including the mushroom monster — many
bones have non-uniform scale embedded in their import-time transforms (FBX
pre/post-rotation, axis conversion, mirrored bones), so the bind pose still
renders correctly (both sides use the same matrix-composed bind world by
construction inside `ComputeInverseBindMatrices`) but **animated poses
deform**, because the matrix-composed animated world differs from the
TRS-composed animated world the keyframes were sampled against.

UE5 sidesteps this entirely by composing **`FTransform * FTransform`**
throughout
(`Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:2683-2701`,
`FillUpComponentSpaceTransforms`) and only converting to matrix at the very
end before GPU upload. UzEngine should do the same.

See [99-uzengine-bug-root-cause.md](99-uzengine-bug-root-cause.md) for the
full diagnosis and the recommended patch.

## Key finding from `02-animation-sequence-and-pose.md` that strengthens the diagnosis

UE's `FTransform::Multiply`
(`Engine/Source/Runtime/Core/Public/Math/TransformNonVectorized.h:1311-1348`)
takes a deliberate special-case branch on negative scales:

```cpp
if (AnyHasNegativeScale(A->Scale3D, B->Scale3D)) {
    MultiplyUsingMatrixWithScale(OutTransform, A, B);
} else {
    OutTransform->Rotation    = B->Rotation * A->Rotation;
    OutTransform->Scale3D     = A->Scale3D * B->Scale3D;
    OutTransform->Translation = B->Rotation * (B->Scale3D * A->Translation) + B->Translation;
}
```

The fallback to a matrix-with-scale path is the *exception*, not the rule —
UE knows that the matrix path has different semantics under non-uniform /
mirrored scale and only uses it when the algebra otherwise would not be
expressible as a pure TRS. UzEngine effectively forces the matrix path on
*every* bone, which is the bug.

The same report flags additive animations (`AAT_LocalSpaceBase`,
`AAT_RotationOffsetMeshSpace`) as a separate possible failure class. UzEngine's
FBX importer (`fbx_importer.cpp:593-704` `ExtractAnimations`) does not preserve
additive metadata — every clip is exported as a fully-baked LOCAL TRS pose,
which is harmless when sampled directly. So additive-vs-full confusion is
**ruled out** for the current MushroomMonster_Idle case; the TRS-vs-matrix
composition gap remains the sole explanation.

## What is NOT the bug (ruled out by this analysis)

- **GPU skinning shader correctness** — bind-pose renders correctly via the GPU
  path.
- **GPU palette upload mechanism** — switching to a Default-heap buffer with
  per-frame staging copy does NOT change the deformation pattern.
- **Bone-index packing** — UzEngine's importer maps FBX cluster indices to
  *global* skeleton indices (no section-local indexing), so there is no
  missing `BoneMap` indirection (cf. UE's per-section `BoneMap`,
  [04-skeletal-mesh-and-vertex-formats.md](04-skeletal-mesh-and-vertex-formats.md)).
- **inverseBindWorld semantic** — UzEngine's `ComputeInverseBindMatrices`
  produces the *inverse of the matrix-composed bind world matrix*, which is
  consistent with how it later composes the runtime world. The name `World`
  is a misnomer (it's actually component-space, since the skeleton has no
  outer transform applied here), but the value is correct relative to the
  composition convention chosen.
- **mul order in the shader** — the row-vector `mul(v, M)` shader matches the
  CPU `MulRowPoint`. Both produce the same output given the same matrix.
- **Bone weight quantisation** — UzEngine uses uint8 weights / 255.0 (same
  convention as UE).
