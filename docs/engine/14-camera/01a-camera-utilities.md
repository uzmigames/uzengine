# Camera — Utilities & Controllers

> **Status**: Planned
> **Source**: `runtime/engine/renderer/camera/`, `runtime/demos/common/`
> **Depends on**: Camera System (§14-01), Input System, Math (§02)

## Purpose

Camera convenience methods and controller classes eliminate manual math and boilerplate in demos and gameplay code. Instead of converting FOV, computing forward vectors from angles, or duplicating mouse-look logic, use ready-made APIs for common patterns.

## Camera Convenience Methods

The `Camera` class provides degree-based and angle-based constructors to avoid manual conversions.

### SetHorizontalFOV

```cpp
/// Set perspective projection from horizontal field of view.
///
/// Converts horizontal FOV to vertical FOV internally using:
///   vFOV = 2 * atan(tan(hFOV/2) / aspect)
///
/// # Arguments
/// * hFovDeg - Horizontal field of view in degrees (e.g., 90 for wide, 60 for narrow)
/// * aspect - Aspect ratio width / height (e.g., 1920 / 1080 ≈ 1.78)
///
/// # Note
/// Most game engines export horizontal FOV; this method eliminates the conversion boilerplate.
void SetHorizontalFOV(float hFovDeg, float aspect);
```

**Example**:

```cpp
Camera cam;
cam.SetHorizontalFOV(90.0f, 1920.0f / 1080.0f);  // 90° horizontal
// Internally sets vFOV ≈ 58.8°
```

### SetPerspectiveDegrees

```cpp
/// Set perspective projection from degree-based parameters.
///
/// A convenience wrapper over SetPerspective(). Converts all parameters from
/// degrees to radians internally.
///
/// # Arguments
/// * vFovDeg - Vertical field of view in degrees
/// * aspect - Aspect ratio width / height
/// * zNear - Near clipping distance (positive)
/// * zFar - Far clipping distance (positive, > zNear)
void SetPerspectiveDegrees(float vFovDeg, float aspect, float zNear, float zFar);
```

**Example**:

```cpp
Camera cam;
cam.SetPerspectiveDegrees(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
// Avoids: glm::radians(45.0f), etc.
```

### SetLookFromAngles

```cpp
/// Set camera position and rotation from Euler angles.
///
/// Constructs the forward vector from yaw/pitch angles and sets the camera
/// position. Roll is always 0 (camera is level). This is the standard FPS
/// camera setup.
///
/// # Arguments
/// * position - Camera world position
/// * yawDeg - Rotation around up axis (Y), in degrees (0° = +Z forward)
/// * pitchDeg - Rotation around right axis (X), in degrees (positive = look up)
///
/// # Note
/// Pitch is clamped to [-89.9°, +89.9°] to avoid gimbal lock singularities.
///
/// # Example
/// ```cpp
/// cam.SetLookFromAngles({0, 1.5f, -5}, 0.0f, 0.0f);  // At origin, looking forward
/// cam.SetLookFromAngles({0, 1.5f, -5}, 90.0f, 0.0f); // Looking right
/// cam.SetLookFromAngles({0, 1.5f, -5}, 0.0f, 30.0f); // Looking up 30°
/// ```
void SetLookFromAngles(Vec3 position, float yawDeg, float pitchDeg);
```

**Example**:

```cpp
Camera cam;
cam.SetLookFromAngles({5.0f, 2.0f, 10.0f}, 45.0f, -15.0f);
// Position at (5, 2, 10), looking 45° to the right, 15° downward
// Avoids: manual glm::lookAt(pos, pos + forward(yaw, pitch), up)
```

## FPSCameraController

A complete first-person camera controller handling mouse look, WASD movement, sprint, and speed adjustment. Used by all demo scenes.

### API

```cpp
/// First-person camera controller: WASD movement + mouse look + sprint.
class FPSCameraController {
private:
    Camera camera_;
    Vec3 position_;
    float yaw_deg_ = 0.0f;
    float pitch_deg_ = 0.0f;
    float move_speed_ = 5.0f;       // Units per second
    float sprint_speed_ = 15.0f;    // Units per second when sprinting
    float mouse_sensitivity_ = 0.1f; // Degrees per pixel

public:
    /// Initialize at position looking forward.
    explicit FPSCameraController(Vec3 initial_position = {0, 1.5f, 0});

    /// Get the current camera.
    Camera& GetCamera() { return camera_; }
    const Camera& GetCamera() const { return camera_; }

    /// Update from input (call once per frame).
    /// Reads keyboard (WASD, Shift), mouse movement, and scroll wheel.
    void Update(float delta_time, const InputState& input);

    /// Set movement speed (default 5 units/sec).
    void SetMoveSpeed(float speed) { move_speed_ = speed; }
    float GetMoveSpeed() const { return move_speed_; }

    /// Set sprint speed (default 15 units/sec).
    void SetSprintSpeed(float speed) { sprint_speed_ = speed; }
    float GetSprintSpeed() const { return sprint_speed_; }

    /// Set mouse sensitivity (default 0.1 degrees/pixel).
    void SetMouseSensitivity(float sensitivity) { mouse_sensitivity_ = sensitivity; }
    float GetMouseSensitivity() const { return mouse_sensitivity_; }

    /// Get current position.
    Vec3 GetPosition() const { return position_; }

    /// Set position directly (useful for respawning, cutscenes).
    void SetPosition(Vec3 position) {
        position_ = position;
        UpdateCamera();
    }

    /// Get view angle as degrees (yaw, pitch).
    std::pair<float, float> GetAngles() const { return {yaw_deg_, pitch_deg_}; }

private:
    /// Update the Camera object from current position/angles.
    void UpdateCamera();
};
```

### Input Mapping

| Input | Action |
|-------|--------|
| **W** | Move forward |
| **A** | Strafe left |
| **S** | Move backward |
| **D** | Strafe right |
| **Shift** | Sprint (3× move speed) |
| **Mouse X** | Yaw (left/right) |
| **Mouse Y** | Pitch (up/down) |
| **Scroll Up** | Increase move speed by 10% |
| **Scroll Down** | Decrease move speed by 10% |

### Usage Example

```cpp
// In demo initialization
FPSCameraController controller({0, 1.5f, -5});
controller.SetMoveSpeed(8.0f);
controller.SetMouseSensitivity(0.15f);

// In game loop
void OnFrame(float delta_time) {
    InputState input = PollInput();
    controller.Update(delta_time, input);

    Camera& cam = controller.GetCamera();
    RenderScene(cam);
}
```

## Current State: Demo Duplication

Today, FPSCameraController exists in `demos/common/fps_camera_controller.h` but isn't part of the engine library. Every demo includes its own copy or reimplements mouse look:

```cpp
// In pbr_test_demo.cpp
struct SimpleCameraController {
    Vec3 position;
    float yaw, pitch;

    void Update(float dt) {
        if (input.key_down('W')) position += forward * move_speed * dt;
        if (input.key_down('A')) position -= right * move_speed * dt;
        // ... 20 lines of repetitive code ...
    }
};

// In deferred_test_demo.cpp (identical logic, different implementation)
class CameraControl {
    Vec3 pos;
    // ... reimplemented ...
};
```

**Problems**:
- Logic duplicated across demos
- Mouse sensitivity tuning in one demo doesn't propagate
- Sprint speed and movement speed are hardcoded
- No way to share improvements across all demos

## Planned Changes

### 1. Move FPSCameraController to Engine Library

```
runtime/engine/renderer/camera/fps_controller.h
runtime/engine/renderer/camera/fps_controller.cpp
```

### 2. Ensure All Demos Use It

Update `pbr_test_demo.cpp`, `deferred_test_demo.cpp`, `textured_quad_demo.cpp` to use:

```cpp
#include "uz_engine/renderer/camera/fps_controller.h"

FPSCameraController controller({0, 1.5f, -5});

// In frame loop
controller.Update(frame_delta, GetInputState());
Render(controller.GetCamera());
```

### 3. Add to Public Engine API

Export `FPSCameraController` from `engine/public/uz_camera.h`:

```cpp
#include "renderer/camera/fps_controller.h"
```

## Rules

1. **NEVER do hFOV→vFOV conversion manually in demos** — ALWAYS use `Camera::SetHorizontalFOV()`
2. **NEVER hardcode camera setup math** — ALWAYS use `Camera::SetPerspectiveDegrees()` or `SetLookFromAngles()`
3. **NEVER duplicate camera controller logic** — ALWAYS use `FPSCameraController` from engine library
4. **ALWAYS make controller settings configurable** — never hardcode move speed, sensitivity, sprint

## Phase Roadmap

| Phase | What Ships |
|-------|-----------|
| **1.5** | `SetHorizontalFOV()`, `SetPerspectiveDegrees()`, `SetLookFromAngles()` convenience methods |
| **2.0** | Move `FPSCameraController` to engine library, update all demos to use it |
| **2.5** | Cinematic camera paths (spline-based), camera shake effects, controller presets (e.g., RTS, flight sim) |
