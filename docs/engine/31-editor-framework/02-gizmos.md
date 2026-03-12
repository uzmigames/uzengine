# Gizmos — Transform Manipulation Handles

> **Status**: Planned (Phase 2–3)
> **Source**: `runtime/editor/gizmo_system.h`, `extension/editor/gizmo_renderer.ts`
> **References**: UE5 FWidget, Godot Node3DGizmo, Unity Handle system

## Purpose

Gizmos are interactive 3D handles that allow users to transform (move, rotate, scale) entities directly in the viewport. They must:

1. Remain constant screen-space size (always visible at any zoom)
2. Support axis-aligned constraints (move only X, or X+Y plane)
3. Provide visual feedback (highlight on hover, different colors per axis)
4. Integrate with undo/redo system (each interaction = one command)
5. Work with snapping (grid, angle, vertex, surface)

## Architecture

```
Editor Viewport Client (C++)
    │
    ├─ Input: mouse move/down/up
    │
    └─ Gizmo System
        ├─ Update() — check mouse proximity to handles
        ├─ Render() — draw handles in screen-space
        └─ GetDelta() — return transform change if being manipulated
            │
            └─ Undo Stack
                └─ Enqueue TransformCommand(old_transform, new_transform)
```

## Gizmo Modes

### Translate Gizmo

```cpp
/// Position gizmo — move entity along X, Y, Z axes or XY/XZ/YZ planes.
class TranslateGizmo {
public:
    enum class ConstraintMode {
        None,        // Free movement (use only when clicking on center)
        X,           // Only X axis
        Y,           // Only Y axis
        Z,           // Only Z axis
        XY,          // Plane parallel to XY (move in X+Y, constrained by Z)
        XZ,
        YZ
    };

    /// Update gizmo state based on mouse position.
    /// Returns the delta transform if gizmo is being manipulated.
    struct GizmoState {
        bool is_active = false;
        ConstraintMode constraint = ConstraintMode::None;
        Vec3 delta = {};  // Change to apply
    };

    GizmoState Update(Vec2 mouse_screen_pos, const Transform& entity_transform,
                      const Camera& camera, float viewport_width, float viewport_height);

    /// Render gizmo handles.
    void Render(CommandList& cmd, const Transform& entity_transform,
                const Camera& camera, float viewport_width, float viewport_height);

private:
    struct Handle {
        Vec3 local_position;        // Relative to entity center
        ConstraintMode constraint;
        uint32_t color;             // RGBA
        bool is_hovered = false;
        bool is_active = false;
    };

    // 6 handles: +X, -X, +Y, -Y, +Z, -Z arrows (and center sphere for free move)
    Handle handles_[7];
    ConstraintMode active_constraint_ = ConstraintMode::None;
    Vec3 drag_start_world_;
    Vec3 drag_start_entity_pos_;
};
```

### Rotate Gizmo

```cpp
/// Rotation gizmo — rotate around X, Y, Z axes or in screen plane.
class RotateGizmo {
public:
    enum class ConstraintMode {
        None,   // Free rotation (screen plane)
        X,      // Rotate around X axis
        Y,      // Rotate around Y axis
        Z       // Rotate around Z axis
    };

    struct GizmoState {
        bool is_active = false;
        ConstraintMode constraint = ConstraintMode::None;
        Quat delta_rotation = {};  // Change to apply
    };

    GizmoState Update(Vec2 mouse_screen_pos, const Transform& entity_transform,
                      const Camera& camera);

    void Render(CommandList& cmd, const Transform& entity_transform,
                const Camera& camera);

private:
    // Three color-coded rings (X=red, Y=green, Z=blue)
    // Plus outer ring for screen-plane rotation
    static constexpr uint32_t kColorX = 0xFF0000FF;  // Red (RGBA)
    static constexpr uint32_t kColorY = 0x00FF00FF;  // Green
    static constexpr uint32_t kColorZ = 0x0000FFFF;  // Blue

    struct Ring {
        Vec3 axis;                  // Rotation axis (X, Y, or Z)
        ConstraintMode constraint;
        uint32_t color;
        bool is_hovered = false;
        bool is_active = false;
    };

    Ring rings_[4];  // X, Y, Z, and screen-plane
    float drag_start_angle_ = 0.0f;
    Vec2 drag_start_mouse_;
};
```

### Scale Gizmo

```cpp
/// Scale gizmo — non-uniform scale along axes.
class ScaleGizmo {
public:
    enum class ConstraintMode {
        None,   // Uniform scale (drag from center)
        X,      // Scale X only
        Y,      // Scale Y only
        Z       // Scale Z only
    };

    struct GizmoState {
        bool is_active = false;
        ConstraintMode constraint = ConstraintMode::None;
        Vec3 scale_factor = Vec3(1, 1, 1);  // Multiply current scale by this
    };

    GizmoState Update(Vec2 mouse_screen_pos, const Transform& entity_transform,
                      const Camera& camera, float viewport_width, float viewport_height);

    void Render(CommandList& cmd, const Transform& entity_transform,
                const Camera& camera);
};
```

## Screen-Space Positioning

Gizmo handles must remain constant-size on screen (e.g., 32 pixels from center). This requires projecting world position to screen space, then scaling handle size:

```cpp
/// Convert world position to screen coordinates.
/// Returns NDC (normalized device coordinates) where (-1, -1) is bottom-left, (1, 1) is top-right.
Vec2 ProjectToScreen(const Vec3& world_pos, const Camera& camera, float width, float height) {
    Vec4 projected = camera.view_projection * Vec4(world_pos, 1.0f);
    Vec2 ndc = projected.xy / projected.w;  // Perspective divide
    Vec2 screen = (ndc + Vec2(1, 1)) * 0.5f * Vec2(width, height);  // To pixel coords
    return screen;
}

/// Get handle size in world units (so it stays constant screen-size).
float GetHandleScreenSize(const Vec3& world_pos, const Camera& camera,
                         float target_screen_pixels, float width, float height) {
    float distance = (world_pos - camera.position).Length();
    float fov_tan = std::tan(camera.fov * 0.5f);  // FOV in radians
    float world_height = 2.0f * distance * fov_tan;
    float screen_height_pixels = height;
    float world_size = world_height * (target_screen_pixels / screen_height_pixels);
    return world_size;
}
```

## Axis Constraint System

```cpp
/// Constrain movement to specific axes or planes.
class AxisConstraint {
public:
    enum class Type {
        Free,      // No constraint
        AxisX,     // Lock to X axis (Y and Z must be constant)
        AxisY,
        AxisZ,
        PlaneXY,   // Move in XY plane (Z constant)
        PlaneXZ,
        PlaneYZ
    };

    /// Given desired delta and constraint type, return clamped delta.
    Vec3 ApplyConstraint(const Vec3& desired_delta, Type constraint) {
        switch (constraint) {
            case Type::AxisX:  return Vec3(desired_delta.x, 0, 0);
            case Type::AxisY:  return Vec3(0, desired_delta.y, 0);
            case Type::AxisZ:  return Vec3(0, 0, desired_delta.z);
            case Type::PlaneXY: return Vec3(desired_delta.x, desired_delta.y, 0);
            case Type::PlaneXZ: return Vec3(desired_delta.x, 0, desired_delta.z);
            case Type::PlaneYZ: return Vec3(0, desired_delta.y, desired_delta.z);
            default: return desired_delta;
        }
    }

    /// Get axis direction (for rendering, e.g., arrow direction).
    Vec3 GetAxisDirection(Type constraint) {
        switch (constraint) {
            case Type::AxisX: return Vec3(1, 0, 0);
            case Type::AxisY: return Vec3(0, 1, 0);
            case Type::AxisZ: return Vec3(0, 0, 1);
            default: return Vec3(0, 0, 1);  // Z-up by convention
        }
    }
};
```

## Snapping System

```cpp
/// Snap movements/rotations to discrete increments.
class SnapController {
public:
    struct SnapSettings {
        bool enable = true;

        // Translate snapping
        float grid_size = 0.25f;       // 0 = no snap
        bool snap_to_grid = true;

        // Rotate snapping
        float angle_snap = 15.0f;      // degrees (0 = no snap)
        bool snap_to_angle = true;

        // Advanced snapping
        bool snap_to_surface = false;  // Raycast from gizmo to nearest surface
        bool snap_to_vertex = false;   // Snap to nearest mesh vertex
    };

    /// Apply snapping to a transform delta.
    Vec3 SnapTranslation(const Vec3& delta, const SnapSettings& settings) {
        if (!settings.snap_to_grid || settings.grid_size <= 0.01f) {
            return delta;
        }

        float grid = settings.grid_size;
        return Vec3(
            std::round(delta.x / grid) * grid,
            std::round(delta.y / grid) * grid,
            std::round(delta.z / grid) * grid
        );
    }

    /// Snap rotation angle.
    float SnapRotation(float angle_radians, const SnapSettings& settings) {
        if (!settings.snap_to_angle || settings.angle_snap <= 0.01f) {
            return angle_radians;
        }

        float angle_deg = angle_radians * kRadiansToDegrees;
        float snap_deg = settings.angle_snap;
        float snapped = std::round(angle_deg / snap_deg) * snap_deg;
        return snapped * kDegreesToRadians;
    }

    /// Snap to nearest surface (raycasts from gizmo handle outward).
    Vec3 SnapToSurface(const Vec3& handle_world_pos, const Scene& scene) {
        // Cast ray outward, find nearest surface, return intersection point
        // (Simplified; real implementation would check multiple directions)
        RaycastResult hit = scene.Raycast(
            handle_world_pos,
            Vec3(0, -1, 0),  // Downward
            1000.0f
        );
        if (hit.is_hit) {
            return hit.position;
        }
        return handle_world_pos;
    }
};
```

## Transform Space

Allow users to toggle between local and world transform:

```cpp
/// Transform space for gizmo operations.
enum class TransformSpace {
    World,   // Gizmo axes = world axes (X, Y, Z)
    Local    // Gizmo axes = entity's local frame (forward, right, up)
};

/// Update gizmo based on selected space.
void GizmoSystem::SetTransformSpace(TransformSpace space) {
    space_ = space;
    // Recalculate handle positions relative to entity's local frame
}

// In rendering: if space == Local, transform gizmo by entity's rotation matrix
```

## Visual Feedback

### Hover Highlighting

```cpp
/// Highlight handles when mouse hovers near them.
void GizmoSystem::UpdateHoverState(Vec2 mouse_screen_pos, const Camera& camera) {
    for (auto& handle : handles_) {
        Vec2 handle_screen = ProjectToScreen(handle.world_position, camera, width, height);
        float distance = (handle_screen - mouse_screen_pos).Length();

        // Hit test: if within 16 pixels, mark as hovered
        handle.is_hovered = distance < 16.0f;
    }
}

// In render: if hovered, brighten color or expand size
uint32_t GetHandleColor(const Handle& handle) {
    uint32_t base_color = handle.color;
    if (handle.is_hovered) {
        // Brighten by 50%
        uint8_t r = (base_color >> 24) & 0xFF;
        uint8_t g = (base_color >> 16) & 0xFF;
        uint8_t b = (base_color >>  8) & 0xFF;
        uint8_t a = (base_color >>  0) & 0xFF;
        r = std::min(255u, (uint32_t)r + 127);
        g = std::min(255u, (uint32_t)g + 127);
        b = std::min(255u, (uint32_t)b + 127);
        return (r << 24) | (g << 16) | (b << 8) | a;
    }
    return base_color;
}
```

### Rendering Gizmo Geometry

```cpp
/// Render gizmo handles as 3D geometry (cones, rings, planes).
void GizmoSystem::RenderTranslateGizmo(CommandList& cmd, const Transform& entity,
                                       const Camera& camera) {
    // X axis: red arrow pointing +X
    RenderArrow(cmd, entity.position,
                entity.rotation * Vec3(1, 0, 0),  // Local X axis in world space
                kHandleSize, 0xFF0000FF,  // Red
                camera);

    // Y axis: green arrow
    RenderArrow(cmd, entity.position,
                entity.rotation * Vec3(0, 1, 0),  // Local Y
                kHandleSize, 0x00FF00FF,  // Green
                camera);

    // Z axis: blue arrow
    RenderArrow(cmd, entity.position,
                entity.rotation * Vec3(0, 0, 1),  // Local Z
                kHandleSize, 0x0000FFFF,  // Blue
                camera);

    // XY plane: small quad between X and Y handles
    RenderPlaneQuad(cmd, entity.position,
                    entity.rotation * Vec3(1, 0, 0),
                    entity.rotation * Vec3(0, 1, 0),
                    0xFFFF00FF * 0.3f,  // Yellow, 30% opaque
                    camera);
}

void RenderArrow(CommandList& cmd, Vec3 base, Vec3 direction,
                 float length, uint32_t color, const Camera& camera) {
    // Draw line from base to base + direction * length
    // Draw small cone at tip
    // (Real implementation would use geometry buffers)
}
```

## Integration with Undo/Redo

Each gizmo drag creates one undoable command:

```cpp
void GizmoSystem::OnMouseUp() {
    if (!is_dragging_) return;

    // Create undo command
    auto command = std::make_unique<TransformCommand>(
        selected_entity_,
        initial_transform_,      // Before drag
        selected_entity_.GetComponent<Transform>()  // After drag
    );

    undo_stack_->ExecuteCommand(std::move(command));
    is_dragging_ = false;
}
```

## Comparison

| Feature | UE5 (FWidget) | Godot (Gizmo) | Unity (Handle) | **UzEngine** |
|---|---|---|---|---|
| **Screen-space sizing** | Yes | Yes | Yes | **Yes** |
| **Axis constraints** | Yes | Yes | Yes | **Yes** |
| **Transform space** | Local/World | Local/World | Local/World | **Local/World** |
| **Snapping** | Grid, vertex, surface | Grid, pixel | Grid | **Grid, angle, surface, vertex** |
| **Undo/Redo** | FTransaction | EditorUndoRedo | Undo.RecordObject | **ICommand pattern** |
| **Custom gizmo types** | Widget API | Node3DGizmo subclass | Handle.* helpers | **(Extensible via behavior)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Translate gizmo (X/Y/Z/plane), basic rendering, screen-space sizing, axis constraints |
| **Phase 3** | Rotate + Scale gizmos, hover highlighting, snapping (grid + angle), transform space toggle, undo/redo integration |
| **Phase 4** | Custom gizmos (user-defined), advanced snapping (surface + vertex), gizmo visibility toggling, handle visibility per-axis |

