<!-- doc-meta
type: quick-reference
title: UE5 Application Layer — Quick Reference
description: One-page summary of UE5 application architecture for UzEngine developers
tags: [ue5, application-layer, quick-ref]
status: complete
date: 2026-03-14
-->

# UE5 Application Layer — Quick Reference

## The Four Tiers (Know These)

```
┌──────────────────────────────────────┐
│   FEngineLoop (main.cpp)             │ Process/executable — PreInit/Init/Tick/Exit
├──────────────────────────────────────┤
│   UGameEngine (Engine)               │ World management, viewport creation
├──────────────────────────────────────┤
│   UGameInstance (Game facing)        │ Game state, players, sessions
├──────────────────────────────────────┤
│   UGameViewportClient (Viewport)     │ Input, rendering (hides D3D12)
└──────────────────────────────────────┘
```

**Game code only sees**: UGameInstance, UGameViewportClient, AActor/UComponent

**Game code NEVER touches**: RHI, GPU, render graphs, D3D12, swapchains

---

## Rendering Isolation (Most Important Pattern)

### The Delegation Chain (Never Skip Layers)
```
Game Code (SetTransform, SetMaterial)
  ↓
UGameEngine::RedrawViewports()
  ↓
UGameViewportClient::Draw()
  ↓
FSceneViewport::Draw()
  ↓
FRenderer::DrawScenes()
  ↓
RHI::IssueCommands() ← ONLY HERE is GPU touched
  ↓
ID3D12Device::ExecuteCommandLists()
```

**Critical rule**: Each layer only calls the NEXT layer. No shortcuts.

### What Each Layer Does
| Layer | Knows | Doesn't Know |
|-------|-------|-------------|
| Game Code | Actors, materials, transforms | Rendering, GPU, RHI |
| GameEngine | Worlds, viewports, Tick order | GPU details, RHI |
| ViewportClient | Input, canvas, screen layout | RHI details |
| SceneViewport | Window, swapchain, present timing | How GPU executes |
| Renderer | Scene graphs, render passes | RHI API details |
| RHI | Device, commands, GPU state | Game logic, rendering design |

---

## Scene Proxy Pattern (Thread Safety)

```cpp
// Game thread
actor->SetTransform(new_transform);

// Internally (happens automatically):
// 1. Actor stores transform
// 2. SceneProxy gets told "SetTransform"
// 3. Render thread updates its copy
// 4. NO RACE CONDITIONS because proxy is separate object
```

**Pattern**: Actor state lives on game thread, proxy lives on render thread. Unidirectional data flow: game → proxy.

---

## Tick Flow (Know the Order)

```
FEngineLoop::Tick()
  ├─ UGameEngine::Tick()
  │  ├─ For each world:
  │  │  ├─ UWorld::Tick()
  │  │  │  ├─ AActor::Tick() for all actors
  │  │  │  ├─ Physics simulation
  │  │  │  └─ AI, scripts
  │  │  ├─ Level streaming
  │  │  └─ Capture updates (sky, reflections)
  │  └─ FTickableGameObject::TickObjects()
  │
  └─ UGameEngine::RedrawViewports()   ← RENDERING happens here, after all ticks
     └─ GPU draws previous frame while CPU simulates next frame
```

**2-frame latency**: GPU frame N-1 while CPU does frame N.

---

## Module System (Plugins)

### IModuleInterface
```cpp
class MyGameModule : public IModuleInterface {
  void StartupModule() override {
    // Load dependencies, initialize
    FModuleManager::Get().LoadModuleChecked("HTTP");
    MyGame::Init();
  }

  void ShutdownModule() override {
    MyGame::Cleanup();
  }

  bool IsGameModule() const override { return true; }
};
```

**Pattern**: Game code is a **plugin/module**, not part of engine. Engine loads game via FModuleManager.

**Benefits**:
- Hot reload without restarting engine
- Multiple game instances in one process
- Game doesn't link engine; engine loads game

---

## World Context (Multi-World Support)

```cpp
struct FWorldContext {
  UWorld* ThisCurrentWorld;           // Actual world
  UGameInstance* OwningGameInstance;
  UGameViewportClient* GameViewport;
  EWorldType::Type WorldType;         // Game / Editor / PIE / Preview
};

class UEngine {
  TArray<FWorldContext> WorldList;    // Can have multiple worlds
};
```

**Why?**: PIE (editor + multiple game worlds), streaming (old map unloading while new loads), testing (isolated worlds).

**Game code accesses via API**:
```cpp
UWorld* world = GetWorld();  // Returns current context's world
```

---

## Lifecycle (4 Phases)

### Phase 1: PreInit
- Parse command line
- Load .ini config
- Initialize logging, module system
- **No UObjects yet, no GPU**

### Phase 2: Init
- Create RHI (D3D12 device, swapchain)
- Create UGameEngine
- Create first UWorld
- Load default map
- Create UGameInstance
- **Everything ready to tick**

### Phase 3: Tick (Continuous)
- World logic (actors, physics)
- RedrawViewports (GPU rendering)
- Loop until quit

### Phase 4: Exit
- UGameEngine::PreExit()
- Module shutdown (reverse order)
- RHI cleanup
- Process exit

---

## Abstraction Rules (What Game Code Can/Cannot Do)

### ✅ Game Code CAN:
```cpp
node->SetTransform(transform);
node->SetMaterial(material);
node->SetStaticMesh(mesh_asset);
scene->SpawnActor(position, rotation);
scene->GetActorsByTag("enemy");
```

### ❌ Game Code CANNOT:
```cpp
rhi_device->CreateBuffer(...);      // NO — bypasses engine
scene->GetGBuffer();                // NO — rendering internals
renderer->SetRenderTarget(...);     // NO — engine job
material->GetShaderResource(...);   // NO — GPU resource
viewport->Present();                // NO — engine manages this
```

---

## Anti-Patterns (What UzEngine Must Avoid)

1. **Direct RHI in game**: Game code touchingD3D12/GPU code
2. **Global mutable state**: Uncontrolled engine state changes
3. **Game holding GPU resources**: Game code lifetime ≠ GPU resource lifetime
4. **Rendering in Tick()**: Game logic and rendering mixed
5. **Engine depending on game**: Invert: game loads as module into engine
6. **Hard-coded renderer selection**: Use vtables, not if/else for DX12 vs Vulkan

---

## For UzEngine: 5-Step Adaptation

1. **Separate Loop from Engine**
   - Create `UzEngineLoop` (process lifecycle)
   - `UzEngine` is systems manager only

2. **Implement World Context**
   - Replace `g_world` with `UzWorldContext[]`
   - Game code: `GetWorld()` API, not context directly

3. **Rendering Delegation Chain**
   - No RHI in game code
   - Validate: game → viewport → renderer → RHI

4. **Scene Proxy Pattern**
   - Actor state (CPU) ↔ Proxy (render thread)
   - Prevent race conditions

5. **Module-Based Architecture**
   - Game code is loadable module
   - Engine doesn't hardcode game classes
   - Enables hot reload

---

## Key Files (UE5 5.6)

- **Main Loop**: `Launch/Public/LaunchEngineLoop.h`
- **Engine Systems**: `Engine/Classes/Engine/GameEngine.h`
- **Tick Code**: `Engine/Private/GameEngine.cpp:1735–1984`
- **Rendering Abstraction**: `Engine/Classes/Engine/GameViewportClient.h`
- **World Management**: `Engine/Classes/Engine/Engine.h:310–486`
- **Module Interface**: `Core/Public/Modules/ModuleInterface.h`
- **Module Manager**: `Core/Public/Modules/ModuleManager.h`

---

## Remember

> Game developers write **game logic**. Engine developers write **plumbing**.
>
> Game code should NEVER see rendering, RHI, GPU, or low-level system details.
>
> **The better the abstraction, the less game code knows about the engine.**
