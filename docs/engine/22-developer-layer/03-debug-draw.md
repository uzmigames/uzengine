# Debug Draw — Visualization Tools

> **Status**: Planned (Phase 2)
> **Source**: `developer/debug_draw/`
> **References**: UE5 FPrimitiveDrawInterface, Godot DebugDraw3D

## Purpose

Immediate-mode 3D debugging API for visualizing game state: physics shapes, paths, trigger volumes, AI sightlines. All shapes are collected during the frame and rendered in a final debug pass (always on top, no Z-test).

## Debug Draw API

```cpp
namespace uz::dev {

/// Draw a line from start to end
void DrawLine(Vec3 from, Vec3 to, uint32_t color_rgba = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Draw a wireframe box (axis-aligned or rotated)
void DrawBox(Vec3 center, Vec3 extents, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);
void DrawBox(const Transform& transform, Vec3 extents, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Draw a wireframe sphere
void DrawSphere(Vec3 center, float radius, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Draw a wireframe capsule
void DrawCapsule(Vec3 center, float radius, float height, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Draw a wireframe cylinder
void DrawCylinder(Vec3 center, float radius, float height, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Draw a cone (useful for FOV visualization)
void DrawCone(Vec3 apex, Vec3 direction, float height, float angle_degrees, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Draw arrow from start to end
void DrawArrow(Vec3 from, Vec3 to, float arrow_size = 10.0f, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Draw text in world space (always faces camera)
void DrawText(Vec3 position, StringView text, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Draw text in screen space (top-left origin, pixels)
void DrawScreenText(int x, int y, StringView text, uint32_t color = 0xFFFFFFFFu);

/// Draw a grid on XZ plane
void DrawGrid(Vec3 center, int grid_size, float cell_size, uint32_t color = 0xFF808080u, float lifetime = 0.0f);

/// Draw coordinate axes (red=X, green=Y, blue=Z)
void DrawAxes(const Transform& transform, float scale = 100.0f, float lifetime = 0.0f);

/// Draw a path (line segments connecting waypoints)
void DrawPath(const std::vector<Vec3>& waypoints, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

/// Clear all persistent debug shapes
void ClearDebugDraw();

}  // namespace uz::dev
```

## Lifetime Modes

Three ways to control shape visibility:

```cpp
// Draw once this frame only
uz::dev::DrawLine(from, to, color, lifetime = 0.0f);

// Draw for N seconds, then fade
uz::dev::DrawLine(from, to, color, lifetime = 5.0f);

// Persist until explicitly cleared
uz::dev::DrawLine(from, to, color, lifetime = -1.0f);

// Clear all persistent shapes
uz::dev::ClearDebugDraw();
```

## Color Utilities

Predefined colors as RGBA hex:

```cpp
namespace uz::dev {

constexpr uint32_t WHITE    = 0xFFFFFFFFu;
constexpr uint32_t RED      = 0xFF0000FFu;
constexpr uint32_t GREEN    = 0xFF00FF00u;
constexpr uint32_t BLUE     = 0xFFFF0000u;
constexpr uint32_t YELLOW   = 0xFF00FFFFu;
constexpr uint32_t CYAN     = 0xFFFFFF00u;
constexpr uint32_t MAGENTA  = 0xFFFF00FFu;
constexpr uint32_t BLACK    = 0xFF000000u;

/// Create color from RGB (alpha = 1.0)
uint32_t MakeColor(uint8_t r, uint8_t g, uint8_t b) {
    return (255u << 24) | (b << 16) | (g << 8) | r;
}

/// Create color from RGBA
uint32_t MakeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

}  // namespace uz::dev
```

## Usage Patterns

### Physics Debugging

```cpp
// Visualize physics bodies
for (auto& body : physics_world->GetBodies()) {
    auto color = (body->IsKinematic() ? YELLOW : (body->IsStatic() ? GRAY : GREEN));
    uz::dev::DrawBox(body->GetTransform(), body->GetExtents(), color, -1.0f);
}

// Show raycast
RaycastHit hit;
if (physics_world->Raycast(from, direction, hit)) {
    uz::dev::DrawLine(from, hit.position, GREEN);  // Hit
    uz::dev::DrawSphere(hit.position, 5.0f, RED);  // Impact point
} else {
    uz::dev::DrawLine(from, from + direction * 1000.0f, RED);  // Miss
}
```

### AI Debugging

```cpp
// Show NPC FOV cone
auto npc = scene->FindEntity("Goblin");
auto transform = npc->GetComponent<Transform>();
uz::dev::DrawCone(
    transform->position,
    transform->GetForward(),
    500.0f,  // view distance
    60.0f,   // FOV angle
    YELLOW,
    -1.0f    // persistent
);

// Show behavior tree path
auto agent = npc->GetComponent<NavMeshAgentComponent>();
if (agent->IsMoving()) {
    uz::dev::DrawPath(agent->GetCurrentPath(), GREEN, -1.0f);
    uz::dev::DrawSphere(agent->GetCurrentDestination(), 50.0f, BLUE, -1.0f);
}
```

### Trigger Volume Visualization

```cpp
// Show active triggers
world->ForEachComponent<TriggerVolumeComponent>([](TriggerVolumeComponent* trigger) {
    auto color = trigger->HasOverlappers() ? RED : BLUE;
    uz::dev::DrawBox(trigger->GetTransform(), trigger->GetExtents(), color, 0.0f);
});
```

### Animation Debugging

```cpp
// Show skeleton bones
auto anim = entity->GetComponent<SkeletalMeshComponent>();
for (auto& bone : anim->GetBones()) {
    auto parent = bone.parent;
    if (parent) {
        uz::dev::DrawLine(bone.world_position, parent->world_position, CYAN);
    }
    uz::dev::DrawSphere(bone.world_position, 5.0f, MAGENTA);
}
```

## Screen-Space Overlay

Debug text drawn on screen:

```cpp
void DebugOverlayUpdate() {
    uz::dev::DrawScreenText(10, 10, "FPS: 120");
    uz::dev::DrawScreenText(10, 30, "Entities: 256");
    uz::dev::DrawScreenText(10, 50, "Draw calls: 512");

    // Color-coded status
    auto color = performance_ok ? 0xFF00FF00u : 0xFF0000FFu;
    uz::dev::DrawScreenText(10, 70, "Frame: 12.3ms", color);
}
```

## Category Filtering

Filter which debug shapes are visible by category:

```cpp
namespace uz::dev {

enum class DebugCategory : uint32_t {
    Physics      = 1 << 0,
    Navigation   = 1 << 1,
    AI           = 1 << 2,
    Rendering    = 1 << 3,
    Triggers     = 1 << 4,
    Animation    = 1 << 5,
    All          = 0xFFFFFFFFu
};

/// Toggle visibility of category
void SetDebugCategoryVisible(DebugCategory category, bool visible);

/// Get visibility of category
bool IsDebugCategoryVisible(DebugCategory category);

}  // namespace uz::dev

// Usage:
// Show only physics and AI shapes
uz::dev::SetDebugCategoryVisible(uz::dev::DebugCategory::Physics, true);
uz::dev::SetDebugCategoryVisible(uz::dev::DebugCategory::AI, true);
uz::dev::SetDebugCategoryVisible(uz::dev::DebugCategory::Navigation, false);
```

## Implementation

### Collection Phase

During the frame, all DrawX() calls append to a command list:

```cpp
namespace uz::dev {

struct DebugShape {
    enum Type { Line, Box, Sphere, Capsule, Arrow, Text, Path, Cone };
    Type type;
    Vec3 position;
    Vec3 direction;
    float radius, height, angle;
    uint32_t color;
    float lifetime;
    std::string text;
    DebugCategory category;
};

static thread_local std::vector<DebugShape> frame_shapes_;
static std::vector<DebugShape> persistent_shapes_;

}  // namespace uz::dev
```

### Rendering Phase

After all game rendering, debug shapes are rendered in a final pass:

```cpp
void DebugDrawSystem::Render(RHI::CommandList& cmd) {
    UZ_PROFILE_SCOPE("DebugDraw::Render");

    // Combine frame + persistent shapes
    std::vector<DebugShape> all_shapes = persistent_shapes_;
    all_shapes.insert(all_shapes.end(), frame_shapes_.begin(), frame_shapes_.end());

    // Filter by category visibility
    std::vector<DebugShape> visible;
    for (auto& shape : all_shapes) {
        if (IsDebugCategoryVisible(shape.category)) {
            visible.push_back(shape);
        }
    }

    // Render each shape
    for (auto& shape : visible) {
        switch (shape.type) {
            case DebugShape::Line:
                RenderLine(cmd, shape.position, shape.direction, shape.color);
                break;
            case DebugShape::Sphere:
                RenderSphere(cmd, shape.position, shape.radius, shape.color);
                break;
            // ... other types
        }
    }

    // Remove expired shapes
    auto now = std::chrono::high_resolution_clock::now();
    persistent_shapes_.erase(
        std::remove_if(persistent_shapes_.begin(), persistent_shapes_.end(),
            [now](const DebugShape& s) {
                if (s.lifetime < 0) return false;  // persistent
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - s.created_time
                ).count();
                return age > s.lifetime;
            }),
        persistent_shapes_.end()
    );

    // Clear frame shapes
    frame_shapes_.clear();
}
```

## Lua Integration

```lua
-- Draw debug line
uz.dev.draw_line(from_pos, to_pos, 0xFF00FF00)

-- Draw persistent sphere
uz.dev.draw_sphere(center, radius, 0xFFFF0000, -1.0)  -- lifetime -1 = persistent

-- Clear all debug shapes
uz.dev.clear_debug_draw()

-- Show/hide categories
uz.dev.set_debug_category_visible("Physics", true)
uz.dev.set_debug_category_visible("Navigation", false)
```

## Performance

Debug drawing is optimized for development builds:
- Shapes collected in memory, rendered once per frame
- No Z-test (always on top)
- Simple shader (untransformed line/triangle rasterization)
- Batch rendering by shape type

Overhead: ~1–2ms per frame with 1000+ shapes (negligible in 60 FPS context).

## Comparison

| Feature | UE5 FPrimitiveDrawInterface | Godot DebugDraw3D | **UzEngine** |
|---------|---------------------------|------------------|--------------|
| Lines | Yes | Yes | Yes |
| Shapes (box, sphere, etc.) | Yes | Limited | Yes |
| World text | Yes | Limited | Yes |
| Screen text | Yes (HUD) | Yes | Yes |
| Persistent shapes | Yes (DebugDraw) | Yes | Yes (lifetime < 0) |
| Category filtering | No | No | Yes |
| Lifetime control | No (persistent only) | Yes | Yes (0, >0, <0) |

## Implementation Phase

- **Phase 2**: Basic shapes (line, box, sphere, capsule, arrow, text), persistent shapes, lifetime modes
- **Phase 3**: Advanced shapes (cone, cylinder, grid, axes, path), screen-space overlay
- **Phase 4**: Category filtering, performance optimization, visual editor (draw shapes by clicking in viewport)
- **Phase 5**: Gizmo tools (move, rotate, scale objects with debug draw gizmos)
