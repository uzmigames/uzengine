# Camera System — Post-Process Volumes

> **Status**: Planned (Phase 3)
> **Source**: `engine/rendering/post_process_volume.h`, `post_process_volume.cpp`
> **Depends on**: Renderer (§12), Camera system, RHI (§3)
> **References**: UE5 `APostProcessVolume`, Godot `WorldEnvironment`

## Purpose

Post-process volumes define regions in the world where specific rendering effects are applied (bloom strength, color grading, motion blur, depth-of-field). As the camera moves through volumes, effects blend smoothly. This enables area-specific visual moods without per-entity overrides.

## Post-Process Volume Architecture

```
World
├── PostProcessVolume A (bloom=2.0, saturation=0.8)
│   └── Unbound (affects entire level)
│
├── PostProcessVolume B (vignette=0.5, color_temp=3000K)
│   ├── Box shape (5m x 10m x 3m)
│   └── Blend distance: 2m
│
└── PostProcessVolume C (motion_blur=0.2)
    ├── Sphere shape (radius 15m)
    └── Blend distance: 5m
```

## PostProcessVolume Struct

```cpp
/// World region with configurable post-process settings.
/// Multiple volumes blend based on camera position.
struct PostProcessVolume {
    // ─────────────────────────────────────────────────────────────────
    // Shape and Bounds
    // ─────────────────────────────────────────────────────────────────

    enum class VolumeShape {
        Infinite,      // Affects entire level (typical for global mood)
        Box,           // Axis-aligned box
        Sphere,        // Sphere
        Capsule,       // Cylinder + rounded ends
    };

    VolumeShape shape = VolumeShape::Infinite;
    Vec3 position;           // World position of volume center
    Vec3 box_extents;        // For box: half-width in each dimension
    float sphere_radius;     // For sphere/capsule: radius
    Quat rotation;           // For rotated boxes

    // ─────────────────────────────────────────────────────────────────
    // Blending
    // ─────────────────────────────────────────────────────────────────

    /// Distance from volume boundary to fully attenuate effect
    /// Camera inside volume = full effect, beyond boundary+blend_distance = zero
    float blend_distance = 2.0f;

    /// Priority for overlapping volumes (higher = more dominant)
    int32_t priority = 0;

    // ─────────────────────────────────────────────────────────────────
    // Post-Process Settings
    // ─────────────────────────────────────────────────────────────────

    struct Settings {
        // Color grading
        float brightness = 1.0f;        // Linear multiplier
        float contrast = 1.0f;          // 1.0 = no change
        float saturation = 1.0f;        // 0 = grayscale, 2 = oversaturated
        Vec3 color_tint = {1, 1, 1};    // RGB tint

        // Color temperature (in Kelvin, ~3000K=warm, ~6500K=neutral, ~10000K=cool)
        float color_temperature = 6500.0f;

        // Bloom
        bool enable_bloom = false;
        float bloom_intensity = 1.0f;
        float bloom_threshold = 1.0f;
        float bloom_blur_size = 1.0f;

        // Depth of field
        bool enable_dof = false;
        float dof_focus_distance = 100.0f;
        float dof_aperture = 8.0f;      // f/stop, higher = more in-focus
        float dof_blur_radius = 5.0f;

        // Motion blur
        bool enable_motion_blur = false;
        float motion_blur_amount = 0.5f;

        // Chromatic aberration
        bool enable_chromatic_abberation = false;
        float chromatic_amount = 0.05f;

        // Vignette
        bool enable_vignette = false;
        float vignette_intensity = 0.3f;
        float vignette_smoothness = 2.0f;

        // Film grain
        bool enable_film_grain = false;
        float film_grain_intensity = 0.1f;

        // Ambient occlusion
        bool enable_ambient_occlusion = false;
        float ao_radius = 100.0f;
        float ao_intensity = 1.0f;
    } settings;
};
```

## Volume Priority and Blending

When the camera is inside multiple overlapping volumes, effects blend based on priority and blend distance:

```cpp
class PostProcessVolumeManager {
public:
    /// Query which volumes affect the camera position
    std::vector<PostProcessVolume*> FindVolumesAtPosition(Vec3 camera_pos);

    /// Blend settings from all active volumes
    PostProcessVolume::Settings ComputeBlendedSettings(
        Vec3 camera_pos,
        const std::vector<PostProcessVolume*>& volumes
    ) {
        PostProcessVolume::Settings result = {};

        for (auto* volume : volumes) {
            float influence = ComputeVolumeInfluence(volume, camera_pos);

            // Blend each setting
            result.brightness += volume->settings.brightness * influence;
            result.saturation += volume->settings.saturation * influence;
            result.bloom_intensity += volume->settings.bloom_intensity * influence;
            // ... etc for all settings ...
        }

        return result;
    }

private:
    /// Compute influence (0 to 1) based on distance from volume
    float ComputeVolumeInfluence(const PostProcessVolume* volume, Vec3 camera_pos) {
        float distance = GetDistanceToVolume(volume, camera_pos);

        // Inside volume = 1.0
        if (distance <= 0.0f) return 1.0f;

        // Beyond blend distance = 0.0
        if (distance >= volume->blend_distance) return 0.0f;

        // Linear falloff
        return 1.0f - (distance / volume->blend_distance);
    }

    /// Signed distance from point to volume surface
    float GetDistanceToVolume(const PostProcessVolume* volume, Vec3 point) {
        switch (volume->shape) {
            case VolumeShape::Infinite:
                return -1.0f;  // Always inside
            case VolumeShape::Sphere:
                return (point - volume->position).Length() - volume->sphere_radius;
            case VolumeShape::Box:
                return DistanceToBox(point, volume->position, volume->box_extents);
            // ... other shapes ...
        }
    }
};
```

## Volume Creation

In C++ code:

```cpp
// Create a cave (dark, moody post-process volume)
PostProcessVolume cave_mood = {
    .shape = PostProcessVolume::VolumeShape::Box,
    .position = {200, 100, -50},
    .box_extents = {150, 100, 200},
    .blend_distance = 3.0f,
    .priority = 10,
    .settings = {
        .brightness = 0.6f,       // Darker
        .saturation = 0.7f,       // Less saturated
        .color_tint = {0.9f, 0.85f, 1.0f},  // Bluish cave lighting
        .enable_vignette = true,
        .vignette_intensity = 0.4f,
    }
};
world->PostProcessManager()->AddVolume(cave_mood);
```

In scene file (`.uzscene`):

```json
{
    "id": "cave_volume",
    "components": {
        "PostProcessVolumeComponent": {
            "shape": "box",
            "extents": [150, 100, 200],
            "blend_distance": 3.0,
            "priority": 10,
            "settings": {
                "brightness": 0.6,
                "saturation": 0.7,
                "color_tint": [0.9, 0.85, 1.0],
                "enable_vignette": true,
                "vignette_intensity": 0.4
            }
        }
    }
}
```

## Example: Forest to Cave Transition

```cpp
// Forest volume (bright, green-tinted)
PostProcessVolume forest_volume = {
    .shape = VolumeShape::Infinite,
    .settings = {
        .brightness = 1.0f,
        .saturation = 1.1f,
        .color_tint = {0.95f, 1.0f, 0.9f},  // Greenish sunlight
        .enable_bloom = true,
        .bloom_intensity = 0.8f,
    }
};

// Cave volume (dark, cool-tinted, sharp contrast)
PostProcessVolume cave_volume = {
    .shape = VolumeShape::Box,
    .position = cave_entrance,
    .box_extents = {100, 100, 150},
    .blend_distance = 5.0f,  // Gradual transition
    .priority = 20,          // Higher priority than forest
    .settings = {
        .brightness = 0.5f,
        .contrast = 1.2f,
        .color_tint = {0.8f, 0.85f, 1.0f},  // Cool underground lighting
        .enable_vignette = true,
        .vignette_intensity = 0.3f,
        .bloom_intensity = 0.0f,  // No bloom underground
    }
};

// As camera crosses threshold, effects blend smoothly:
// At forest entrance: forest (100%) + cave (0%)
// At boundary: forest (50%) + cave (50%)
// 2m inside cave: forest (0%) + cave (100%)
```

## Common Preset Configurations

### Underwater (Teal, Hazy)

```cpp
PostProcessVolume underwater = {
    .settings = {
        .brightness = 0.7f,
        .saturation = 0.6f,
        .color_tint = {0.3f, 0.7f, 0.9f},
        .color_temperature = 4000.0f,
        .enable_dof = true,
        .dof_focus_distance = 300.0f,
        .dof_aperture = 2.0f,
        .dof_blur_radius = 15.0f,
        .chromatic_amount = 0.08f,
        .bloom_intensity = 0.2f,
    }
};
```

### Desert Sandstorm (Warm, Dusty, Low Contrast)

```cpp
PostProcessVolume sandstorm = {
    .settings = {
        .brightness = 0.8f,
        .contrast = 0.6f,
        .saturation = 0.8f,
        .color_tint = {1.0f, 0.85f, 0.6f},
        .color_temperature = 3500.0f,
        .enable_film_grain = true,
        .film_grain_intensity = 0.15f,
        .chromatic_amount = 0.1f,
        .enable_motion_blur = true,
        .motion_blur_amount = 0.3f,
    }
};
```

### Burned Ruins (Red, High Contrast, Desaturated)

```cpp
PostProcessVolume ruins = {
    .settings = {
        .brightness = 0.6f,
        .contrast = 1.4f,
        .saturation = 0.3f,
        .color_tint = {1.0f, 0.4f, 0.3f},
        .color_temperature = 2800.0f,
        .enable_vignette = true,
        .vignette_intensity = 0.5f,
        .vignette_smoothness = 3.0f,
    }
};
```

## Performance Considerations

Post-process effects are GPU-intensive. Each enabled effect adds a fullscreen shader pass:

```
Active Effects → Total Passes
1 bloom       → 1 pass
2 bloom + dof → 2 passes
3 bloom + dof + motion_blur → 3 passes + history buffer
```

**Guidelines**:
- Keep enabled effects per volume to 2-3 maximum
- Expensive: DOF (requires quad read), Motion Blur (requires history buffer)
- Cheap: Color grading, vignette, bloom (single pass)
- Use `enable_motion_blur` only for dynamic camera (not slow pan)

## Lua API

```lua
-- Create a post-process volume in script
function create_cave_mood(ctx)
    local volume = ctx.world:create_entity("CaveVolume")
    volume:add_component("PostProcessVolume", {
        shape = "box",
        extents = {150, 100, 200},
        blend_distance = 3.0,
        priority = 10,
        settings = {
            brightness = 0.6,
            saturation = 0.7,
            color_tint = {0.9, 0.85, 1.0},
            enable_vignette = true,
            vignette_intensity = 0.4,
        }
    })
end
```

## Engine Comparison

| Feature | UE5 | Godot 4 | Unity | **UzEngine** |
|---------|-----|---------|-------|------------|
| PP Volumes | `APostProcessVolume` (infinite/bounded) | `WorldEnvironment` | Post-Processing Stack | **PostProcessVolume** (infinite/box/sphere) |
| Blending | Linear falloff by distance | Not built-in | Manual fade | **Priority + blend_distance** |
| Settings | 20+ post-process parameters | Environment properties | VolumeProfile list | **Bloom, DOF, color grading, vignette, etc.** |
| Performance | Optimized via ubershaders | Heavy | Stack passes | **Per-effect pass, 2-3 typical** |

## Rules

1. **NEVER enable more than 3 post-process effects per volume** — framerate will tank
2. **ALWAYS use Infinite shape for global mood** — one volume sets baseline, bounded override details
3. **NEVER set blend_distance to zero** — abrupt cuts feel jarring; use 2-5m
4. **ALWAYS test overlapping volumes** — priority must be clearly defined
5. **NEVER use high-cost effects (DOF, motion blur) in fast-paced areas** — latency matters in combat
