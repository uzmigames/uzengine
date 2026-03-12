# Animation System — Root Motion

> **Status**: Planned (Phase 4)
> **Source**: `engine/animation/root_motion.h/.cpp`, `engine/components/movement_component.h`
> **Depends on**: Animation controller (§19.5), physics (§25)

## Purpose

Root motion means the animation itself drives character movement, rather than the player controller. Useful for:

- **Realistic distances** — footsteps match animation stride length
- **Attacking** — lunge distance from animation (not guessed by programmer)
- **Climbing** — ladder animation positions character correctly
- **Cinematic movement** — cutscene walk paths

Without root motion, animations play "in place" and the engine moves the character via player input — footsteps and animation don't match reality.

## Concept

The **root bone** (usually Hips) is a special transform that can be extracted from the animation curve and applied to the entity's world position instead of the skeleton's local position:

```
Animation Pose:
  Hips.position = (1, 0, 0)  ← from animation keyframe
  Spine.position = (0, 2, 0)  ← relative to Hips

Without Root Motion:
  Entity.position = (100, 0, -50)  ← independent of animation
  Hips.world = (100, 0, -50) + Hips.local = (101, 0, -50)

With Root Motion:
  Entity.position += Hips.animation.delta  ← animation drives movement
  Hips.world = Entity.position            ← Hips IS the entity position
```

## Configuration

```cpp
class AnimationController {
public:
    enum class RootMotionMode : uint8_t {
        NoExtraction,        // animation plays in place (default)
        ExtractTranslation,  // XY movement from animation, Z stays relative
        ExtractRotation,     // rotational movement from animation
        ExtractAll,          // both translation and rotation
    };

    void SetRootMotionMode(RootMotionMode mode) { root_motion_mode = mode; }
    RootMotionMode GetRootMotionMode() const { return root_motion_mode; }

    /// Get root motion delta this frame.
    Transform GetRootMotionDelta() const { return root_motion_delta; }
};
```

### Project Configuration

In `.uzproject`:

```json
{
  "animation": {
    "root_bone_id": 0,
    "default_root_motion": "extract_translation"
  }
}
```

Or per-animation:

```json
{
  "format": "uzanim",
  "name": "charge_attack",
  "root_motion": {
    "mode": "extract_translation",
    "enabled": true
  }
}
```

## Extraction

### Root Bone Identification

The root bone is typically the first joint or explicitly marked:

```cpp
uint32_t skeleton->GetRootBoneId() const {
    // Usually joints[0], but could be named "Armature" or "Root"
    for (const auto& joint : joints) {
        if (joint.parent_id == UINT32_MAX) {
            return joint.id;
        }
    }
    return 0;  // fallback
}
```

### Computing Root Motion Delta

Root motion is the change in root bone position from frame to frame:

```cpp
void AnimationController::UpdateRootMotion(float delta_time) {
    uint32_t root_id = skeleton->GetRootBoneId();

    // Get root bone transform at previous frame
    Vec3 prev_root_pos = root_bone_previous_position;

    // Sample animation at current time
    AnimSequence* anim = GetCurrentAnimation();
    std::vector<Transform> pose = anim->Sample(current_time);

    // Get root bone transform in local space
    Vec3 curr_root_pos = pose[root_id].position;

    // Delta in animation space
    Vec3 delta = curr_root_pos - prev_root_pos;

    // Store for next frame
    root_bone_previous_position = curr_root_pos;

    // Compute root motion based on mode
    switch (root_motion_mode) {
        case RootMotionMode::NoExtraction:
            root_motion_delta = {0, 0, 0, {0, 0, 0, 1}, {1, 1, 1}};
            break;

        case RootMotionMode::ExtractTranslation:
            root_motion_delta.position = delta;
            root_motion_delta.rotation = pose[root_id].rotation;
            root_motion_delta.scale = pose[root_id].scale;
            break;

        case RootMotionMode::ExtractRotation:
            root_motion_delta.position = {0, 0, 0};
            root_motion_delta.rotation = pose[root_id].rotation;
            break;

        case RootMotionMode::ExtractAll:
            root_motion_delta.position = delta;
            root_motion_delta.rotation = pose[root_id].rotation;
            root_motion_delta.scale = pose[root_id].scale;
            break;
    }
}
```

## Character Movement

### Movement Component Integration

The movement component applies root motion:

```cpp
class MovementComponent : public SceneComponent {
public:
    void ApplyRootMotion(const Transform& root_delta, float delta_time) {
        // Translate character
        Vec3 velocity = root_delta.position / delta_time;
        MoveCharacter(velocity * delta_time);

        // Rotate character
        if (root_delta.rotation.LengthSquared() > 0.01f) {
            Quat current_rot = GetWorldRotation();
            Quat new_rot = current_rot * root_delta.rotation;
            SetWorldRotation(new_rot);
        }
    }
};

class SkeletalMeshComponent : public PrimitiveComponent {
public:
    void Update(float delta_time) override {
        // Get root motion from animation
        Transform root_motion = anim_controller->GetRootMotionDelta();

        // Apply to character movement
        MovementComponent* move = GetOwner()->GetComponent<MovementComponent>();
        if (move) {
            move->ApplyRootMotion(root_motion, delta_time);
        }

        // For root motion extraction, keep root bone at origin
        if (anim_controller->GetRootMotionMode() != RootMotionMode::NoExtraction) {
            // Set root bone position to (0, 0, 0) to avoid double-movement
            std::vector<Transform> pose = anim_controller->GetPose();
            uint32_t root_id = skeleton->GetRootBoneId();
            pose[root_id].position = {0, 0, 0};  // zero out root translation
        }

        // Compute final joint matrices
        skeleton->ComputeJointMatrices(pose, joint_matrices);
    }
};
```

### Player Input Override

Root motion can be blended with player input:

```cpp
void CharacterController::Update(float delta_time) {
    // Get root motion from animation
    Transform root_motion = skeletal_mesh->GetRootMotionDelta();

    // Get player input
    float input_speed = (input->GetAxis("Forward") + input->GetAxis("Backward"));
    float input_strafe = (input->GetAxis("Right") + input->GetAxis("Left"));

    Vec3 input_velocity = {input_strafe, 0, input_speed} * max_speed;

    // Blend root motion + player input
    float blend = 0.7f;  // 70% animation, 30% input
    Vec3 final_velocity = Lerp(input_velocity, root_motion.position, blend);

    movement->MoveCharacter(final_velocity);
}
```

## Ladder Climbing

Root motion is essential for climbing animations:

```cpp
class LadderComponent : public TriggerComponent {
private:
    Vec3 ladder_start;
    Vec3 ladder_end;
    float ladder_height;

public:
    void OnCharacterEnter(Entity character) {
        // Play climb animation
        auto anim = character.GetComponent<SkeletalMeshComponent>()->anim_controller;
        anim->SetRootMotionMode(RootMotionMode::ExtractTranslation);
        anim->GetState("Climbing")->Play();

        // Root motion carries character up ladder
        // Character.position += root_motion.position each frame
        // By end of animation, character is at ladder_end
    }
};
```

## Animation Event Synchronization

Root motion makes animation events critical:

```cpp
struct AnimNotify {
    float time;
    std::string event_name;

    // Example: "footstep" event should fire when foot actually lands
    // Animation @ 60fps: frame 10 = 10/60 = 0.167s into animation
    // Root motion at frame 10 = 0.167s * character_speed
};

// In animation:
// Frame 10: emit "footstep" (left foot lands)
// Frame 35: emit "footstep" (right foot lands)

void CharacterController::OnAnimNotify(StringView event_name) {
    if (event_name == "footstep") {
        uz::Audio::PlayAt(world, footstep_sound, GetPosition());
        SpawnFootstepDecal(GetPosition());
    }
}
```

## Montages (One-Shot Animations)

Temporary animations that override state machine:

```cpp
class Montage {
public:
    ResourceHandle<AnimSequence> animation;
    bool use_root_motion = true;
    float blend_in_time = 0.2f;
    float blend_out_time = 0.2f;
};

class AnimationController {
public:
    void PlayMontage(const Montage& montage) {
        current_montage = montage;
        montage_time = 0.0f;
        // Montage overrides state machine until finished
    }

    void Update(float delta_time) {
        if (current_montage) {
            // Update montage
            montage_time += delta_time;

            if (current_montage->use_root_motion) {
                // Extract root motion from montage
                root_motion_delta = ExtractRootMotion(current_montage->animation, montage_time);
            }

            if (montage_time >= current_montage->animation->GetDuration()) {
                current_montage = nullptr;  // back to state machine
            }
        } else {
            // Normal state machine update
            UpdateStateMachine(delta_time);
        }
    }
};

// Usage: attack animation with lunge
Montage attack_montage;
attack_montage.animation = AssetManager::Load<AnimSequence>("res://anims/lunge_attack.uzanim");
attack_montage.use_root_motion = true;

anim_controller->PlayMontage(attack_montage);
// Character lunges forward per animation
```

## Network Replication

For multiplayer, replicate root motion:

```cpp
// Server
class NetworkTransformComponent : public SceneComponent {
    void Replicate() {
        if (IsLocalPlayer()) {
            // Get root motion from animation
            Transform root_motion = skeletal_mesh->GetRootMotionDelta();

            // Send to server
            SendToServer(root_motion);
        }
    }
};

// Server/Client
class CharacterAnimator {
    void Update(float delta_time) {
        if (IsRemotePlayer()) {
            // Apply replicated root motion
            Transform replicated_motion = GetReplicatedRootMotion();
            movement->MoveCharacter(replicated_motion.position);
            movement->SetRotation(replicated_motion.rotation);
        } else {
            // Local: use actual animation root motion
            auto controller = skeletal_mesh->anim_controller;
            Transform root_motion = controller->GetRootMotionDelta();
            movement->MoveCharacter(root_motion.position);
        }
    }
};
```

## Foot Locking (Advanced)

Prevent foot sliding during transitions:

```cpp
class FootLockingIK {
private:
    struct LockedFoot {
        uint32_t joint_id;
        Vec3 locked_position;
        float unlock_time;  // when to release lock
    };

    std::vector<LockedFoot> locked_feet;

public:
    void LockFoot(uint32_t foot_id, Vec3 world_pos, float unlock_delay) {
        locked_feet.push_back({foot_id, world_pos, unlock_delay});
    }

    void ApplyFootLocking(std::vector<Transform>& pose, float delta_time) {
        // Keep locked feet at locked position during transition
        for (auto& foot : locked_feet) {
            pose[foot.joint_id].position = foot.locked_position;

            foot.unlock_time -= delta_time;
            if (foot.unlock_time <= 0.0f) {
                locked_feet.erase(&foot);
            }
        }
    }
};

// Usage: when transitioning between walk and run
auto walk_anim = anim->GetState("Walk");
auto run_anim = anim->GetState("Run");

// Transition: lock left foot until after transition
foot_locking.LockFoot(skeleton->GetJointByName("Foot_L")->id,
                      left_foot_world_pos, transition_time);

anim->Transition(run_anim, transition_time);
```

## Debugging

```cpp
// Draw root motion vector
void DebugDrawRootMotion(const AnimationController& anim) {
    Transform root_delta = anim.GetRootMotionDelta();
    Vec3 start = character->GetPosition();
    Vec3 end = start + root_delta.position;

    debug_draw->DrawLine(start, end, {1, 0, 0, 1});  // red line
}

// Log root motion
if (debug_mode) {
    Transform rm = anim_controller->GetRootMotionDelta();
    UZ_LOG(Animation, Debug, "Root Motion: pos={:.2f}, rot={:.2f}°",
           rm.position.Length(), AngleBetween(Quat::Identity, rm.rotation));
}
```

## Rules

1. **ALWAYS** extract translation, not rotation — rotation comes from animation, not root
2. **NEVER** apply root motion AND input velocity simultaneously — blend instead
3. **ALWAYS** zero root bone position when extracting root motion — prevent double-movement
4. **NEVER** use root motion in place animations (idles, emotes) — only for locomotion
5. **ALWAYS** synchronize footstep audio/VFX with anim notify events — match animation timing
