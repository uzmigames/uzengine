## 1. AnimationClip

- [ ] 1.1 Create `runtime/engine/animation/animation_clip.h` — AnimationClip: name, duration (float), frame_rate (float), loop (bool), root_motion (bool). Vector<BoneChannel> channels. BoneChannel: bone_index, vector<KeyframeVec3> pos_keys, vector<KeyframeQuat> rot_keys, vector<KeyframeVec3> scl_keys. Keyframe: time (float), value.
- [ ] 1.2 Create `runtime/engine/animation/animation_clip.cpp` — SamplePose(time, Skeleton&) → Pose. For each channel: find bracketing keyframes, interpolate (lerp pos/scale, slerp rotation). Looping: wrap time via fmod(time, duration). Missing channels use bind pose.
- [ ] 1.3 Create `runtime/engine/animation/pose.h` — Pose: vector<BoneTransform> local_transforms (indexed by bone). BlendPoses(a, b, alpha) → lerp/slerp each bone. AdditivePose(base, additive, alpha). Identity pose from Skeleton bind.

## 2. .uzanim Format

- [ ] 2.1 Create `runtime/engine/animation/animation_io.h/.cpp` — SaveAnimation(clip, path): binary format magic 'ZANM', version, header (duration, fps, channel_count, flags), then per-channel: bone_index, key_counts, keyframe arrays. LoadAnimation(path) → AnimationClip.
- [ ] 2.2 Extend glTF/FBX importer — Extract animation data: channels per bone, keyframe times + values. Convert to UzEngine BoneChannel format. Output .uzanim alongside .uzskel.

## 3. AnimationPlayer

- [ ] 3.1 Create `runtime/engine/animation/animation_player.h` — AnimationPlayer: Play(clip_name), Stop(), Pause(), Resume(). speed (float, 1.0), current_time (float), is_playing (bool). Crossfade(target_clip, blend_time): blend from current to target over N seconds.
- [ ] 3.2 Create `runtime/engine/animation/animation_player.cpp` — Update(dt): advance current_time += dt * speed. Sample current clip → Pose. If crossfading: sample both, BlendPoses(old, new, alpha). Notify when clip ends (fire event, handle loop/stop).

## 4. AnimStateMachine

- [ ] 4.1 Create `runtime/engine/animation/anim_state_machine.h` — AnimStateMachine: vector<AnimState> states, vector<AnimTransition> transitions. AnimState: name, clip_name, loop. AnimTransition: from_state, to_state, blend_time, vector<Condition>. Condition: param_name, op (GT/LT/EQ/NEQ), value.
- [ ] 4.2 Create `runtime/engine/animation/anim_state_machine.cpp` — Evaluate(params): check all transitions from current_state, if conditions met → start transition. During transition, blend old and new state poses over blend_time. SetFloat(name, val), SetBool(name, val), GetCurrentState().
- [ ] 4.3 Create .uzanim_sm format — JSON: { "states": [...], "transitions": [...], "parameters": [...] }. Loaded at scene init.

## 5. Blend Space 1D

- [ ] 5.1 Create `runtime/engine/animation/blend_space_1d.h/.cpp` — BlendSpace1D: vector<BlendEntry> entries sorted by threshold. BlendEntry: threshold (float), clip_name. Evaluate(parameter) → finds bracketing entries, computes blend alpha, returns blended Pose.

## 6. Root Motion

- [ ] 6.1 Create `runtime/engine/animation/root_motion.h/.cpp` — ExtractRootMotion(clip, prev_time, curr_time) → delta_position (Vec3), delta_rotation (Quat). Handles loop wrapping. Zero root bone in output pose to prevent double-movement.
- [ ] 6.2 Wire to CharacterController — AnimationSubsystem: if root_motion enabled, call CCharacterController::Move(delta_position) instead of letting animation visually move the mesh.

## 7. AnimNotify Events

- [ ] 7.1 Create `runtime/engine/animation/anim_notify.h` — AnimNotify: time (float), event_name (string), params (JSON string). Stored per AnimationClip.
- [ ] 7.2 Create notify dispatch — AnimationPlayer::Update checks if playback crossed any notify times. Fires EventBus<AnimNotifyEvent> { entity, event_name, params }.
- [ ] 7.3 Wire to audio/VFX — Built-in handlers: "play_sound" → uz.audio.play_at(), "spawn_vfx" → uz.vfx.spawn_at(). Custom events forwarded to Lua.

## 8. Animation Subsystem

- [ ] 8.1 Create `runtime/engine/animation/animation_subsystem.h/.cpp` — AnimationSubsystem (WorldSubsystem): TickGroup::PrePhysics. For each entity with SkeletalMeshComponent + AnimationPlayer: update player → get Pose → compute bone palette → upload to GPU. Root motion applied before physics step.

## 9. Lua Animation API

- [ ] 9.1 Create `runtime/engine/scripting/api/lua_animation.h/.cpp` — Register `uz.animation` in LuaVM. play(entity, clip), stop(entity), set_speed(entity, speed), crossfade(entity, clip, time). State machine: set_float(entity, param, val), set_bool(entity, param, val), get_current_state(entity).

## 10. Testing

- [ ] 10.1 Unit tests for AnimationClip — sample at various times, loop wrapping, missing channels.
- [ ] 10.2 Unit tests for Pose — blend two poses, additive pose.
- [ ] 10.3 Unit tests for AnimStateMachine — transitions, conditions, crossfade timing.
- [ ] 10.4 Unit tests for BlendSpace1D — boundary values, single entry, interpolation.
- [ ] 10.5 Unit tests for root motion — delta extraction, loop wrap, zero root.
- [ ] 10.6 Unit tests for AnimNotify — crossing detection, loop wrap crossing.
- [ ] 10.7 Unit tests for .uzanim I/O — save/load round-trip.

## 11. Demo

- [ ] 11.1 Extend `demos/skeletal_mesh/` — play idle/walk animation on skinned character. State machine: idle↔walk driven by input. Headless + screenshot.

## 12. Documentation

- [ ] 12.1 Update `docs/engine/19-animation/` with clips, state machine, blend spaces, root motion, notifies.
- [ ] 12.2 Add `uz.animation` to `docs/lua-api-reference.md`.
- [ ] 12.3 Document .uzanim and .uzanim_sm formats.

## 13. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 13.1 Update or create documentation covering the implementation
- [ ] 13.2 Write tests covering the new behavior
- [ ] 13.3 Run tests and confirm they pass
