# Animation System Spec

## Overview

The animation system drives skeletal mesh deformation via keyframe clips, a state machine, blending, and a notify (event) system that can trigger sounds, VFX, and scripts at specific animation frames.

---

## 1. Animation Clip

### Data Structure

```cpp
struct AnimKeyVec3 { float time; glm::vec3 value; };
struct AnimKeyQuat { float time; glm::quat value; };

struct BoneChannel {
    uint16_t bone_index;
    std::vector<AnimKeyVec3> pos_keys;
    std::vector<AnimKeyQuat> rot_keys;
    std::vector<AnimKeyVec3> scl_keys;
};

struct AnimNotify {
    float       time;           // trigger time in seconds
    std::string event_name;     // "footstep_left", "spawn_vfx", "play_sound"
    std::string params_json;    // payload as JSON string
};

struct AnimationClip {
    std::string              name;
    float                    duration;           // seconds
    bool                     looping = false;
    bool                     has_root_motion = false;
    bool                     is_additive = false;
    std::vector<BoneChannel> channels;    // sparse: only animated bones
    std::vector<AnimNotify>  notifies;
};
```

### Sampling

```cpp
glm::quat sample_rotation(const BoneChannel& ch, float t)
{
    if (ch.rot_keys.empty()) return {1,0,0,0};
    if (ch.rot_keys.size() == 1 || t <= ch.rot_keys.front().time)
        return ch.rot_keys.front().value;
    if (t >= ch.rot_keys.back().time)
        return ch.rot_keys.back().value;

    auto it = std::lower_bound(ch.rot_keys.begin(), ch.rot_keys.end(), t,
        [](const AnimKeyQuat& k, float v){ return k.time < v; });
    auto hi = it; auto lo = it - 1;
    float alpha = (t - lo->time) / (hi->time - lo->time);
    return glm::normalize(glm::slerp(lo->value, hi->value, alpha));
}
```

---

## 2. Pose Representation

A `Pose` is a flat array of bone-local transforms (position, rotation, scale per bone):

```cpp
struct BoneTransform {
    glm::vec3 position = {0,0,0};
    glm::quat rotation = {1,0,0,0};
    glm::vec3 scale    = {1,1,1};
};

struct Pose {
    std::vector<BoneTransform> bones;  // index = bone index in Skeleton
    int bone_count;
};

// Blend two poses (linear position/scale, SLERP rotation)
void blend_poses(const Pose& a, const Pose& b, float alpha, Pose& out);

// Apply a pose and compute world-space matrices for GPU upload
void compute_bone_palette(const Pose& pose, const Skeleton& sk,
                          std::vector<glm::mat4>& palette_out);
// palette[i] = world_pose[i] * sk.bones[i].inverse_bind_world
```

---

## 3. Animation State Machine

### Data (`.uzanim_sm` text format)

```ini
[uzanimstatemachine version=1 name="HumanoidLocomotion"]

[state name=idle]
clip    = res://anim/idle.uzanim
looping = true

[state name=walk]
clip    = res://anim/walk.uzanim
looping = true

[state name=run]
clip    = res://anim/run.uzanim
looping = true

[state name=jump_up]
clip    = res://anim/jump.uzanim
looping = false
on_end  = transition:land

[state name=land]
clip    = res://anim/land.uzanim
looping = false
on_end  = state:idle

[transition name=idle_to_walk]
from       = idle
to         = walk
blend_time = 0.2
condition  = speed > 0.5

[transition name=walk_to_run]
from       = walk
to         = run
blend_time = 0.15
condition  = speed > 3.5

[transition name=walk_to_idle]
from       = walk
to         = idle
blend_time = 0.3
condition  = speed < 0.1

[transition name=any_to_jump]
from       = *
to         = jump_up
blend_time = 0.1
condition  = is_jumping == true
```

### State Machine Runtime

```cpp
struct AnimStateMachine {
    std::vector<AnimState>    states;
    int                       current = 0;
    int                       next    = -1;      // -1 = no transition
    float                     blend_alpha  = 0;
    float                     blend_time   = 0;
    float                     blend_elapsed = 0;
    float                     cur_time  = 0;
    float                     next_time = 0;

    // Parameters set by game code
    std::unordered_map<std::string, float> float_params;
    std::unordered_map<std::string, bool>  bool_params;

    // Call each frame
    void update(float dt);

    // Returns final blended pose
    void evaluate(const Skeleton& sk, Pose& out_pose);

    // Parameter setters (IPC-accessible)
    void set_float(std::string_view name, float v);
    void set_bool(std::string_view name, bool v);
};
```

---

## 4. Animation Blending

### Cross-Fade (Full Body)

```cpp
void blend_poses(const Pose& a, const Pose& b, float alpha, Pose& out)
{
    for (int i = 0; i < a.bone_count; ++i) {
        out.bones[i].position = glm::mix(a.bones[i].position, b.bones[i].position, alpha);
        out.bones[i].rotation = glm::normalize(
            glm::slerp(a.bones[i].rotation, b.bones[i].rotation, alpha));
        out.bones[i].scale    = glm::mix(a.bones[i].scale, b.bones[i].scale, alpha);
    }
}
```

### Layered Blend (Upper/Lower Body)

A `BlendMask` defines per-bone weight (0 = use pose A, 1 = use pose B):

```ini
# blend_masks/upper_body.uzmask
[mask name=UpperBody]
; bones from spine_02 upward = 1.0 (upper body anim)
; rest = 0.0 (locomotion anim)
spine_02  = 1.0
spine_03  = 1.0
clavicle_l = 1.0
upperarm_l = 1.0
lowerarm_l = 1.0
hand_l     = 1.0
; (right side and head mirrored)
```

### Additive Blend

Used for overlay animations (weapon recoil, hit reactions, breathing) on top of locomotion:

```
final_rot[i] = base_rot[i] * additive_delta_rot[i]
```

The additive delta is computed at import time:
```
additive_delta = clip_pose - reference_pose (e.g., T-pose)
```

---

## 5. AnimNotify System

### Notify Types

| Event | Params JSON | Handler |
|-------|------------|---------|
| `footstep_left` | `{"surface":"concrete","volume":0.8}` | Plays footstep sound at left foot position |
| `footstep_right` | `{"surface":"wood","volume":0.7}` | Same, right foot |
| `play_sound` | `{"asset":"res://audio/whoosh.ogg","volume":1.0,"spatial":true}` | One-shot audio at node position |
| `spawn_vfx` | `{"asset":"res://vfx/dust.uzparticles","attach_bone":"foot_l"}` | Spawn particle at bone |
| `anim_event` | `{"name":"attack_start"}` | Generic event to game script |
| `ik_hint` | `{"target_bone":"hand_r","enable":true}` | Enable/disable IK for frame range |

### Runtime Check

```cpp
void AnimStateMachine::fire_notifies(float prev_t, float cur_t,
                                     const AnimationClip& clip)
{
    for (auto& n : clip.notifies) {
        bool crossed = (looping)
            ? (cur_t < prev_t) ? (n.time >= prev_t || n.time <= cur_t)
                               : (n.time > prev_t && n.time <= cur_t)
            : (n.time > prev_t && n.time <= cur_t);
        if (crossed) {
            event_bus.fire(n.event_name, n.params_json);
        }
    }
}
```

### Audio + VFX Integration

The EventBus routes `play_sound` notifies to **miniaudio**:

```cpp
event_bus.on("play_sound", [](const std::string& params_json) {
    auto p = json::parse(params_json);
    audio_system.play_one_shot(
        p["asset"].get<std::string>(),
        p.value("volume", 1.0f),
        p.value("spatial", false) ? node_world_pos : std::nullopt);
});
```

VFX notifies go to the particle system (Phase 4):

```cpp
event_bus.on("spawn_vfx", [](const std::string& params_json) {
    auto p = json::parse(params_json);
    glm::mat4 xform = skeleton.get_bone_world_transform(p["attach_bone"]);
    vfx_system.spawn(p["asset"], xform);
});
```

---

## 6. Root Motion

Root motion extracts root bone delta and applies it to the character controller, preventing animation-driven drift from physics position.

```cpp
struct RootMotionDelta { glm::vec3 translation; glm::quat rotation; };

RootMotionDelta extract_root_motion(
    const AnimationClip& clip, float prev_t, float cur_t,
    const Skeleton& sk)
{
    BoneTransform t0 = sample_bone(clip, 0, prev_t);  // root = bone 0
    BoneTransform t1 = sample_bone(clip, 0, cur_t);

    // Loop boundary handling
    if (cur_t < prev_t && clip.looping) {
        BoneTransform tend = sample_bone(clip, 0, clip.duration);
        BoneTransform tstart = sample_bone(clip, 0, 0.0f);
        t0 = t1;  // simplified — proper implementation accumulates both halves
    }

    return {
        .translation = t1.position - t0.position,
        .rotation    = t1.rotation * glm::inverse(t0.rotation)
    };
}

// In character update:
auto rm = extract_root_motion(clip, prev_t, cur_t, *skeleton);
character_controller.move(rm.translation);   // drive physics capsule
character_yaw += glm::eulerAngles(rm.rotation).y;
pose.bones[0].position = {0,0,0};  // zero root in render pose
```

---

## 7. Inverse Kinematics

### Two-Bone IK (Foot Placement, Elbow)

```cpp
// Analytical 2-bone IK: root → mid → end targeting `target`
// pole_vector: hints the direction of the "knee" or "elbow"
void two_bone_ik(
    glm::vec3 root, glm::vec3 mid, glm::vec3 end,
    glm::vec3 target, glm::vec3 pole,
    glm::quat& root_rot_out, glm::quat& mid_rot_out);
```

Used for foot placement IK: raycast from foot down, move foot bone to hit point, apply two-bone IK on the leg chain.

### FABRIK (Spine, Tail, Tentacles)

Iterative, works on arbitrary length chains. Maximum 10 iterations, tolerance 1mm.

---

## 8. Animation File Formats

### `.uzanim` Binary

```
Header (32 bytes):
  magic          uint32 = 'ZANM'
  version        uint32 = 1
  duration       float
  channel_count  uint32
  notify_count   uint32
  flags          uint32 (bit0=looping, bit1=root_motion, bit2=additive)
  reserved       8 bytes

Per Channel (variable):
  bone_index     uint16
  pos_key_count  uint16
  rot_key_count  uint16
  scl_key_count  uint16
  [float, float3] × pos_key_count   (time + position)
  [float, float4] × rot_key_count   (time + quat xyzw)
  [float, float3] × scl_key_count   (time + scale)

Per Notify (176 bytes):
  time           float
  event_name     char[32]
  params_json    char[128]
  reserved       12 bytes
```

### `.uzanim_sm` Text (State Machine)

INI-like, as shown in section 3. AI-friendly: states, transitions, conditions defined as plain text rules.

---

## 9. IPC Handlers

```
scene.playAnimation   { id, clip, loop?, speed?, layer? }
scene.stopAnimation   { id, layer? }
scene.setAnimParam    { id, name, value }
scene.getAnimState    { id }  → { state_name, time, params }
scene.setMorphWeight  { id, morph_name, weight }
scene.getMorphs       { id }  → [{ name, weight }]
scene.setIKTarget     { id, bone_chain_tip, target_world_pos }
scene.clearIKTarget   { id, bone_chain_tip }
```
