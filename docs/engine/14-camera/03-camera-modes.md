# Camera System — Camera Modes

> **Status**: Planned (Phase 2)
> **Source**: `engine/components/camera_component.h`, configuration examples
> **Depends on**: CameraComponent (§14.1), SpringArmComponent (§14.2)
> **References**: UE5 camera modes (first-person, third-person, cinematic), Godot `Camera3D` modes

## Purpose

Different games require different camera perspectives and behaviors. A camera "mode" is a configuration preset for `CameraComponent` and optional `SpringArmComponent` that handles a specific viewpoint style. This document catalogs common modes and their setup.

## Camera Modes Overview

```
Camera Mode
├── Perspective (angle, FOV)
├── Attachment point (player head, behind shoulder, fixed world point)
├── Collision handling (spring arm vs fixed)
├── Input binding (mouse/gamepad drives what?)
└── Culling layer mask
```

## Mode 1: Third-Person (Behind Camera)

Standard action game camera: character visible, positioned behind and above shoulder.

```cpp
void SetupThirdPersonCamera(Entity player) {
    // Create spring arm attached to player
    SceneComponent* root = player.GetComponent<SceneComponent>();

    SpringArmComponent* arm = player.CreateComponent<SpringArmComponent>();
    arm->SetParent(root);
    arm->SetLocalPosition({0, 0, 0});
    arm->target_arm_length = 350.0f;      // 3.5m behind
    arm->socket_offset = {0, 80, 0};      // At eye level
    arm->target_offset = {40, 0, -20};    // Slight right and forward
    arm->camera_lag_speed = 10.0f;
    arm->probe_radius = 15.0f;
    arm->use_pawn_control_rotation = true;

    // Create camera at end of spring arm
    CameraComponent* camera = player.CreateComponent<CameraComponent>();
    camera->SetParent(arm);
    camera->SetLocalPosition({0, 0, 0});
    camera->fov = 75.0f;                  // Wider FOV for action
    camera->near_plane = 10.0f;
    camera->is_active = true;

    // Input: mouse rotates camera, WASD moves character forward
    player.SetupInputMapping({
        "Look_X" -> "mouse_x",
        "Look_Y" -> "mouse_y",
        "Move_Forward" -> "input_forward",
    });
}
```

**Use cases**: Action games, adventure games, most third-person games.

**Pros**:
- See the character model (visual feedback)
- Familiar to players
- Collision avoidance works well

**Cons**:
- Not immersive
- Camera can feel sluggish without good lag smoothing

## Mode 2: First-Person

Camera mounted at player's eye level, character model hidden.

```cpp
void SetupFirstPersonCamera(Entity player) {
    // Get head bone from skeletal mesh
    SkeletalMeshComponent* mesh = player.GetComponent<SkeletalMeshComponent>();
    int32_t head_bone = mesh->GetBoneIndex("head");

    // Create camera at head position
    CameraComponent* camera = player.CreateComponent<CameraComponent>();
    camera->SetParent(mesh, head_bone);  // Attach to bone
    camera->SetLocalPosition({0, 10, 0});  // Slightly above bone
    camera->fov = 90.0f;                 // Wide FOV for immersion
    camera->near_plane = 5.0f;           // Close clipping to see hands
    camera->is_active = true;

    // Input: mouse directly controls camera pitch/yaw
    player.SetupInputMapping({
        "Look_X" -> mouse_delta_x,
        "Look_Y" -> mouse_delta_y,
    });

    // Hide character model (first-person view)
    mesh->SetVisibility(false);
}
```

**Use cases**: Shooters, horror games, immersive sims.

**Pros**:
- Most immersive
- Direct player control
- No occlusion issues (nothing in front of camera)

**Cons**:
- Can't see character
- Prone to motion sickness if frame rate is unstable
- Can't see character's equipped weapons without special handling

## Mode 3: Top-Down

Fixed overhead camera, character visible below.

```cpp
void SetupTopDownCamera(Entity player) {
    // Create camera not attached to player (fixed world position)
    Entity camera_pivot = world->CreateEntity("CameraPivot");

    CameraComponent* camera = camera_pivot.CreateComponent<CameraComponent>();
    camera->SetLocalRotation(Quat::FromAxisAngle({1, 0, 0}, Radians(-45.0f)));
    camera->SetLocalPosition({0, 500, 300});  // High above and back
    camera->fov = 60.0f;
    camera->is_active = true;

    // Camera follows player XY but stays at fixed height
    class TopDownCameraController {
        void Update(Entity player, Entity camera_pivot) {
            Vec3 player_pos = player.GetWorldPosition();
            Vec3 camera_pos = {
                player_pos.x,
                500.0f,  // Fixed height
                player_pos.z + 300.0f
            };
            camera_pivot->SetWorldPosition(camera_pos);
        }
    };
}
```

**Use cases**: Strategy games, top-down action (Zelda-like), puzzle games.

**Pros**:
- Full visibility of surroundings
- Great for puzzle and strategy gameplay
- Easy pathfinding visualization

**Cons**:
- Less immersive
- Character feels small on screen
- Harder to judge depth of moving objects

## Mode 4: Side-Scroller

2D camera movement along X and Y only, fixed Z depth.

```cpp
void SetupSideScrollerCamera(Entity player) {
    // Orthographic camera for 2D-like feel
    CameraComponent* camera = player.CreateComponent<CameraComponent>();
    camera->is_ortho = true;
    camera->ortho_size = 500.0f;        // Visible height in world units
    camera->near_plane = -1000.0f;
    camera->far_plane = 1000.0f;

    // Follow player in X and Y only
    class SideScrollerController {
        void Update(Entity player, CameraComponent* camera) {
            Vec3 player_pos = player.GetWorldPosition();
            camera->SetLocalPosition({
                player_pos.x,
                player_pos.y + 100.0f,  // Look slightly up
                -1000.0f                 // Fixed depth for 2D sort order
            });
        }
    };

    camera->is_active = true;
}
```

**Use cases**: Metroidvania, 2D platformers, pixel art games.

**Pros**:
- Perfect for 2D/pseudo-2D games
- Depth sorting is straightforward (Z position)
- Easy to control camera parallax

**Cons**:
- Very limited field of view
- Modern players may find it dated

## Mode 5: Orbital (Free Look)

Camera orbits around a target point, rotates freely.

```cpp
class OrbitalCameraController {
    Entity target;
    float orbit_distance = 400.0f;
    float orbit_yaw = 0.0f;      // Horizontal rotation
    float orbit_pitch = -30.0f;  // Vertical angle

    void Update(float dt, InputSystem* input) {
        CameraComponent* camera = GetSubsystem<CameraSystem>()->GetActiveCamera();

        // Mouse input rotates orbit
        Vec2 mouse_delta = input->GetMouseDelta();
        orbit_yaw += mouse_delta.x * 0.5f;
        orbit_pitch = Clamp(orbit_pitch + mouse_delta.y * 0.5f, -89.0f, 89.0f);

        // Compute orbital position around target
        Vec3 target_pos = target->GetWorldPosition();
        Quat yaw_rot = Quat::FromAxisAngle({0, 1, 0}, Radians(orbit_yaw));
        Quat pitch_rot = Quat::FromAxisAngle({1, 0, 0}, Radians(orbit_pitch));
        Quat total_rot = yaw_rot * pitch_rot;

        Vec3 forward = total_rot.RotateVector({0, 0, -orbit_distance});
        Vec3 camera_pos = target_pos + forward;

        camera->SetWorldPosition(camera_pos);
        camera->LookAt(target_pos, {0, 1, 0});  // Look at target

        // Mouse wheel adjusts distance
        float scroll = input->GetMouseScroll();
        orbit_distance = Clamp(orbit_distance - scroll * 50.0f, 100.0f, 1000.0f);
    }
};
```

**Use cases**: RPG character screen, model viewers, boss fights (sometimes).

**Pros**:
- Flexible viewing angle
- Great for character inspection
- Works well for showcasing models

**Cons**:
- Not suitable for navigation-heavy games
- Requires clear spatial understanding from player

## Mode 6: Fixed Cinematic

Camera at a predetermined world position, optionally follows along a spline.

```cpp
class CinematicCameraController {
    std::vector<Vec3> keyframe_positions;
    std::vector<Vec3> keyframe_targets;
    float timeline = 0.0f;  // Current position on spline (0 to 1)

    void Update(float dt) {
        CameraComponent* camera = GetSubsystem<CameraSystem>()->GetActiveCamera();

        // Interpolate camera position along spline
        timeline += dt / cinematic_duration;
        if (timeline > 1.0f) {
            // Cinematic done, return control to player
            game_instance->Resume();
            return;
        }

        // Catmull-Rom spline interpolation
        Vec3 cam_pos = SplineCurve::Evaluate(keyframe_positions, timeline);
        Vec3 look_target = SplineCurve::Evaluate(keyframe_targets, timeline);

        camera->SetWorldPosition(cam_pos);
        camera->LookAt(look_target, {0, 1, 0});
    }
};
```

**Use cases**: Cutscenes, level introductions, dramatic moments.

**Pros**:
- Complete visual control
- Cinematic quality guaranteed
- Can frame action perfectly

**Cons**:
- Expensive to author (requires curve editing)
- Not interactive
- Can feel claustrophobic if player is locked out

## Mode 7: Dialogue Camera

Close-up on NPC face, camera slightly offset and angled.

```cpp
void SetupDialogueCamera(Entity npc) {
    // Look at face, slightly to the side
    SkeletalMeshComponent* mesh = npc->GetComponent<SkeletalMeshComponent>();
    int32_t head_bone = mesh->GetBoneIndex("head");

    CameraComponent* camera = npc->CreateComponent<CameraComponent>();
    camera->SetParent(mesh, head_bone);
    camera->SetLocalPosition({80, 20, 150});  // To the side and forward
    camera->fov = 50.0f;                      // Slightly zoomed in
    camera->is_active = true;

    // Look at eye level, not top of head
    camera->LookAt(npc->GetWorldPosition() + Vec3{0, 160, 0}, {0, 1, 0});
}
```

**Use cases**: NPC conversations, dialogue scenes, close-ups.

**Pros**:
- Focuses player attention
- Intimate feel
- Easy to frame face for expressions

**Cons**:
- Limited context (can't see surroundings)
- Requires good facial animation

## Mode Switching

Transition smoothly between modes:

```cpp
void CameraSystem::SwitchMode(CameraMode new_mode, float transition_time = 0.5f) {
    // Store old camera state
    CameraComponent* old_camera = GetActiveCamera();
    Vec3 old_pos = old_camera->GetWorldPosition();
    Quat old_rot = old_camera->GetWorldRotation();

    // Setup new camera
    SetupCamera(new_mode);
    CameraComponent* new_camera = GetActiveCamera();

    // Blend between old and new over transition_time
    float blend_progress = 0.0f;
    while (blend_progress < 1.0f) {
        blend_progress += dt / transition_time;

        Vec3 blended_pos = Lerp(old_pos, new_camera->GetWorldPosition(), blend_progress);
        Quat blended_rot = Quat::Slerp(old_rot, new_camera->GetWorldRotation(), blend_progress);

        new_camera->SetWorldPosition(blended_pos);
        new_camera->SetWorldRotation(blended_rot);

        Yield();  // Pause and wait for next frame
    }
}
```

## Engine Comparison

| Mode | UE5 | Godot 4 | Unity | **UzEngine** |
|------|-----|---------|-------|------------|
| Third-Person | `USpringArmComponent` + `UCameraComponent` | `SpringArm3D` | Cinemachine | **SpringArmComponent** + **CameraComponent** |
| First-Person | `UCameraComponent` (mounted to socket) | `Camera3D` (child of head) | FPS template | **CameraComponent** (parent to bone) |
| Top-Down | Fixed `ACamera` entity | `Camera3D` (world entity) | Fixed Camera | **CameraComponent** (world entity) |
| Orbital | Custom controller | CustomScript | Cinemachine Orbital | **OrbitalCameraController** class |
| Cinematic | Matinee sequences | AnimationPlayer + Camera | Timeline / Cinemachine | **CinematicCameraController** spline |

## Rules

1. **ALWAYS attach first-person cameras to bones** — head motion looks natural
2. **NEVER use near_plane < 5cm** — too close and Z-fighting occurs
3. **ALWAYS test camera modes with gamepad input** — mouse control is different
4. **NEVER switch modes abruptly** — blend over 0.3-0.5 seconds
5. **ALWAYS cull back faces for first-person** — avoid seeing inside character model
