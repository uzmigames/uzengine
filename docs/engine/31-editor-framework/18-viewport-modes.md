# Viewport Modes — Shading Visualization & Debug Rendering

> **Status**: Done (v0.3.0+ — 13 view modes: lit, albedo, worldnormal, metallic, roughness, ao, depth, worldposition, lighting, shadows, wireframe, unlit, overview)
> **Source**: `runtime/editor/viewport.h`, `runtime/editor/viewport_modes.h`, `extension/editor/viewport_ui.ts`
> **References**: UE5 Viewport Modes (Lit, Wireframe, LightOnly, etc.), Godot 3D Viewport, Three.js DebugMode

## Purpose

A viewport mode system provides alternative visualizations of the scene for debugging, optimization, and content validation. Rather than showing only the final rendered result (Lit mode), developers can inspect materials, lighting contributions, collision geometry, navigation data, and rendering performance.

## Architecture

```
User selects Viewport Mode
    │
    └─ Runtime receives mode change via IPC
        │
        ├─ Switch RenderGraph or shader variant
        │   ├─ Lit (standard PBR)
        │   ├─ Unlit (skip lighting calculations)
        │   ├─ Wireframe (render edge lines)
        │   ├─ Normal debug (visualize normals)
        │   ├─ Buffer visualization (show GBuffer channels)
        │   └─ Debug rendering overlay (overdraw, light complexity, etc.)
        │
        └─ Store in EditorViewport::current_mode_
            │
            └─ Each frame, select appropriate render pass
```

## Shading Modes

### Lit (Default)

Standard physically-based rendering with all lighting, shadows, reflections.

```cpp
/// Render with full lighting, shadows, reflections.
class LitMode : public IViewportMode {
public:
    StringView GetName() const override { return "Lit"; }

    /// Switch RenderGraph to standard PBR pass.
    void Activate(RenderWorld& world) override {
        // world.render_graph_.SelectPass("pbr");
        // Enable all lights, shadows, IBL reflection
    }

    void Deactivate(RenderWorld& world) override {
        // Cleanup if needed
    }
};
```

### Unlit

Render materials without light calculations — shows pure base color.

```cpp
class UnlitMode : public IViewportMode {
public:
    StringView GetName() const override { return "Unlit"; }

    void Activate(RenderWorld& world) override {
        // Render only baseColor (diffuse texture) without any shading
        // Useful for seeing pure material without light influence
    }
};
```

### Wireframe

Render mesh edges as lines, useful for mesh topology inspection.

```cpp
class WireframeMode : public IViewportMode {
public:
    StringView GetName() const override { return "Wireframe"; }

    void Activate(RenderWorld& world) override {
        // Switch to wireframe fill mode (D3D12: D3D12_FILL_MODE_WIREFRAME)
        // Render all edges in a single color (e.g., white or per-material)
    }
};
```

### Normal Debug

Visualize surface normals as RGB color (R=X, G=Y, B=Z).

```cpp
class NormalDebugMode : public IViewportMode {
public:
    StringView GetName() const override { return "Normals"; }

    void Activate(RenderWorld& world) override {
        // Replace output with (normal.x * 0.5 + 0.5, normal.y * 0.5 + 0.5, normal.z * 0.5 + 0.5)
        // Shows surface curvature and smoothing group issues
    }
};
```

### Channel Visualization

Isolate individual material properties:

- **BaseColor Only** — pure diffuse, no metallic/roughness effects
- **Metallic** — intensity map (white=metallic, black=dielectric)
- **Roughness** — surface roughness (white=rough, black=mirror)
- **Ambient Occlusion (AO)** — occlusion map visualization
- **Emissive** — self-illumination channel
- **Depth** — linearized depth (black=near, white=far)

```cpp
class BaseColorOnlyMode : public IViewportMode {
public:
    StringView GetName() const override { return "BaseColor Only"; }

    void Activate(RenderWorld& world) override {
        // Output: baseColor with F0=0.04, roughness=1.0
        // Disables metallic/roughness/emissive influence
    }
};

class MetallicMode : public IViewportMode {
public:
    StringView GetName() const override { return "Metallic"; }

    void Activate(RenderWorld& world) override {
        // Output: (metallic, metallic, metallic)
        // Inspect metallic distribution across surface
    }
};

class RoughnessMode : public IViewportMode {
public:
    StringView GetName() const override { return "Roughness"; }

    void Activate(RenderWorld& world) override {
        // Output: (roughness, roughness, roughness)
        // Verify smooth/rough material balance
    }
};
```

## Debug Visualization Modes

These modes help identify rendering performance problems:

### Overdraw

Render mesh overdraw count (how many times each pixel is covered). High overdraw indicates:
- Heavy transparency overdraw
- Poor occlusion culling
- Overlapping geometry

```cpp
class OverdrawMode : public IViewportMode {
public:
    StringView GetName() const override { return "Overdraw"; }

    void Activate(RenderWorld& world) override {
        // Each pixel accumulates color additively
        // Black = 1x coverage (ideal)
        // Red = 2x coverage (acceptable)
        // Yellow/White = 3x+ (problematic)

        // Shader output: baseColor = (overdraw_count / max_overdraw) * heatmap_color;
    }
};
```

### Shader Complexity

Estimate relative shader cost per pixel:

- **Green** = simple (few texture lookups, math ops)
- **Yellow** = moderate complexity
- **Red** = expensive (many samples, complex calculations)

```cpp
class ShaderComplexityMode : public IViewportMode {
public:
    StringView GetName() const override { return "Shader Complexity"; }

    void Activate(RenderWorld& world) override {
        // Render each material with color proportional to instruction count
        // Estimates from shader reflection metadata (UniformBufferSize, texture sample count)

        // Heuristic: baseColor = ComputeComplexityColor(texture_count, math_ops);
    }
};
```

### Light Complexity

Show lighting cost per pixel — how many lights affect each surface.

```cpp
class LightComplexityMode : public IViewportMode {
public:
    StringView GetName() const override { return "Light Complexity"; }

    void Activate(RenderWorld& world) override {
        // Render with color based on active light count
        // Green (0 lights) → Yellow (4-8) → Red (16+ lights)
        // Helps identify over-lit areas where culling can help
    }
};
```

### Light Only

Render only light contribution (no materials), useful for lighting validation.

```cpp
class LightOnlyMode : public IViewportMode {
public:
    StringView GetName() const override { return "Light Only"; }

    void Activate(RenderWorld& world) override {
        // Skip material shading, output only: lighting_contribution
        // Ignore baseColor, metallic, roughness, emissive
        // Lets you see pure light without material confusion
    }
};
```

### Reflection Only

Isolate specular reflections (environment or planar mirrors).

```cpp
class ReflectionOnlyMode : public IViewportMode {
public:
    StringView GetName() const override { return "Reflection Only"; }

    void Activate(RenderWorld& world) override {
        // Output only: specular_reflection (IBL + planar reflections)
        // Skip diffuse, direct lighting, and base color
        // Validate reflection capture placement and quality
    }
};
```

## Buffer Visualization

View individual GBuffer channels directly:

| Mode | Channel | Purpose |
|------|---------|---------|
| **Albedo** | RT0 | Diffuse color (sRGB) |
| **Normal** | RT1 | Surface normal (RGB encoded) |
| **Metallic/Roughness** | RT2 | R=Metallic, G=Roughness |
| **Depth** | DS | Depth buffer (linearized) |
| **Motion Vectors** | RT3 | Velocity for TAA/motion blur |
| **Ambient Occlusion** | RT4 | AO map (if used) |
| **Emissive** | RT5 | Self-illumination |

```cpp
class GBufferVisualizationMode : public IViewportMode {
private:
    enum Channel {
        kAlbedo,
        kNormal,
        kMetallicRoughness,
        kDepth,
        kMotionVectors,
        kAO,
        kEmissive,
    } channel_;

public:
    GBufferVisualizationMode(Channel ch) : channel_(ch) {}

    StringView GetName() const override {
        switch (channel_) {
            case kAlbedo: return "GBuffer: Albedo";
            case kNormal: return "GBuffer: Normal";
            case kMetallicRoughness: return "GBuffer: Metallic/Roughness";
            case kDepth: return "GBuffer: Depth";
            case kMotionVectors: return "GBuffer: Motion Vectors";
            case kAO: return "GBuffer: AO";
            case kEmissive: return "GBuffer: Emissive";
        }
        return "Unknown";
    }

    void Activate(RenderWorld& world) override {
        // Copy selected GBuffer RT to backbuffer
        // For depth: linearize with (depth - near) / (far - near)
        // For normals: decode from RGB representation
    }
};
```

## Collision & Physics Visualization

Debug physics and collision geometry:

```cpp
class CollisionVisualizationMode : public IViewportMode {
public:
    StringView GetName() const override { return "Collisions"; }

    void Activate(RenderWorld& world) override {
        // Render all collision shapes as wireframe overlays:
        // - Green = Static collider (fixed)
        // - Blue = Dynamic rigidbody (physics-simulated)
        // - Yellow = Kinematic (script-controlled)
        // - Magenta = Trigger (non-physical detection zone)
    }

    void SetShowTriggers(bool show) { show_triggers_ = show; }
    void SetShowPhysBounds(bool show) { show_phys_bounds_ = show; }

private:
    bool show_triggers_ = true;
    bool show_phys_bounds_ = false;
};
```

## Navigation Visualization

Inspect navmesh and pathfinding data:

```cpp
class NavigationVisualizationMode : public IViewportMode {
public:
    StringView GetName() const override { return "Navigation"; }

    void Activate(RenderWorld& world) override {
        // Render NavMesh as semi-transparent overlay (green)
        // Show obstacles in red, restricted areas in orange
        // Optional: draw sample pathfinding traces in yellow
    }

    void SetShowNavMesh(bool show) { show_navmesh_ = show; }
    void SetShowObstacles(bool show) { show_obstacles_ = show; }
    void SetShowRestrictedAreas(bool show) { show_restricted_ = show; }
    void SetTestPath(Vec3 from, Vec3 to) { test_path_start_ = from; test_path_end_ = to; }

private:
    bool show_navmesh_ = true;
    bool show_obstacles_ = true;
    bool show_restricted_ = false;
    Vec3 test_path_start_, test_path_end_;
};
```

## Grid & Reference Visualization

```cpp
class GridMode : public IViewportMode {
public:
    StringView GetName() const override { return "Grid"; }

    void Activate(RenderWorld& world) override {
        // Render ground plane grid
        // Grid major/minor lines at configurable sizes
        // Origin axes (red=X, green=Y, blue=Z)
    }

    void SetGridSize(float size) { grid_size_ = size; }
    void SetGridDensity(int cells_per_unit) { cells_per_unit_ = cells_per_unit; }
    void SetShowAxes(bool show) { show_axes_ = show; }

private:
    float grid_size_ = 1.0f;
    int cells_per_unit_ = 10;
    bool show_axes_ = true;
};
```

## Camera Presets

Quick-switch between standard orthographic & perspective views:

| Preset | View | Camera Position |
|--------|------|-----------------|
| **Perspective** | 3D (default) | User-controlled |
| **Top** | Orthographic from +Z | Looking straight down |
| **Bottom** | Orthographic from -Z | Looking straight up |
| **Front** | Orthographic from -Y | Looking along Y axis |
| **Back** | Orthographic from +Y | Looking backwards |
| **Left** | Orthographic from -X | Looking along X axis |
| **Right** | Orthographic from +X | Looking along -X axis |

```cpp
/// Switch to preset camera orientation.
void EditorViewport::SetCameraPreset(CameraPreset preset) {
    Mat4 view_matrix;
    switch (preset) {
        case CameraPreset::Top:
            view_matrix = Mat4::LookAt(camera_.position + Vec3(0, 100, 0),
                                       camera_.position, Vec3(0, -1, 0));
            projection_ = Mat4::Orthographic(-100, 100, -100, 100, 0.1f, 1000.f);
            break;

        case CameraPreset::Front:
            view_matrix = Mat4::LookAt(camera_.position + Vec3(0, 0, 100),
                                       camera_.position, Vec3(0, 1, 0));
            projection_ = Mat4::Orthographic(-100, 100, -100, 100, 0.1f, 1000.f);
            break;

        // ... other presets
    }

    camera_.view = view_matrix;
    camera_.projection = projection_;
}
```

## Viewport Splitting

Show 2x2 or 2x1 layout with different modes in each pane:

```cpp
class SplitViewportMode {
public:
    enum SplitLayout {
        kNone,        // Single viewport
        k2x1,         // 2 columns, 1 row
        k1x2,         // 1 column, 2 rows
        k2x2,         // 2x2 grid
    };

    void SetLayout(SplitLayout layout) {
        layout_ = layout;
        // Resize each pane accordingly
    }

    /// Each pane can have independent mode
    void SetPaneMode(int pane_index, IViewportMode* mode) {
        panes_[pane_index].mode = mode;
    }

    /// Common use: top-left = Lit, others = Wireframe, Normals, Metallic
    void SetCommonPresets(CommonPresetType type) {
        if (type == CommonPresetType::kMaterialPreview) {
            SetPaneMode(0, lit_mode_.get());
            SetPaneMode(1, wireframe_mode_.get());
            SetPaneMode(2, normal_mode_.get());
            SetPaneMode(3, basecolor_mode_.get());
        }
    }

private:
    struct Pane {
        std::unique_ptr<IViewportMode> mode;
        Rect viewport_rect;
    };
    SplitLayout layout_;
    std::array<Pane, 4> panes_;
};
```

## Selection Highlighting

Different highlight modes when objects are selected:

```cpp
class SelectionHighlight {
public:
    enum Mode {
        kOutline,          // Render object outline
        kTint,             // Tint object with highlight color
        kWireframeOverlay, // Semi-transparent wireframe overlay
    };

    void SetMode(Mode mode) { highlight_mode_ = mode; }
    void SetHighlightColor(Color color) { highlight_color_ = color; }
    void SetOutlineThickness(float thickness) { outline_thickness_ = thickness; }

    /// When rendering selected entities, apply appropriate highlight
    void ApplyHighlight(RenderPass& pass, Entity selected_entity) {
        switch (highlight_mode_) {
            case kOutline: {
                // Render silhouette pass, expand by outline_thickness_
                RenderSilhouette(pass, selected_entity, highlight_color_);
                break;
            }
            case kTint: {
                // Modulate entity color with highlight_color_
                float tint_blend = 0.3f;  // 30% blend
                pass.shader_constants.tint_color =
                    Lerp(Color(1,1,1,1), highlight_color_, tint_blend);
                break;
            }
            case kWireframeOverlay: {
                // Render wireframe on top of shaded geometry
                RenderWireframeOverlay(pass, selected_entity);
                break;
            }
        }
    }

private:
    Mode highlight_mode_ = kOutline;
    Color highlight_color_ = Color(0.3f, 0.8f, 1.0f, 1.0f);  // Cyan
    float outline_thickness_ = 2.0f;
};
```

## Statistics Overlay

Display real-time performance metrics:

```cpp
class StatsOverlay {
public:
    struct Stats {
        float fps;
        float frame_time_ms;
        uint32_t draw_calls;
        uint32_t triangle_count;
        uint64_t vram_used_mb;
        uint32_t entity_count;
        uint32_t light_count;
        uint32_t shadow_casting_lights;
    };

    void SetShowStats(bool show) { show_stats_ = show; }
    void SetShowDetailedStats(bool show) { show_detailed_ = show; }

    void Render(RenderPass& pass) {
        if (!show_stats_) return;

        Stats stats = GatherStats();

        // Render simple overlay text
        std::string text = fmt::format(
            "FPS: {:.1f}\n"
            "Frame: {:.2f} ms\n"
            "DrawCalls: {}\n"
            "Triangles: {}\n"
            "VRAM: {} MB\n"
            "Entities: {}",
            stats.fps, stats.frame_time_ms, stats.draw_calls,
            stats.triangle_count, stats.vram_used_mb, stats.entity_count
        );

        if (show_detailed_) {
            text += fmt::format(
                "\nLights: {} ({} shadow-casting)\n",
                stats.light_count, stats.shadow_casting_lights
            );
        }

        RenderTextOverlay(pass, text, position_, font_size_);
    }

private:
    Stats GatherStats() {
        // Query GPU, render stats, ECS world
        Stats stats{};
        stats.fps = 1.0f / frame_time_delta_;
        stats.frame_time_ms = frame_time_delta_ * 1000.0f;
        stats.draw_calls = render_pass_.draw_calls_count_;
        stats.triangle_count = render_pass_.triangle_count_;
        stats.vram_used_mb = gpu_memory_.GetUsedMemoryMB();
        stats.entity_count = scene_.GetEntityCount();
        stats.light_count = scene_.GetLightCount();
        return stats;
    }

    bool show_stats_ = true;
    bool show_detailed_ = false;
    Vec2 position_ = Vec2(10, 10);
    float font_size_ = 14.0f;
};
```

## Interface Definition

All viewport modes derive from a common interface:

```cpp
/// Base class for viewport rendering modes.
class IViewportMode {
public:
    virtual ~IViewportMode() = default;

    /// Human-readable name for UI menu.
    virtual StringView GetName() const = 0;

    /// Called when mode is activated (show in viewport).
    virtual void Activate(RenderWorld& world) = 0;

    /// Called when mode is deactivated (switching to another mode).
    virtual void Deactivate(RenderWorld& world) = 0;

    /// Optional: additional UI controls specific to this mode.
    /// Return JSON schema for TypeScript UI to render.
    virtual std::string GetUISchema() const {
        return "{}";  // No extra controls by default
    }

    /// Optional: handle UI control changes from editor.
    virtual void OnUIControlChanged(StringView control_name, const Variant& new_value) {}
};

/// Manages available viewport modes and current selection.
class ViewportModeManager {
public:
    void RegisterMode(std::unique_ptr<IViewportMode> mode) {
        modes_.emplace_back(std::move(mode));
    }

    void SelectMode(StringView mode_name) {
        if (current_mode_) current_mode_->Deactivate(render_world_);

        current_mode_ = nullptr;
        for (auto& mode : modes_) {
            if (mode->GetName() == mode_name) {
                current_mode_ = mode.get();
                current_mode_->Activate(render_world_);
                break;
            }
        }
    }

    StringView GetCurrentModeName() const {
        return current_mode_ ? current_mode_->GetName() : "None";
    }

    void GetAllModeNames(std::vector<std::string>& out_names) const {
        for (const auto& mode : modes_) {
            out_names.push_back(std::string(mode->GetName()));
        }
    }

private:
    std::vector<std::unique_ptr<IViewportMode>> modes_;
    IViewportMode* current_mode_ = nullptr;
};
```

## TypeScript UI Integration

```typescript
// extension/editor/viewport_modes.ts
class ViewportModeUI {
    constructor(private runtimeBridge: RuntimeBridge) {}

    async updateModeList() {
        const modes = await this.runtimeBridge.call('viewport.get_modes');
        this.renderModeDropdown(modes);
    }

    async selectMode(modeName: string) {
        await this.runtimeBridge.call('viewport.set_mode', { mode: modeName });

        // Fetch UI schema for mode-specific controls
        const schema = await this.runtimeBridge.call('viewport.get_mode_ui_schema');
        this.renderModeControls(schema);
    }

    renderModeDropdown(modes: string[]) {
        const dropdown = document.getElementById('viewport-mode-select');
        dropdown.innerHTML = '';
        for (const mode of modes) {
            const option = document.createElement('option');
            option.textContent = mode;
            option.value = mode;
            dropdown.appendChild(option);
        }
        dropdown.addEventListener('change', (e) => {
            this.selectMode((e.target as HTMLSelectElement).value);
        });
    }

    renderModeControls(schema: any) {
        const container = document.getElementById('viewport-mode-controls');
        container.innerHTML = '';

        // Render controls based on schema (sliders, toggles, color pickers, etc.)
        for (const [name, config] of Object.entries(schema)) {
            const control = this.createControl(name as string, config as any);
            container.appendChild(control);

            control.addEventListener('change', async (e) => {
                await this.runtimeBridge.call('viewport.set_mode_control', {
                    control: name,
                    value: (e.target as any).value,
                });
            });
        }
    }

    private createControl(name: string, config: any): HTMLElement {
        switch (config.type) {
            case 'toggle':
                return this.createToggle(name, config.default);
            case 'slider':
                return this.createSlider(name, config.min, config.max, config.default);
            case 'color':
                return this.createColorPicker(name, config.default);
            default:
                return document.createElement('div');
        }
    }
}
```

## IPC Messages

```cpp
// IPC protocol for viewport mode control
struct ViewportModeMessages {
    // Query available modes
    struct GetModes {
        static constexpr StringView kMethod = "viewport.get_modes";
        // Response: { modes: ["Lit", "Unlit", "Wireframe", ...] }
    };

    // Set active mode
    struct SetMode {
        static constexpr StringView kMethod = "viewport.set_mode";
        std::string mode;  // "Lit", "Wireframe", etc.
    };

    // Get UI schema for current mode
    struct GetModeUISchema {
        static constexpr StringView kMethod = "viewport.get_mode_ui_schema";
        // Response: { "grid_size": { type: "slider", min: 0.1, max: 10.0, default: 1.0 }, ... }
    };

    // Update mode-specific control
    struct SetModeControl {
        static constexpr StringView kMethod = "viewport.set_mode_control";
        std::string control;  // "grid_size", "show_collision", etc.
        nlohmann::json value;
    };
};
```

## Comparison

| Feature | UE5 | Godot | Unity | **UzEngine** |
|---|---|---|---|---|
| **Shading modes** | Lit, Unlit, Wireframe, Light Complexity, Quadratic Complexity | Unlit, Shaded, Wireframe | Standard, Wireframe, Texture-only | **Lit, Unlit, Wireframe, Normal, BaseColor, Metallic, Roughness, AO, Emissive** |
| **GBuffer visualization** | Yes (per-channel) | No (Godot uses forward) | No | **Yes (all channels)** |
| **Debug overlays** | Overdraw, Shader Complexity, Light Complexity, Reflection Only | Basic grid/axis | No | **Overdraw, Shader Complexity, Light Complexity, Light Only, Reflection Only** |
| **Physics visualization** | Yes (collision, simulation) | Yes (collision, constraints) | Gizmo-based | **Collision shapes, trigger volumes, physics bounds** |
| **NavMesh visualization** | Yes (with debug drawing) | Yes (navigation mesh overlay) | No | **NavMesh, obstacles, pathfinding test** |
| **Camera presets** | Front/Side/Top | Front/Side/Top | Limited | **Perspective + 6 orthographic sides** |
| **Viewport split** | Yes (2x2) | Yes (2-3 panes) | Limited | **2x1, 1x2, 2x2 with independent modes** |
| **Selection highlight** | Outline, Wireframe overlay | Highlight, Outline | Highlight | **Outline, Tint, Wireframe overlay** |
| **Stats overlay** | FPS, Draw calls, Memory | Basic profiler | Profiler window | **FPS, Frame time, Draw calls, Triangles, VRAM, Entity count, Lights** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Lit, Unlit, Wireframe, Normal modes; GBuffer visualization (Albedo, Normal, Depth); Grid overlay; Camera presets (Perspective, Top, Front); Stats overlay (basic FPS/draw calls) |
| **Phase 3** | Metallic, Roughness, AO, Emissive modes; Collision visualization; NavMesh overlay; Overdraw debug mode; Shader Complexity estimate |
| **Phase 4** | Light Only, Reflection Only, Light Complexity visualization; Viewport splitting (2x2, 2x1); Selection highlight modes (outline, tint, wireframe overlay) |

