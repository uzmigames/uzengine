# Developer Layer — Profiling, Debug Draw, Hot-Reload, and Automation

> **Status**: Planned (Phase 1–4)
> **Source**: `developer/`, core development tools stripped from shipping builds
> **References**: UE5 Developer/ directory and FPrimitiveDrawInterface, Godot TOOLS_ENABLED + EditorPlugin, Tracy profiler, dear ImGui

## Purpose

The Developer Layer contains systems that exist only in development builds (`UZ_DEV_BUILD=1`). These tools enable profiling, debugging, hot-reload, and test automation without bloating shipping binaries. All code is compiled out via preprocessor guards, producing zero overhead in `UZ_SHIPPING` builds.

## Architecture

```
Game Code / Engine / Plugins
        │
        ├── Always available: world, components, physics, scripting
        │
        └── (If UZ_DEV_BUILD) Developer Layer
              ├── developer/profiling/     — CPU/GPU timing, memory tracking
              ├── developer/debug_draw/    — Debug lines, shapes, text overlays
              ├── developer/hot_reload/    — Asset & code hot-reload with dependency tracking
              ├── developer/console/       — CVar system, command input
              ├── developer/automation/    — Headless testing, screenshot capture, benchmarks
              └── developer/imgui/         — Immediate-mode debug UI (optional)

┌─────────────────────────────────────────────────────────────┐
│  UZ_SHIPPING = 0: Development Build                        │
│  ├── UZ_DEV_BUILD = 1 (profiling, hot-reload, debug draw)  │
│  ├── UZ_EDITOR_BUILD = 0 or 1 (VSCode editor panels)       │
│  └── Size: ~50MB                                            │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  UZ_SHIPPING = 1: Shipping Build                           │
│  ├── UZ_DEV_BUILD = 0 (NO profiling, no hot-reload)        │
│  ├── UZ_EDITOR_BUILD = 0 (NO editor panels)                │
│  └── Size: ~15MB                                            │
└─────────────────────────────────────────────────────────────┘
```

## Profiling — CPU, GPU, Memory

CPU profiling via Tracy integration, GPU timing via D3D12 QueryHeap, memory tracking via allocation hooks.

### CPU Profiler (Tracy)

```cpp
/// Mark a scope for profiling. Macros compile to nothing in UZ_SHIPPING builds.
#if UZ_DEV_BUILD
    #include "developer/profiling/profiler.h"
    #define UZ_PROFILE_SCOPE(name) uz::dev::ProfileScope _scope(name)
#else
    #define UZ_PROFILE_SCOPE(name) ((void)0)
#endif

// Usage:
void PhysicsSystem::Update(float dt) {
    UZ_PROFILE_SCOPE("Physics::Update");

    for (auto& body : bodies_) {
        UZ_PROFILE_SCOPE("PhysicsBody::Integrate");
        IntegrateVelocity(body, dt);
    }
}
```

### GPU Profiler

DirectX 12 timestamp queries capture GPU time per render pass.

```cpp
namespace uz::dev {
    /// Record timestamp at GPU command list.
    /// Timestamps are latched 1–2 frames later.
    void RecordGPUTimestamp(const char* name);

    /// Get frame's GPU profiling stats.
    struct GPUProfileFrame {
        std::vector<std::pair<std::string, float>> passes;  // {name, ms}
        float total_ms;
    };
    GPUProfileFrame GetLastGPUFrame();
}

// Usage:
void Renderer::RenderFrame() {
    uz::dev::RecordGPUTimestamp("GBuffer");
    RenderGBuffer(cmd_list);

    uz::dev::RecordGPUTimestamp("Lighting");
    RenderLighting(cmd_list);

    uz::dev::RecordGPUTimestamp("Composite");
    RenderComposite(cmd_list);
}
```

### Memory Profiler

Allocation tracking via custom allocators. Detects leaks and tracks peak usage.

```cpp
namespace uz::dev {
    struct MemoryStats {
        size_t total_allocated;    // bytes currently allocated
        size_t total_freed;        // bytes freed (cumulative)
        size_t peak_allocated;     // max bytes allocated in session
        size_t live_allocations;   // count of active allocations

        std::vector<std::pair<std::string, size_t>> top_allocators;  // by category
    };

    MemoryStats GetMemoryStats();
    bool HasLeaks() const;  // returns true if allocations > freed
}
```

## Debug Draw — Lines, Shapes, Text

Immediate-mode debug rendering API for visualizing game state without UI overhead.

```cpp
namespace uz::dev {
    /// Draw a line from `from` to `to`. Color is RGBA (premultiplied).
    /// `lifetime`: 0 = draw once, >0 = seconds, <0 = persistent (until cleared).
    void DrawLine(Vec3 from, Vec3 to, uint32_t color_rgba = 0xFFFFFFFFu, float lifetime = 0.0f);

    /// Draw a wireframe box (axis-aligned or rotated).
    void DrawBox(const Transform& transform, Vec3 extents, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

    /// Draw a wireframe sphere.
    void DrawSphere(Vec3 center, float radius, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

    /// Draw text in world space (rendered facing camera).
    void DrawText(Vec3 position, StringView text, uint32_t color = 0xFFFFFFFFu, float lifetime = 0.0f);

    /// Draw text in screen space (top-left origin, pixels).
    void DrawScreenText(int x, int y, StringView text, uint32_t color = 0xFFFFFFFFu);

    /// Clear all persistent debug shapes.
    void ClearDebugDraw();
}

// Usage: visualize physics raycast results
uz::RaycastHit hit;
if (physics_world->Raycast(from, direction, hit)) {
    uz::dev::DrawLine(from, hit.position, 0xFF00FF00u);  // green = hit
    uz::dev::DrawSphere(hit.position, 0.2f, 0xFF0000FFu);  // red sphere at impact
} else {
    uz::dev::DrawLine(from, from + direction * 1000, 0xFFFF0000u);  // red = no hit
}
```

**Rendering**: Debug shapes are collected during the frame and rendered in a final pass after all game rendering, using a simple unlit shader. No Z-test — always visible.

## Hot-Reload — Assets and Code

Automatic detection and reloading of changed assets without restarting.

### FileWatcher

Monitors asset directories for changes. Supports glob patterns.

```cpp
namespace uz::dev {
    /// Fired when a file matching the pattern changes on disk.
    /// Called on FileWatcher thread — handler must be thread-safe.
    using FileChangeCallback = std::function<void(StringView path, FileChangeEvent)>;

    enum class FileChangeEvent { Created, Modified, Deleted };

    /// Register a directory and pattern (e.g., "res:/**/*.lua") for watching.
    void WatchAssets(StringView vfs_pattern, FileChangeCallback callback);
}
```

### Dependency Graph

Tracks which assets depend on which, enabling cascade reloads (e.g., shader changes trigger dependent material reloads).

```cpp
namespace uz::dev {
    /// Register a dependency: `dependent` uses `dependency`.
    /// When `dependency` reloads, `dependent` is marked dirty.
    void RegisterDependency(StringView dependent, StringView dependency);

    /// Get all assets that depend on this one (transitive closure).
    std::vector<std::string> GetDependents(StringView asset_path);
}

// Usage: when shader X changes, mark all materials using X as dirty
auto dependents = uz::dev::GetDependents("res://shaders/pbr.hlsl");
for (auto& mat_path : dependents) {
    ResourceSystem::ReloadResource(mat_path);  // queued, not immediate
}
```

### Asset Hot-Reload

Hot-reload supports:
- **Shaders**: Change `.hlsl`, press F5 → shader recompiles, dependent materials update
- **Textures**: Replace `.png` on disk → texture reloads, materials refresh
- **Meshes**: Update `.gltf` → mesh asset reloads, scenes refresh references
- **Lua scripts**: Edit `.lua` → script re-parses, `on_stop()` + `on_start()` called
- **C++ code**: Edit `.cpp` → DLL recompiles, game behavior reloads (editor only)

### C++ Hot-Reload (Editor Only)

```
FileWatcher detects .cpp/.h change
  ↓
Engine pauses main loop, saves world state
  ↓
Unload game.dll
  ↓
Recompile via Zig build
  ↓
Load new game.dll
  ↓
Call OnBeginPlay() on Behavior instances
  ↓
Restore camera, selected entities, viewport state
  ↓
Resume main loop
```

**State preservation**: Editor saves entity transforms, component properties, and scene structure. Component-specific state (e.g., AI variables) is re-initialized via `OnBeginPlay()`.

## Console — Commands and CVars

Quake-style console for runtime tweaking and commands.

### Console API

```cpp
namespace uz::dev {
    /// Register a command callable from console.
    /// Args are whitespace-separated tokens. Return false to print usage.
    using ConsoleCommand = std::function<bool(const std::vector<std::string>& args)>;
    void RegisterCommand(StringView name, StringView usage, ConsoleCommand fn);

    /// Get command history (last N lines).
    std::vector<std::string> GetConsoleHistory(int max_lines = 100);

    /// Find matching commands for autocomplete.
    std::vector<std::string> GetCommandMatches(StringView prefix);

    /// Programmatically execute a console command.
    bool Execute(StringView command_line);
}

// Usage: register a teleport command
uz::dev::RegisterCommand(
    "teleport",
    "teleport <x> <y> <z>  — Teleport player to position",
    [](const auto& args) {
        if (args.size() != 4) return false;  // wrong arg count, print usage
        float x = std::stof(args[1]);
        float y = std::stof(args[2]);
        float z = std::stof(args[3]);
        player->SetPosition({x, y, z});
        return true;
    }
);

// In console: `teleport 10 5 20`
```

### CVar System

CVars are runtime-tweakable variables. Changes apply immediately without recompilation.

```cpp
#if UZ_DEV_BUILD
    #include "developer/console/cvar.h"
    #define UZ_CVAR(type, name, default_value, description) \
        static uz::dev::CVar<type> name(#name, default_value, description)
#else
    #define UZ_CVAR(type, name, default_value, description) \
        static constexpr type name = default_value
#endif

// Usage in code:
UZ_CVAR(float, r_shadowDistance, 100.0f, "Max shadow draw distance");

void RenderShadows() {
    float max_distance = r_shadowDistance.Get();  // reads current value
    // ... render with max_distance
}

// In console: `r_shadowDistance 200` → updates live
```

**CVar Categories**: Prefix denotes subsystem (`r_` = renderer, `p_` = physics, `a_` = audio, `g_` = game).

## Automation and Testing

Framework for automated testing, CI integration, and regression testing.

### Headless Mode

Run game without rendering, for CI/automated testing.

```cpp
int main(int argc, char** argv) {
    bool headless = HasCommandLineArg("-headless");

    uz::EngineConfig config;
    config.headless = headless;  // Skip window creation, RHI device
    uz::Engine engine(config);

    engine.Run();  // Runs at max speed, no vsync, frame rate uncapped
}
```

### Test Framework

```cpp
namespace uz::dev {
    /// Register a test function. Called during test run.
    /// Return false if test fails.
    using TestFn = std::function<bool(World& world)>;
    void RegisterTest(StringView name, TestFn fn);

    /// Run all registered tests, report results.
    struct TestResults {
        int passed, failed;
        std::vector<std::string> failures;
    };
    TestResults RunAllTests();
}

// Usage:
uz::dev::RegisterTest("physics_raycast", [](uz::World& world) {
    uz::RaycastHit hit;
    bool success = world.Physics().Raycast(from, dir, hit);
    return success && (hit.distance > 0);
});

uz::dev::RegisterTest("animation_playback", [](uz::World& world) {
    auto entity = world.Create();
    world.Attach<AnimationComponent>(entity, "content/anim.uzanim");
    world.Tick(1.0f / 60.0f);  // advance 1 frame
    return true;  // manual inspection would check state
});
```

### Screenshot Capture and Regression Testing

```cpp
namespace uz::dev {
    /// Capture backbuffer to PNG file.
    /// Called on render thread via deferred queue.
    void CaptureScreenshot(StringView filename);

    /// Compare two screenshots for per-pixel differences.
    /// Returns tolerance: 0.0 = identical, 1.0 = completely different.
    float CompareScreenshots(StringView golden_path, StringView actual_path);
}

// Usage: capture a screenshot for manual review
uz::dev::CaptureScreenshot("screenshots/golden_frame_001.png");

// Or regression test with threshold
float diff = uz::dev::CompareScreenshots("golden.png", "actual.png");
bool test_passed = (diff < 0.01f);  // allow 1% pixel difference
```

## ImGui Integration (Optional)

Immediate-mode UI for rapid prototyping and debugging. Not intended to replace VSCode editor panels — meant for in-game debug overlays.

```cpp
namespace uz::dev {
    /// Initialize ImGui, bind to viewport.
    void ImGuiInit(Window* viewport);
    void ImGuiShutdown();

    /// Called per frame by main loop.
    void ImGuiNewFrame();
    void ImGuiRender(RHI::CommandList& cmd);
}

// Usage: entity inspector
void OnImGui() {
    if (ImGui::Begin("Entity Inspector")) {
        ImGui::Text("Selected: %s", selected_entity.DebugName());

        // Component list
        if (ImGui::TreeNode("Components")) {
            world.ForEachComponent(selected_entity, [](Component* comp) {
                ImGui::Text("  %s", comp->TypeName());
            });
            ImGui::TreePop();
        }

        ImGui::End();
    }
}
```

## Build Configuration

### Development Build (`UZ_DEV_BUILD=1`)

Includes all developer tools. Compiled with `-O2` (optimized but debuggable).

```cmake
# CMakeLists.txt
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(UZ_DEV_BUILD=1)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(UZ_DEV_BUILD=1)  # still dev-friendly
endif()
```

### Shipping Build (`UZ_SHIPPING=1`)

All developer code stripped via preprocessor. Compiled with `-O3 -DNDEBUG` (maximum optimization).

```cpp
// developer/developer_config.h
#if defined(UZ_SHIPPING)
    #define UZ_DEV_BUILD 0
#else
    #define UZ_DEV_BUILD 1
#endif

// Usage:
#if UZ_DEV_BUILD
    #include "developer/profiling/profiler.h"
#endif
```

## Dependency Rules

```
developer/  → core/, rhi/, renderer/, engine/, platform/  (leaf: can use anything)
engine/     ↛ developer/  (game code never imports developer/)
core/       ↛ developer/  (strict: development is a leaf)
rhi/        ↛ developer/  (gpu layer is agnostic)
```

Invariant: No module outside developer/ may include developer headers without a `#if UZ_DEV_BUILD` guard. This ensures developer code is optional.

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Profiler | Unreal Insights (Tracy-based) | Built-in (limited) | Profiler window | **Tracy integration + GPU timelines** |
| Debug draw | FPrimitiveDrawInterface lines/shapes | DebugDraw3D | Gizmos/Debug.Draw | **DrawLine/DrawBox/DrawSphere** |
| Hot-reload | Blueprint instant, C++ compiled-out | GDScript instant, C# domain reload | C# domain reload | **Lua instant, C++ DLL (editor)** |
| Automation | Automation Framework (CI-friendly) | None built-in | Unity Test Framework | **Headless mode + RegisterTest** |
| Console | Slate console (Visual) | Built-in (limited) | None | **CVar system + RegisterCommand** |
| Stripping | Developer/ compiled out in shipping | Stripped via TOOLS_ENABLED | Removed by IL2CPP | **Preprocessor guards** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 1** | `UZ_PROFILE_SCOPE` CPU profiling, GPU timestamps, memory stats |
| **Phase 2** | Debug draw (lines, boxes, spheres, text), CVar system, console basic |
| **Phase 3** | Hot-reload (assets + Lua), FileWatcher, dependency graph |
| **Phase 4** | Headless mode, test automation, screenshot capture, ImGui overlay |
| **Phase 5** | Advanced profiling (per-system breakdown, frame graphs), CVar persistence |
