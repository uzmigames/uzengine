<!-- doc-meta
Title: VFX Parameter UI Quick Start
Path: docs/guides/vfx-parameter-ui-quickstart.md
Type: Implementation Guide
Tags: vfx, ui, parameters, imgui, viewport-editor
Status: Complete
Date: 2026-03-30
-->

# VFX Parameter UI Quick Start — ImGui Implementation Checklist

Quick reference for implementing VFX parameter editing UI in UzEngine viewport. Based on Niagara's design patterns, simplified for ImGui.

---

## Part 1: Parameter Organization

### Step 1: Define Parameter Groups

Group your VFX parameters into logical categories. Niagara uses these 6 main groups:

```cpp
enum class VfxParameterCategory {
    Spawn,        // SpawnRate, SpreadAngle, SpawnShape
    Particle,     // Lifetime, InitialVelocity, InitialSize, InitialColor
    Physics,      // Gravity, Drag, VortexForce, WindForce
    Rendering,    // BlendMode, Material, SizeOverLife, ColorOverLife, Opacity
    Simulation,   // bLocalSpace, SimulationMode (CPU/GPU), MaxParticles
    Advanced      // RandomSeed, Determinism, AllocationMode, BoundsMode
};
```

### Step 2: Create Parameter Struct

```cpp
struct VfxEmitterParameters {
    // ─── Spawn Category ───
    float spawn_rate = 10.0f;              // particles per second
    float spawn_spread_angle = 45.0f;      // degrees, [0-360]
    uz::Vec3 spawn_offset = {0, 0, 0};     // offset from origin
    
    // ─── Particle Category ───
    float lifetime_min = 1.0f;             // seconds, [0.1-100]
    float lifetime_max = 2.0f;             // seconds, [0.1-100]
    uz::Vec3 velocity_initial = {0, 0, 0};
    float size_initial = 1.0f;             // [0.01-10]
    uz::Vec4 color_initial = {1,1,1,1};   // RGBA linear
    
    // ─── Physics Category ───
    uz::Vec3 gravity = {0, 0, -980.0f};    // cm/s², Earth default
    float drag = 0.1f;                     // [0-1], 0=no drag
    float vortex_strength = 0.0f;          // [0-100]
    uz::Vec3 wind_velocity = {0, 0, 0};    // cm/s
    
    // ─── Rendering Category ───
    uz::BlendMode blend_mode = uz::BlendMode::Translucent;
    float size_over_life_scale = 1.0f;     // size multiplier at lifetime end
    uz::Vec4 color_over_life_end = {1,1,1,0};  // color at end of life
    
    // ─── Simulation Category ───
    bool local_space = false;
    uint32_t max_particles = 10000;
    bool use_determinism = false;
    int32_t determinism_seed = 0;          // if use_determinism
};
```

### Step 3: Group Parameters by Category in UI

```cpp
struct VfxUIState {
    VfxParameterCategory active_category = VfxParameterCategory::Spawn;
    std::unordered_map<std::string, bool> category_expanded;
    std::unordered_map<std::string, float> param_hover_values;  // for tooltips
};
```

---

## Part 2: ImGui Widget Mapping

### Spawn Parameters

```cpp
void draw_spawn_params(VfxEmitterParameters& params, VfxUIState& ui) {
    ImGui::SeparatorText("SPAWN");
    
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Spawn Rate", &params.spawn_rate, 0, 500, "%.1f px/s");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Particles spawned per second");
    }
    
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Spread Angle", &params.spawn_spread_angle, 0, 360, "%.0f°");
    
    ImGui::SetNextItemWidth(180);
    ImGui::DragFloat3("Spawn Offset", params.spawn_offset.data, 0.1f, -1000, 1000);
}
```

### Particle Parameters

```cpp
void draw_particle_params(VfxEmitterParameters& params, VfxUIState& ui) {
    ImGui::SeparatorText("PARTICLE");
    
    // Lifetime range slider
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloatRange("Lifetime (min/max)", 
        &params.lifetime_min, 
        &params.lifetime_max, 
        0.1f, 100.0f, 
        "%.2f s");
    
    // Initial velocity
    ImGui::SetNextItemWidth(180);
    ImGui::DragFloat3("Initial Velocity", params.velocity_initial.data, 10.0f, -10000, 10000);
    
    // Initial size
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Initial Size", &params.size_initial, 0.01f, 10.0f);
    
    // Initial color with alpha
    ImGui::SetNextItemWidth(180);
    ImGui::ColorEdit4("Initial Color", params.color_initial.data, ImGuiColorEditFlags_NoInputs);
}
```

### Physics Parameters

```cpp
void draw_physics_params(VfxEmitterParameters& params, VfxUIState& ui) {
    ImGui::SeparatorText("PHYSICS");
    
    ImGui::SetNextItemWidth(180);
    ImGui::DragFloat3("Gravity", params.gravity.data, 10.0f, -5000, 5000, "%.0f");
    ImGui::SameLine();
    if (ImGui::Button("Earth##gravity")) params.gravity = {0, 0, -980};
    if (ImGui::Button("Low G##gravity")) params.gravity = {0, 0, -160};
    
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Drag", &params.drag, 0, 1, "%.2f");
    
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Vortex Force", &params.vortex_strength, 0, 100);
    
    ImGui::SetNextItemWidth(180);
    ImGui::DragFloat3("Wind Velocity", params.wind_velocity.data, 10.0f, -1000, 1000);
}
```

### Rendering Parameters

```cpp
void draw_rendering_params(VfxEmitterParameters& params, VfxUIState& ui) {
    ImGui::SeparatorText("RENDERING");
    
    // Blend mode enum
    const char* blend_modes[] = { "Opaque", "Translucent", "Additive", "Modulate" };
    int blend_idx = static_cast<int>(params.blend_mode);
    ImGui::SetNextItemWidth(180);
    if (ImGui::Combo("Blend Mode", &blend_idx, blend_modes, IM_ARRAYSIZE(blend_modes))) {
        params.blend_mode = static_cast<uz::BlendMode>(blend_idx);
    }
    
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Size Over Life", &params.size_over_life_scale, 0, 5, "%.2f x");
    
    ImGui::SetNextItemWidth(180);
    ImGui::ColorEdit4("Color at End", params.color_over_life_end.data, ImGuiColorEditFlags_NoInputs);
}
```

### Simulation Parameters

```cpp
void draw_simulation_params(VfxEmitterParameters& params, VfxUIState& ui) {
    ImGui::SeparatorText("SIMULATION");
    
    ImGui::Checkbox("Local Space", &params.local_space);
    ImGui::SameLine();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Particles move relative to emitter origin");
    }
    
    ImGui::SetNextItemWidth(180);
    ImGui::DragInt("Max Particles", (int*)&params.max_particles, 100, 1, 100000);
    
    ImGui::Checkbox("Deterministic RNG", &params.use_determinism);
    if (params.use_determinism) {
        ImGui::SetNextItemWidth(180);
        ImGui::DragInt("RNG Seed", &params.determinism_seed, 1, 0, INT_MAX);
    }
}
```

---

## Part 3: Parameter Value Constraints

### Define Min/Max for Each Parameter

```cpp
struct VfxParameterConstraints {
    // Spawn
    static constexpr float SpawnRate_Min = 0.1f;
    static constexpr float SpawnRate_Max = 5000.0f;
    
    // Particle
    static constexpr float Lifetime_Min = 0.01f;
    static constexpr float Lifetime_Max = 300.0f;
    static constexpr float InitialSize_Min = 0.01f;
    static constexpr float InitialSize_Max = 100.0f;
    
    // Physics
    static constexpr float Gravity_Min = -10000.0f;
    static constexpr float Gravity_Max = 10000.0f;
    static constexpr float Drag_Min = 0.0f;
    static constexpr float Drag_Max = 1.0f;
    
    static void clamp_all(VfxEmitterParameters& params) {
        params.spawn_rate = uz::clamp(params.spawn_rate, SpawnRate_Min, SpawnRate_Max);
        params.lifetime_min = uz::clamp(params.lifetime_min, Lifetime_Min, Lifetime_Max);
        params.lifetime_max = uz::clamp(params.lifetime_max, Lifetime_Min, Lifetime_Max);
        params.size_initial = uz::clamp(params.size_initial, InitialSize_Min, InitialSize_Max);
        // ... clamp all others
    }
};
```

---

## Part 4: Real-Time Update Integration

### Detect Parameter Changes

```cpp
struct VfxEmitterState {
    VfxEmitterParameters current_params;
    VfxEmitterParameters prev_params;
    bool needs_restart = false;
    
    void update_from_ui(const VfxEmitterParameters& new_params) {
        prev_params = current_params;
        current_params = new_params;
        
        // Determine if emitter should restart
        needs_restart = 
            (current_params.spawn_rate != prev_params.spawn_rate) ||
            (current_params.lifetime_min != prev_params.lifetime_min) ||
            (current_params.lifetime_max != prev_params.lifetime_max) ||
            (current_params.blend_mode != prev_params.blend_mode) ||
            (current_params.max_particles != prev_params.max_particles);
    }
    
    void tick(float delta_time) {
        if (needs_restart) {
            reset_particles();
            needs_restart = false;
        }
        
        // Update constant buffer with current params
        update_cb_from_params(current_params);
        
        // Simulate particles
        simulate(delta_time);
        
        // Render
        render();
    }
};
```

### Categorize Restart vs Live Update

```cpp
enum class ParameterUpdateType {
    LiveUpdate,      // Gravity, drag, wind — update CB, don't kill particles
    Restart,         // SpawnRate, lifetime, size, blend — kill & respawn
    StopAndRestart   // BlendMode, material — also invalidate render state
};

constexpr ParameterUpdateType get_update_type(const char* param_name) {
    if (std::string(param_name) == "gravity" || std::string(param_name) == "drag" || std::string(param_name) == "wind_velocity") {
        return ParameterUpdateType::LiveUpdate;
    }
    if (std::string(param_name) == "blend_mode") {
        return ParameterUpdateType::StopAndRestart;
    }
    return ParameterUpdateType::Restart;
}
```

---

## Part 5: UI State Management

### Main UI Loop

```cpp
class VfxParameterEditorUI {
private:
    VfxEmitterParameters display_params;  // What's shown in UI
    VfxEmitterParameters committed_params;  // What's applied to emitter
    VfxUIState ui_state;
    
public:
    void draw_frame() {
        ImGui::SetNextWindowSize({400, 600}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos({20, 20}, ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("VFX Parameters", nullptr, ImGuiWindowFlags_NoMove)) {
            draw_category_tabs();
            
            switch (ui_state.active_category) {
                case VfxParameterCategory::Spawn:
                    draw_spawn_params(display_params, ui_state);
                    break;
                case VfxParameterCategory::Particle:
                    draw_particle_params(display_params, ui_state);
                    break;
                case VfxParameterCategory::Physics:
                    draw_physics_params(display_params, ui_state);
                    break;
                case VfxParameterCategory::Rendering:
                    draw_rendering_params(display_params, ui_state);
                    break;
                case VfxParameterCategory::Simulation:
                    draw_simulation_params(display_params, ui_state);
                    break;
                default: break;
            }
            
            ImGui::Spacing();
            draw_action_buttons();
        }
        ImGui::End();
    }
    
private:
    void draw_category_tabs() {
        const char* category_names[] = {
            "Spawn", "Particle", "Physics", "Rendering", "Simulation", "Advanced"
        };
        
        for (int i = 0; i < IM_ARRAYSIZE(category_names); i++) {
            if (ImGui::Button(category_names[i], ImVec2(60, 0))) {
                ui_state.active_category = static_cast<VfxParameterCategory>(i);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    
    void draw_action_buttons() {
        if (ImGui::Button("Reset to Defaults", ImVec2(150, 0))) {
            display_params = VfxEmitterParameters();  // Default constructor
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Restart Effect", ImVec2(150, 0))) {
            commit_and_restart();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Apply", ImVec2(100, 0))) {
            commit_parameters();
        }
    }
    
    void commit_parameters() {
        VfxParameterConstraints::clamp_all(display_params);
        committed_params = display_params;
        // Notify emitter of changes
    }
    
    void commit_and_restart() {
        commit_parameters();
        // Force restart
    }
};
```

---

## Part 6: Keyboard Shortcuts for Quick Iteration

```cpp
void draw_keyboard_help() {
    ImGui::Text("Keyboard Shortcuts:");
    ImGui::BulletText("Space: Pause/resume effect");
    ImGui::BulletText("R: Restart effect");
    ImGui::BulletText("D: Reset to defaults");
    ImGui::BulletText("1-6: Switch parameter category");
    ImGui::BulletText("Arrow up/down: Increase/decrease focused slider");
    ImGui::BulletText("Ctrl+Z: Undo");
    ImGui::BulletText("Ctrl+Shift+Z: Redo");
}

void handle_keyboard_shortcuts(VfxEmitterState& emitter) {
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        emitter.toggle_paused();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        emitter.restart();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_D)) {
        // Reset to defaults
    }
    if (ImGui::IsKeyPressed(ImGuiKey_1)) { /* ... */ }
    // ... etc
}
```

---

## Part 7: Preset System

### Save/Load Presets

```cpp
struct VfxPreset {
    std::string name;
    VfxEmitterParameters params;
};

class VfxPresetManager {
private:
    std::vector<VfxPreset> presets;
    
public:
    void save_preset(const std::string& name, const VfxEmitterParameters& params) {
        auto it = std::find_if(presets.begin(), presets.end(),
            [&](const VfxPreset& p) { return p.name == name; });
        
        if (it != presets.end()) {
            it->params = params;  // overwrite
        } else {
            presets.push_back({name, params});
        }
    }
    
    bool load_preset(const std::string& name, VfxEmitterParameters& out_params) {
        auto it = std::find_if(presets.begin(), presets.end(),
            [&](const VfxPreset& p) { return p.name == name; });
        
        if (it != presets.end()) {
            out_params = it->params;
            return true;
        }
        return false;
    }
    
    void draw_ui() {
        ImGui::SeparatorText("PRESETS");
        
        static char preset_name[64] = "";
        ImGui::InputText("Preset Name", preset_name, sizeof(preset_name));
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            // save_preset(preset_name, current_params);
        }
        
        ImGui::ListBoxHeader("Presets", ImVec2(200, 150));
        for (const auto& preset : presets) {
            if (ImGui::Selectable(preset.name.c_str())) {
                // load_preset(preset.name, display_params);
            }
        }
        ImGui::ListBoxFooter();
    }
};
```

---

## Part 8: Troubleshooting Checklist

| Issue | Cause | Fix |
|-------|-------|-----|
| Parameters don't apply | Not calling `emitter->apply_params()` | Call after ImGui change detected |
| Emitter doesn't restart | Missing `needs_restart = true` | Check condition logic in `update_from_ui()` |
| Sliders feel slow | UIMin/UIMax too wide | Adjust slider range, allow keyboard override |
| Can't undo changes | Not tracking undo/redo | Implement transaction system or store prev_params |
| GPU buffer out of sync | Updating CB on wrong thread | Always update in render thread (ENQUEUE_RENDER_COMMAND) |
| Effect looks wrong | Clamping values incorrectly | Verify min/max match reference (Niagara) |

---

## Summary

**Minimal viable VFX parameter UI:**

1. Define ~20 core parameters (spawn, lifetime, color, size, gravity, blend)
2. Create ImGui sliders/color pickers for each
3. Track `needs_restart` boolean based on parameter changes
4. Update constant buffer + restart emitter on next tick
5. Add preset save/load for quick iteration

**Advanced features (optional):**
- Parameter curves (size/color over lifetime)
- Nested parameter groups (emitter → module parameters)
- Data binding UI (link parameter to texture data)
- Parameter constraints validation
- Undo/redo stack

For a quick implementation, focus on part 1-4 above. Parts 5-8 are polish.

---

**See Also:**
- `docs/analysis/niagara-editor-ui-patterns.md` — Full Niagara architecture analysis
- `docs/guides/vfx-rendering-quickstart.md` (future) — Rendering implementation details
