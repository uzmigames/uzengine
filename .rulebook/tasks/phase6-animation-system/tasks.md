## 1. Animation Clip Runtime

- [ ] 1.1 `AnimationClip` struct: duration, channels (BoneChannel sparse), notifies, flags
- [ ] 1.2 `BoneChannel`: position_keys, rotation_keys, scale_keys (time-sorted)
- [ ] 1.3 `sample_clip(clip, time, skeleton, pose_out)` — binary-search + SLERP
- [ ] 1.4 Loop handling: wrap time modulo duration
- [ ] 1.5 `.uzanim` binary loader (see spec format)

## 2. State Machine

- [ ] 2.1 `AnimState` struct: clip, looping, playback_rate, on_end
- [ ] 2.2 `AnimTransition` struct: to_state, blend_time, condition string
- [ ] 2.3 `AnimStateMachine::update(dt)` — advance time, evaluate transitions
- [ ] 2.4 `AnimStateMachine::evaluate(skeleton, pose_out)` — cross-fade blend
- [ ] 2.5 Condition parser: simple expression eval for `speed > 0.5`, `is_jumping == true`
- [ ] 2.6 `set_float(name, val)`, `set_bool(name, val)` — IPC-exposed parameters
- [ ] 2.7 `.uzanim_sm` text file loader

## 3. Animation Blending

- [ ] 3.1 `blend_poses(a, b, alpha, out)` — full-body linear blend
- [ ] 3.2 `BlendMask` struct: per-bone float weight [0..1]
- [ ] 3.3 `blend_poses_masked(a, b, mask, out)` — layered blend (upper/lower body)
- [ ] 3.4 `.uzmask` file loader (INI-like, lists bone names + weights)
- [ ] 3.5 Additive blend: `add_poses(base, additive_delta, weight, out)`

## 4. AnimNotify System

- [ ] 4.1 `AnimNotify` struct: time, event_name, params_json
- [ ] 4.2 `fire_notifies(prev_t, cur_t, clip)` — emit events on time crossing
- [ ] 4.3 Loop-boundary crossing: correct event detection when time wraps
- [ ] 4.4 EventBus wires `play_sound` → miniaudio one-shot at node position
- [ ] 4.5 EventBus wires `spawn_vfx` → VFX system (stubbed in Phase 3, real in Phase 4)
- [ ] 4.6 EventBus wires `anim_event` → IPC notification (AI agents can subscribe)

## 5. Root Motion

- [ ] 5.1 `extract_root_motion(clip, prev_t, cur_t)` → `{translation, rotation}`
- [ ] 5.2 Zero root bone in render pose after extraction
- [ ] 5.3 Pass root motion delta to CharacterController (Physics Phase 3)
- [ ] 5.4 Loop boundary: correct root motion accumulation at clip wrap

## 6. Inverse Kinematics

- [ ] 6.1 `two_bone_ik(root, mid, end, target, pole)` — analytical, returns rotations
- [ ] 6.2 `fabrik(joints[], target, max_iter)` — iterative, arbitrary chain length
- [ ] 6.3 Foot placement IK: raycast from foot down, apply two-bone IK to leg chain
- [ ] 6.4 `scene.setIKTarget(node_id, tip_bone, target_world)` IPC handler

## 7. Animation Editor Panel (VSCode)

- [ ] 7.1 Timeline view in Animation Preview panel: show clip duration, notify markers
- [ ] 7.2 Click notify marker → shows params in Inspector sidebar
- [ ] 7.3 Playback controls: ▶ Play, ■ Stop, ⏮ Rewind, time scrubber
- [ ] 7.4 State machine visualizer: boxes for states, arrows for transitions (webview SVG)
- [ ] 7.5 Live parameter sliders: float params (speed, etc.) with live preview

## 8. Testing

- [ ] 8.1 Idle→Walk transition at correct blend time
- [ ] 8.2 Footstep sound plays at exact left/right foot contact frames
- [ ] 8.3 Upper body aim blend: lower body walks, upper body aims separately
- [ ] 8.4 Root motion: character moves forward at correct speed from animation
- [ ] 8.5 Foot placement IK: foot sticks to ground on uneven surface
