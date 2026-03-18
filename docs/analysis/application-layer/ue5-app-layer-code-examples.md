<!-- doc-meta
type: architecture-analysis
title: UE5 Application Layer — Code Examples
description: Real code snippets from UE5 5.6 showing rendering abstraction, lifecycle, and module patterns
tags: [ue5, code-examples, rendering, application-layer]
status: complete
date: 2026-03-14
-->

# UE5 Application Layer — Code Examples

This document provides real code snippets from UE5 5.6 showing how patterns are implemented.

---

## Example 1: Rendering Isolation via Delegation Chain

### FEngineLoop::Tick() → Rendering
From `Launch/Public/LaunchEngineLoop.h` and `Engine/Private/GameEngine.cpp`

```cpp
// GAME LOOP (in main loop)
int32 FEngineLoop::Tick() {
  // 1. Update world time
  FApp::UpdateDeltaTime();

  // 2. Call engine tick
  GEngine->Tick(FApp::GetDeltaTime(), false);

  // Note: Game code NEVER called here
  // Only engine systems
}

// ENGINE SYSTEMS TICK
void UGameEngine::Tick(float DeltaSeconds, bool bIdleMode) {
  // First: all world logic
  for (int32 WorldIdx = 0; WorldIdx < WorldList.Num(); ++WorldIdx) {
    FWorldContext &Context = WorldList[WorldIdx];
    if (Context.World() == NULL || !Context.World()->ShouldTick()) continue;

    GWorld = Context.World();
    Context.World()->Tick(LEVELTICK_All, DeltaSeconds);  // AActor::Tick() called here
  }

  // Second: tickable objects
  FTickableGameObject::TickObjects(nullptr, LEVELTICK_All, false, DeltaSeconds);

  // Third: input/audio
  if (GameViewport != NULL && !bIdleMode) {
    GameViewport->Tick(DeltaSeconds);  // Input buffering
  }

  // AFTER all game logic, rendering
  // Note: Different from Tick! Happens in RedrawViewports next
}

// VIEWPORT RENDERING (happens AFTER Tick completes)
void UGameEngine::RedrawViewports(bool bShouldPresent) {
  if (GameViewport != NULL) {
    GameViewport->LayoutPlayers();  // Decide splitscreen layout
    if (GameViewport->Viewport != NULL) {
      GameViewport->Viewport->Draw(bShouldPresent);  // FSceneViewport::Draw()
    }
  }
}
```

**Key insight**: `RedrawViewports()` is called AFTER Tick(), so game logic is complete before any GPU work.

### Game Code Never Sees Rendering

Game code in Actor::Tick():
```cpp
void ACharacter::Tick(float DeltaTime) {
  // Game code — CPU only
  Velocity += Acceleration * DeltaTime;
  Location += Velocity * DeltaTime;

  // Component automatically handles rendering
  // This calls:
  // 1. CharacterMesh->SetWorldLocation(Location)
  // 2. Which invalidates SceneProxy
  // 3. Proxy updates next frame
  // But game code NEVER sees any of this GPU stuff
}
```

Game code CANNOT write:
```cpp
void ACharacter::Tick(float DeltaTime) {
  // FORBIDDEN — game code cannot touch rendering
  GetMesh()->GetRenderProxy()->SetTransform(...);  // NO
  FRenderer::DrawActorDEBUG(this);                  // NO
  GetViewport()->Presenter.Present();               // NO
}
```

---

## Example 2: Scene Proxy Pattern (Thread-Safe Rendering State)

From `Engine/Public/PrimitiveSceneProxy.h`

### How UE5 Keeps Game Thread and Render Thread Safe

**Game thread (Actor)**:
```cpp
void AStaticMeshActor::SetStaticMesh(class UStaticMesh* InMesh) {
  StaticMeshComponent->SetStaticMesh(InMesh);
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InMesh) {
  StaticMesh = InMesh;  // Store on game thread

  if (bIsAttached) {
    // Mark proxy as needing update
    MarkRenderStateDirty();

    // Create new proxy for render thread
    FSceneInterface* Scene = GetWorld()->Scene;
    if (Scene) {
      // Queue command for render thread
      ENQUEUE_RENDER_COMMAND(FUpdateProxy)(
        [this, Scene](FRHICommandListImmediate& RHICmdList) {
          // Render thread: safe to create/update proxy here
          FPrimitiveSceneProxy* NewProxy = CreateSceneProxy();
          // ...
        });
    }
  }
}
```

**Render thread (FPrimitiveSceneProxy)**:
```cpp
class FStaticMeshSceneProxy : public FPrimitiveSceneProxy {
  const FStaticMeshRenderData* StaticMeshRenderData;  // Owned by render thread
  FMatrix LocalToWorld;                               // Copy for render thread

  // Render thread can safely read these — game thread never touches them
  virtual void GetDynamicMeshElements(
    const TArray<const FSceneView*>& Views,
    const FSceneViewFamily& ViewFamily,
    uint32 VisibilityMap,
    FMeshElementCollector& Collector) const override {
    // Render thread work — uses proxy data, not actor data
    Collector.AddMesh(...);
  }
};
```

**Why this works**:
1. Game thread updates Actor::Transform
2. Game thread marks proxy dirty
3. Render thread sees dirty flag
4. Render thread updates its own copy of data
5. **No race conditions** because proxy is a separate object

---

## Example 3: Module System (Game as Plugin)

From `Core/Public/Modules/ModuleInterface.h`

### Engine-Side (Does NOT Know About Game)
```cpp
// engine.cpp
class FEngineLoop {
  int32 FEngineLoop::Init() {
    // 1. Load core modules
    FModuleManager::Get().LoadModuleChecked(TEXT("Core"));
    FModuleManager::Get().LoadModuleChecked(TEXT("CoreUObject"));
    FModuleManager::Get().LoadModuleChecked(TEXT("Engine"));

    // 2. Create engine
    GEngine = NewObject<UGameEngine>();

    // 3. Load GAME module (engine doesn't know what it is!)
    FName GameModuleName = FApp::GetGameName();
    FModuleManager::Get().LoadModuleChecked(GameModuleName);
    // ^ This calls MyGame::StartupModule()

    return 0;
  }
};
```

### Game-Side (Game Module)
```cpp
// game_module.cpp
class FMyGameModule : public IModuleInterface {
public:
  virtual void StartupModule() override {
    UE_LOG(LogGame, Display, TEXT("Game Module Starting"));

    // Load dependencies
    FModuleManager::Get().LoadModuleChecked(TEXT("HTTP"));
    FModuleManager::Get().LoadModuleChecked(TEXT("WebAPI"));

    // Initialize game systems
    FGameDelegates::Get().OnEnginePreExit.AddRaw(this, &FMyGameModule::OnEngineExit);
  }

  virtual void ShutdownModule() override {
    UE_LOG(LogGame, Display, TEXT("Game Module Shutting Down"));
    // Cleanup (happens automatically in reverse order)
  }

  virtual bool IsGameModule() const override {
    return true;  // This is a game module, not engine
  }

private:
  void OnEngineExit() {
    // Cleanup during engine shutdown
  }
};

// Export module instance
IMPLEMENT_MODULE(FMyGameModule, MyGame);
```

**Key insight**: Engine NEVER calls `MyGame::Init()` directly. Game code is invoked ONLY via module interface. Engine is decoupled from game.

---

## Example 4: World Context (Multi-World Support)

From `Engine/Classes/Engine/Engine.h` lines 310–486

### Defining World Context
```cpp
struct FWorldContext {
  TEnumAsByte<EWorldType::Type> WorldType;  // Game / Editor / EditorPreview / PIE

  FName ContextHandle;                       // Unique ID for this context

  TObjectPtr<UWorld> ThisCurrentWorld;       // The actual world

  TObjectPtr<UGameInstance> OwningGameInstance;  // Game-specific state

  TObjectPtr<UGameViewportClient> GameViewport;  // For rendering

  // Per-context level streaming state
  TArray<TObjectPtr<ULevel>> LoadedLevelsForPendingMapChange;
  FLevelStreamingStatus PendingLevelStreamingStatusUpdates;

  // Per-context network drivers
  TArray<FNamedNetDriver> ActiveNetDrivers;

  // Set current world, update external references
  void SetCurrentWorld(UWorld* World) {
    ThisCurrentWorld = World;
    // Update any game code that holds references to this world
    for (UWorld*& ExternalRef : ExternalReferences) {
      ExternalRef = World;  // Keep external pointers in sync
    }
  }
};
```

### Using Multiple Worlds
```cpp
class UEngine {
  TArray<FWorldContext> WorldList;  // Multiple worlds possible

public:
  FWorldContext& GetWorldContextFromHandle(FName Handle) {
    for (FWorldContext& Context : WorldList) {
      if (Context.ContextHandle == Handle) {
        return Context;
      }
    }
    check(false);  // Not found
    return WorldList[0];
  }
};

// Game code accesses via API, not raw context
void AMyActor::BeginPlay() {
  UWorld* MyWorld = GetWorld();  // Returns context's world
  // Game code never knows about FWorldContext
}
```

### PIE Example (Editor + Multiple Game Worlds)
```cpp
// Editor has two contexts:
// 1. Editor world context (for editing)
WorldList[0].WorldType = EWorldType::Editor;
WorldList[0].ThisCurrentWorld = EditorWorld;

// 2. PIE world context #1 (first game window)
WorldList[1].WorldType = EWorldType::PIE;
WorldList[1].ThisCurrentWorld = PIEWorld1;
WorldList[1].OwningGameInstance = PIEGameInstance1;

// 3. PIE world context #2 (second game window)
WorldList[2].WorldType = EWorldType::PIE;
WorldList[2].ThisCurrentWorld = PIEWorld2;
WorldList[2].OwningGameInstance = PIEGameInstance2;

// During tick:
for (FWorldContext& Context : WorldList) {
  Context.World()->Tick(...);  // Each world ticks independently
}
```

---

## Example 5: Game Instance (Game-Specific State Only)

From `Engine/Classes/GameInstance.h`

### What Game Instance Owns
```cpp
UCLASS(config=Game, transient, BlueprintType, Blueprintable)
class UGameInstance : public UObject {
public:
  // Game-specific state
  UPROPERTY()
  TArray<TObjectPtr<ULocalPlayer>> LocalPlayers;  // Players in this game instance

  UPROPERTY()
  TObjectPtr<class UOnlineSession> OnlineSession;  // Multiplayer session

  UPROPERTY()
  TArray<TObjectPtr<UObject>> ReferencedObjects;  // Objects to keep alive

protected:
  struct FWorldContext* WorldContext;  // Points to engine's context (not owned)

  // Subsystems for game logic
  FSubsystemCollection<UGameInstanceSubsystem> SubsystemCollection;
};
```

### What Game Instance Does NOT Own
```cpp
// These are owned by engine, NOT by game instance
// (Even though game instance may have pointers to them)

struct FWorldContext {
  UWorld* ThisCurrentWorld;              // Engine owns world
  UGameViewportClient* GameViewport;    // Engine owns viewport
  TArray<FNamedNetDriver> ActiveNetDrivers;  // Engine manages netdrivers
};

class UEngine {
  TArray<FWorldContext> WorldList;       // Engine owns contexts
  TSharedPtr<FSceneViewport> SceneViewport;  // Engine owns viewport
};
```

### Game Code Subclasses Game Instance
```cpp
// game_instance.h
UCLASS()
class AMyGameInstance : public UGameInstance {
public:
  virtual void Init() override;
  virtual void Shutdown() override;

  // Game-specific subsystems
  UPROPERTY()
  class UMySaveGameManager* SaveGameManager;

  UPROPERTY()
  class UMyAudioManager* AudioManager;

  // Game state
  int32 CurrentLevel = 0;
  TArray<class APlayer*> ActivePlayers;

private:
  virtual void Init() override {
    Super::Init();

    // Game initialization
    SaveGameManager = NewObject<UMySaveGameManager>();
    AudioManager = NewObject<UMyAudioManager>();

    LoadGameState();
  }

  virtual void Shutdown() override {
    SaveGameState();
    Super::Shutdown();
  }
};
```

**Key insight**: Game instance manages GAME STATE, never rendering/viewport/world lifetime.

---

## Example 6: Lifecycle Flow (All 4 Phases)

### Phase 1: PreInit
```cpp
int32 FEngineLoop::PreInit(const TCHAR* CmdLine) {
  // Parse command line
  FParse::Param(FCommandLine::Get(), TEXT("editor"));
  FParse::Value(FCommandLine::Get(), TEXT("-game="), GameName);

  // Load .ini config
  GConfig->LoadFile(TEXT("Engine/Binaries/Win64/DefaultEngine.ini"));

  // Initialize logging
  FOutputDeviceFile* LogFile = new FOutputDeviceFile(*LogPath, true);
  GLog->AddOutputDevice(LogFile);

  // Load core modules
  FModuleManager::Get().LoadModuleChecked(TEXT("Core"));
  FModuleManager::Get().LoadModuleChecked(TEXT("CoreUObject"));
  FModuleManager::Get().LoadModuleChecked(TEXT("Engine"));

  // At this point: NO GPU, NO WORLD, NO GAMEINSTANCE
  return 0;
}
```

### Phase 2: Init
```cpp
int32 FEngineLoop::Init() override {
  // Create RHI context
  RHICreateCommandQueue();
  RHICreateSwapChain(Width, Height);

  // Create engine
  GEngine = NewObject<UGameEngine>();

  // Initialize engine
  GEngine->Init(this);

  // Inside UGameEngine::Init():
  // 1. Create first world
  UWorld* World = NewObject<UWorld>();

  // 2. Create game instance
  UGameInstance* GameInstance = NewObject<UGameInstance>();

  // 3. Create viewport
  UGameViewportClient* Viewport = NewObject<UGameViewportClient>();
  Viewport->Init(Context, GameInstance);

  // 4. Load default map
  World->ServerTravel("Map_MainMenu");

  // At this point: GPU ready, world exists, game instance ready
  return 0;
}
```

### Phase 3: Tick (Continuous Loop)
```cpp
while (GIsRunning) {
  // FEngineLoop::Tick()
  FApp::UpdateDeltaTime();

  // UGameEngine::Tick()
  {
    // A. World logic phase
    for (auto& Context : WorldList) {
      Context.World()->Tick(LEVELTICK_All, DeltaTime);  // AActor::Tick() here
    }

    // B. Viewport input phase
    GameViewport->Tick(DeltaTime);
  }

  // C. Rendering phase (separate from logic!)
  GEngine->RedrawViewports(true);

  // D. Sync frame
  RHISyncFrame();
}
```

### Phase 4: Exit
```cpp
void FEngineLoop::Exit() {
  // Call engine shutdown
  GEngine->PreExit();

  // Inside UGameEngine::PreExit():
  // 1. Game instance shutdown
  GameInstance->Shutdown();

  // 2. World destruction
  DestroyWorld(World);

  // 3. Viewport cleanup
  GameViewport->Close();

  // Module shutdown (reverse order)
  FModuleManager::Get().UnloadModule(TEXT("MyGame"));
  FModuleManager::Get().UnloadModule(TEXT("Engine"));
  FModuleManager::Get().UnloadModule(TEXT("CoreUObject"));
  FModuleManager::Get().UnloadModule(TEXT("Core"));

  // RHI cleanup
  RHIDestroySwapChain();
  RHIDestroyCommandQueue();

  // Exit
  return 0;
}
```

---

## Example 7: What NOT to Do (Anti-Patterns)

### ❌ WRONG: Direct RHI in Game Code
```cpp
// FORBIDDEN in game actor code
void AEnemy::Tick(float DeltaTime) {
  // WRONG — direct RHI calls
  ComPtr<ID3D12Device> Device = GetRHIDevice();
  D3D12_BUFFER_DESC BufferDesc = { ... };
  Device->CreateCommittedResource(...);  // NO NO NO

  // Problem: Bypasses engine management, race conditions, memory leaks
}
```

### ✅ CORRECT: Game Code Uses Abstractions
```cpp
// CORRECT — game code uses engine abstractions
void AEnemy::Tick(float DeltaTime) {
  // Set properties, engine handles rendering
  GetMesh()->SetStaticMesh(MeshAsset);
  GetMesh()->SetMaterial(0, MaterialAsset);

  // Engine handles proxy creation, GPU buffer management, etc.
  // Game code NEVER sees RHI
}
```

---

## Summary: Copy These Patterns to UzEngine

1. **Separate Loop from Engine**: FEngineLoop (PreInit/Init/Tick/Exit) vs. UGameEngine (world management)
2. **Rendering Isolation**: Game → ViewportClient → SceneViewport → Renderer → RHI (chain, never skip)
3. **Scene Proxy**: Actor state (CPU) ↔ Proxy (render thread) with ENQUEUE_RENDER_COMMAND
4. **Module System**: Game is IModuleInterface plugin loaded by engine
5. **World Context**: FWorldContext[] array for multi-world, not global GWorld
6. **Game Instance**: Owns game state only, never rendering/viewport/world lifetime
7. **Lifecycle**: PreInit (no GPU) → Init (GPU ready) → Tick (loop) → Exit (cleanup)

