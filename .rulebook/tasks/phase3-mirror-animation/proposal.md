# Proposal: phase3-mirror-animation

## Why
Action games require mirrored variants of many animations: left punch and right punch, left dodge and right dodge, left-hand weapon hold and right-hand weapon hold. Without runtime mirroring, every animation must be authored twice — once for each side — which doubles animation production work and doubles animation memory usage. Runtime mirroring flips an animation across the character's YZ plane and swaps left/right bone transforms, producing a correct mirror from a single source asset.

## What Changes
- `MirrorDataAsset` (.uzmirror INI file) defines bone name mapping pairs (e.g., `Bone_LeftHand = Bone_RightHand`, `Bone_LeftFoot = Bone_RightFoot`) for a given skeleton
- `AnimationPlayer::play_mirrored(anim_handle, mirror_data_handle)` API plays any animation in mirrored mode
- Mirror transform computation applied as a post-process on the final evaluated pose: negate X-axis translation for all bones, negate Y and Z components of rotation quaternions for paired bones, swap transform values between mapped bone pairs
- Mirror post-process runs after blend tree evaluation and before IK passes so IK still operates on correct geometry
- Blend tree nodes can tag individual states as mirrored, enabling blend graphs that seamlessly mix mirrored and non-mirrored clips

## Impact
- Affected specs: `docs/specs/animation-system.md`, `docs/specs/skeletal-mesh.md`
- Affected code: `src/animation/`, `src/asset_pipeline/`
- Breaking change: NO
- User benefit: Animation production workload for symmetric actions is halved; animation memory usage is reduced proportionally; designers can mirror any animation at runtime without re-exporting from DCC tools.
