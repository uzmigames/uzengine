# UE5 Deep Architecture Analysis

> Extracted from UE 5.6 source at `W:\UE_5.6_Source\Engine\Source\`
> Areas NOT covered in `cross-engine-analysis.md` (which already covered FPrimitiveSceneProxy, UClass/DECLARE_CLASS, FMemStack/FMemMark, ENQUEUE_RENDER_COMMAND, immutable snapshots)

---

## Area 1: Hot-Reload / Live Coding

### How UE5 Solves It

UE5 has **two separate systems** for runtime code patching:

1. **Legacy HotReload** (`Developer/HotReload/`) -- Recompiles an entire module DLL, unloads the old one, loads the new one. Requires full class reinstancing (old UClass -> new UClass mapping). Heavyweight and deprecated in practice.

2. **Live Coding** (`Developer/Windows/LiveCoding/`) -- Uses **Live++ (LPP)** by Molecular Matters to patch functions in-place via binary patching. No DLL unload/reload. Functions are rewritten at the machine code level while the process is running.

### Key Files

| File | Purpose |
|------|---------|
| `Developer/Windows/LiveCoding/Public/ILiveCodingModule.h` | Public interface: `Compile()`, `IsCompiling()`, `Tick()`, `GetOnPatchCompleteDelegate()` |
| `Developer/Windows/LiveCoding/Private/LiveCodingModule.cpp` | Implementation: DLL notification hooks, LivePP integration, page reservation |
| `Developer/Windows/LiveCoding/Private/LiveCodingModule.h` | State machine: `NotRunning -> Starting -> Running -> RunningAndEnabled` |
| `Developer/HotReload/Private/HotReload.cpp` | Legacy: `FReload`, `FScopedHotReload`, class reinstancing via `FReload` |
| `Developer/HotReload/Public/IHotReload.h` | Legacy interface: `RecompileModule()`, `RebindPackages()`, `DoHotReloadFromEditor()` |

### Central Pattern: LivePP Binary Patching

```cpp
// LiveCodingModule.cpp:345-509

// 1. Register for DLL load/unload notifications via NT API
FNtDllFunction RegisterFunc("LdrRegisterDllNotification");
RegisterFunc(0, OnDllNotification, this, &CallbackCookie);

// 2. Enumerate all loaded modules via PEB (Process Environment Block)
const TEB* ThreadEnvBlock = reinterpret_cast<TEB*>(NtCurrentTeb());
const PEB* ProcessEnvBlock = ThreadEnvBlock->ProcessEnvironmentBlock;
// Walk InMemoryOrderModuleList to register every DLL with LivePP

// 3. Reserve virtual memory pages near loaded modules for patches
// LivePP needs code patches within +-128MB of original code (x64 rel32 jumps)
constexpr uint64 DefaultPadding = 128ull * 1024ull * 1024ull;
LivePP::ReservePages(ReservedPages, LowerBound, UpperBound, PageAlignment);

// 4. Compile triggers LivePP recompile -> binary patches loaded in-process
void FLiveCodingModule::Compile(...) {
    UpdateModules(false);
    GIsCompileActive = true;
    LppTriggerRecompile();  // Tells LivePP to compile changes
}
```

### Reinstancing Protocol (Editor Only)

When Live Coding patches code that changes class layout (new UPROPERTY, changed struct size), UE5 must **reinstance** all existing objects:

```cpp
// LiveCodingModule.cpp:825-893 (AttemptSyncLivePatching)
if (GHasLoadedPatch) {
    // 1. Snapshot all existing UObjects
    TArray<UObject*> StartingObjects;
    for (TObjectIterator<UObject> It; It; ++It)
        StartingObjects.Add(*It);

    // 2. Process newly loaded classes (records old->new class mappings)
    ProcessNewlyLoadedUObjects();

    // 3. Finalize reinstancing (copies property values old->new instances)
    Reload->Finalize(false);

    // 4. Fix up class default objects for dead classes
    for (TObjectIterator<UClass> It; It; ++It) {
        if (Class->GetName().StartsWith(TEXT("LIVECODING_")) ||
            Class->GetName().StartsWith(TEXT("REINST_")))
            Class->SetDefaultObject(DummyDefaultObject);
    }

    // 5. GC to destroy old instances
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
}
```

### Non-Editor (Runtime) Mode

For non-editor builds, `FNullReload` replaces `FReload`. It logs errors if structural changes are detected but **does not reinstance** -- only function body changes are supported at runtime.

```cpp
// FNullReload -- runtime builds can only do function-level patching
virtual bool GetEnableReinstancing(bool bHasChanged) const {
    if (bHasChanged && !bEnabledMessage) {
        UE_LOG(LogLiveCoding, Error,
            TEXT("Object structure changes detected. LiveCoding re-instancing "
                 "isn't supported in builds without the editor"));
    }
    return false;  // Never reinstance in non-editor
}
```

### UzEngine Implication

UzEngine's `game.dll` hot-reload should follow the **non-editor Live Coding model**:
- **Function-level patching only** for gameplay code (body changes OK, ABI changes require restart)
- Use Windows DLL notification (`LdrRegisterDllNotification`) to detect when game.dll is replaced
- The `GameContext` struct must have a **stable ABI** (no layout changes between reloads)
- Reserved virtual memory pages near the DLL base are critical for x64 patching
- A **state machine** (`NotRunning -> Starting -> Running`) prevents race conditions during reload
- **Delegate pattern** (`FOnPatchCompleteDelegate`) notifies the editor when reload completes

---

## Area 2: Blueprint VM / Scripting Integration

### How UE5 Solves It

UE5 uses a **custom bytecode VM** for Blueprint execution, with a **thunk-based binding** system for C++ <-> Blueprint interop. The VM is a simple **stack machine** with ~120 opcodes.

### Key Files

| File | Purpose |
|------|---------|
| `Runtime/CoreUObject/Public/UObject/Script.h` | Bytecode opcodes (`EExprToken`), `FBlueprintContext`, function flags |
| `Runtime/CoreUObject/Public/UObject/ScriptMacros.h` | Parameter marshaling macros (`P_GET_PROPERTY`, `P_GET_STRUCT`, etc.) |
| `Runtime/CoreUObject/Public/UObject/ObjectMacros.h:761` | `DECLARE_FUNCTION(func)` / `DEFINE_FUNCTION(func)` |
| `Runtime/CoreUObject/Private/UObject/ScriptCore.cpp` | `UObject::ProcessEvent()`, `CallFunction()`, `ProcessInternal()` |

### Central Pattern: Thunk-Based C++ Binding

Every C++ function exposed to Blueprints gets a **thunk** -- a static function with a fixed signature:

```cpp
// ObjectMacros.h:761
#define DECLARE_FUNCTION(func) \
    static void func(UObject* Context, FFrame& Stack, RESULT_DECL)
#define DEFINE_FUNCTION(func) \
    void func(UObject* Context, FFrame& Stack, RESULT_DECL)

// Usage in generated code:
DEFINE_FUNCTION(UMyClass::execMyFunction) {
    P_GET_PROPERTY(FFloatProperty, Z_Param_Speed);  // Pop float from stack
    P_GET_OBJECT(AActor, Z_Param_Target);            // Pop UObject* from stack
    P_FINISH;                                         // Consume end-of-params marker

    P_NATIVE_BEGIN;
    // Actual C++ implementation
    P_THIS->MyFunction(Z_Param_Speed, Z_Param_Target);
    P_NATIVE_END;
}
```

### ProcessEvent -- The VM Entry Point

```cpp
// ScriptCore.cpp:2032
void UObject::ProcessEvent(UFunction* Function, void* Parms) {
    // Safety: reject unreachable objects, paused debugging, etc.
    checkf(!IsUnreachable(), ...);

    if (Function->FunctionFlags & FUNC_Native) {
        // Native function -- check network callspace
        int32 FunctionCallspace = GetFunctionCallspace(Function, NULL);
        if (FunctionCallspace & FunctionCallspace::Remote)
            CallRemoteFunction(Function, Parms, NULL, NULL);
        if ((FunctionCallspace & FunctionCallspace::Local) == 0)
            return;  // Remote-only: don't execute locally
    }
    else if (Function->Script.Num() == 0) {
        return;  // No bytecode = nothing to execute
    }

    // Build stack frame and execute
    // ... (allocates FFrame on stack, runs bytecode interpreter)
}
```

### CallFunction -- Native vs Script Dispatch

```cpp
// ScriptCore.cpp:1111
void UObject::CallFunction(FFrame& Stack, RESULT_DECL, UFunction* Function) {
    if (Function->FunctionFlags & FUNC_Native) {
        // Direct native call via function pointer
        Function->Invoke(this, Stack, RESULT_PARAM);
    } else {
        // Interpret bytecode via ProcessScriptFunction
        ProcessScriptFunction(this, Function, Stack, RESULT_PARAM, ProcessInternal);
    }
}
```

### Bytecode Opcodes

The VM has ~120 opcodes defined in `EExprToken`:
- **Variable access**: `EX_LocalVariable`, `EX_InstanceVariable`, `EX_DefaultVariable`
- **Flow control**: `EX_Jump`, `EX_JumpIfNot`, `EX_SwitchValue`, `EX_PushExecutionFlow`
- **Function calls**: `EX_VirtualFunction`, `EX_FinalFunction`, `EX_LocalFinalFunction`, `EX_CallMath`
- **Constants**: `EX_IntConst`, `EX_FloatConst`, `EX_StringConst`, `EX_ObjectConst`
- **Casting**: `EX_DynamicCast`, `EX_MetaCast`, `EX_ObjToInterfaceCast`
- **Debugging**: `EX_Breakpoint`, `EX_Tracepoint`, `EX_WireTracepoint`

### Thread-Local Context

```cpp
// Script.h:73
class FBlueprintContext {
    static FBlueprintContext* GetThreadSingleton();
    FVirtualStackAllocator* GetVirtualStackAllocator();
private:
    FVirtualStackAllocator VirtualStackAllocator;  // Per-thread stack for VM frames
};
```

### UzEngine Implication

For LuaJIT integration, UzEngine should follow these patterns:
- **Thunk-based binding**: Each C++ function exposed to Lua gets a wrapper with a fixed `lua_CFunction` signature -- analogous to UE5's `DECLARE_FUNCTION` thunks
- **ProcessEvent equivalent**: A single `CallLuaFunction(lua_State*, const char* name, void* args)` entry point, mirroring `ProcessEvent`
- **Native vs Script dispatch**: Check if function is C++ (direct call) or Lua (interpreter call), same branching pattern
- **Per-thread state**: LuaJIT should use `lua_newthread()` per engine thread, similar to `FBlueprintContext` thread singleton
- **Bytecode is an implementation detail**: Users never see opcodes, they write in the high-level language. UzEngine should never expose LuaJIT bytecode to users.

---

## Area 3: Animation System Architecture (AnimGraph)

### How UE5 Solves It

UE5's animation system uses a **tree of FAnimNode_Base structs** compiled from AnimGraph Blueprints. The tree is evaluated bottom-up with two phases: **Update** (compute weights/timers) then **Evaluate** (produce bone transforms).

### Key Files

| File | Purpose |
|------|---------|
| `Engine/Classes/Animation/AnimNodeBase.h` | `FAnimNode_Base` -- the runtime node base struct |
| `Engine/Public/Animation/AnimInstanceProxy.h` | `FAnimInstanceProxy` -- game-thread proxy for the anim instance |
| `Engine/Public/Animation/AnimNodeData.h` | `FAnimNodeData` -- constant/folded node data, separated from instance data |
| `Engine/Classes/Animation/AnimNode_StateMachine.h` | State machine node |
| `AnimGraphRuntime/Public/AnimNodes/` | All blend/IK/pose nodes |

### Central Pattern: Node-Based Evaluation Pipeline

```cpp
// AnimNodeBase.h:851
USTRUCT()
struct FAnimNode_Base {
    GENERATED_BODY()

    // Phase 1: Called when node first runs or state machine re-enters
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context);

    // Phase 2: Called when LOD changes (cache bone indices)
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context);

    // Phase 3: Update weights, timers, blend parameters (top-down)
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context);

    // Phase 4: Produce bone transforms (bottom-up evaluation)
    virtual void Evaluate_AnyThread(FPoseContext& Output);

    // Optional: Component-space evaluation (IK, skeletal controls)
    virtual void EvaluateComponentSpace_AnyThread(FComponentSpacePoseContext& Output);

    // Thread safety check
    virtual bool CanUpdateInWorkerThread() const { return true; }

    // Game-thread pre-update (gather non-thread-safe data)
    virtual bool HasPreUpdate() const { return false; }
    virtual void PreUpdate(const UAnimInstance* InAnimInstance) {}
};
```

### Pose Links -- Tree Connectivity

```cpp
// AnimNodeBase.h:748
USTRUCT(BlueprintInternalUseOnly)
struct FPoseLinkBase {
    FAnimNode_Base* LinkedNode;  // Non-serialized runtime pointer
    int32 LinkID;               // Serialized ID for reconstruction

    void Initialize(const FAnimationInitializeContext& Context);
    void Update(const FAnimationUpdateContext& Context);
};

struct FPoseLink : public FPoseLinkBase {
    void Evaluate(FPoseContext& Output);  // Recurse into linked node
};
```

### Context Propagation with Weights

```cpp
// AnimNodeBase.h:352
struct FAnimationUpdateContext : public FAnimationBaseContext {
    float CurrentWeight;          // Blend weight at this point in the tree
    float RootMotionWeightModifier;
    float DeltaTime;

    // Create sub-context with modified weight (for blend nodes)
    FAnimationUpdateContext FractionalWeight(float WeightMultiplier) const {
        Result.CurrentWeight = CurrentWeight * WeightMultiplier;
        return Result;
    }
};
```

### Data Folding -- Separating Constant vs Instance Data

UE5 separates constant animation data (set at compile time) from per-instance data:

```cpp
// AnimNodeData.h:92
struct FAnimNodeData {
    const void* GetData(FNodeDataId InId, const FAnimNode_Base* InNode,
                        const UObject* InCurrentObject = nullptr) const;
    // Data can come from:
    // - Class sparse data (constant, shared across instances)
    // - Instance data (per-AnimInstance, mutable)
};

// Flags indicate which virtual functions are bound
enum class EAnimNodeDataFlags : uint32 {
    HasInitialUpdateFunction  = 0x1,
    HasBecomeRelevantFunction = 0x2,
    HasUpdateFunction         = 0x4,
};
```

### UzEngine Implication

For UzEngine Phase 3 animation system:
- **Node-based tree** with `Initialize -> CacheBones -> Update -> Evaluate` lifecycle
- **FPoseLink** pattern: each node has typed links to children, resolved by ID at load time
- **Weight propagation**: `FAnimationUpdateContext` carries blend weight through the tree
- **Thread-safe design**: `_AnyThread` suffix convention, `HasPreUpdate()` for game-thread data gathering
- **Data folding**: Separate constant node parameters from per-instance state (reduces memory for many instances of same anim graph)
- **Message stack** replaces ancestor tracking -- scoped messages propagate context through the tree without explicit parent pointers

---

## Area 4: Chaos Physics Integration Pattern

### How UE5 Solves It

Chaos physics runs on a **separate physics thread** (or async task). Integration with the game thread uses a **callback object pattern** with typed input/output queues. Game thread pushes input at game tick rate; physics thread consumes it at physics substep rate.

### Key Files

| File | Purpose |
|------|---------|
| `Runtime/Experimental/Chaos/Public/Chaos/SimCallbackObject.h` | `ISimCallbackObject` -- physics thread callback interface |
| `Runtime/Experimental/Chaos/Public/Chaos/SimCallbackInput.h` | `FSimCallbackInput/Output` -- typed data exchange |
| `Runtime/PhysicsCore/Public/Chaos/ChaosScene.h` | `FChaosScene` -- scene-level solver wrapper |
| `Runtime/PhysicsCore/Public/Chaos/ChaosEngineInterface.h` | Engine-facing types (surface types, impulse falloff) |

### Central Pattern: Typed Callback Object

```cpp
// SimCallbackObject.h:32
enum class ESimCallbackOptions : uint16 {
    Presimulate           = 1 << 0,
    MidPhaseModification  = 1 << 1,
    CCDModification       = 1 << 2,
    ContactModification   = 1 << 3,
    StrainModification    = 1 << 5,
    ParticleRegister      = 1 << 6,
    ParticleUnregister    = 1 << 7,
    RunOnFrozenGameThread = 1 << 8,
    Rewind                = 1 << 9,
    PreIntegrate          = 1 << 11,
    PostIntegrate         = 1 << 12,
    PreSolve              = 1 << 13,
    PostSolve             = 1 << 14,
};
```

The callback interface provides hooks at every stage of the physics pipeline:

```cpp
// SimCallbackObject.h:67
class ISimCallbackObject {
public:
    FReal GetSimTime_Internal() const;
    FReal GetDeltaTime_Internal() const;

    // Called at each physics pipeline stage (override to hook in)
    void PreSimulate_Internal()     { OnPreSimulate_Internal(); }
    void PreIntegrate_Internal()    { OnPreIntegrate_Internal(); }
    void PostIntegrate_Internal()   { OnPostIntegrate_Internal(); }
    void MidPhaseModification_Internal(FMidPhaseModifierAccessor&);
    void CCDModification_Internal(FCCDModifierAccessor&);
    void StrainModification_Internal(FStrainModifierAccessor&);
    void ContactModification_Internal(FCollisionContactModifier&);

protected:
    virtual void OnPreSimulate_Internal() {}
    virtual void OnPreIntegrate_Internal() {}
    virtual void OnPostIntegrate_Internal() {}
    // ... etc
};
```

### Typed Input/Output Pattern

```cpp
// SimCallbackInput.h
struct FSimCallbackInput {
    void SetNumSteps_External(int32 InNumSteps);  // Game thread sets substep count
    virtual ~FSimCallbackInput() = default;
protected:
    int32 NumSteps;
};

struct FSimCallbackOutput {
    FReal InternalTime;  // When this output was generated
};

// Usage: FChaosSceneCallbackInput extends FSimCallbackInput
struct FChaosSceneCallbackInput : public FSimCallbackInput {
    FVec3 Gravity;
    void Reset() {}
};

struct FChaosSceneSimCallback : public TSimCallbackObject<FChaosSceneCallbackInput> {
    virtual void OnPreSimulate_Internal() override;
};
```

### Thread Ownership via `_Internal` / `_External` Naming

Chaos uses a naming convention to enforce thread ownership:
- `_Internal` = only callable on the **physics thread**
- `_External` = only callable on the **game thread**
- `FreeOutputData_External()` -- game thread frees output after consuming
- `Release_Internal()` -- physics thread releases input after consuming

### Scene Integration

```cpp
// ChaosScene.h:84
class FChaosScene {
    Chaos::FPhysicsSolver* GetSolver() const;

    void StartFrame();
    void SetUpForFrame(const FVector* NewGrav, float InDeltaSeconds, ...);
    void EndFrame();

    // Async task control
    FGraphEventArray GetCompletionEvents();
    void WaitPhysScenes();
    void WaitSolverTasks();

    // Delegates for post-tick notifications
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnPhysScenePostTick, FChaosScene*);
    FOnPhysScenePostTick OnPhysScenePostTick;
};
```

### UzEngine Implication

For Jolt Physics integration:
- **Callback object pattern**: Create a `JoltSimCallback` that mirrors `ISimCallbackObject` with typed input/output
- **Thread-safe data exchange**: Use SPSC queues (game thread produces, physics thread consumes), same as `FSimCallbackInput`
- **Pipeline hooks**: Provide hooks at `PreStep`, `PostStep`, `ContactCallback` stages
- **`_Internal`/`_External` naming**: Adopt the thread ownership convention to prevent cross-thread bugs
- **Output handles**: Use RAII handles (`FSimCallbackOutputHandle`) for output data to prevent leaks
- **Delegate for post-tick**: `FOnPhysScenePostTick` pattern lets systems react to physics completion

---

## Area 5: World Partition / Asset Streaming

### How UE5 Solves It

World Partition divides the world into **spatial cells** organized in a **runtime hash grid**. Streaming is driven by **streaming sources** (cameras, player positions) that determine which cells should be loaded/activated.

### Key Files

| File | Purpose |
|------|---------|
| `Engine/Public/WorldPartition/WorldPartition.h` | `UWorldPartition` -- top-level partition manager |
| `Engine/Public/WorldPartition/WorldPartitionStreamingSource.h` | `FWorldPartitionStreamingSource` -- source definition with spherical sectors |
| `Engine/Private/WorldPartition/WorldPartitionStreamingPolicy.cpp` | `UpdateStreamingSources()`, `UpdateStreamingState()` |
| `Engine/Private/WorldPartition/WorldPartitionSubsystem.cpp` | Subsystem that drives streaming each frame |
| `Engine/Public/WorldPartition/RuntimeSpatialHash/` | Spatial hash grid implementation |

### Central Pattern: Source-Driven Streaming

```cpp
// WorldPartitionStreamingSource.h:343
struct FWorldPartitionStreamingSource {
    FName Name;
    FVector Location;
    FRotator Rotation;
    EStreamingSourceTargetState TargetState;  // Loaded vs Activated
    EStreamingSourcePriority Priority;        // Highest..Lowest
    FVector Velocity;                         // For predictive loading
    TArray<FStreamingSourceShape> Shapes;     // Spheres or spherical sectors
    TSet<FName> TargetGrids;                  // Which grids this source affects

    // Hash-based change detection (skip updates when sources haven't moved)
    void UpdateHash();
    uint32 GetHash(bool bInclude3DInformation = true) const;
};

// Spherical sector for directional streaming (e.g., camera frustum)
class FSphericalSector {
    FVector Center;
    FReal Radius;
    FVector Axis;
    FReal Angle;  // 360 = full sphere, <360 = sector

    bool IntersectsBox(const FBox2D& InBox) const;  // Grid cell test
};
```

### Streaming State Machine

```cpp
enum class EWorldPartitionInitState {
    Uninitialized,
    Initializing,
    Initialized,
    Uninitializing
};

// Each cell can be in one of these states:
enum class EStreamingSourceTargetState : uint8 {
    Loaded,     // Cell data in memory but not visible
    Activated   // Cell data in memory AND actors spawned/visible
};
```

### Optimized Update Loop

```cpp
// WorldPartitionStreamingPolicy.cpp:104
void UWorldPartitionStreamingPolicy::UpdateStreamingSources(bool bCanOptimizeUpdate) {
    TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionStreamingPolicy::UpdateStreamingSources);

    // Hash-based skip: if sources haven't moved, skip the update
    const uint32 NewHash = WorldPartitionSubsystem->GetStreamingSourcesHash();
    if (bCanOptimizeUpdate && (UpdateStreamingSourcesHash == NewHash))
        return;  // No change -- skip expensive cell intersection tests

    UpdateStreamingSourcesHash = NewHash;
    // ... proceed with cell intersection tests
}
```

### Shape-Based Cell Intersection

```cpp
// WorldPartitionStreamingSource.h:181
static void ForEachShape(float InGridLoadingRange, ...,
                          TFunctionRef<void(const FSphericalSector&)> InOperation) {
    if (InShapes.IsEmpty()) {
        // Default: sphere centered at source location
        FSphericalSector LocalShape(FVector::ZeroVector, InDefaultRadius);
        InOperation(LocalShape.TransformBy(Transform));
    } else {
        for (const FStreamingSourceShape& Shape : InShapes) {
            FSphericalSector LocalShape(Shape.Location, ShapeRadius, ShapeAxis, ShapeAngle);
            if (LocalShape.IsValid())
                InOperation(LocalShape.TransformBy(Transform));
        }
    }
}
```

### UzEngine Implication

For Phase 4 streaming:
- **Streaming source abstraction**: Don't couple to "player position" -- use a source interface that cameras, triggers, and AI can all implement
- **Spherical sector shapes**: Directional loading (camera frustum) is critical for open worlds
- **Hash-based skip**: Compare source hash each frame; skip cell testing when nothing moved -- huge performance win
- **Two-phase loading**: `Loaded` (data in memory) vs `Activated` (actors spawned) prevents pop-in
- **Grid naming**: Target specific grids per source (e.g., HLOD grid vs gameplay grid vs vegetation grid)
- **Velocity-based prediction**: Use source velocity for predictive loading ahead of camera motion

---

## Area 6: Unreal Insights / Profiling (TraceLog)

### How UE5 Solves It

UE5 uses a custom **structured tracing system** (TraceLog / Unreal Insights) built on a channel-based event model. Events are defined with typed fields, emitted via macros, and sent to either a file or a remote Insights tool over TCP.

### Key Files

| File | Purpose |
|------|---------|
| `Runtime/TraceLog/Public/Trace/Trace.h` | Core trace API: `Initialize()`, `SendTo()`, `WriteTo()`, channels |
| `Runtime/TraceLog/Public/Trace/Config.h` | Build configuration for trace enablement |
| `Runtime/Core/Public/ProfilingDebugging/CpuProfilerTrace.h` | `TRACE_CPUPROFILER_EVENT_SCOPE` macro and `FCpuProfilerTrace` |

### Central Pattern: Channel-Based Structured Events

```cpp
// Trace.h -- Event definition macros
UE_TRACE_CHANNEL(ChannelName)                    // Declare a trace channel
UE_TRACE_EVENT_BEGIN(Logger, Event)               // Begin event type definition
    UE_TRACE_EVENT_FIELD(uint32, MyField)          // Typed field
UE_TRACE_EVENT_END()                              // End definition
UE_TRACE_LOG(Logger, Event, ChannelExpr)           // Emit an event

// Channels can be toggled at runtime:
UE_TRACE_API bool ToggleChannel(const TCHAR* ChannelName, bool bEnabled);
UE_TRACE_API void EnumerateChannels(ChannelIterFunc IterFunc, void* User);
```

### CPU Profiler Scope Macro -- The Core Pattern

```cpp
// CpuProfilerTrace.h:449
#define TRACE_CPUPROFILER_EVENT_SCOPE(Name) \
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(Name, true)

// Expands to:
//   static uint32 __CpuProfilerEventSpecId_LINE;
//   FCpuProfilerTrace::FEventScope __CpuProfilerEventScope_LINE(
//       __CpuProfilerEventSpecId_LINE, #Name, true, __FILE__, __LINE__);
```

The `FEventScope` RAII class:

```cpp
// CpuProfilerTrace.h:181
class FCpuProfilerTrace::FEventScope {
    FORCEINLINE FEventScope(uint32& InOutSpecId, const ANSICHAR* InEventName,
                            bool bInCondition, const ANSICHAR* File, uint32 Line)
        : bEnabled(bInCondition && CpuChannel)
    {
        if (bEnabled) {
            // First call: register event type and cache SpecId
            GetOrCreateSpecId(InOutSpecId, InEventName, File, Line);
            // All calls: emit begin event with cached SpecId
            OutputBeginEvent(FPlatformAtomics::AtomicRead_Relaxed(
                (volatile int32*)&InOutSpecId));
        }
    }

    FORCEINLINE ~FEventScope() {
        if (bEnabled) OutputEndEvent();
    }

    bool bEnabled;
};
```

### Static vs Dynamic Events

```cpp
// STATIC (zero overhead after first call -- SpecId cached in static local):
TRACE_CPUPROFILER_EVENT_SCOPE(MySystem::Update)

// DYNAMIC (higher overhead -- name lookup each call):
TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Actor_%s"), *ActorName))

// CONDITIONAL (only trace when condition is true):
TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(MySystem::HeavyWork, bShouldProfile)

// PER-CHANNEL (requires both CpuChannel AND custom channel enabled):
TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(Physics::Solve, PhysicsChannel)
```

### Transport Layer

```cpp
// Trace.h
struct FInitializeDesc {
    uint32 TailSizeBytes     = 4 << 20;  // 4MB tail buffer for late connections
    bool   bUseWorkerThread  = true;     // Dedicated IO thread
    bool   bUseImportantCache = true;    // Cache "important" events (definitions)
};

// Output options:
bool SendTo(const TCHAR* Host, uint32 Port=0);     // TCP to Unreal Insights
bool WriteTo(const TCHAR* Path);                     // .utrace file
bool RelayTo(UPTRINT Handle, IoWriteFunc, IoCloseFunc);  // Custom callback
bool WriteSnapshotTo(const TCHAR* Path);              // Dump tail buffer
```

### Statistics

```cpp
struct FStatistics {
    uint64 BytesSent;              // To current endpoint
    uint64 BytesTraced;            // Uncompressed
    uint64 BytesEmitted;           // Not yet written
    uint64 MemoryUsed;             // Allocator total
    uint64 BlockPoolAllocated;     // TLS block pool
    uint32 SharedBufferAllocated;
    uint32 CacheAllocated;
    uint32 CacheUsed;              // Important events cache
    uint32 CacheWaste;             // Retired cache buffers
};
```

### UzEngine Implication

For UzEngine debug/profiling system:
- **RAII scope macro** is the critical pattern: `UZ_PROFILE_SCOPE("name")` that wraps `FCpuProfilerTrace::FEventScope`
- **Static SpecId caching**: Use a `static uint32` per call site, populated on first call via atomic CAS -- zero overhead after first call
- **Channel system**: Define channels (`RenderChannel`, `PhysicsChannel`, `ScriptChannel`) that can be toggled at runtime
- **Dual output**: Support both file output (`.uztrace`) and TCP streaming to a profiler tool
- **Tail buffer**: Keep last N MB of trace data in a ring buffer so late-connecting profilers can see recent history
- **Conditional tracing**: `bEnabled && Channel` check at macro expansion -- when profiling is off, the entire scope is a no-op (single branch)
- **Thread registration**: `ThreadRegister()` must be called per-thread before tracing -- integrate with job system thread startup
- **Important event cache**: Event definitions (names, file/line) are cached separately and replayed to new connections

### Additional: TraceAnalysis / TraceServices Pipeline

Beyond the emission macros (`TRACE_CPUPROFILER_EVENT_SCOPE`), UE5 has a full **analysis pipeline** in 6 modules:

| Module | Purpose |
|--------|---------|
| `Developer/TraceAnalysis/` | Core analysis engine: `IAnalyzer`, `FAnalysisContext`, `FStoreClient` |
| `Developer/TraceServices/` | Data models: `TMonotonicTimeline`, `IEditableTimeline`, paged arrays |
| `Developer/TraceInsightsCore/` | UI framework: tables, filters, config for Insights panels |
| `Developer/TraceInsights/` | The actual Unreal Insights application UI |
| `Developer/TraceInsightsFrontend/` | Frontend integration layer |
| `Developer/TraceTools/` | Standalone trace tools |

#### IAnalyzer -- Event Subscription Pattern

```cpp
// TraceAnalysis: Analyzer.h
class IAnalyzer {
    struct FInterfaceBuilder {
        // Subscribe to specific events by logger+event name
        virtual void RouteEvent(uint16 RouteId, const ANSICHAR* Logger,
                                const ANSICHAR* Event, bool bScoped=false) = 0;
        // Subscribe to ALL events from a logger
        virtual void RouteLoggerEvents(uint16 RouteId, const ANSICHAR* Logger, bool bScoped=false) = 0;
        // Subscribe to every event in the stream
        virtual void RouteAllEvents(uint16 RouteId, bool bScoped=false) = 0;
    };

    // Called at analysis start -- subscribe to events here
    virtual void OnAnalysisBegin(const FOnAnalysisContext& Context);

    // Called for each matching event -- RouteId identifies which subscription
    virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context);

    // Event data is accessed via typed getters with coercion
    // FEventData::GetValue<float>("Duration")
    // FEventData::GetString("Name", OutString)
    // FEventData::GetArray<uint32>("Indices")
};
```

#### FStoreClient -- Trace Store Access

```cpp
// TraceAnalysis: StoreClient.h
class FStoreClient {
    static FStoreClient* Connect(const TCHAR* Host, uint32 Port = 0);

    uint32 GetTraceCount() const;
    const FTraceInfo* GetTraceInfo(uint32 Index) const;
    FTraceData ReadTrace(uint32 Id) const;  // Returns IInDataStream

    // Session management (live connections)
    uint32 GetSessionCount() const;
    const FSessionInfo* GetSessionInfo(uint32 Index) const;
};
```

#### TMonotonicTimeline -- Analysis Data Model

```cpp
// TraceServices: MonotonicTimeline.h
struct FMonotonicTimelineDefaultSettings {
    enum {
        MaxDepth = 1024,
        ScopeEntriesPageSize = 65536,
        EventsPageSize = 65536,
        DetailLevelsCount = 6,  // Multi-resolution for zoom levels
    };
    // Detail levels: 0.0, 0.0001, 0.001, 0.008, 0.04, 0.2 seconds
};

template<typename InEventType>
class IEditableTimeline {
    virtual void AppendBeginEvent(double StartTime, const InEventType& Event) = 0;
    virtual void AppendEndEvent(double EndTime) = 0;
};
```

#### UzEngine Implication (Analysis Pipeline)

- **3-layer architecture**: Emission (macros) -> Transport (file/TCP) -> Analysis (IAnalyzer subscribers)
- **RouteId-based dispatch**: Analyzers subscribe by name and get routed events with a uint16 ID -- efficient dispatch without string comparison per event
- **Multi-resolution timelines**: Store events at multiple detail levels (6 in UE5) so zooming out in a profiler UI doesn't require re-scanning all events
- **Paged arrays**: Use page-based allocation (64K entries/page) for timeline data to avoid reallocation during analysis
- **Store service**: A separate process manages trace files and live sessions -- the profiler tool connects to it, not directly to the engine

---

## Cross-Cutting Patterns Summary

| Pattern | UE5 Implementation | UzEngine Application |
|---------|--------------------|--------------------|
| **Thread ownership naming** | `_Internal` / `_External` suffixes | Adopt for physics/render thread boundaries |
| **RAII scopes everywhere** | `FEventScope`, `FScopedHotReload`, `FLiveCodingModule::EState` | Use for profiling, reload, physics callbacks |
| **Hash-based change detection** | Streaming source hash skip | Apply to scene graph dirty flags |
| **Static local caching** | SpecId in `TRACE_CPUPROFILER_EVENT_SCOPE` | Use for any first-call-only registration |
| **Typed input/output queues** | `FSimCallbackInput` / `FSimCallbackOutput` | Physics ↔ game thread data exchange |
| **State machines** | Live Coding: `NotRunning -> Starting -> Running` | Hot-reload, streaming, play mode |
| **Thunk binding** | `DECLARE_FUNCTION(func)` fixed signature | Lua binding wrappers |
