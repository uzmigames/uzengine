<!-- doc-meta
title: Unity Application/Engine Abstraction Architecture
draft: false
type: analysis
category: rendering
priority: P0
version: 1.0
created: 2026-03-14
tags:
  - unity
  - application-abstraction
  - playerloop
  - rendering-pipeline
  - engine-architecture
  - subsystem-registration
  - game-developer-api
-->

# Unity Application/Engine Abstraction Architecture

## Executive Summary

Unity's brilliance lies in **three architectural layers** that hide GPU/rendering complexity from game developers:

1. **Subsystem Registration** — Custom PlayerLoop allows engine subsystems to hook per-frame updates without game code knowing about timing order.
2. **Global Configuration APIs** — `Application.targetFrameRate`, `QualitySettings`, `RenderSettings` expose control without exposing internal state.
3. **Component Abstraction** — `Camera.Render()`, `MeshRenderer`, `Graphics.DrawMesh` provide two levels of rendering: declarative (game code) vs procedural (advanced).
4. **Render Pipeline Polymorphism** — `RenderPipelineAsset` + `ScriptableRenderPipeline` (SRP) allow swapping entire rendering backends (BRP → URP → HDRP → custom) without changing game code.

**Key Pattern**: Game developers see **ONLY data + behavior**, never:
- Device creation
- Command list management
- Texture/mesh upload
- Render pass registration
- G-Buffer setup
- Shadow atlas setup
- Barrier management

## How Unity Hides Rendering Boilerplate

### Layer 1: PlayerLoop Subsystem Registration

**What the developer sees:**
```csharp
// Game code — nothing about rendering internals
public class MyGameBehavior : MonoBehaviour {
    void Awake() { /* initialize */ }
    void Start() { /* first frame logic */ }
    void Update() { /* per-frame gameplay */ }
}
```

**What Unity does under the hood:**
- Implements a custom player loop with **nested subsystem registration**
- Each subsystem (Physics, Input, Rendering, Animation, etc.) has an `IPlayerLoopSystem`
- Subsystems declare their `updateDelegate` + tick order
- The `PlayerLoop.GetCurrentPlayerLoop()` API allows **custom insertion** without global switch statements

**The Contract:**
- Developers implement lifecycle methods; Unity calls them at the right time
- Order is topologically sorted by dependency (initialization phases)
- Tick groups (`PrePhysics`, `PostPhysics`, `Update`, `PostUpdate`) auto-schedule
- No per-frame register/unregister overhead — loop is built once, runs identically each frame

**UzEngine Translation:**
- `ISubsystem` interface with `GetTickGroup()` (same as Unity)
- `SubsystemRegistry` topologically sorts by `GetDependencies()` (matches NetworkUpdateLoop pattern)
- Per-frame tick loop calls `subsystem->Tick(dt)` in group order
- **Key difference**: UzEngine uses C++ virtual dispatch, Unity uses reflection + delegates

### Layer 2: Global Application Settings (Stateless from Perspective of Game Code)

**What game code can control:**

```csharp
// Frame rate
Application.targetFrameRate = 60;  // -1 = unlimited

// Quality/rendering
QualitySettings.vSyncCount = 1;        // 0,1,2 = off/1x/2x vsync
QualitySettings.antiAliasing = 4;      // MSAA
QualitySettings.anisotropicFiltering = 1; // AF level

// Lighting/environment
RenderSettings.ambientLight = Color.white;
RenderSettings.ambientMode = AmbientMode.Trilight;
RenderSettings.skybox = mySkyboxMaterial;
RenderSettings.fogColor = Color.gray;
RenderSettings.fog = true;

// Active render pipeline
GraphicsSettings.renderPipelineAsset = urpAsset;
```

**What's hidden:**
- How `targetFrameRate` translates to `WaitForNextFrame()` timing
- How `QualitySettings` values propagate to PSO variants (shader keywords: `MSAA_4x`, `AF_ANISO`)
- How `RenderSettings.skybox` triggers cubemap binding in the lighting pass
- How `GraphicsSettings.renderPipelineAsset` swaps the entire `Render()` function implementation

**The Contract:**
- Game code is **configuration**, not orchestration
- All frame timing is automatic (vsync handled by RHI layer, target frame rate clamped)
- All PSO/shader variant selection is automatic (Unity builds PSO cache per quality level)
- Render pipeline **interface is fixed** (`Render(ScriptableRenderContext, Camera[])`), implementation is pluggable

**UzEngine Translation:**
```cpp
// Create a global settings singleton
struct RenderSettings {
    Color ambientLight;
    float targetFrameRate;
    uint32_t shadowResolution;
    uint32_t shadowAtlasSize;
    // ... expose via Lua
};

// Accessed via singleton
uz::RenderSettings::Get().SetAmbientLight({1, 1, 1, 1});
uz::RenderSettings::Get().SetTargetFrameRate(60);
```

### Layer 3: Camera.Render() Abstraction

**What game code sees:**
```csharp
public class MyCameraController : MonoBehaviour {
    public Camera camera;

    void Update() {
        // Move camera
        transform.position += Input.GetAxis("Vertical") * moveSpeed;

        // That's it. Rendering happens automatically.
        // No calls to: GrabDepthTexture, RenderGBuffer, ComputeLighting, etc.
    }
}
```

**What Camera.Render() actually does:**
1. Queries `GraphicsSettings.renderPipelineAsset` to get the active pipeline
2. Calls `RenderPipelineAsset.Render(ScriptableRenderContext ctx, Camera[] cameras)`
3. The SRP implementation (URP/HDRP/custom) handles:
   - **G-Buffer pass**: Render geometry with normal, albedo, metallic, roughness
   - **Lighting pass**: Read G-Buffer, compute per-pixel lighting (deferred) OR per-object lighting (forward)
   - **Post-processing**: Bloom, tone-mapping, color grading
   - **UI**: Composite UI canvas on top

**The key insight**: The `ScriptableRenderContext` is the only API game code never directly touches:
- `ctx.DrawRenderers()` — takes a `DrawingSettings` (which cameras, which lights, which layers)
- Returns a list of visible objects (already culled, sorted by material)
- SRP controls pass order, barrier placement, resource transitions

**Example: Switching From BRP to URP**
```csharp
// Before (old code)
camera.renderingPath = RenderingPath.Deferred;

// After (new code)
GraphicsSettings.renderPipelineAsset = urpDeferredAsset;
// ZERO changes to game code. Camera.Render() now uses URP deferred.
```

**UzEngine Translation:**
```cpp
// Abstract render pipeline interface
struct IRenderPipeline {
    virtual void Render(SceneRenderer* scene, Camera* cameras[], int camCount) = 0;
};

// Game code never knows about this interface
class GameController {
    void OnFrame(float dt) {
        // Move camera
        camera.position += input.GetMovement() * moveSpeed;

        // Rendering is implicit (happens in subsystem)
    }
};

// Engine internally:
class RenderSubsystem : ISubsystem {
    void Tick(float dt) override {
        // Fetch active pipeline asset from config
        auto* pipeline = RenderSettings::Get().GetActivePipeline();

        // Execute
        SceneRenderer scene = GatherSceneEnvironment();
        pipeline->Render(&scene, cameras, camCount);
    }
};
```

### Layer 4: Two-Level Rendering Abstraction

**Level 1: Declarative (Game Code)**
```csharp
// Game object with a mesh
public class MyEnemy : MonoBehaviour {
    void Start() {
        GetComponent<MeshRenderer>().material = enemyMaterial;
    }
}
```

**Level 2: Procedural (Advanced/Tools)**
```csharp
// Direct GPU command (bypasses MeshRenderer, for gizmos/debug)
void OnPostRender() {
    Graphics.DrawMesh(
        mesh: debugMesh,
        position: transform.position,
        rotation: transform.rotation,
        material: debugMaterial,
        layer: LayerMask.NameToLayer("Default")
    );
}
```

**What's hidden:**
- `MeshRenderer` — Unity automatically schedules the mesh in the right render pass (deferred/forward)
- `Graphics.DrawMesh` — Low-level override for one-off draws (editor gizmos, debug geometry, custom rendering)
- No game code cares about:
  - Which G-Buffer slot the normal maps to
  - Whether we're in deferred or forward mode
  - When the mesh gets uploaded to GPU

**UzEngine Translation:**
```cpp
// Level 1: Component-based (game code)
class MeshRendererComponent {
    Handle<uz::GPUMesh> mesh;
    Handle<uz::Material> material;
    // ... declared once, auto-scheduled by RenderSubsystem
};

// Level 2: Procedural (advanced)
void DebugDraw() {
    auto* cmdList = GetCommandList();
    GetPipelines()->DrawDebugMesh(
        cmdList,
        debugMesh,
        transform.position,
        transform.rotation,
        debugMaterial
    );
}
```

### Layer 5: RenderPipelineAsset — Pluggable Rendering Backends

**The Abstraction:**
```csharp
public abstract class RenderPipelineAsset : ScriptableObject {
    public abstract RenderPipeline CreatePipeline();
}

public abstract class RenderPipeline {
    public abstract void Render(
        ScriptableRenderContext context,
        Camera[] cameras
    );
}
```

**What happens when you switch:**
```csharp
// Change this line...
GraphicsSettings.renderPipelineAsset = hdrpAsset;

// ... and the ENTIRE rendering pipeline swaps:
// HDRP:
//   - Ray-traced reflections
//   - Compute-based volumetric fog
//   - Tessellation support
//   - Complex material model (hair, fabric, skin)
//
// To URP:
//   - No ray-tracing
//   - No tessellation
//   - Simpler material model
//   - More performant on mobile

// GAME CODE UNCHANGED ✓
```

**The key design principle:**
- Only the **interface** is fixed: `Render(ScriptableRenderContext, Camera[])`
- Everything **internal** is pluggable: G-Buffer layout, lighting model, post-processing, shader variants
- Swapping pipelines can be **resource-intensive** (PSO recompilation, memory layout changes)
- But it's **guaranteed to work** — both URP and HDRP implement the same contract

**UzEngine Translation:**
```cpp
struct IRenderPipeline {
    virtual void Render(
        SceneRenderer* scene,
        Camera* cameras[],
        int camCount
    ) = 0;
};

// Built-in pipelines
class DeferredPipeline : IRenderPipeline {
    void Render(...) override {
        // G-Buffer pass
        // Lighting pass
        // Composite
    }
};

class ForwardPipeline : IRenderPipeline {
    void Render(...) override {
        // Forward lit pass
        // Composite
    }
};

// Runtime swap
struct RenderSettings {
    IRenderPipeline* GetActivePipeline() const {
        switch (activeMode_) {
            case RenderMode::Deferred: return &deferredPipeline_;
            case RenderMode::Forward: return &forwardPipeline_;
        }
    }
};

// CLI selection
./app --render-mode deferred
./app --render-mode forward
```

### Layer 6: SceneManager.LoadScene — Async Abstraction

**What game code sees:**
```csharp
// Sync load (blocks main thread)
SceneManager.LoadScene("LevelOne");

// Async load (non-blocking, with progress)
var async = SceneManager.LoadSceneAsync("LevelOne");
while (!async.isDone) {
    progressBar.value = async.progress;
    yield return null;
}
```

**What's hidden:**
- Mesh/texture streaming from disk
- GPU upload scheduling (might be deferred to avoid hitches)
- Material compilation (shader variants for scene-specific keywords)
- Lighting bake data decompression
- Navmesh activation
- Audio clip loading
- Physics collider initialization

**The Contract:**
- `LoadSceneAsync()` returns `AsyncOperation` with a `progress` property
- `progress` = 0.0 to 0.9 during async loading
- `progress` = 1.0 when ready (but not yet activated if `allowSceneActivation = false`)
- `isDone = true` after scene is fully activated

**UzEngine Translation:**
```cpp
// Async scene load with progress
struct AsyncSceneLoad {
    float progress;    // 0.0 to 1.0
    bool isDone;
    bool IsLoading() const { return progress < 1.0f; }
};

AsyncSceneLoad* LoadSceneAsync(const char* path);

// Game code
void OnUpdate(float dt) {
    if (loadOp && loadOp->IsLoading()) {
        progressBar.SetProgress(loadOp->progress);
    }
}
```

---

## Unity vs UE5 Comparison

### Unity (Studied)
| Aspect | Approach |
|--------|----------|
| **Subsystems** | PlayerLoop with nested registration, `IPlayerLoopSystem` interface |
| **Tick order** | Topologically sorted by dependency within each phase |
| **Rendering** | RenderPipelineAsset (pluggable), SRP (URP/HDRP are examples) |
| **Settings** | `Application.targetFrameRate`, `QualitySettings`, `RenderSettings` statics |
| **Scene load** | `SceneManager.LoadSceneAsync()` with progress + allow-deferred activation |
| **Game developer sees** | Awake/Start/Update lifecycle, Camera/MeshRenderer components, transform hierarchy |

### UE5
| Aspect | Approach |
|--------|----------|
| **Subsystems** | `FSubsystem` classes with explicit `Initialize()/Deinitialize()`, registered in engine module |
| **Tick order** | `TickableGroup` (PrePhysics, Physics, PostPhysics, etc.) sorted by `TickInterval` |
| **Rendering** | `FSceneRenderer` (monolithic, not pluggable; customization via render features) |
| **Settings** | `GEngine->ExecuteConsoleCommand()` (CVars), `FRenderingSettings` (not as clean as Unity) |
| **Scene load** | `LoadLevelInstance()` / `LoadLevelInstanceBySoftPtr()` with load progress |
| **Game developer sees** | AActor/UActorComponent hierarchy, Pawn/Controller, blueprints or C++ code |

### Key Differences

**1. Subsystem Registration**
- **Unity**: Declarative via `GetDependencies()` virtual, registry auto-sorts
- **UE5**: Declarative in engine module/constructor, registry auto-sorts
- **Winner**: Tie — both prevent manual scheduling

**2. Global Settings**
- **Unity**: `static Application`, `static QualitySettings` — clean, discoverable
- **UE5**: CVars + scattered settings — powerful but fragmented
- **Winner**: Unity (API surface is smaller, more predictable)

**3. Render Pipeline**
- **Unity**: `RenderPipelineAsset` interface, pluggable at runtime (`GraphicsSettings.renderPipelineAsset = ...`)
- **UE5**: Monolithic `FSceneRenderer`, customization via forward-declared render features
- **Winner**: Unity (true polymorphism, no recompilation to switch pipelines)

**4. Scene Loading**
- **Unity**: `AsyncOperation` with `progress` property, `allowSceneActivation` to defer activation
- **UE5**: Explicit `LoadLevelInstance()` + `WaitForLevel()` promises
- **Winner**: Tie — both allow non-blocking loads

---

## UzEngine Adaptation Strategy

### 1. Subsystem Registration (Already Implemented ✓)

UzEngine already has the right model:

```cpp
// runtime/engine/app/subsystem.h
class ISubsystem {
    virtual const char* GetName() const = 0;
    virtual InitPhase GetInitPhase() const = 0;
    virtual TickGroup GetTickGroup() const = 0;
    virtual std::vector<const char*> GetDependencies() const = 0;
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;
    virtual void Tick(float deltaTime) = 0;
};
```

**Status**: Perfect match to Unity pattern. No changes needed.

### 2. Application-Level Settings (Needs Implementation)

**Current**: Settings are ad-hoc in various subsystems.

**Target**: Create a unified `ApplicationSettings` class:

```cpp
/// runtime/engine/app/application_settings.h
class ApplicationSettings {
public:
    // Singleton access
    static ApplicationSettings& Get();

    // Frame rate
    void SetTargetFrameRate(int fps);  // -1 = unlimited
    int GetTargetFrameRate() const;

    // Rendering quality
    void SetShadowResolution(uint32_t res);
    uint32_t GetShadowResolution() const;

    void SetAntiAliasing(uint32_t msaaSamples);
    uint32_t GetAntiAliasing() const;

    // Environment
    void SetAmbientLight(const uz::Color& color);
    const uz::Color& GetAmbientLight() const;

    void SetAmbientIntensity(float intensity);
    float GetAmbientIntensity() const;

    // VSync
    void SetVSyncEnabled(bool enabled);
    bool IsVSyncEnabled() const;

    // Active render pipeline (pluggable)
    void SetActivePipeline(IRenderPipeline* pipeline);
    IRenderPipeline* GetActivePipeline() const;
};
```

**Integrate into Lua:**
```lua
uz.app.set_target_frame_rate(60)
uz.app.set_shadow_resolution(2048)
uz.app.set_ambient_light({1, 1, 1})
uz.app.set_ambient_intensity(0.3)
```

### 3. Render Pipeline Polymorphism (Partially Implemented)

**Current**: `render_path_t` enum selects deferred vs forward via `GetDefaultRenderPath()`.

**Target**: Upgrade to true pluggable interface:

```cpp
/// runtime/engine/renderer/core/render_pipeline.h
struct IRenderPipeline {
    virtual ~IRenderPipeline() = default;

    /// Name for debugging/logging
    virtual const char* GetName() const = 0;

    /// Main render function
    virtual void Render(
        SceneRenderer* scene,
        const Camera& camera,
        uint32_t width,
        uint32_t height,
        RgResourcePool* rgPool
    ) = 0;
};

/// Built-in pipelines
class DeferredRenderPipeline : IRenderPipeline {
    const char* GetName() const override { return "Deferred"; }
    void Render(...) override;
};

class ForwardRenderPipeline : IRenderPipeline {
    const char* GetName() const override { return "Forward"; }
    void Render(...) override;
};
```

**Usage:**
```cpp
// runtime/demos/pbr_test/pbr_test_demo.cpp
class PBRTestDemo : public DemoApplication {
    void OnFrame(float dt) override {
        // Toggle with F key
        if (input->WasKeyPressed(Key::F)) {
            auto* app = &ApplicationSettings::Get();
            IRenderPipeline* current = app->GetActivePipeline();

            IRenderPipeline* next = (current == &deferredPipeline_)
                ? &forwardPipeline_
                : &deferredPipeline_;

            app->SetActivePipeline(next);
            UZ_LOG(Renderer, Info, "Switched to: {}", next->GetName());
        }
    }
};
```

**Subsystem integration:**
```cpp
// New: RenderPipelineSubsystem
class RenderPipelineSubsystem : ISubsystem {
    const char* GetName() const override { return "RenderPipeline"; }
    InitPhase GetInitPhase() const override { return InitPhase::Init; }
    TickGroup GetTickGroup() const override { return TickGroup::PostUpdate; }

    std::vector<const char*> GetDependencies() const override {
        return {"Renderer"}; // Must run after Renderer init
    }

    bool Init() override {
        auto* app = &ApplicationSettings::Get();
        app->SetActivePipeline(&deferredPipeline_);  // Default
        return true;
    }

    void Tick(float dt) override {
        auto* app = &ApplicationSettings::Get();
        auto* pipeline = app->GetActivePipeline();

        SceneRenderer scene = GatherSceneEnvironment();
        pipeline->Render(&scene, &currentCamera_, width_, height_, rgPool_);
    }
};
```

### 4. DemoApplication as Model for Game Code

**Current**: `DemoApplication` already eliminates boilerplate for demos.

**Status**: Use DemoApplication pattern as **reference architecture** for game code:

```cpp
/// game/src/game_application.h
class GameApplication : public DemoApplication {
public:
    GameApplication();

protected:
    void OnInit() override;      // Load level, spawn player, etc.
    void OnFrame(float dt) override; // Game update + rendering (implicit)
    void OnShutdown() override;
};
```

Game code **never directly:**
- Creates RHI device
- Records command lists
- Manages textures/meshes (done via asset system)
- Registers render passes
- Manages shadows/G-Buffer

All of that is **implicit in the subsystem pipeline**.

### 5. Scene Loading (Needs Implementation)

**Current**: Scene loading is synchronous or ad-hoc.

**Target**: Async scene loader with progress:

```cpp
/// runtime/engine/core/scene_loader.h
struct AsyncSceneLoad {
    float progress;    // 0.0 to 1.0
    bool isDone;
    bool allowActivation;  // False = pause at 0.9

    bool IsLoading() const { return !isDone; }
    bool IsReady() const { return progress >= 0.9f; }
};

class SceneLoaderSubsystem : ISubsystem {
    AsyncSceneLoad* LoadSceneAsync(const char* path);
    void ActivateScene(AsyncSceneLoad* load);
};
```

**Lua integration:**
```lua
local async = uz.scene.load_async("levels/level_one")

function update()
    if async.is_loading then
        set_progress_bar(async.progress)
    elseif async.is_ready and input.jump then
        uz.scene.activate(async)
    end
end
```

---

## Actionable Patterns for UzEngine

### Pattern 1: Subsystem Tick Group (Already Correct)

**What UzEngine does right:**
- `PrePhysics`, `PostPhysics`, `Update`, `PostUpdate` tick groups match Unity exactly
- Topological sort by `GetDependencies()` prevents manual scheduling
- **Subclass developers never write timing code**

### Pattern 2: Settings as Singleton (Needed)

**Implementation:**
```cpp
class ApplicationSettings {
    static ApplicationSettings& Get() {
        static ApplicationSettings instance;
        return instance;
    }

    // All settings are non-virtual, stored in members
    // Accessed via Lua: uz.app.set_*, uz.app.get_*
};
```

**Why**: Game code (C++ or Lua) should **configure**, not orchestrate.

### Pattern 3: Render Pipeline as Interface (Partial)

**Current code:**
```cpp
// Old way
enum class RenderPath { Deferred, Forward };
RenderPath renderPath = GetDefaultRenderPath();
if (renderPath == RenderPath::Deferred) {
    // G-Buffer pass
} else {
    // Forward lit pass
}
```

**New way (polymorphic):**
```cpp
// New way
IRenderPipeline* pipeline = app->GetActivePipeline();
pipeline->Render(&scene, &camera, width, height, rgPool);
// No switch. Polymorphism handles it.
```

**Why**: Enables runtime pipeline swaps without recompilation. True abstraction.

### Pattern 4: Two-Level Rendering (Already Correct)

**Level 1** (declarative):
```cpp
// Game code
class EnemySpawner : ITickable {
    void OnTick(float dt) {
        // Spawn enemy — auto-rendered via MeshRendererComponent
        auto entity = scene->CreateEntity();
        entity.AddComponent<TransformComponent>({.position = spawnPos});
        entity.AddComponent<MeshRendererComponent>({.mesh = enemyMesh, .material = mat});
    }
};
```

**Level 2** (procedural):
```cpp
// Debug/advanced rendering
void DebugDraw() {
    cmdList->DrawDebugCube(center, halfExtent, color);
    cmdList->DrawDebugLine(from, to, color);
}
```

**Why**: Game code uses Level 1 (implicitly rendered), tools/debug use Level 2 (explicit).

### Pattern 5: DemoApplication Template (Already Implemented)

**UzEngine has this perfect:**
- `OnInit()` — load resources (cmdList is recording)
- `OnFrame(dt)` — render (cmdList is recording)
- `OnShutdown()` — cleanup
- Base class handles: device, swapchain, fence sync, screenshot, camera, stats

**Extend this pattern to game code**:
```cpp
class MyGame : public DemoApplication {
    void OnInit() override {
        // Load level, create world, spawn player
        // NO device/window creation needed
    }

    void OnFrame(float dt) override {
        // Game update + rendering is implicit
        // Just move camera + update game state
    }
};
```

---

## Summary: What Game Developers Must NOT See

| What Game Code Should Never Touch | Why | Who Handles It |
|----------------------------------|-----|---|
| Device creation | Multi-backend abstraction | RHI layer + DemoApplication |
| Command list recording | GPU synchronization details | RenderSubsystem |
| Texture upload | Asset system handles async loading | AssetManager |
| Mesh upload | Streaming, LOD, vertex layout | MeshImporter + GPUMesh |
| Render pass registration | Dynamic pass ordering + barriers | RenderGraph |
| G-Buffer setup | Pipeline-specific layout | IRenderPipeline implementation |
| Shadow atlas management | Light clustering + tile allocation | ShadowPass |
| Barrier placement | GPU cache coherency | RenderGraph resource transitions |
| PSO compilation | Shader variants + quality levels | PipelineLibrary |
| Target frame rate calculation | vsync + frame capping | TimeManager |

---

## References

- **Unity Application API**: [https://docs.unity3d.com/ScriptReference/Application-targetFrameRate.html](https://docs.unity3d.com/ScriptReference/Application-targetFrameRate.html)
- **Unity PlayerLoop Customization**: [https://docs.unity3d.com/6000.3/Documentation/Manual/player-loop-customizing.html](https://docs.unity3d.com/6000.3/Documentation/Manual/player-loop-customizing.html)
- **Unity ScriptableRenderPipeline**: [https://docs.unity3d.com/6000.3/Documentation/Manual/scriptable-render-pipeline-introduction.html](https://docs.unity3d.com/6000.3/Documentation/Manual/scriptable-render-pipeline-introduction.html)
- **Unity Deferred Rendering**: [https://docs.unity3d.com/Manual/RenderTech-DeferredShading.html](https://docs.unity3d.com/Manual/RenderTech-DeferredShading.html)
- **Unity SceneManager.LoadSceneAsync**: [https://docs.unity3d.com/ScriptReference/SceneManagement.SceneManager.LoadSceneAsync.html](https://docs.unity3d.com/ScriptReference/SceneManagement.SceneManager.LoadSceneAsync.html)
- **Unity MonoBehaviour Lifecycle**: [https://docs.unity3d.com/2023.2/Documentation/Manual/ExecutionOrder.html](https://docs.unity3d.com/2023.2/Documentation/Manual/ExecutionOrder.html)
- **Unity Graphics.DrawMesh**: [https://docs.unity3d.com/ScriptReference/Graphics.DrawMesh.html](https://docs.unity3d.com/ScriptReference/Graphics.DrawMesh.html)
- **Unity RenderPipeline Switching**: [https://docs.unity3d.com/Manual/srp-setting-render-pipeline-asset.html](https://docs.unity3d.com/Manual/srp-setting-render-pipeline-asset.html)
