<!-- doc-meta
title: Unity Architecture Patterns — Quick Reference
draft: false
type: reference
category: rendering
priority: P0
version: 1.0
created: 2026-03-14
tags:
  - unity
  - architecture
  - abstraction-layers
  - game-engine
  - reference
-->

# Unity Architecture Patterns — Quick Reference

## The Five-Layer Abstraction Model

```
┌─────────────────────────────────────────────────────────────┐
│ GAME CODE LAYER                                             │
│ ────────────────────────────────────────────────────────────│
│ public class MyGame : MonoBehaviour {                       │
│     void Update() {                                         │
│         transform.position += input.forward * speed;       │
│         // That's IT. No rendering calls.                  │
│     }                                                       │
│ }                                                           │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ COMPONENT LAYER (Declarative)                              │
│ ────────────────────────────────────────────────────────────│
│ • MeshRenderer — auto-scheduled by renderer                │
│ • Camera — Camera.Render() called by engine loop           │
│ • Light — auto-collected by SceneEnvironment               │
│ • AudioSource — auto-updated by AudioSubsystem             │
│                                                             │
│ Game code: GetComponent<MeshRenderer>().material = mat;    │
│ Renderer: Auto-schedules mesh in deferred/forward pass     │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ SUBSYSTEM REGISTRATION LAYER (Ordered Execution)           │
│ ────────────────────────────────────────────────────────────│
│ PlayerLoop (topologically sorted by dependency)            │
│ ├─ Initialization Phase                                    │
│ │  ├─ Input Init                                           │
│ │  └─ Rendering Init                                       │
│ │                                                           │
│ ├─ Frame Loop (every frame)                                │
│ │  ├─ PrePhysics (Input, Animation)                       │
│ │  ├─ Physics (Physics Step)                               │
│ │  ├─ PostPhysics (Physics Queries)                        │
│ │  ├─ Update (Gameplay)                                    │
│ │  ├─ PostUpdate (Camera, UI, Audio Sync)                 │
│ │  └─ Rendering (All subsystems complete)                 │
│ │                                                           │
│ └─ Shutdown Phase (reverse order)                          │
│                                                             │
│ Subsystems declare: GetDependencies(), GetTickGroup()      │
│ Engine: Auto-sorts + calls in correct order                │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ SETTINGS LAYER (Global Configuration)                      │
│ ────────────────────────────────────────────────────────────│
│ static Application.targetFrameRate = 60;                   │
│ static QualitySettings.antiAliasing = 4;  (MSAA 4x)        │
│ static RenderSettings.ambientLight = Color.white;          │
│ static GraphicsSettings.renderPipelineAsset = urpAsset;    │
│                                                             │
│ → Affects all subsystems without game code changes         │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ RENDER PIPELINE LAYER (Pluggable)                          │
│ ────────────────────────────────────────────────────────────│
│ interface: Render(ScriptableRenderContext, Camera[])       │
│                                                             │
│ Built-in Render Pipeline (BRP):                           │
│   ├─ SetPass (per light)                                   │
│   ├─ DrawRenderers (per light)                             │
│   └─ Composite + post                                      │
│                                                             │
│ Universal Render Pipeline (URP):                           │
│   ├─ G-Buffer Pass (if deferred)                           │
│   ├─ Forward Pass (objects lit directly)                   │
│   ├─ Lighting Pass (if deferred)                           │
│   └─ Post-processing                                       │
│                                                             │
│ High Definition Render Pipeline (HDRP):                    │
│   ├─ G-Buffer + Depth Prepass                             │
│   ├─ Ray-traced Reflections                                │
│   ├─ Volumetric Fog (compute)                              │
│   ├─ Lighting (clustered + screen-space)                   │
│   └─ Post-processing (bloom, color grading, etc.)          │
│                                                             │
│ Game code: unchanged when pipeline switches                │
│ (Only property on RenderPipelineAsset changes)             │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ GPU/RHI LAYER (Hidden)                                      │
│ ────────────────────────────────────────────────────────────│
│ • Device creation/destruction                              │
│ • Command list recording                                   │
│ • Texture/mesh upload (GPU memory)                         │
│ • Render state transitions (barriers, cache invalidation)  │
│ • Pipeline state objects (PSOs)                            │
│ • Descriptor tables (DX12/Vulkan)                          │
│                                                             │
│ → Completely hidden from game code                         │
└─────────────────────────────────────────────────────────────┘
```

---

## Key Patterns by Question

### 1. How does game code render without GPU knowledge?

**Answer**: Component-based + automatic scheduling.

```csharp
// Game code
var go = new GameObject("Enemy");
go.AddComponent<MeshRenderer>().material = enemyMat;
go.AddComponent<Transform>().position = spawnPos;

// Engine loop (automatic):
foreach (var renderer in FindAllMeshRenderers()) {
    culledList.Add(renderer);
}
currentPipeline.Render(culledList);  // No game code calls this
```

---

### 2. How does the engine find the right frame rate?

**Answer**: Subsystem registration + settings layer.

```
Application.targetFrameRate = 60 (C# static property)
    ↓
TimeManager subsystem reads this each frame
    ↓
Requests vsync or frame sleep from OS/RHI
    ↓
Next frame starts after time constraint met
```

---

### 3. How do you switch from deferred to forward rendering?

**Answer**: RenderPipelineAsset polymorphism (no game code change).

```csharp
// Change ONE line
GraphicsSettings.renderPipelineAsset = forwardAsset;

// Game code ZERO changes
// Camera.Render() still works
// MeshRenderer components unchanged
// Lighting still works (different algorithm, same interface)
```

---

### 4. How does a game developer know what to implement?

**Answer**: Minimal interface (MonoBehaviour lifecycle methods).

```csharp
public class PlayerController : MonoBehaviour {
    void Awake()   { /* initialize */ }
    void Start()   { /* first frame setup */ }
    void Update()  { /* per-frame logic */ }
    void OnDestroy() { /* cleanup */ }
}
```

No other methods needed. Engine calls the right ones at the right time.

---

### 5. How does scene loading work without blocking the main thread?

**Answer**: AsyncOperation with progress + deferred activation.

```csharp
var async = SceneManager.LoadSceneAsync("LevelOne");

// Every frame:
while (!async.isDone) {
    progressBar.value = async.progress;  // 0.0 → 0.9
    yield return null;  // Don't freeze
}

// When ready:
async.allowSceneActivation = true;  // 0.9 → 1.0
```

---

## What Game Code NEVER Sees

| Category | Examples | Why Hidden |
|----------|----------|-----------|
| **Device Management** | IDXGIFactory, ID3D12Device, VkInstance | Multi-platform abstraction |
| **Command Lists** | Recording, barriers, resource transitions | Implementation detail |
| **Texture/Mesh Upload** | Staging buffers, DMA, GPU memory layout | AssetManager + RHI layer |
| **Render Passes** | G-Buffer, lighting, shadow, post-processing | Pipeline-specific |
| **GPU State** | PSO caching, root signature slots, descriptors | RHI+Shader compiler |
| **Synchronization** | Fences, semaphores, CPU-GPU sync | FrameBuffer synchronization |
| **Shader Compilation** | DXC invocation, permutation variants | ShaderCompiler subsystem |
| **Memory Layout** | Constant buffer packing, vertex formats | Type system → shader reflection |

---

## UzEngine Correspondence

### Already Matches Unity ✓

| Unity Pattern | UzEngine Equivalent | Location |
|---------------|-------------------|----------|
| `IPlayerLoopSystem` | `ISubsystem` | `runtime/engine/app/subsystem.h` |
| Tick groups | `TickGroup` enum | `subsystem.h` |
| Dependency sorting | `GetDependencies()` | `SubsystemRegistry` |
| MonoBehaviour lifecycle | `OnFrame()` hook | `runtime/demos/common/demo_application.h` |
| Scene graph | EnTT ECS + Transform | `runtime/engine/ecs/` |

### Needs Implementation ✗

| Unity Pattern | UzEngine Needs | Task | Effort |
|---------------|----------------|------|--------|
| `Application.targetFrameRate` | `ApplicationSettings` singleton | P1 | 2d |
| `QualitySettings.*` | Settings propagation to shaders | P1 | 2d |
| `RenderPipelineAsset` | `IRenderPipeline` interface | P1 | 3d |
| `GraphicsSettings.renderPipelineAsset` | Runtime pipeline switching | P1 | 1d |
| `LoadSceneAsync()` | `AsyncSceneLoad` with progress | P2 | 2d |
| `SceneManager.LoadScene` | Async loader subsystem | P2 | 1d |

---

## Translation Examples

### Example 1: Setting Frame Rate

**Unity**:
```csharp
Application.targetFrameRate = 60;
```

**UzEngine (Target)**:
```cpp
uz::ApplicationSettings::Get().SetTargetFrameRate(60);

// Or from Lua:
uz.app.set_target_frame_rate(60)
```

---

### Example 2: Toggling Render Path

**Unity**:
```csharp
if (Input.GetKeyDown(KeyCode.F)) {
    GraphicsSettings.renderPipelineAsset = (current == urpDeferred)
        ? urpForward
        : urpDeferred;
}
// Game code unchanged! MeshRenderers still render.
```

**UzEngine (Target)**:
```cpp
if (input->WasKeyPressed(Key::F)) {
    auto& app = ApplicationSettings::Get();
    auto* current = app.GetActivePipeline();
    auto* next = (current == &deferredPipeline)
        ? &forwardPipeline
        : &deferredPipeline;
    app.SetActivePipeline(next);
}
// Game code unchanged! Components still render.
```

---

### Example 3: Async Scene Load

**Unity**:
```csharp
public IEnumerator LoadLevelWithProgress() {
    var async = SceneManager.LoadSceneAsync("LevelOne");
    while (!async.isDone) {
        progressBar.value = async.progress;
        yield return null;
    }
}
```

**UzEngine (Target)**:
```cpp
// C++
auto* load = sceneLoader->LoadSceneAsync("levels/level_one");
while (load->IsLoading()) {
    progressBar->SetProgress(load->progress);
}
sceneLoader->ActivateScene(load);

// Lua
local async = uz.scene.load_async("levels/level_one")
while uz.scene.is_loading(async) do
    set_progress_bar_value(uz.scene.get_load_progress(async))
    coroutine.yield()
end
uz.scene.activate(async)
```

---

## Implementation Priority

### Phase 1: Critical (8-12 days total)
1. ✓ Subsystem architecture (already done)
2. ✓ DemoApplication pattern (already done)
3. **ApplicationSettings singleton** (2-3 days)
4. **IRenderPipeline interface** (3-4 days)
5. **Documentation + tests** (1-2 days)

### Phase 2: High (2-3 days)
6. **Async scene loading** (2-3 days)
7. **Lua API parity** (included in Phase 1-2)

### Phase 3: Nice-to-Have (1-2 days)
8. Performance monitoring (stats subsystem)
9. VR subsystem integration
10. Platform-specific subsystems (Windows, macOS, Linux)

---

## Success Metrics

| Metric | Target | How to Test |
|--------|--------|-------------|
| Game developers can write `OnFrame()` without RHI types | 100% | Code review: no `ID3D12*`, no `ComPtr<>`, no `RHI*` in game code |
| Settings are discoverable | 100% | All settings in `ApplicationSettings::Get().*` |
| Render pipelines are pluggable | 100% | Can swap deferred ↔ forward at runtime with 1 line |
| Lua has feature parity | 100% | All `Application.*` available as `uz.app.*` |
| Scene loading shows progress | 100% | Loading screen displays `uz.scene.get_load_progress()` |

---

## References

### Full Analysis
- `unity-application-abstraction.md` — Complete deep dive (20+ pages)

### Implementation Roadmap
- `unity-application-abstraction-uzengine-roadmap.md` — Concrete tasks (12 days, 4 phases)

### Code Locations
- `runtime/engine/app/subsystem.h` — Already correct
- `runtime/demos/common/demo_application.h` — Reference implementation
- `runtime/engine/app/time_manager.h` — Fixed-timestep model

### External References
- [Unity PlayerLoop Customization](https://docs.unity3d.com/6000.3/Documentation/Manual/player-loop-customizing.html)
- [Unity RenderPipelineAsset](https://docs.unity3d.com/6000.3/Documentation/Manual/scriptable-render-pipeline-introduction.html)
- [Unity SceneManager.LoadSceneAsync](https://docs.unity3d.com/ScriptReference/SceneManagement.SceneManager.LoadSceneAsync.html)
- [Unity Application.targetFrameRate](https://docs.unity3d.com/ScriptReference/Application-targetFrameRate.html)
