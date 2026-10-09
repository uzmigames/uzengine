# Bone-Attached VFX Emitter

## Why

The current VFX system can spawn particles FROM skeletal mesh bones (`skeletal_spawn_module`) but cannot ATTACH an emitter's transform to a bone so the whole effect tracks the bone in real time. Production-critical for weapon trails, muzzle flashes, hand magic, cloth emitters, and cinematic rig VFX — every shipped action game needs this. Niagara exposes it via `USceneComponent::AttachToComponent` with bone socket name.

Without this, anyone placing a weapon trail has to manually update the emitter position every frame from gameplay code, which breaks reuse and prevents data-driven authoring.

## What Changes

- `VfxEmitterComponent`: add `attachEntity` (Entity handle) + `attachBoneName` (string).
- Scene sync: when `attachEntity` is valid and owns a `SkeletalMeshComponent`, resolve the bone world transform each frame and override the emitter's world matrix.
- Fallback: if bone name is empty, attach to entity root transform (parent attach).
- Fallback: if entity/bone invalid, log once + fall back to the component's own SceneComponent.
- Editor UI: add "Attach" section in Parameters panel with entity picker + bone dropdown populated from the selected skeletal mesh.
- `.uzfx` asset serialization: round-trip `attachEntity` (by name) + `attachBoneName`.

## Impact

- Affected specs: VFX emitter runtime, scene sync pipeline
- Affected code: `vfx_emitter_component.h`, `particle_emitter.h`, `scene_vfx_sync.cpp`, `emitter_serializer.h`, `vfx_editor_overlay.cpp`
- Breaking change: NO (new optional fields default to empty)
- User benefit: rig-driven VFX authoring without gameplay-side per-frame transform updates
