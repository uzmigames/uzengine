## 1. Physics Asset Loader

- [ ] 1.1 `PhysicsAsset::load(uzphys_path, skeleton)` — parse `.uzphys` INI format
- [ ] 1.2 Body definitions: bone name, shape (capsule/sphere/box), dimensions, mass
- [ ] 1.3 Constraint definitions: body_a, body_b, type (swing_twist/hinge), angle limits

## 2. Jolt Ragdoll Build

- [ ] 2.1 `PhysicsAsset::build_jolt_ragdoll(skeleton)` → `JPH::RagdollSettings*`
- [ ] 2.2 Per body: `JPH::CapsuleShape` / `JPH::SphereShape` / `JPH::BoxShape`
- [ ] 2.3 Per constraint: `JPH::SwingTwistConstraintSettings` / `JPH::HingeConstraintSettings`
- [ ] 2.4 `settings->Stabilize()` after all parts added
- [ ] 2.5 Body initial position from skeleton bind pose (world-space)

## 3. Ragdoll Activation

- [ ] 3.1 `RagdollComponent::activate(current_anim_pose, character_velocity)`
- [ ] 3.2 Pose ragdoll to match current animation (no visual pop at activation)
- [ ] 3.3 Inherit character velocity on all bodies
- [ ] 3.4 Switch `SkeletalMeshComponent` pose source: animation → ragdoll readback

## 4. Ragdoll Readback

- [ ] 4.1 Each frame in ragdoll mode: read `JPH::Body` positions/rotations
- [ ] 4.2 Convert to world-space BoneTransforms
- [ ] 4.3 Compute local transforms from world (inverse hierarchy walk)
- [ ] 4.4 Feed into bone palette for GPU skinning

## 5. Active Ragdoll (Physical Animation)

- [ ] 5.1 `apply_physical_animation_motors(target_anim_pose, blend_strength)`
- [ ] 5.2 Compute angular impulse per body toward animated target rotation
- [ ] 5.3 `blend_strength` exposed as `scene.setProperty` (0=pure physics, 1=fully animated)

## 6. IPC Handlers

- [ ] 6.1 `scene.activateRagdoll(node_id)` — switch to ragdoll mode
- [ ] 6.2 `scene.deactivateRagdoll(node_id)` — blend back to animation
- [ ] 6.3 `scene.setRagdollBlend(node_id, blend)` — physical animation strength

## 7. Testing

- [ ] 7.1 Character falls with correct ragdoll physics on `activateRagdoll`
- [ ] 7.2 No visual pop at ragdoll activation (pose matches last anim frame)
- [ ] 7.3 Active ragdoll blend=0.5: character keeps rough animated shape while physics applies
- [ ] 7.4 Multiple ragdolls active simultaneously without performance collapse
