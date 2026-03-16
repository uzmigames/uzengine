<!-- doc-meta
title: Unity Abstraction → UzEngine Roadmap
draft: false
type: implementation-guide
category: application-architecture
priority: P1
version: 1.0
created: 2026-03-14
tags:
  - unity
  - application-architecture
  - playerloop
  - render-pipeline
  - implementation-tasks
  - uzengine
-->

# From Unity Abstraction to UzEngine Implementation

This document translates findings from `unity-application-abstraction.md` into **concrete actionable tasks** for UzEngine.

## Current State (2026-03-14)

### What UzEngine Already Has ✓

1. **Subsystem Architecture** — `ISubsystem` with `InitPhase` and `TickGroup`
   - Location: `runtime/engine/app/subsystem.h`
   - Status: **Perfect match** to Unity's PlayerLoop pattern
   - No changes needed

2. **DemoApplication Abstraction** — Eliminates GPU boilerplate for demos
   - Location: `runtime/demos/common/demo_application.h`
   - Status: **Excellent pattern**, use as template for game code
   - Could be documented better

3. **Time Management** — Fixed timestep accumulation + time scale
   - Location: `runtime/engine/app/time_manager.h`
   - Status: **Correct**, matches Unity
   - No changes needed

4. **Render Path Selection** — Forward vs Deferred toggle (enum-based)
   - Location: `runtime/engine/renderer/scene_renderer.h`
   - Status: **Works but not pluggable** — uses if-statements, not polymorphism
   - **Needs upgrade** to `IRenderPipeline` interface

### What UzEngine Lacks ✗

1. **ApplicationSettings Singleton** — No unified settings API
   - Game code cannot call `uz::ApplicationSettings::Get().SetTargetFrameRate(60)`
   - Currently scattered: shader params in `constant_buffers.h`, lighting in `scene_lighting.h`, etc.
   - **Priority**: P1 — blocks Lua API parity

2. **Pluggable Render Pipeline** — Only enum-based selection
   - Cannot swap pipelines at runtime without recompilation
   - No true polymorphism (`IRenderPipeline` interface)
   - **Priority**: P1 — blocks forward/deferred toggle + custom pipelines

3. **Async Scene Loading** — Synchronous or partial async
   - No `AsyncSceneLoad` struct with progress tracking
   - No `allowActivation` deferral (like Unity)
   - **Priority**: P2 — needed for loading screens

4. **Lua API Parity** — Settings not exposed to Lua
   - `uz.app.set_target_frame_rate()` doesn't exist
   - `uz.app.set_shadow_resolution()` doesn't exist
   - **Priority**: P1 — scripting must have feature parity

---

## Implementation Roadmap

### Phase 1: ApplicationSettings Singleton (P1, ~2-3 days)

**Goal**: Unified settings API for all engine configuration.

#### Task 1.1: Create ApplicationSettings class

**File**: `runtime/engine/app/application_settings.h`

```cpp
namespace uz {

class ApplicationSettings {
public:
    static ApplicationSettings& Get();

    // ── Frame Rate ──────────────────────────────────────────────
    void SetTargetFrameRate(int fps);  // -1 = unlimited
    int GetTargetFrameRate() const;

    // ── Rendering Quality ───────────────────────────────────────
    void SetShadowResolution(uint32_t res);
    uint32_t GetShadowResolution() const;

    void SetShadowAtlasSize(uint32_t size);
    uint32_t GetShadowAtlasSize() const;

    void SetAntiAliasing(uint32_t msaaSamples);
    uint32_t GetAntiAliasing() const;

    void SetMaxDirectionalLights(uint32_t count);
    uint32_t GetMaxDirectionalLights() const;

    void SetMaxPointLights(uint32_t count);
    uint32_t GetMaxPointLights() const;

    // ── Environment Lighting ────────────────────────────────────
    void SetAmbientLight(const Color& color);
    const Color& GetAmbientLight() const;

    void SetAmbientIntensity(float intensity);
    float GetAmbientIntensity() const;

    void SetSkyboxMaterial(Handle<Material> mat);
    Handle<Material> GetSkyboxMaterial() const;

    // ── VSync / Frame Limiting ──────────────────────────────────
    void SetVSyncEnabled(bool enabled);
    bool IsVSyncEnabled() const;

    void SetVSyncCount(int count);  // 0 = off, 1 = 60Hz, 2 = 30Hz (if 60Hz display)
    int GetVSyncCount() const;

    // ── Render Pipeline (pluggable) ──────────────────────────
    void SetActivePipeline(IRenderPipeline* pipeline);
    IRenderPipeline* GetActivePipeline() const;

private:
    ApplicationSettings() = default;

    int targetFrameRate_ = -1;
    uint32_t shadowResolution_ = 2048;
    uint32_t shadowAtlasSize_ = 4096;
    uint32_t antiAliasingLevel_ = 1;
    uint32_t maxDirectionalLights_ = 4;
    uint32_t maxPointLights_ = 256;

    Color ambientLight_ = Color{1, 1, 1, 1};
    float ambientIntensity_ = 0.3f;
    Handle<Material> skyboxMaterial_;

    bool vsyncEnabled_ = false;
    int vsyncCount_ = 1;

    IRenderPipeline* activePipeline_ = nullptr;
};

} // namespace uz
```

**Dependencies**:
- `runtime/engine/renderer/core/render_pipeline.h` (forward declare `IRenderPipeline`)
- Color, Handle<Material> types (already exist)

**Tests**:
```cpp
// test/core/test_application_settings.cpp
TEST(ApplicationSettings, Singleton) {
    auto& s1 = uz::ApplicationSettings::Get();
    auto& s2 = uz::ApplicationSettings::Get();
    EXPECT_EQ(&s1, &s2);
}

TEST(ApplicationSettings, TargetFrameRate) {
    auto& settings = uz::ApplicationSettings::Get();
    settings.SetTargetFrameRate(60);
    EXPECT_EQ(settings.GetTargetFrameRate(), 60);
}
```

---

#### Task 1.2: Create ApplicationSettingsSubsystem

**File**: `runtime/engine/app/application_settings_subsystem.h`

```cpp
class ApplicationSettingsSubsystem : ISubsystem {
public:
    const char* GetName() const override { return "ApplicationSettings"; }
    InitPhase GetInitPhase() const override { return InitPhase::PreInit; }
    TickGroup GetTickGroup() const override { return TickGroup::None; }

    bool Init() override;
    void Shutdown() override;
};
```

**Responsibilities**:
- Load settings from config file or defaults
- Feed settings to render subsystem (shadow resolution, MSAA, etc.)
- Handle runtime changes (e.g., shadow resolution → rebuild shadow atlas)

**Integration**:
- Register in application initialization
- Before RenderSubsystem and TimeManager (they depend on settings)

---

#### Task 1.3: Expose to Lua

**File**: `runtime/engine/scripting/api/core/uz_app_api.cpp`

```cpp
// uz.app namespace
// uz.app.set_target_frame_rate(fps)
// uz.app.get_target_frame_rate()
// uz.app.set_shadow_resolution(res)
// uz.app.get_shadow_resolution()
// uz.app.set_ambient_light({r, g, b, a})
// uz.app.get_ambient_light()
// uz.app.set_ambient_intensity(intensity)
// uz.app.get_ambient_intensity()
// uz.app.set_vsync_enabled(enabled)
// uz.app.get_vsync_enabled()
```

**Tests**:
```lua
-- Lua test
uz.app.set_target_frame_rate(60)
assert(uz.app.get_target_frame_rate() == 60)

uz.app.set_shadow_resolution(2048)
assert(uz.app.get_shadow_resolution() == 2048)

uz.app.set_ambient_light({1, 1, 1, 1})
local light = uz.app.get_ambient_light()
assert(light.r == 1 and light.g == 1)
```

---

### Phase 2: Pluggable Render Pipeline (P1, ~3-4 days)

**Goal**: Replace enum-based render path selection with polymorphic `IRenderPipeline`.

#### Task 2.1: Define IRenderPipeline Interface

**File**: `runtime/engine/renderer/core/render_pipeline.h` (NEW)

```cpp
namespace uz {

/// Abstract interface for rendering pipelines.
///
/// Game code (via ApplicationSettings) selects a pipeline at runtime.
/// The renderer calls the selected pipeline's Render() method each frame.
/// This enables swapping deferred ↔ forward without recompilation.
struct IRenderPipeline {
    virtual ~IRenderPipeline() = default;

    /// Returns the pipeline name (e.g., "Deferred", "Forward", "Clustered").
    /// Used for logging and debugging.
    [[nodiscard]] virtual const char* GetName() const = 0;

    /// Main render method, called once per camera per frame.
    ///
    /// # Arguments
    /// * `scene` — Scene environment (lights, sky, post-process, etc.)
    /// * `camera` — Camera to render from
    /// * `width`, `height` — Render target dimensions
    /// * `rgPool` — Render graph resource pool (persistent across frames)
    virtual void Render(
        const SceneEnvironment& scene,
        const Camera& camera,
        uint32_t width,
        uint32_t height,
        rg::RgResourcePool* rgPool
    ) = 0;
};

} // namespace uz
```

**Key design**:
- Minimal interface (only 2 methods) — easy to implement new pipelines
- Takes `SceneEnvironment` (not raw lights) — encapsulation
- Returns void — pipeline owns result submission
- Uses RenderGraph (not CommandList directly) — abstraction

---

#### Task 2.2: Implement DeferredRenderPipeline

**File**: `runtime/engine/renderer/passes/deferred_render_pipeline.h/.cpp`

**Rename from**: The current deferred path implementation (scattered in `scene_renderer.cpp`)

```cpp
class DeferredRenderPipeline : public IRenderPipeline {
public:
    const char* GetName() const override { return "Deferred"; }

    void Render(
        const SceneEnvironment& scene,
        const Camera& camera,
        uint32_t width,
        uint32_t height,
        rg::RgResourcePool* rgPool
    ) override {
        // Current G-Buffer pass logic
        // Current lighting pass logic
        // Current composite logic
    }

private:
    // Move member variables from SceneRenderer
};
```

---

#### Task 2.3: Implement ForwardRenderPipeline

**File**: `runtime/engine/renderer/passes/forward_render_pipeline.h/.cpp`

**Rename from**: The current forward path implementation

```cpp
class ForwardRenderPipeline : public IRenderPipeline {
public:
    const char* GetName() const override { return "Forward"; }

    void Render(
        const SceneEnvironment& scene,
        const Camera& camera,
        uint32_t width,
        uint32_t height,
        rg::RgResourcePool* rgPool
    ) override {
        // Current forward lit pass logic
    }
};
```

---

#### Task 2.4: Create RenderPipelineSubsystem

**File**: `runtime/engine/renderer/core/render_pipeline_subsystem.h/.cpp`

```cpp
class RenderPipelineSubsystem : ISubsystem {
public:
    const char* GetName() const override { return "RenderPipeline"; }
    InitPhase GetInitPhase() const override { return InitPhase::Init; }
    TickGroup GetTickGroup() const override { return TickGroup::PostUpdate; }

    std::vector<const char*> GetDependencies() const override {
        return {"Renderer", "ApplicationSettings"};
    }

    bool Init() override;
    void Shutdown() override;
    void Tick(float deltaTime) override;

private:
    DeferredRenderPipeline deferredPipeline_;
    ForwardRenderPipeline forwardPipeline_;
};
```

**Responsibilities**:
- Gather scene environment (lights, sky, post-process)
- Get active pipeline from ApplicationSettings
- Call pipeline->Render()
- Handle pipeline switching (if user changes it mid-frame)

**Tick order**: PostUpdate (after all game logic, before presentation)

---

#### Task 2.5: Remove SceneRenderer.h Render Path Enum

**File**: `runtime/engine/renderer/scene_renderer.h`

**Change**:
```cpp
// OLD
enum class RenderPath { Deferred, Forward };
class SceneRenderer {
    RenderPath renderPath_;
    void Render(const Camera& camera);  // If statement inside
};

// NEW — delete entire Render() method from SceneRenderer
// Rendering is now driven by RenderPipelineSubsystem
```

**Impact**:
- Demos that toggle with F key now do: `ApplicationSettings::Get().SetActivePipeline(...)`
- No more if-chains on `renderPath_`

---

#### Task 2.6: Update PBRTestDemo

**File**: `runtime/demos/pbr_test/pbr_test_demo.cpp`

**Change**:
```cpp
// OLD
if (renderPath_ == RenderPath::Deferred) {
    // ... deferred logic
} else {
    // ... forward logic
}

// NEW
void OnFrame(float dt) override {
    if (input->WasKeyPressed(Key::F)) {
        auto& settings = ApplicationSettings::Get();
        auto* current = settings.GetActivePipeline();

        auto* next = (current->GetName() == "Deferred")
            ? &forwardPipeline
            : &deferredPipeline;

        settings.SetActivePipeline(next);
        UZ_LOG(Renderer, Info, "Switched to: {}", next->GetName());
    }
}
```

---

### Phase 3: Async Scene Loading (P2, ~2-3 days)

**Goal**: Async scene loader with progress tracking (like Unity's `LoadSceneAsync`).

#### Task 3.1: Define AsyncSceneLoad Struct

**File**: `runtime/engine/scene/async_scene_load.h`

```cpp
namespace uz {

/// Result of an async scene load operation.
struct AsyncSceneLoad {
    float progress = 0.0f;      /// [0.0, 1.0]
    bool isDone = false;
    bool allowActivation = true; /// False = pause at 0.9

    bool IsLoading() const { return progress < 1.0f; }
    bool IsReady() const { return progress >= 0.9f; }
};

} // namespace uz
```

---

#### Task 3.2: Create SceneLoaderSubsystem

**File**: `runtime/engine/scene/scene_loader_subsystem.h/.cpp`

```cpp
class SceneLoaderSubsystem : ISubsystem {
public:
    const char* GetName() const override { return "SceneLoader"; }
    InitPhase GetInitPhase() const override { return InitPhase::PreInit; }
    TickGroup GetTickGroup() const override { return TickGroup::PrePhysics; }

    bool Init() override;
    void Shutdown() override;
    void Tick(float deltaTime) override;

    /// Start async load of a scene.
    /// Returns a handle that caller polls each frame.
    Handle<AsyncSceneLoad> LoadSceneAsync(const char* path);

    /// Activate a loaded scene (must be ready: progress >= 0.9).
    void ActivateScene(Handle<AsyncSceneLoad> load);

    /// Get the current load operation (may be null).
    Handle<AsyncSceneLoad> GetCurrentLoad() const;
};
```

**Implementation details**:
- Each frame, advance progress by loading N meshes/textures/etc.
- At 0.9, pause and wait for `allowActivation = true` before 1.0
- At 1.0, activate the scene (actually load it into world)

---

#### Task 3.3: Expose to Lua

**File**: `runtime/engine/scripting/api/scene/uz_scene_api.cpp`

```lua
-- uz.scene.load_async(path) → handle
-- uz.scene.get_load_progress(handle) → float [0, 1]
-- uz.scene.is_ready(handle) → bool
-- uz.scene.set_allow_activation(handle, allow)
-- uz.scene.activate(handle)

local async = uz.scene.load_async("levels/level_one")

function update()
    if uz.scene.is_loading(async) then
        local progress = uz.scene.get_load_progress(async)
        set_progress_bar_value(progress)
    elseif uz.scene.is_ready(async) and input_jump then
        uz.scene.activate(async)
    end
end
```

---

### Phase 4: Documentation & Lua Parity (P1, ~1-2 days)

#### Task 4.1: Update Docs

**Files**:
- `docs/specs/1.3-renderer.md` — Add ApplicationSettings + IRenderPipeline
- `docs/engine/12-rendering/05-application-settings.md` — New file on settings API
- `docs/lua-api-reference.md` — Add uz.app.*, uz.scene.load_async()

---

#### Task 4.2: Write Tests

**Files**:
- `test/core/test_application_settings.cpp` — Settings API
- `test/renderer/test_render_pipeline.cpp` — Pipeline switching
- `test/scene/test_scene_loader.cpp` — Async loading

---

## Integration Checklist

### Before Starting Phase 1
- [ ] Review `docs/analysis/rendering/unity-application-abstraction.md`
- [ ] Confirm DemoApplication usage in all demos
- [ ] List all current settings scattered in codebase (grep for "shadow_", "msaa", etc.)

### After Phase 1 Complete
- [ ] ApplicationSettings tested + Lua API working
- [ ] All demos updated to use ApplicationSettings::Get() instead of direct members

### After Phase 2 Complete
- [ ] F-key toggle in PBRTestDemo works with both pipelines
- [ ] No if-statements on renderPath_ remain in codebase
- [ ] SceneRenderer.h no longer mentions "deferred" or "forward"

### After Phase 3 Complete
- [ ] Loading screen demo works with AsyncSceneLoad
- [ ] Lua can call uz.scene.load_async() and uz.scene.activate()

### After Phase 4 Complete
- [ ] All new APIs documented in /docs/
- [ ] All tests passing with 95%+ coverage
- [ ] Game developers can write game code without GPU knowledge

---

## Effort Estimate

| Phase | Task | Est. Days | Priority |
|-------|------|----------|----------|
| 1 | ApplicationSettings singleton + Lua | 2-3 | P1 |
| 2 | IRenderPipeline polymorphism | 3-4 | P1 |
| 3 | AsyncSceneLoad + Lua | 2-3 | P2 |
| 4 | Docs + tests | 1-2 | P1 |
| **Total** | | **8-12** | |

---

## Success Criteria

✓ Game developers can write `OnFrame()` without seeing:
- Device creation
- Command list recording
- RHI API types (ID3D12*, ComPtr<>, etc.)
- Texture/mesh upload (AssetManager handles it)
- Render pass registration (Subsystems handle it)
- G-Buffer layout (Pipeline handles it)

✓ Settings are centralized: `uz::ApplicationSettings::Get()`

✓ Render pipelines are pluggable: `SetActivePipeline(new_pipeline)` swaps deferred ↔ forward at runtime

✓ Lua has full parity: `uz.app.*`, `uz.scene.load_async()`, etc.

✓ Scene loading shows progress: `uz.scene.get_load_progress()` for loading screens

---

## References

- **unity-application-abstraction.md** — Full analysis of Unity's abstraction
- **runtime/demos/common/demo_application.h** — Model for game code abstraction
- **runtime/engine/app/subsystem.h** — Already-correct subsystem pattern
