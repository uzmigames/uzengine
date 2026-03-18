<!-- doc-meta
type: architecture-analysis
title: UE5 Application Layer Architecture
description: Deep analysis of UE5's FEngineLoop, UEngine, UGameEngine, and UGameInstance hierarchy that separates engine boilerplate from game logic
tags: [ue5, application-layer, engine-architecture, lifecycle, rendering-abstraction]
status: complete
date: 2026-03-14
-->

# UE5 Application Layer Architecture — Patterns for UzEngine

This document analyzes how UE5 structures its top-level application/engine classes to cleanly separate "engine plumbing" from "game code." The goal is to extract patterns UzEngine can adapt to hide all rendering complexity from game developers.

## Executive Summary

UE5 uses a **strict layering model** with four distinct tiers:

| Layer | Class | Responsibility |
|-------|-------|-----------------|
| **Process/Executable** | `FEngineLoop` (main.cpp) | Pre-init config, module loading, main loop tick |
| **Engine Systems** | `UGameEngine` → `UEngine` | World management, viewport creation, game tick orchestration |
| **Game Instance** | `UGameInstance` | Game-specific state (players, sessions, save games) |
| **Viewport Client** | `UGameViewportClient` | Input routing, rendering calls (completely hides D3D12/GPU from game code) |

**Key architectural principle**: Game code (blueprints, game mode, pawns) NEVER touches:
- RHI (D3D12, Vulkan)
- Render graph construction
- Viewport/swapchain management
- Buffer/texture creation (that's asset pipeline → component → proxy)
- Present/readback timing

All of that is hidden behind virtual interfaces and delegated to viewport/renderer systems that are **engine-only**, not game-visible.

---

## 1. The Four-Tier Architecture

### 1.1 FEngineLoop (Process/Executable Layer)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Launch\Public\LaunchEngineLoop.h` (lines 48-200)

FEngineLoop is the **main process-level loop**, not game code.

**Responsibilities**:
- `PreInit()` — Parse command line, detect editor mode, load .ini files, initialize logging
- `PreInitPreStartupScreen()` — Early startup (splash screen preparation)
- `PreInitPostStartupScreen()` — Late startup (modules loaded)
- `LoadPreInitModules()` / `LoadCoreModules()` / `LoadStartupModules()` — Module system bootstrap
- `Init()` — Create UGameEngine, call UGameEngine::Init()
- `Tick()` — Call UGameEngine::Tick() in a loop
- `Exit()` — Cleanup, call UGameEngine::PreExit()

**Code structure**:
```cpp
class FEngineLoop : public IEngineLoop {
  int32 PreInit(const TCHAR* CmdLine);
  int32 Init() override;
  void Tick() override;
  void Exit();
};
```

**Pattern for UzEngine**:
- Create `UzEngineLoop` class separate from `UzEngine` class
- FEngineLoop = process lifecycle only
- Game code NEVER calls FEngineLoop methods

**Why this separation?**:
- Allows same UEngine to be used in multiple contexts (standalone, editor, PIE, server)
- Clear boundary: executable setup vs. engine lifecycle
- Testability: can unit-test engine without main loop

---

### 1.2 UGameEngine (Engine Systems Layer)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\GameEngine.h` (lines 23-200)

Derives from `UEngine`. This is the **concrete game engine** that manages worlds, viewports, and rendering.

**Key members**:
```cpp
UCLASS(config=Engine, transient, MinimalAPI)
class UGameEngine : public UEngine {
  UPROPERTY(transient) TObjectPtr<UGameInstance> GameInstance;
  TWeakPtr<class SWindow> GameViewportWindow;
  TSharedPtr<class FSceneViewport> SceneViewport;
  TSharedPtr<class SViewport> GameViewportWidget;

  // Pure virtual from UEngine — must implement
  virtual void Tick(float DeltaSeconds, bool bIdleMode) override;
  virtual void RedrawViewports(bool bShouldPresent = true) override;
};
```

**Tick() implementation** (`GameEngine.cpp` lines 1735–1984):
```
1. Non-world tick:
   - Validate delta time
   - Flush server logs if dedicated
   - Update game viewports (layout, drop detail level)
   - Tick media framework (if active)

2. Per-world tick (in FWorldContext array):
   - TickWorldTravel() — handle map changes
   - World->Tick(LEVELTICK_All, DeltaSeconds) — tick all actors
   - UpdateSkyCaptureContents() / UpdateReflectionCaptureContents()
   - UpdateLevelStreaming()
   - ConditionalCommitMapChange()

3. Tickable objects:
   - FTickableGameObject::TickObjects() — non-world tickers

4. Viewport tick:
   - GameViewport->Tick(DeltaSeconds) — input/audio this frame
   - RedrawViewports() — called AFTER logic complete

5. Show game window (first frame only)
```

**RedrawViewports() implementation** (`GameEngine.cpp` lines 775–787):
```cpp
void UGameEngine::RedrawViewports(bool bShouldPresent) {
  if (GameViewport != NULL) {
    GameViewport->LayoutPlayers();  // Decide splitscreen layout
    if (GameViewport->Viewport != NULL) {
      GameViewport->Viewport->Draw(bShouldPresent);  // FSceneViewport::Draw()
    }
  }
}
```

**Key observation**: The engine NEVER calls DX12 code. It delegates to:
1. `UGameViewportClient::Draw()` — high-level viewport interface
2. `FSceneViewport::Draw()` — scene rendering (still RHI-agnostic)
3. Only the renderer knows about D3D12

**Pattern for UzEngine**:
```cpp
// uzengine.h
class UzEngine : public UzEngineBase {
  virtual void Tick(float delta_time, bool idle_mode);
  virtual void RedrawViewports(bool should_present);
};

// In Tick():
// 1. Tick all worlds (ECS iteration)
// 2. Gather scene state into SceneEnvironment snapshot
// 3. UpdateLevelStreaming()
// 4. Tick viewport

// In RedrawViewports():
// 1. GameViewport->Tick()
// 2. GameViewport->Draw() ← routes to renderer, never touches GPU code
```

**Critical design**: `UGameEngine::Tick()` and `RedrawViewports()` are **logically separated**:
- Tick = world simulation (actors, physics, logic)
- RedrawViewports = rendering (called after tick completes)
- This allows render thread to work on previous frame while game thread simulates next frame

---

### 1.3 UEngine (Base Engine Interface)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Engine.h` (lines 1–400)

The abstract base that defines the engine contract.

**Key responsibilities**:
- Manage `FWorldContext` array (can have multiple worlds: main game, editor, PIE 1, PIE 2, etc.)
- Subsystem collection (via `Subsystems/SubsystemCollection.h`)
- Audio device management
- Module system interface
- Console/command interface

**World context lookup**:
```cpp
struct FWorldContext {
  TEnumAsByte<EWorldType::Type> WorldType;  // Game / Editor / EditorPreview / PIE
  FName ContextHandle;
  TObjectPtr<UWorld> ThisCurrentWorld;      // Private — accessed via World()
  TObjectPtr<UGameInstance> OwningGameInstance;
  TObjectPtr<UGameViewportClient> GameViewport;
  TArray<TObjectPtr<ULevel>> LoadedLevelsForPendingMapChange;
  // ... per-context state for level streaming, travel, net drivers
};
```

Game code accesses worlds via context handles, not raw pointers. This allows the engine to manage lifetime without game code holding stale references.

**Pattern for UzEngine**:
- Adopt `WorldContext` concept in UzEngine
- Game code calls `uz_engine::get_world_for_context(handle)` not global `g_world`
- Allows testing, PIE, and multi-world scenarios without code changes

---

### 1.4 UGameInstance (Game-Facing API)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\GameInstance.h` (lines 143–200)

This is the **game-facing entry point** for game-specific state.

**Minimal required interface for game code**:
```cpp
UCLASS(config=Game, transient, BlueprintType, Blueprintable)
class UGameInstance : public UObject, public FExec {
public:
  struct FWorldContext* WorldContext;  // Points to the engine's context

  UPROPERTY()
  TArray<TObjectPtr<ULocalPlayer>> LocalPlayers;

  UPROPERTY()
  TObjectPtr<class UOnlineSession> OnlineSession;

  UPROPERTY()
  TArray<TObjectPtr<UObject>> ReferencedObjects;  // Keep alive
};
```

**What UGameInstance does NOT own**:
- The world (owned by FWorldContext)
- The viewport (owned by UGameEngine)
- The renderer (owned by engine systems)
- Networking drivers (owned by FWorldContext)

**What UGameInstance owns**:
- Game-specific players and controllers
- Networking session state
- Save game state
- Audio device handle
- Analytics
- Subsystems (via FSubsystemCollection)

**Pattern for UzEngine**:
```cpp
class UzGameInstance : public UzObject {
  // Game-specific state only
  std::vector<UzLocalPlayer*> local_players;
  std::unique_ptr<UzOnlineSession> online_session;

  // DON'T add: renderer, viewport, world context, audio system
  // Those belong to engine layer
};
```

**Key rule**: Game blueprints/code subclass UGameInstance and override:
```cpp
virtual void Init() { /* load game state */ }
virtual void Shutdown() { /* cleanup */ }
```

They do NOT implement game loop or rendering — that's the engine's job.

---

## 2. Rendering Abstraction Hierarchy

### 2.1 How Rendering is Hidden: The Delegation Chain

Game code → UGameEngine → UGameViewportClient → FSceneViewport → Renderer → RHI

**Each layer only knows the NEXT layer's interface, never skips layers.**

#### 2.1.1 UGameViewportClient (Viewport Abstraction)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\GameViewportClient.h`

```cpp
UCLASS(Within=Engine, transient, config=Engine)
class UGameViewportClient : public UScriptViewportClient, public FExec {
public:
  // Input routing (game code never sees raw messages)
  virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
  virtual bool InputAxis(const FInputKeyEventArgs& EventArgs) override;
  virtual bool InputTouch(...) override;
  virtual bool InputMotion(...) override;

  // Rendering — completely hides GPU implementation
  virtual void Draw(FViewport* Viewport, FCanvas* SceneCanvas) override;

  // Game state queries (used by renderer to get what to render)
  virtual void FinalizeViews(FSceneViewFamily*, const TMap<ULocalPlayer*, FSceneView*>&);

  UPROPERTY(transient)
  TObjectPtr<class UConsole> ViewportConsole;

  UPROPERTY()
  TObjectPtr<UGameInstance> GameInstance;
};
```

**Key pattern**: `Draw()` receives a `FCanvas*` (abstract rendering command buffer), NOT an `ID3D12CommandList*`.

Game code NEVER calls Draw(). It's called by:
```cpp
// GameEngine.cpp line 784
GameViewport->Viewport->Draw(bShouldPresent);
```

This indirection allows the viewport implementation to:
- Swap renderers (DX12 ↔ Vulkan) without game code knowing
- Change rendering pipeline (forward ↔ deferred) without code changes
- Mock rendering in tests
- Render to different targets (screen, texture, network stream)

#### 2.1.2 FSceneViewport (Rendering Interface)

**Responsibility**: Bridge between game logic and rendering system.

Key methods (called by Draw()):
```cpp
FSceneViewport {
  void Draw(bool bShouldPresent);  // Calls renderer
  void BeginRenderingViewport(...);
  void EndRenderingViewport(bool bPresent);
};
```

**Important**: FSceneViewport is NOT the renderer itself. It's a thin adapter that:
1. Collects visible actors (via scene capture/frustum culling)
2. Calls the renderer with "draw this list of actors"
3. Handles window resize, vsync, present
4. Routes to platform-specific swapchain

**Pattern for UzEngine**:
```cpp
class UzSceneViewport {
  // Interface only
  virtual void Draw(bool should_present) = 0;
  virtual void BeginFrame() = 0;
  virtual void EndFrame(bool should_present) = 0;
};

class UzSceneViewportDX12 : public UzSceneViewport {
  ComPtr<IDXGISwapChain> swapchain;
  ComPtr<ID3D12CommandQueue> command_queue;

  void Draw(bool should_present) override {
    // Only DX12-specific code here
  }
};
```

#### 2.1.3 Draw() Call Graph

```
FEngineLoop::Tick()
  → UGameEngine::Tick()
      → UWorld::Tick()
          → ULocalPlayer updates controller
          → AActor::Tick() for all actors
          → Physics simulation
      ← (World tick complete, all physics/logic done)
  → UGameEngine::RedrawViewports()
      → UGameViewportClient::Tick() [input buffering]
      → UGameViewportClient::Draw()
          → FSceneViewport::Draw(bShouldPresent)
              → FRenderer::DrawScenes()
                  → BuildRenderingCommands()
                  → FlushRenderingCommands()
                      [GPU work happens here]
          → Present() [DX12-specific]
```

**Critical observation**: The game logic tick and render tick are **completely separate phases**. Game code can only affect rendering indirectly via:
- Setting actor transforms
- Enabling/disabling components
- Changing material parameters
- Updating particle emitters

**Game code CANNOT**:
- Issue GPU commands directly
- Create render targets
- Compile shaders dynamically
- Access vertex buffers

This is the core pattern that keeps rendering isolated.

---

### 2.2 Rendering Abstraction Levels in UE5

UE5 has **three abstraction layers** for rendering:

1. **Scene Proxy Layer** — Actor → RenderProxy (CPU mirroring of render state)
   - Game thread updates AActor transform
   - Actor::CreateRenderState_Concurrent() creates FPrimitiveSceneProxy
   - Proxy is a lightweight copy of what renderer needs to know
   - Proxy lives on render thread, never accessed by game thread after creation

2. **Scene → View Layer** — RenderProxy → View (what each camera sees)
   - FSceneViewFamily collects all FPrimitiveSceneProxy
   - FSceneView describes camera parameters (FOV, near/far, matrices)
   - No game objects here — just rendering data

3. **View → RHI Layer** — View → RHI commands (actual GPU work)
   - FRenderer walks FSceneView, issues RHI calls
   - RHI vtable (RHICreateBuffer, RHIDispatch, etc.) is platform-specific
   - Game code NEVER sees this layer

**Pattern for UzEngine**:
```cpp
// Game code can do this (SceneProxy pattern)
node->transform = glm::mat4(1.0f);  // Game thread updates CPU state
// Proxy automatically mirrored to render thread

// Game code CANNOT do this
render_device->CreateBuffer(...);  // FORBIDDEN — hides implementation
scene->GetGeometryBuffer()->UpdateVertex(...);  // FORBIDDEN — bypasses proxy
```

---

## 3. Module System (Plugin Architecture)

### 3.1 IModuleInterface Pattern

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Modules\ModuleInterface.h`

```cpp
class IModuleInterface {
public:
  virtual void StartupModule() {
    // Called after module DLL loaded, before game code runs
    // Load dependencies here: FModuleManager::Get().LoadModuleChecked("HTTP");
  }

  virtual void PreUnloadCallback() { }
  virtual void PostLoadCallback() { }  // Hot reload

  virtual void ShutdownModule() {
    // Called during shutdown, dependencies still available
    // Reverse order of StartupModule()
  }

  virtual bool SupportsDynamicReloading() { return true; }
  virtual bool SupportsAutomaticShutdown() { return true; }
  virtual bool IsGameModule() const { return false; }
};
```

**Key principle**: Modules are **dependency-ordered**. A module that depends on HTTP loads HTTP first, so both are available in StartupModule(). Shutdown is reverse-ordered, so dependencies are still loaded.

### 3.2 FModuleManager Singleton

```cpp
FModuleManager& FModuleManager::Get() { return GetModuleManagerSingleton(); }
IModuleInterface* ModulePtr = FModuleManager::Get().LoadModuleChecked("Audio");
```

**Pattern for UzEngine**:
```cpp
// uzengine/module_system.h
class UzModuleManager {
  static UzModuleManager& Get();
  UzModule* LoadModule(const char* name);
  void UnloadModule(const char* name);
};

// In a game DLL's entry point:
extern "C" {
  UZ_EXPORT void uz_module_startup(UzModuleContext* ctx) {
    // Load dependencies
    uz::LoadModule("uz_physics");
    uz::LoadModule("uz_audio");
    // Initialize game logic
  }

  UZ_EXPORT void uz_module_shutdown(UzModuleContext* ctx) {
    // Cleanup (reverse order automatic)
  }
}
```

**Why this matters**: Allows game code as a plugin, not a monolithic executable. Enables:
- Hot reload of game code without restarting engine
- Multiple game instances in same engine process
- Game code doesn't link engine code; engine loads game as plugin

---

## 4. Lifecycle: PreInit → Init → Tick → Exit

### 4.1 Phase 1: PreInit

**FEngineLoop::PreInit()** runs BEFORE UGameEngine exists.

```cpp
int32 FEngineLoop::PreInit(const TCHAR* CmdLine) {
  // 1. Parse command line
  // 2. Load .ini files (DefaultEngine.ini, DefaultGame.ini)
  // 3. Initialize logging system
  // 4. Detect mode (editor, game, commandlet)
  // 5. Create module manager
  // 6. Load core modules (Core, CoreUObject, Engine)
  // 7. Create Slate renderer
  // WORLD DOESN'T EXIST YET
}
```

**What can't happen in PreInit**:
- No UObjects yet (UGameEngine doesn't exist)
- No rendering context
- No audio device
- No input system

**Pattern for UzEngine**:
```cpp
int32 UzEngineLoop::PreInit(const char* cmd_line) {
  // 1. Parse command line
  // 2. Load configs
  // 3. Create RHI (but don't init GPU yet — just detect device)
  // 4. Initialize logging
  // 5. Module system setup
  return 0;  // success
}
```

### 4.2 Phase 2: Init

**FEngineLoop::Init()** → calls `UGameEngine::Init()`

```cpp
int32 FEngineLoop::Init() override {
  // Create the UGameEngine (singleton)
  GEngine = NewObject<UGameEngine>();

  // Call into engine
  GEngine->Init(this);
}

// In UGameEngine::Init()
void UGameEngine::Init(IEngineLoop* InEngineLoop) {
  Super::Init(InEngineLoop);  // UEngine::Init()

  // Initialize audio device
  // Create game viewport
  // Load default map
  // Create game instance
}
```

**What happens in Init**:
1. RHI fully initialized (D3D12 device, swapchain)
2. Renderer initialized
3. Default map loaded
4. Game instance created
5. First world created

**Pattern for UzEngine**:
```cpp
int32 UzEngineLoop::Init() {
  g_engine = new UzEngine();
  g_engine->Init(this);

  // At this point:
  // - GPU device exists
  // - Default map loaded
  // - Game instance ready
  // - Ready to tick
}
```

### 4.3 Phase 3: Tick (Continuous)

FEngineLoop calls UGameEngine::Tick() every frame in main loop.

**Tick responsibilities**:
1. **World logic** — Actor::Tick(), physics, AI
2. **Input processing** — Route input to player controllers
3. **Rendering** — RedrawViewports()

**Rendering timing is CRITICAL**:
```
Frame N-1 (GPU rendering) —————— [happens on GPU while CPU does frame N logic]
Frame N (CPU logic) ———————————— [game thread does simulation]
Frame N (CPU rendering prep) ——— [setup GPU commands for frame N]
Frame N+1 (GPU rendering) —————— [GPU executes frame N commands]
```

UE5 uses a **2-frame latency**: game thread is always 1 frame ahead of GPU.

### 4.4 Phase 4: Exit

Called when game quits:

```cpp
// FEngineLoop::Exit()
GEngine->PreExit();  // UGameEngine/UEngine cleanup
// Unload modules (reverse order)
// Shutdown RHI
```

---

## 5. World Context Architecture

UE5 manages multiple worlds via `FWorldContext` array, not a global `GWorld`.

```cpp
class UEngine {
  TArray<FWorldContext> WorldList;  // Can have multiple worlds
};

struct FWorldContext {
  UWorld* ThisCurrentWorld;         // Current world for this context
  UGameInstance* OwningGameInstance;
  UGameViewportClient* GameViewport;
  EWorldType::Type WorldType;       // Game / Editor / EditorPreview / PIE
};
```

**Why this design?**:
1. **PIE (Play-in-Editor)** — Editor world + multiple PIE worlds can coexist
2. **Streaming** — Loading new maps while old one unloads
3. **Testing** — Create isolated worlds for unit tests
4. **Multi-instance** — Server world + multiple client worlds in one process

**Game code rarely touches this directly** — instead accesses via API:
```cpp
UWorld* world = GetWorld();  // Returns context's current world
```

**Pattern for UzEngine**:
```cpp
struct UzWorldContext {
  Handle<UzWorld> world;
  Handle<UzGameInstance> game_instance;
  Handle<UzViewportClient> viewport;
  WorldType type;  // Game / Editor / Simulation
};

class UzEngine {
  std::vector<UzWorldContext> world_list;

  Handle<UzWorld> GetWorld() {
    return world_list[current_context_idx].world;
  }
};
```

---

## 6. Key Design Patterns UzEngine Should Adopt

### 6.1 Separate Loop from Engine

**Don't do**:
```cpp
class MyEngine {
  void MainLoop() { /* entire loop */ }
};
```

**Do**:
```cpp
class UzEngineLoop {
  int PreInit();
  int Init();
  void Tick();
  void Exit();
};

class UzEngine {
  // Receives calls from UzEngineLoop, never manages loop
  virtual void Tick(float delta);
  virtual void RedrawViewports(bool should_present);
};
```

**Why**: Allows engine to be tested without main loop. Allows engine to be embedded in other apps (editor, tests, game).

### 6.2 Rendering Delegation Chain

**Never skip layers**:
```cpp
// CORRECT: Each layer only calls the next
GameEngine::RedrawViewports()
  → GameViewportClient::Draw()
    → SceneViewport::Draw()
      → Renderer::DrawScenes()
        → RHI::IssueCommands()

// WRONG: Bypassing layers
GameEngine::Tick()
  → RHICreateBuffer()  // NO! Engine shouldn't touch RHI
```

### 6.3 Scene Proxy Pattern for Rendering State

**Don't mirror game objects to render thread directly**:
```cpp
// WRONG
void Actor::SetTransform(mat4 T) {
  transform = T;
  // Render thread reads 'transform' → race condition!
}

// CORRECT (UE5 pattern)
void Actor::SetTransform(mat4 T) {
  transform = T;  // Game thread only
  if (bIsRendered) {
    SceneProxy->SetTransform(T);  // Safe copy for render thread
  }
}
```

### 6.4 Module-Based Architecture

**Don't hardcode game code into engine**:
```cpp
// WRONG
class Engine {
  void InitGame() {
    MyGame::Init();  // Engine depends on game code!
  }
};

// CORRECT
class Engine {
  void LoadGameModule(const char* name) {
    auto module = ModuleManager::LoadModule(name);
    module->StartupModule();
  }
};
```

### 6.5 Abstraction Over Platform/Graphics

**Don't expose RHI to game code**:
```cpp
// WRONG
class Renderer {
  ID3D12Device* GetDevice() { return device; }  // Game code can abuse this
};

// CORRECT
class Renderer {
  // Public interface hides D3D12
  void SetShaderParameter(const char* name, float value);
  void SetMaterialParameter(Material*, const char* name, Texture*);
  // D3D12 only visible internally
};
```

---

## 7. UzEngine Adaptation Roadmap

### Phase 1: Separate Loop from Engine
- Create `UzEngineLoop` class (PreInit, Init, Tick, Exit)
- Move main loop logic from `UzEngine` to `UzEngineLoop`
- `UzEngine` becomes a pure systems manager, not a loop manager

### Phase 2: Implement World Context Pattern
- Replace global `g_world` with `FWorldContext`-style per-context worlds
- Game code calls `GetWorld()` not `g_world`
- Add PIE/multi-world support

### Phase 3: Rendering Abstraction
- Ensure `RedrawViewports()` never touches GPU code
- All GPU operations go through viewport client interface
- Validate no RHI calls in game code

### Phase 4: Game Instance Pattern
- Separate game-specific state (players, sessions) into `UzGameInstance`
- Engine owns rendering/viewport, game instance owns game state
- Clear boundary prevents circular dependencies

### Phase 5: Module System
- Game code becomes a loadable module, not part of engine
- Hot reload support via module manager
- Multi-instance game worlds in same engine

---

## 8. Anti-Patterns UE5 Avoids (and UzEngine Should Avoid Too)

### Don't do this in UzEngine:

1. **Direct RHI calls in game code**
   - ❌ `actor->mesh->buffer = rhi_create_buffer(...)`
   - ✅ `actor->SetStaticMesh(asset)` ← lets engine handle GPU creation

2. **Global mutable engine state**
   - ❌ `extern UzEngine* g_engine;` then modify it
   - ✅ `UzEngine::Get().Method()` ← const where possible

3. **Game code holding render resources**
   - ❌ `struct Actor { Handle<RenderTarget> rt; }`
   - ✅ `struct Actor { MaterialId mat_id; }` ← handle, not resource

4. **Rendering in Tick**
   - ❌ `void Actor::Tick() { Renderer::DrawTriangle(...); }`
   - ✅ `void Actor::Tick() { transform = ....; }` ← let proxy handle rendering

5. **Engine code using game classes**
   - ❌ `void Engine::Init() { MyGameMode::Init(); }`
   - ✅ `void Engine::Init() { LoadGameModule(); }` ← inverted dependency

6. **Hard-coded renderer type**
   - ❌ `if (DX12) { ... } else if (Vulkan) { ... }`
   - ✅ Virtual interface `Renderer::BeginFrame()` ← vtable at RHI level

---

## Summary Table: UE5 → UzEngine Mapping

| UE5 Concept | File Path | UzEngine Equivalent |
|-------------|-----------|-------------------|
| FEngineLoop | Launch/Public/LaunchEngineLoop.h | UzEngineLoop |
| UGameEngine | Engine/Classes/Engine/GameEngine.h | UzEngine |
| UEngine | Engine/Classes/Engine/Engine.h | UzEngineBase |
| FWorldContext | Engine/Classes/Engine/Engine.h | UzWorldContext |
| UGameInstance | Engine/Classes/GameInstance.h | UzGameInstance |
| UGameViewportClient | Engine/Classes/Engine/GameViewportClient.h | UzGameViewportClient |
| FSceneViewport | Renderer/Public/FSceneViewport.h | UzSceneViewport |
| IModuleInterface | Core/Public/Modules/ModuleInterface.h | UzModuleInterface |
| FModuleManager | Core/Public/Modules/ModuleManager.h | UzModuleManager |
| AActor/UActorComponent | Engine/Classes/GameFramework/Actor.h | UzNode/UzComponent |
| FPrimitiveSceneProxy | Engine/Public/PrimitiveSceneProxy.h | UzSceneProxy |

---

## References

- **FEngineLoop**: `W:\UE_5.6_Source\Engine\Source\Runtime\Launch\Public\LaunchEngineLoop.h` (200 lines)
- **UGameEngine::Tick()**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\GameEngine.cpp:1735–1984`
- **UGameEngine::RedrawViewports()**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\GameEngine.cpp:775–787`
- **UGameViewportClient**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\GameViewportClient.h`
- **IModuleInterface**: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Modules\ModuleInterface.h`
- **FWorldContext**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Engine.h:310–486`
