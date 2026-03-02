# Camera System Spec

## Overview

UzEngine's camera system is data-driven and fully configurable from `.uzcam` files. The default runtime camera components are a **Spring Arm** (boom arm with collision avoidance) and a **Camera** with configurable FOV, DoF, and shake. Camera behavior is scriptable via IPC and exposable to game scripts.

---

## 1. Camera Components (Node Types)

| Node Type | Properties |
|-----------|-----------|
| `Camera` | `fov`, `near`, `far`, `projection` (perspective/ortho), `is_main`, `dof` |
| `SpringArm` | `arm_length`, `socket_offset`, `lag_speed`, `rotation_lag_speed`, `collision_test`, `probe_radius` |

### Scene Example

```ini
[uzscene version=1 name="PlayerCamera"]

[node id=1 type=Node3D name="CameraRoot" parent=0]

[node id=2 type=SpringArm name="SpringArm" parent=1]
arm_length         = 400.0
socket_offset_z    = 80.0
lag_speed          = 10.0
rotation_lag_speed = 8.0
collision_test     = true
probe_radius       = 12.0

[node id=3 type=Camera name="MainCamera" parent=2]
fov      = 90.0
near     = 10.0
far      = 100000.0
is_main  = true
dof_enabled       = true
dof_focus_dist    = 500.0
dof_focus_range   = 200.0
dof_far_blur      = 0.8
```

---

## 2. Camera Modes

```cpp
enum class CameraMode { ThirdPerson, FirstPerson, Orbit, Cinematic };
```

### Third Person (Spring Arm)

```cpp
void SpringArm::update(float dt, glm::vec3 pivot, glm::quat desired_rot,
                       glm::vec3& out_pos, glm::quat& out_rot,
                       const PhysicsWorld& phys)
{
    // Smooth pivot and rotation with lag
    float pa  = 1.0f - expf(-dt * lag_speed);
    float ra  = 1.0f - expf(-dt * rotation_lag_speed);
    arm_pos   = glm::mix(arm_pos, pivot + glm::vec3(0,0,socket_offset_z), pa);
    arm_rot   = glm::normalize(glm::slerp(arm_rot, desired_rot, ra));

    glm::vec3 arm_dir  = arm_rot * glm::vec3(0, -1, 0);
    glm::vec3 ideal_cam = arm_pos + arm_dir * arm_length;

    // Collision avoidance: sphere-cast from pivot to ideal position
    float dist = arm_length;
    if (collision_test) {
        if (phys.sphere_cast(arm_pos, ideal_cam, probe_radius)) {
            dist = hit.fraction * arm_length - probe_radius;
            dist = std::max(dist, 0.0f);
        }
    }

    out_pos = arm_pos + arm_dir * dist;
    out_rot = arm_rot;
}
```

### Orbit Mode (Editor camera / cinematic)

```cpp
void CameraSystem::update_orbit(float dt)
{
    // Input-driven
    orbit_yaw   += input.get_axis2d("IA_Look").x * dt * 120.0f;
    orbit_pitch -= input.get_axis2d("IA_Look").y * dt * 80.0f;
    orbit_pitch  = glm::clamp(orbit_pitch, -80.0f, 80.0f);

    glm::quat rot = glm::quat(glm::vec3(
        glm::radians(orbit_pitch), glm::radians(orbit_yaw), 0.0f));
    camera.position = orbit_target + rot * glm::vec3(0, -orbit_distance, 0);
    camera.look_at(orbit_target);
}
```

### Cinematic (Spline Path)

```cpp
void CameraSystem::update_cinematic(float dt)
{
    spline_t += dt * cinematic_speed;
    camera.position = catmull_rom_evaluate(position_spline, spline_t);
    if (cinematic_look_at_mode == LookAtMode::SplineLookAt)
        camera.rotation = look_at_quat(camera.position,
            catmull_rom_evaluate(look_at_spline, spline_t));
    else if (cinematic_look_at_mode == LookAtMode::TrackActor)
        camera.rotation = look_at_quat(camera.position, tracked_node->world_pos());
}
```

---

## 3. Field of View

```cpp
struct FovSettings {
    float default_fov  = 90.0f;
    float ads_fov      = 45.0f;    // aim-down-sights
    float sprint_fov   = 100.0f;   // sprint FOV stretch
    float interp_speed = 8.0f;     // lerp speed
};

// In update:
float target_fov = is_aiming ? fov.ads_fov
                 : is_sprinting ? fov.sprint_fov
                 : fov.default_fov;
camera.fov = glm::mix(camera.fov, target_fov, 1.0f - expf(-dt * fov.interp_speed));
```

---

## 4. Camera Shake

Multiple shakes accumulate additively. Driven by Perlin noise with exponential decay envelope:

```cpp
struct CameraShake {
    float magnitude  = 5.0f;    // degrees
    float frequency  = 20.0f;   // oscillation Hz
    float duration   = 0.5f;
    float elapsed    = 0.0f;

    glm::vec3 evaluate(float t) const {
        float env = powf(1.0f - glm::clamp(t / duration, 0.0f, 1.0f), 2.0f);
        return {
            perlin(t * frequency + 0.0f) * magnitude * env,
            perlin(t * frequency + 100.f) * magnitude * env,
            0.0f
        };
    }
    bool expired() const { return elapsed >= duration; }
};

// Accumulate:
glm::vec3 total = {0,0,0};
for (auto& s : active_shakes) {
    total += s.evaluate(s.elapsed);
    s.elapsed += dt;
}
camera.rotation *= glm::quat(glm::radians(total));
```

---

## 5. Depth of Field

Post-process parameters driven by camera:

```cpp
struct DoFParams {
    bool  enabled       = false;
    float focus_dist    = 500.0f;   // cm — auto-focus can drive this
    float focus_range   = 200.0f;   // in-focus zone
    float near_blur     = 0.0f;     // 0..1
    float far_blur      = 1.0f;     // 0..1
    float bokeh_radius  = 8.0f;     // pixels
};
```

Auto-focus: raycast from camera center, set `focus_dist = hit.distance` each frame (smooth toward target with lerp).

---

## 6. Camera Preset File (`.uzcam`)

```ini
; cameras/third_person.uzcam

[mode]
type = third_person

[spring_arm]
arm_length         = 400.0
socket_offset_z    = 80.0
lag_speed          = 10.0
rotation_lag_speed = 8.0
collision_test     = true
probe_radius       = 12.0

[fov]
default      = 90.0
ads          = 45.0
sprint       = 100.0
interp_speed = 8.0

[dof]
enabled      = true
focus_dist   = 500.0
focus_range  = 200.0
near_blur    = 0.0
far_blur     = 0.8
bokeh_radius = 6.0

[shake preset=land]
magnitude  = 3.0
frequency  = 15.0
duration   = 0.4

[shake preset=gunshot]
magnitude  = 1.5
frequency  = 25.0
duration   = 0.2

[shake preset=explosion]
magnitude  = 8.0
frequency  = 10.0
duration   = 1.2
```

---

## 7. Editor Camera (Viewport)

The editor uses an **orbit / fly camera** driven by mouse + keyboard input from the VSCode viewport webview. Not tied to the game camera.

| Input | Action |
|-------|--------|
| Right-click drag | Rotate |
| Right-click + WASD | Fly |
| Right-click + QE | Up / Down |
| Middle-click drag | Pan |
| Scroll | Fly speed / zoom |
| F | Frame selected node |

Editor camera state serialized per-project: `editor_state.json` saves the last camera position/rotation so it's restored on reopen.

---

## 8. IPC Handlers (camera.*)

```
camera.setMode       { mode: "third_person" | "first_person" | "orbit" | "cinematic" }
camera.setOrbitTarget { position: [x,y,z] }
camera.setFov        { fov: 75.0 }
camera.addShake      { preset: "gunshot" }   or { magnitude, frequency, duration }
camera.setDoF        { enabled, focus_dist, focus_range, far_blur }
camera.setSpringArm  { arm_length, lag_speed, ... }
camera.getState      {}   → { position, rotation, fov, mode }
camera.setCinematic  { path_points: [[x,y,z],...], look_at_points: [...], speed: 1.0 }
```
