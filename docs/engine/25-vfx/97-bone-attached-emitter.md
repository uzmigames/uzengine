# Bone-Attached Emitter

## Overview

A VFX emitter can be attached to another entity — optionally to a specific
skeletal-mesh bone — so its world transform tracks the target every frame.
This is the engine's equivalent of Niagara's `AttachToComponent` with a socket
name: the typical use cases are weapon trails, muzzle flashes, hand-driven
magic, cloth emitters on rigs and cinematic VFX.

## Data Model

Two fields on `VfxEmitterComponent` drive the behavior:

| Field | Type | Meaning |
|---|---|---|
| `attachEntity` | `Entity` | Target entity the emitter follows. `Invalid()` disables attach. |
| `attachBoneName` | `std::string` | Optional bone/socket on the target's skeletal mesh. Empty = entity root. |

An asset-level default `attachBoneNameDefault` is stored in the runtime
`ParticleEmitter` and serialized into the `.uzfx` JSON. Scene sync uses it as
the fallback when the ECS component does not carry a placement-specific
override.

## Resolve Order

Each frame, `SyncVfxEmitters()` calls `ResolveAttachTransform()` which picks
the emitter's world position in the following order:

1. `attachEntity` is valid AND the component's `attachBoneName` (or the
   runtime's `attachBoneNameDefault`) matches a bone on that entity's
   `SkeletalMeshComponent` skeleton → the bone's model-space world matrix is
   composed with the entity's `SceneComponent` world matrix, and the
   translation is used as the emitter position.
2. `attachEntity` is valid but no bone matches → the entity's
   `SceneComponent` world position is used (parent attach).
3. `attachEntity` is invalid / missing → the emitter uses its own
   `SceneComponent` position (default behavior).

Bone names are resolved through `Skeleton::FindBoneIndex`. The bone's world
transform is read from `SkeletalMeshInstance::GetBoneWorldMatrix`, which is
cached during the per-frame `UpdatePaletteFromPose` walk.

## Diagnostics

Misconfiguration emits a single warning per `(entity, boneName)` key to avoid
log spam:

```
VFX attach target entity id=... gen=... is invalid — falling back to emitter SceneComponent
VFX attach bone 'hand_r' not found on skeleton for entity id=... — falling back to entity root
```

Once the warning has been emitted, the resolve path silently falls back to
the next option in the order above.

## Editor

The split-layout editor exposes the bone name in the Parameters panel under
the **Attach** module. The field edits `ParticleEmitter::attachBoneNameDefault`,
which is the value serialized into the asset. The ECS-level
`VfxEmitterComponent::attachEntity` + `attachBoneName` are set by the scene
loader / gameplay code on a per-placement basis.

## Asset Format

`.uzfx` gains an optional top-level key:

```json
{
  "maxParticles": 1024,
  "attachBoneName": "hand_r",
  "...": "..."
}
```

The field is omitted when empty.

## Runtime Fields

`ParticleEmitter` exposes two frame-written read-only fields consumers can
inspect:

| Field | Meaning |
|---|---|
| `attachedWorldPosition` | World position produced by the most recent resolve. |
| `attachedThisFrame` | `true` when the resolve succeeded this frame. |
