# Unity Engine Deep Architecture Analysis

**Document Version**: 1.0
**Date**: 2026-03-04
**Classification**: Architecture Reference
**Status**: Active — Supplements `cross-engine-analysis.md`

---

## Purpose

This document provides deep architectural analysis of 7 Unity subsystems not covered in the initial cross-engine analysis. Each area includes Unity's internal architecture, key APIs with realistic code examples, named patterns, and concrete implications for UzEngine.

**Prerequisite**: Read `cross-engine-analysis.md` first. That document covers NativeArray<T>, TypeIndex, CommandBuffer arena, Entity generational indices, and Archetype chunks. This document goes deeper on the areas listed below.

---

## Table of Contents

1. [Hot-Reload of Scripting](#1-hot-reload-of-scripting)
2. [Mono/IL2CPP Backend Architecture](#2-monoil2cpp-backend-architecture)
3. [Animation System (Animator + Playables API)](#3-animation-system-animator--playables-api)
4. [PhysX Integration Pattern](#4-physx-integration-pattern)
5. [Addressables / Asset Streaming](#5-addressables--asset-streaming)
6. [Profiler / Memory Profiler Architecture](#6-profiler--memory-profiler-architecture)
7. [DOTS Job System Scheduler (Deep Dive)](#7-dots-job-system-scheduler-deep-dive)

---

## 1. Hot-Reload of Scripting

### 1.1 Architecture Overview

Unity's scripting hot-reload is built on the CLR's ability to unload and reload AppDomains (pre-2021) or individual assemblies (2021+ with domain reload disabled). The core flow:

```
Game Thread: detect script change → compile C# → trigger reload
    │
    ▼
AssemblyReloadEvents.beforeAssemblyReload
    │  ← user code saves transient state
    ▼
Serialize all MonoBehaviour fields via SerializedObject
    │  ← Unity walks all managed objects, serializes to binary blob
    ▼
Unload old AppDomain / Assembly
    │  ← all managed references become invalid
    ▼
Load new AppDomain / Assembly
    │  ← new types loaded, static constructors run
    ▼
Deserialize MonoBehaviour fields back onto new instances
    │  ← binary blob → new managed objects
    ▼
AssemblyReloadEvents.afterAssemblyReload
    │  ← user code restores transient state
    ▼
Resume editor
```

### 1.2 Key APIs and Classes

**SerializedObject / SerializedProperty**: Unity's reflection-based serialization system that walks every field marked `[SerializeField]` (or public by default) on MonoBehaviours. During domain reload, Unity serializes the entire managed object graph into an intermediate binary format, then deserializes it back after the new assembly loads.

```csharp
// Unity internally does something equivalent to:
foreach (var monoBehaviour in FindObjectsOfType<MonoBehaviour>())
{
    var serialized = new SerializedObject(monoBehaviour);
    // Iterates all SerializedProperty fields
    var iterator = serialized.GetIterator();
    while (iterator.NextVisible(true))
    {
        reloadBlob.Write(iterator.propertyPath, iterator.boxedValue);
    }
}

// After new assembly loads:
foreach (var (instanceId, blob) in reloadBlobs)
{
    var obj = EditorUtility.InstanceIDToObject(instanceId);
    var serialized = new SerializedObject(obj);
    foreach (var (path, value) in blob)
    {
        serialized.FindProperty(path).boxedValue = value;
    }
    serialized.ApplyModifiedPropertiesWithoutUndo();
}
```

**AssemblyReloadEvents**: Two hooks for user code to participate in the reload lifecycle:

```csharp
// Registered by user code to save/restore non-serialized state
[InitializeOnLoad]
static class HotReloadHandler
{
    static HotReloadHandler()
    {
        AssemblyReloadEvents.beforeAssemblyReload += OnBeforeReload;
        AssemblyReloadEvents.afterAssemblyReload += OnAfterReload;
    }

    static void OnBeforeReload()
    {
        // Save state that can't be serialized:
        // - network connections
        // - file handles
        // - native plugin state
        // - dictionary contents (not serializable by Unity)
        EditorPrefs.SetString("MyPlugin_State",
            JsonUtility.ToJson(s_transientState));
    }

    static void OnAfterReload()
    {
        // Restore from EditorPrefs or ScriptableSingleton
        s_transientState = JsonUtility.FromJson<TransientState>(
            EditorPrefs.GetString("MyPlugin_State"));
    }
}
```

### 1.3 Domain Reload vs. Enter Play Mode Settings (2021+)

Starting in Unity 2021, the editor supports **skipping domain reload** when entering play mode. This dramatically reduces iteration time (from ~3-5 seconds to <0.3 seconds) but requires user code to properly reset static state:

```csharp
// Without domain reload, static fields persist between play sessions.
// This attribute marks methods that must run to reset state:
[RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
static void ResetStaticState()
{
    // Without this, s_instance from the previous play session leaks
    s_instance = null;
    s_eventSubscribers.Clear();
    s_cachedLookup = new Dictionary<int, Entity>();
}
```

**Three reload modes**:

| Mode | Domain Reload | Scene Reload | Play Mode Entry Time |
|------|--------------|-------------|---------------------|
| Classic (default) | Yes | Yes | ~3-5s |
| Fast Enter | No | Yes | ~0.5s |
| Fastest | No | No | ~0.1s |

The architectural insight: Unity discovered that full domain reload is too expensive for rapid iteration, but removing it shifts the burden to developers to manage static state explicitly. This is analogous to the C++ game.dll hot-reload problem where global/static state must be serialized and restored.

### 1.4 How Objects Survive Domain Reload

Unity's internal `InstanceID` system is the key mechanism. Every `UnityEngine.Object` (including MonoBehaviours, GameObjects, ScriptableObjects) has a C++-side native object identified by an `InstanceID` (int32). During domain reload:

1. Native C++ objects are **not destroyed** -- they persist in native memory
2. Managed C# wrappers **are destroyed** (AppDomain unloaded)
3. After reload, Unity creates **new managed wrappers** that point to the same native objects via InstanceID
4. Serialized field data is restored from the binary blob

```
Before Reload:                    After Reload:
┌──────────────────────┐         ┌──────────────────────┐
│ C# MonoBehaviour     │         │ NEW C# MonoBehaviour │
│   m_CachedPtr ───────┼──┐     │   m_CachedPtr ───────┼──┐
│   health = 100       │  │     │   health = 100       │  │ (restored from blob)
│   [NonSerialized]    │  │     │   [NonSerialized]    │  │
│   tempCache = {...}  │  │     │   tempCache = null   │  │ (lost!)
└──────────────────────┘  │     └──────────────────────┘  │
                          │                               │
┌─────────────────────────┴──┐  ┌─────────────────────────┴──┐
│ Native C++ Object          │  │ SAME Native C++ Object     │
│   InstanceID = 12345       │  │   InstanceID = 12345       │
│   Transform, Renderer, etc │  │   Transform, Renderer, etc │
└────────────────────────────┘  └────────────────────────────┘
```

### 1.5 Pattern: **Dual-Layer Identity Survival**

The pattern: maintain identity in a layer that does NOT participate in the reload (native C++ side), and use that identity to reconnect the reloaded layer (managed C#) to persistent state.

### 1.6 UzEngine Implications

UzEngine's game.dll hot-reload protocol (spec: `game-dll-abi.md`) already follows a similar pattern:

1. ECS registry (EnTT) lives in `UzEngine.exe` and survives across `FreeLibrary`/`LoadLibrary`
2. `game_dll_on_stop()` is called before unload (analogous to `beforeAssemblyReload`)
3. `game_dll_on_start()` is called after reload (analogous to `afterAssemblyReload`)

**Gap identified**: The current spec says "Serialize current game state to level_arena scratch buffer" but does not define:
- **What** gets serialized (which ECS components? global game state?)
- **How** serialization works (binary memcpy of POD? JSON? custom visitor?)
- **Where** non-ECS state lives (Lua VM state, timers, event subscriptions)

**Recommendation**: Adopt Unity's `SerializedObject` concept as a **ComponentSerializer** interface:

```cpp
// Each component type that needs hot-reload survival registers a serializer
struct ComponentSerializer {
    ComponentId type;
    void (*serialize)(const void* component, ArenaWriter* writer);
    void (*deserialize)(void* component, ArenaReader* reader);
};
```

For Lua hot-reload (~60ms target): Lua VM is destroyed and rebuilt (like Unity's AppDomain), but Lua `on_save_state()` / `on_restore_state()` callbacks let scripts save/restore transient data. This mirrors Unity's `AssemblyReloadEvents` exactly.

---

## 2. Mono/IL2CPP Backend Architecture

### 2.1 Architecture Overview

Unity abstracts the scripting runtime behind an internal **Scripting Backend** abstraction. The two backends compile from the same C# source but produce radically different executables:

```
                    C# Source (.cs files)
                          │
                    Roslyn Compiler
                          │
                    .NET DLLs (IL bytecode)
                     ╱              ╲
              ┌─────┘                └─────┐
              ▼                            ▼
         Mono Backend                 IL2CPP Backend
         ┌──────────┐               ┌──────────────┐
         │ JIT      │               │ IL2CPP.exe   │
         │ Compiler │               │ transpiler   │
         │          │               │ IL → C++     │
         └──────────┘               └──────────────┘
              │                            │
              ▼                            ▼
         Native code                  C++ source
         (at runtime)                      │
                                    C++ Compiler
                                    (Clang/MSVC)
                                           │
                                           ▼
                                    Native binary
                                    (AOT compiled)
```

### 2.2 IUnityInterfaces: The Native Plugin C API

Unity provides a stable C API for native plugins (`IUnityInterfaces`) that is remarkably similar to UzEngine's `GameContext` pattern:

```c
// Unity's native plugin interface (simplified from IUnityInterface.h)
typedef struct IUnityInterfaces {
    // Get a specific interface by GUID
    void* (*GetInterface)(UnityInterfaceGUID guid);
    // Register a new interface (for inter-plugin communication)
    void (*RegisterInterface)(UnityInterfaceGUID guid, void* ptr);
} IUnityInterfaces;

// Plugin entry point — called by Unity at load time
// This is the C ABI boundary between Unity (C++) and the plugin (C/C++)
void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    IUnityGraphics* graphics = unityInterfaces->GetInterface(
        IUnityGraphics_GUID);

    graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

    // Access the D3D12 device directly
    IUnityGraphicsD3D12v7* d3d12 = unityInterfaces->GetInterface(
        IUnityGraphicsD3D12v7_GUID);
    ID3D12Device* device = d3d12->GetDevice();
    ID3D12CommandQueue* queue = d3d12->GetCommandQueue();

    // Now the plugin can submit its own GPU work
}

// Lifecycle callbacks
void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginUnload()
{
    // Cleanup native resources
}
```

### 2.3 P/Invoke: The C# to C++ Boundary

Unity uses standard .NET P/Invoke for crossing the managed/native boundary. This is the equivalent of calling C functions from a higher-level scripting layer:

```csharp
// C# side: declare the native function
public class NativePhysics
{
    // Calls into a C function exported from a native DLL
    [DllImport("physics_plugin", CallingConvention = CallingConvention.Cdecl)]
    private static extern int RaycastBatch(
        [In] RaycastInput[] inputs,
        [Out] RaycastResult[] results,
        int count);

    // Managed wrapper that handles marshaling and validation
    public static RaycastResult[] BatchRaycast(RaycastInput[] inputs)
    {
        var results = new RaycastResult[inputs.Length];
        int hitCount = RaycastBatch(inputs, results, inputs.Length);
        return results;
    }
}

// C side: the actual implementation
// Compiled into physics_plugin.dll
extern "C" __declspec(dllexport)
int RaycastBatch(const RaycastInput* inputs, RaycastResult* results, int count)
{
    int hits = 0;
    for (int i = 0; i < count; i++) {
        if (physics_world->Raycast(inputs[i], &results[i])) {
            hits++;
        }
    }
    return hits;
}
```

**Key marshaling rules**:
- Blittable types (int, float, struct of blittable) cross the boundary with **zero copy** -- the CLR pins the managed array and passes the pointer directly
- Non-blittable types (string, class, arrays of objects) require marshaling, which allocates and copies
- `[StructLayout(LayoutKind.Sequential)]` ensures C# struct layout matches C struct layout

### 2.4 Burst Compiler: C# to SIMD Native Code

Burst is Unity's LLVM-based AOT compiler for a subset of C# (no GC allocations, no managed objects, no virtual calls). It transforms Jobs from C# into highly optimized SIMD native code:

```csharp
[BurstCompile(CompileSynchronously = true)]
struct TransformUpdateJob : IJobParallelFor
{
    // NativeArray<T> is a struct wrapping a native pointer — zero GC
    [ReadOnly] public NativeArray<float4x4> ParentWorldMatrices;
    [ReadOnly] public NativeArray<float4x4> LocalMatrices;
    public NativeArray<float4x4> WorldMatrices;  // output

    public void Execute(int index)
    {
        // Burst compiles this to AVX2 matrix multiply instructions
        WorldMatrices[index] = math.mul(
            ParentWorldMatrices[index],
            LocalMatrices[index]);
    }
}

// Scheduling:
var job = new TransformUpdateJob
{
    ParentWorldMatrices = parentArray,
    LocalMatrices = localArray,
    WorldMatrices = outputArray,
};
// Splits work across all worker threads, 64 items per batch
JobHandle handle = job.Schedule(entityCount, batchSize: 64);
```

Burst compiles this to code equivalent to hand-written C++ SIMD intrinsics:

```cpp
// What Burst generates (conceptual — actual output is LLVM IR → native):
void TransformUpdateJob_Execute_Burst(
    const float4x4* parents, const float4x4* locals,
    float4x4* output, int begin, int end)
{
    for (int i = begin; i < end; i += 4) // vectorized loop
    {
        __m256 p_row0 = _mm256_load_ps(&parents[i].c0.x);  // AVX2 load
        __m256 l_row0 = _mm256_load_ps(&locals[i].c0.x);
        // ... matrix multiply via FMA instructions ...
        _mm256_store_ps(&output[i].c0.x, result);
    }
}
```

### 2.5 Pattern: **Tiered Compilation Boundary**

The pattern: provide multiple compilation tiers (interpreted → JIT → AOT/SIMD) behind the same source language. User code is written once; the compiler selects the optimal tier based on context.

- **Editor**: Mono JIT for fast iteration (no compilation step)
- **Build (mobile/console)**: IL2CPP for AOT native code (no JIT needed at runtime)
- **Hot paths (Jobs)**: Burst for SIMD-optimized native code

### 2.6 UzEngine Implications

UzEngine's equivalent tiered system:

| Unity Tier | UzEngine Equivalent | Speed | Use Case |
|-----------|-------------------|-------|----------|
| C# Mono JIT | Lua (LuaJIT interpreter) | Instant reload | Prototyping, NPC scripts |
| C# IL2CPP AOT | game.dll C++ (MSVC/Clang) | ~2.5s rebuild | Game systems, physics callbacks |
| Burst SIMD | Engine C++ with intrinsics | Full rebuild | Core systems, rendering |

**Key insight from IUnityInterfaces**: Unity's `GetInterface(GUID)` pattern is a **capability discovery** mechanism. UzEngine's `GameContext` is currently a flat struct of function pointers. As it grows, consider a GUID-based interface query:

```c
// Instead of adding fields to GameContext forever:
typedef struct GameContext {
    uint32_t version;
    void* (*query_interface)(const char* interface_id);
    // ... minimal core fields ...
} GameContext;

// Usage in game.dll:
auto* audio = (AudioAPI*)ctx->query_interface("uz.audio.v1");
auto* physics = (PhysicsAPI*)ctx->query_interface("uz.physics.v1");
```

This prevents GameContext from becoming a monolithic struct that breaks ABI on every addition.

**Key insight from P/Invoke**: For Lua C API bindings, UzEngine should follow the same blittable-type principle. All data crossing the Lua/C++ boundary should be POD structs (Vec3, Quat, Mat4) that can be passed by value without marshaling overhead.

---

## 3. Animation System (Animator + Playables API)

### 3.1 Animator Controller: State Machine Architecture

Unity's Animator Controller is a **graph-based state machine** with layers, sub-state machines, blend trees, and parameter-driven transitions. The internal architecture:

```
AnimatorController (asset)
├── Parameters (float speed, bool isGrounded, trigger jump)
├── Layer 0: Base Layer (weight=1.0, blendMode=Override)
│   ├── State: Idle
│   │   └── Motion: idle_clip.anim
│   ├── State: Locomotion
│   │   └── Motion: BlendTree (1D, parameter=speed)
│   │       ├── 0.0 → idle_clip.anim
│   │       ├── 0.5 → walk_clip.anim
│   │       └── 1.0 → run_clip.anim
│   ├── State: Jump
│   │   └── Motion: jump_clip.anim (exitTime=0.8)
│   ├── Transition: Idle→Locomotion (condition: speed > 0.1)
│   ├── Transition: Locomotion→Idle (condition: speed < 0.05)
│   └── AnyState→Jump (condition: trigger jump, canTransitionToSelf=false)
│
├── Layer 1: Upper Body (weight=1.0, blendMode=Override, mask=UpperBody)
│   ├── State: Empty (default — upper body uses base layer)
│   ├── State: Aim (motion: aim_overlay.anim)
│   └── Transition: Empty→Aim (condition: bool isAiming)
│
└── Layer 2: Additive Hits (weight=1.0, blendMode=Additive)
    ├── State: NoHit (empty)
    └── State: HitReact (motion: hit_flinch_additive.anim)
```

**Blend Trees** are recursive structures within a state. A 2D blend tree samples motion based on two parameters:

```csharp
// Runtime interaction:
Animator animator = GetComponent<Animator>();

// Set parameters that drive transitions and blend trees
animator.SetFloat("speed", characterVelocity.magnitude);
animator.SetBool("isGrounded", controller.isGrounded);

// Trigger a one-shot transition
if (Input.GetKeyDown(KeyCode.Space))
    animator.SetTrigger("jump");

// Layer weight control (smooth transition for upper body override)
float aimWeight = Mathf.MoveTowards(
    animator.GetLayerWeight(1),
    isAiming ? 1f : 0f,
    Time.deltaTime * 5f);
animator.SetLayerWeight(1, aimWeight);
```

### 3.2 Playables API: Manual Animation Graph

The Playables API (introduced Unity 2017) gives full programmatic control over animation evaluation. It replaces the Animator Controller with a code-built graph:

```csharp
public class CustomAnimationController : MonoBehaviour
{
    PlayableGraph _graph;
    AnimationMixerPlayable _locomotionMixer;
    AnimationMixerPlayable _layerMixer;
    AnimationClipPlayable _idlePlayable;
    AnimationClipPlayable _walkPlayable;
    AnimationClipPlayable _runPlayable;

    void Start()
    {
        _graph = PlayableGraph.Create("CustomAnimation");

        // Create clip playables
        _idlePlayable = AnimationClipPlayable.Create(_graph, idleClip);
        _walkPlayable = AnimationClipPlayable.Create(_graph, walkClip);
        _runPlayable  = AnimationClipPlayable.Create(_graph, runClip);

        // Locomotion blend (manual 1D blend tree)
        _locomotionMixer = AnimationMixerPlayable.Create(_graph, 3);
        _graph.Connect(_idlePlayable, 0, _locomotionMixer, 0);
        _graph.Connect(_walkPlayable, 0, _locomotionMixer, 1);
        _graph.Connect(_runPlayable,  0, _locomotionMixer, 2);

        // Upper body layer mixer
        _layerMixer = AnimationLayerMixerPlayable.Create(_graph, 2);
        _graph.Connect(_locomotionMixer, 0, _layerMixer, 0);
        // Port 1: upper body override (connected later when aiming)
        _layerMixer.SetInputWeight(0, 1f);
        _layerMixer.SetLayerMaskFromAvatarMask(1, upperBodyMask);

        // Output to the Animator component
        var output = AnimationPlayableOutput.Create(_graph, "output",
            GetComponent<Animator>());
        output.SetSourcePlayable(_layerMixer);

        _graph.Play();
    }

    void Update()
    {
        // Manual blend tree: compute weights based on speed
        float speed = _characterController.velocity.magnitude;
        float idleWeight = Mathf.Clamp01(1f - speed * 2f);
        float walkWeight = Mathf.Clamp01(speed * 2f) *
                           Mathf.Clamp01(2f - speed);
        float runWeight  = Mathf.Clamp01(speed - 1f);

        // Normalize weights (must sum to 1.0)
        float total = idleWeight + walkWeight + runWeight;
        _locomotionMixer.SetInputWeight(0, idleWeight / total);
        _locomotionMixer.SetInputWeight(1, walkWeight / total);
        _locomotionMixer.SetInputWeight(2, runWeight / total);
    }

    void OnDestroy()
    {
        if (_graph.IsValid())
            _graph.Destroy();
    }
}
```

### 3.3 Avatar and Humanoid Retargeting

Unity's humanoid retargeting maps any skeleton to a canonical **Avatar** bone structure (55 bones for humanoid). This allows animations created for one character to play on any other humanoid:

```
Source Skeleton                  Avatar (canonical)              Target Skeleton
┌──────────────┐               ┌──────────────────┐            ┌──────────────┐
│ mixamorig:   │   Mapping     │ Humanoid Bones:  │  Mapping   │ UE4_Mannequin│
│   Hips       │──────────────►│   Hips           │───────────►│   pelvis     │
│   Spine      │               │   Spine          │            │   spine_01   │
│   Spine1     │               │   Chest          │            │   spine_02   │
│   Neck       │               │   Neck           │            │   neck_01    │
│   Head       │               │   Head           │            │   head       │
│   LeftShoulder│              │   LeftUpperArm   │            │   upperarm_l │
│   LeftArm    │               │   LeftLowerArm   │            │   lowerarm_l │
│   LeftHand   │               │   LeftHand       │            │   hand_l     │
└──────────────┘               └──────────────────┘            └──────────────┘

1. Retarget animation from source → avatar space (normalize bone lengths)
2. Apply avatar-space pose to target (scale to target bone lengths)
```

The key API:

```csharp
// Avatar Mask: defines which bones participate in a layer
AvatarMask mask = new AvatarMask();
mask.SetHumanoidBodyPartActive(AvatarMaskBodyPart.LeftArm, true);
mask.SetHumanoidBodyPartActive(AvatarMaskBodyPart.RightArm, true);
mask.SetHumanoidBodyPartActive(AvatarMaskBodyPart.Head, true);
// Lower body excluded — uses base locomotion layer

// Apply mask to animation layer
animator.SetLayerWeight(1, 1.0f);
// Mask is set on the AnimatorController asset in editor
```

### 3.4 AnimationJob API: Custom IK with Burst

The `IAnimationJob` interface lets users write custom animation processing (IK, procedural motion) that runs inside the animation evaluation pipeline with Burst compilation:

```csharp
[BurstCompile]
struct FootIKJob : IAnimationJob
{
    // Bone handles obtained from the Animator
    public TransformStreamHandle leftFootBone;
    public TransformStreamHandle rightFootBone;
    public TransformStreamHandle leftKneeBone;
    public TransformStreamHandle rightKneeBone;
    public TransformSceneHandle  rootBone;

    // IK targets from raycasts (set before job runs)
    public NativeArray<float3> footTargets;  // [0]=left, [1]=right

    public void ProcessAnimation(AnimationStream stream)
    {
        // Read current bone positions (from animation evaluation)
        float3 leftFoot  = leftFootBone.GetPosition(stream);
        float3 rightFoot = rightFootBone.GetPosition(stream);

        // Two-bone IK: adjust leg chain to reach raycast hit point
        SolveTwoBoneIK(
            stream, leftKneeBone, leftFootBone,
            footTargets[0], // target from ground raycast
            float3(0, 0, 1) // pole vector hint (knee forward)
        );

        SolveTwoBoneIK(
            stream, rightKneeBone, rightFootBone,
            footTargets[1],
            float3(0, 0, 1)
        );

        // Adjust root height so feet reach the ground
        float3 rootPos = rootBone.GetPosition(stream);
        float lowestOffset = math.min(
            footTargets[0].y - leftFoot.y,
            footTargets[1].y - rightFoot.y);
        rootBone.SetPosition(stream,
            rootPos + new float3(0, lowestOffset, 0));
    }

    public void ProcessRootMotion(AnimationStream stream) { }

    static void SolveTwoBoneIK(
        AnimationStream stream,
        TransformStreamHandle mid,
        TransformStreamHandle end,
        float3 target, float3 pole)
    {
        // Analytical two-bone IK (same math as UzEngine's two_bone_ik)
        // Uses law of cosines to compute joint angles
        // Burst compiles this to ~20 SIMD instructions
    }
}
```

### 3.5 Pattern: **Layered Evaluation Pipeline with Injectable Stages**

The pattern: animation evaluation proceeds through a fixed pipeline (sample clips → blend tree → state machine → layer mixing → IK → output), but each stage is an injectable/replaceable node. The Playables API makes this explicit -- each `Playable` is a processing node in a DAG.

```
ClipPlayable(idle) ──┐
ClipPlayable(walk) ──┤── MixerPlayable ── LayerMixer ── IKJob ── Output
ClipPlayable(run)  ──┘       │                 │
                      weights from          mask from
                      blend tree logic      AvatarMask
```

### 3.6 UzEngine Implications

UzEngine's animation spec (`animation-system.md`) already defines:
- AnimationClip with BoneChannels (equivalent to Unity's AnimationClip)
- AnimStateMachine with states, transitions, conditions (equivalent to Animator Controller)
- blend_poses() for cross-fade (equivalent to Unity's MixerPlayable)
- BlendMask for upper/lower body (equivalent to AvatarMask)
- Two-bone IK and FABRIK (equivalent to AnimationJob IK)

**Gap: No equivalent to Playables API.** The current spec uses a monolithic `AnimStateMachine` struct. Unity's Playables API teaches us that exposing the animation graph as composable nodes enables:
1. Custom blend logic without modifying engine code
2. Procedural animation layers (IK, look-at, aim offset) as graph nodes
3. Runtime graph modification (add/remove nodes for gameplay effects)

**Recommendation**: Design the animation system with a `AnimNode` base class that forms a DAG:

```cpp
struct AnimNode {
    virtual void evaluate(float dt, Pose& out) = 0;
};

struct AnimClipNode    : AnimNode { /* samples a clip */ };
struct AnimBlendNode   : AnimNode { /* blends N children */ };
struct AnimStateMachine : AnimNode { /* state machine with transitions */ };
struct AnimIKNode      : AnimNode { /* applies IK on top of input pose */ };
```

This is more composable than the current flat `AnimStateMachine` struct and matches how Unity, UE5, and Godot all structure their animation pipelines.

**Gap: No humanoid retargeting.** UzEngine targets custom skeletons per mesh, not a canonical humanoid mapping. This is fine for Phase 3 but should be noted as a future gap if UzEngine ever supports marketplace/asset-store animations.

---

## 4. PhysX Integration Pattern

### 4.1 Architecture: Physics Step Synchronization

Unity runs physics on a **fixed timestep** (default 50 Hz / 20ms) decoupled from the rendering frame rate. The sync model:

```
Frame N (variable dt, e.g., 16.6ms at 60 FPS):
┌─────────────────────────────────────────────────┐
│ Update()           │ Multiple FixedUpdate()     │ LateUpdate()        │
│ (game logic)       │ calls if dt accumulated    │ (camera, post-fx)   │
│                    │                            │                     │
│ Read physics       │ FixedUpdate() #1           │ Interpolate for     │
│ results from       │   user code               │ render (optional)    │
│ last step          │   Physics.Simulate()       │                     │
│                    │   Collision callbacks       │                     │
│                    │                            │                     │
│                    │ FixedUpdate() #2           │                     │
│                    │   (if accumulated time     │                     │
│                    │    >= 2 × fixedDeltaTime)  │                     │
└─────────────────────────────────────────────────┘
```

The accumulator pattern:

```csharp
// Unity's internal loop (simplified)
float accumulator = 0f;

void EngineLoop(float frameDeltaTime)
{
    accumulator += frameDeltaTime;

    // Update() — once per frame
    CallUpdate(frameDeltaTime);

    // FixedUpdate() + Physics.Simulate() — may run 0, 1, or N times
    while (accumulator >= Time.fixedDeltaTime)
    {
        CallFixedUpdate(Time.fixedDeltaTime);
        Physics.Simulate(Time.fixedDeltaTime);
        Physics.SyncTransforms(); // write PhysX results back to Transform
        accumulator -= Time.fixedDeltaTime;
    }

    // LateUpdate() — once per frame, after all physics
    CallLateUpdate(frameDeltaTime);

    // Render with interpolated transforms
    float alpha = accumulator / Time.fixedDeltaTime;
    InterpolateRenderTransforms(alpha);
}
```

### 4.2 PhysicsScene: Multi-Scene Physics

Unity 2018+ supports multiple independent physics worlds via `PhysicsScene`. This enables:
- Prediction simulation (run physics forward without affecting the main scene)
- Isolated physics for UI or mini-games
- Server-side physics simulation separate from client visual physics

```csharp
// Create a separate physics scene for server-side prediction
Scene predictionScene = SceneManager.CreateScene(
    "Prediction", new CreateSceneParameters(LocalPhysicsMode.Physics3D));
PhysicsScene predictionPhysics = predictionScene.GetPhysicsScene();

// Copy relevant bodies to prediction scene
// ... (instantiate copies of dynamic bodies)

// Step prediction physics independently
for (int i = 0; i < predictionSteps; i++)
{
    predictionPhysics.Simulate(Time.fixedDeltaTime);
}

// Read predicted positions
Vector3 predictedPos = predictionBody.transform.position;
```

### 4.3 Collision Callbacks via Unity Message System

Unity's collision callbacks use the **Message** system (SendMessage-based polymorphism). When PhysX detects a collision, Unity internally calls `SendMessage("OnCollisionEnter", collision)` on both GameObjects involved:

```csharp
public class DamageReceiver : MonoBehaviour
{
    // Called by Unity's message system when PhysX detects collision enter
    void OnCollisionEnter(Collision collision)
    {
        // collision.contacts[0].point — world-space contact point
        // collision.contacts[0].normal — contact normal
        // collision.relativeVelocity — relative velocity at impact
        // collision.gameObject — the other object

        float impactForce = collision.impulse.magnitude;
        if (impactForce > damageThreshold)
        {
            TakeDamage(impactForce * damageMultiplier);
            SpawnImpactVFX(collision.contacts[0].point,
                           collision.contacts[0].normal);
        }
    }

    void OnCollisionStay(Collision collision)
    {
        // Called every fixed update while objects remain in contact
        // Used for: friction sounds, grinding particles, pressure plates
    }

    void OnCollisionExit(Collision collision)
    {
        // Contact broken — stop friction effects
    }

    // Trigger versions (for overlap detection without physics response)
    void OnTriggerEnter(Collider other) { /* volume entered */ }
    void OnTriggerStay(Collider other)  { /* inside volume */ }
    void OnTriggerExit(Collider other)  { /* volume exited */ }
}
```

### 4.4 Pattern: **Fixed-Step Accumulator with Render Interpolation**

The core pattern: physics runs at a fixed rate for determinism, while rendering interpolates between the last two physics states for smooth visuals at any frame rate.

```
Physics state at t=0.00: pos_A
Physics state at t=0.02: pos_B (fixedDeltaTime = 0.02)
Render frame at t=0.015: lerp(pos_A, pos_B, 0.015/0.02) = lerp(A, B, 0.75)
```

### 4.5 UzEngine Implications

UzEngine's physics spec (`physics-system.md`) uses Jolt Physics with a similar fixed-step model. The current `game_dll_on_fixed_update(float fixed_dt)` export at 60 Hz matches Unity's FixedUpdate concept.

**Gap: No render interpolation.** UzEngine's current spec does not mention interpolating transform positions between physics steps for smooth rendering. Without this, objects visually "stutter" at physics rates below the render frame rate.

**Recommendation**: Add transform interpolation:

```cpp
// After physics step, store previous and current transforms
struct CPhysicsInterpolation {
    Vec3 prev_position;
    Quat prev_rotation;
    Vec3 curr_position;
    Quat curr_rotation;
};

// In render submission, interpolate based on accumulator remainder
float alpha = physics_accumulator / fixed_dt;
Vec3 render_pos = lerp(interp.prev_position, interp.curr_position, alpha);
Quat render_rot = slerp(interp.prev_rotation, interp.curr_rotation, alpha);
```

**Gap: No multi-scene physics.** Not needed for Phase 3, but valuable for Phase 4+ (multiplayer prediction, isolated physics).

**Match: Collision callback pattern.** UzEngine already uses EventBus for physics events, which is equivalent to Unity's Message-based collision callbacks. The pattern is the same: physics engine detects contact → engine dispatches event → user code handles.

---

## 5. Addressables / Asset Streaming

### 5.1 Evolution: AssetBundles to Addressables

Unity's asset loading has evolved through three generations:

```
Gen 1: Resources.Load<T>("path")
  │  Pros: Simple, synchronous
  │  Cons: All assets in Resources/ included in build, no streaming
  │
Gen 2: AssetBundle.LoadFromFile("bundle.ab")
  │  Pros: Streaming, downloadable, shared dependencies
  │  Cons: Manual dependency management, manual memory management
  │
Gen 3: Addressables.LoadAssetAsync<T>("address")
     Pros: Automatic deps, reference counting, async, remote+local
     Cons: Complex configuration, async-everywhere pattern
```

### 5.2 Addressables Internal Architecture

```
                    ┌─────────────────────────────┐
                    │   Addressables API           │
                    │   LoadAssetAsync<T>(key)     │
                    └──────────┬──────────────────┘
                               │
                    ┌──────────▼──────────────────┐
                    │   Resource Manager           │
                    │   ┌─ Locate providers        │
                    │   │  (where is the asset?)   │
                    │   ├─ Load providers           │
                    │   │  (how to load it?)        │
                    │   └─ Ref counting tracker     │
                    └──────────┬──────────────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
    ┌─────────▼──────┐ ┌──────▼───────┐ ┌──────▼──────────┐
    │ Local Provider  │ │ Remote CDN   │ │ Built-in Scene  │
    │ (file:// or     │ │ (http://     │ │ Provider        │
    │  StreamingAssets)│ │  cdn.game.co)│ │                 │
    └────────────────┘ └──────────────┘ └─────────────────┘
```

### 5.3 AsyncOperationHandle Pattern

All Addressables operations return an `AsyncOperationHandle<T>` — Unity's primary async pattern for asset loading:

```csharp
public class WeaponLoader : MonoBehaviour
{
    private AsyncOperationHandle<GameObject> _weaponHandle;

    public async void LoadWeapon(string weaponAddress)
    {
        // Option 1: await pattern (requires UniTask or Addressables 1.19+)
        _weaponHandle = Addressables.LoadAssetAsync<GameObject>(weaponAddress);
        GameObject prefab = await _weaponHandle.Task;
        Instantiate(prefab, transform);

        // Option 2: callback pattern
        Addressables.LoadAssetAsync<GameObject>(weaponAddress).Completed +=
            (AsyncOperationHandle<GameObject> handle) =>
            {
                if (handle.Status == AsyncOperationStatus.Succeeded)
                {
                    Instantiate(handle.Result, transform);
                }
                else
                {
                    Debug.LogError($"Failed to load weapon: {weaponAddress}");
                }
            };
    }

    void OnDestroy()
    {
        // CRITICAL: Must release to decrement reference count
        // If ref count reaches 0, the underlying AssetBundle is unloaded
        if (_weaponHandle.IsValid())
            Addressables.Release(_weaponHandle);
    }
}
```

### 5.4 Reference Counting for Automatic Unload

Addressables tracks reference counts per loaded asset. When the count reaches zero, the asset and its AssetBundle can be unloaded:

```csharp
// Internal reference counting (simplified)
class AddressablesRefCounter
{
    Dictionary<string, int> _refCounts = new();
    Dictionary<string, AssetBundle> _loadedBundles = new();

    public void IncrementRef(string bundleKey)
    {
        _refCounts.TryGetValue(bundleKey, out int count);
        _refCounts[bundleKey] = count + 1;
    }

    public void DecrementRef(string bundleKey)
    {
        if (!_refCounts.TryGetValue(bundleKey, out int count)) return;
        count--;
        if (count <= 0)
        {
            // All references released — safe to unload
            _loadedBundles[bundleKey].Unload(true);
            _loadedBundles.Remove(bundleKey);
            _refCounts.Remove(bundleKey);
        }
        else
        {
            _refCounts[bundleKey] = count;
        }
    }
}
```

### 5.5 Streaming World / Scene Streaming

Unity's scene streaming loads scenes additively and asynchronously:

```csharp
public class WorldStreamer : MonoBehaviour
{
    [Serializable]
    public struct StreamingZone
    {
        public string sceneAddress;  // Addressable key
        public Bounds bounds;        // AABB in world space
        public float loadDistance;
        public float unloadDistance;
    }

    public StreamingZone[] zones;
    private Dictionary<string, AsyncOperationHandle<SceneInstance>> _loadedScenes;

    void Update()
    {
        Vector3 playerPos = player.transform.position;

        foreach (var zone in zones)
        {
            float dist = Vector3.Distance(playerPos,
                zone.bounds.ClosestPoint(playerPos));
            bool isLoaded = _loadedScenes.ContainsKey(zone.sceneAddress);

            if (!isLoaded && dist < zone.loadDistance)
            {
                // Start loading — non-blocking, runs on background thread
                var handle = Addressables.LoadSceneAsync(
                    zone.sceneAddress,
                    LoadSceneMode.Additive);
                _loadedScenes[zone.sceneAddress] = handle;
            }
            else if (isLoaded && dist > zone.unloadDistance)
            {
                // Unload — also async
                Addressables.UnloadSceneAsync(
                    _loadedScenes[zone.sceneAddress]);
                _loadedScenes.Remove(zone.sceneAddress);
            }
        }
    }
}
```

### 5.6 Pattern: **Address-Based Indirection with Ref-Counted Lifetime**

The pattern: assets are identified by a stable address (string key), loaded asynchronously, tracked by reference count, and automatically released when no longer needed. The system is provider-based so the same API works for local files, remote CDN, and built-in resources.

### 5.7 UzEngine Implications

UzEngine's asset pipeline spec (`asset-pipeline.md`) defines:
- `res://` path-based asset references
- `ResourceManager::get()` / `ResourceManager::load()` with atomic ref counting
- Background job loading via JobDispatcher

**Gap: No async operation handle.** The current spec returns `const void*` from `asset_load_raw()` synchronously. There is no async completion callback or Future/Promise pattern for game.dll code to know when an asset is ready.

**Recommendation**: Add an `AssetHandle` that wraps the async state:

```c
typedef struct AssetHandle { uint64_t id; } AssetHandle;
typedef void (*AssetLoadCallback)(AssetHandle handle, const void* data,
                                   size_t size, void* userdata);

// In GameContext:
AssetHandle (*asset_load_async)(const char* res_path,
                                AssetLoadCallback on_complete,
                                void* userdata);
bool        (*asset_is_ready)(AssetHandle handle);
const void* (*asset_get_data)(AssetHandle handle, size_t* out_size);
void        (*asset_release)(AssetHandle handle);  // decrement ref count
```

**Gap: No scene streaming.** The current `.uzscene` spec loads entire scenes. Phase 4 should add sub-scene streaming with distance-based load/unload zones.

---

## 6. Profiler / Memory Profiler Architecture

### 6.1 Unity Profiler Architecture

Unity's profiler has two tiers:

1. **Built-in Profiler**: Always-on lightweight sampling with minimal overhead (~0.1ms/frame). Captures CPU timings, GPU timings, memory allocations, draw calls, physics contacts.

2. **Deep Profile**: Instruments every managed method call. High overhead (~10-50x slowdown). Uses Mono's profiling hooks to intercept every method entry/exit.

```
┌─────────────────────────────────────────────────┐
│  Game Process                                    │
│                                                  │
│  ┌─────────────────────────────────────────┐    │
│  │ Profiler Recorder (C++ native)          │    │
│  │  ┌─ Frame timeline (sampled markers)    │    │
│  │  ├─ Memory tracker (alloc/free counts)  │    │
│  │  ├─ GPU queries (timestamp + pipeline)  │    │
│  │  └─ Custom counters (user-defined)      │    │
│  └────────────────┬────────────────────────┘    │
│                   │ Binary protocol              │
│                   │ (ring buffer, zero-alloc)     │
│                   ▼                              │
│  ┌─────────────────────────────────────────┐    │
│  │ Profiler Transport (socket or file)     │    │
│  │  → Editor Profiler Window               │    │
│  │  → .raw file for offline analysis        │    │
│  └─────────────────────────────────────────┘    │
└─────────────────────────────────────────────────┘
```

### 6.2 ProfilerRecorder API for Custom Samples

Unity 2020+ introduced `ProfilerRecorder` for zero-allocation profiling of custom metrics:

```csharp
public class CustomProfiler : MonoBehaviour
{
    // Static profiler markers — defined once, used per frame
    static readonly ProfilerMarker s_AIUpdate =
        new ProfilerMarker("MyGame.AIUpdate");
    static readonly ProfilerMarker s_PathFind =
        new ProfilerMarker("MyGame.Pathfinding");

    // Custom counter (appears in Profiler window)
    static readonly ProfilerCounterValue<int> s_ActiveNPCs =
        new ProfilerCounterValue<int>(
            ProfilerCategory.Scripts,
            "Active NPCs",
            ProfilerMarkerDataUnit.Count);

    void Update()
    {
        // Scoped marker — auto-begins on construction, ends on Dispose
        using (s_AIUpdate.Auto())
        {
            int activeCount = 0;
            foreach (var npc in npcs)
            {
                if (!npc.isActive) continue;
                activeCount++;

                using (s_PathFind.Auto())
                {
                    npc.UpdatePathfinding();
                }

                npc.UpdateBehaviorTree();
            }
            s_ActiveNPCs.Value = activeCount;
        }
    }
}

// Reading profiler data programmatically:
var recorder = ProfilerRecorder.StartNew(
    ProfilerCategory.Render, "Draw Calls Count");
// ... later ...
int drawCalls = (int)recorder.LastValue;
recorder.Dispose();
```

### 6.3 Memory Profiler: Snapshot Architecture

Unity's Memory Profiler package captures full heap snapshots. The snapshot includes:

```
Memory Snapshot Contents:
├── Managed Heap
│   ├── All managed objects (C# objects, arrays, strings)
│   ├── GC generation info
│   └── Managed-to-native references (InstanceID links)
├── Native Memory
│   ├── All native allocations (textures, meshes, audio clips)
│   ├── Root references (who holds what alive)
│   └── Allocation stack traces (debug builds only)
├── Metadata
│   ├── Type descriptors (size, field offsets, names)
│   ├── Connection graph (what references what)
│   └── Native object type tree
└── Statistics
    ├── Total managed memory, total native memory
    ├── Per-type memory breakdown
    └── Fragmentation analysis
```

The snapshot process:

```csharp
// Trigger a snapshot from code
using Unity.MemoryProfiler;

MemoryProfiler.TakeSnapshot(
    "snapshot_frame_1234.snap",
    (string path, bool success) =>
    {
        if (success)
            Debug.Log($"Snapshot saved: {path}");
    },
    CaptureFlags.ManagedObjects |
    CaptureFlags.NativeObjects |
    CaptureFlags.NativeAllocations);

// Internally, Unity:
// 1. Pauses the world (GC.Collect + stop all threads)
// 2. Walks the entire managed heap (every GC-tracked object)
// 3. Walks native allocation tracking (every malloc'd block)
// 4. Serializes the connection graph (who references whom)
// 5. Writes binary .snap file
// 6. Resumes the world
// Total pause: 50-200ms depending on heap size
```

### 6.4 NativeLeakDetection

Unity's native memory leak detector tracks unmanaged allocations:

```csharp
// Enable detailed leak detection (editor only, significant overhead)
NativeLeakDetection.Mode = NativeLeakDetectionMode.EnabledWithStackTrace;

// When a NativeArray or NativeList is not disposed:
// Unity logs: "A Native Collection has not been disposed, resulting in a
//              memory leak. Allocated at: StackTrace..."

// The safety system uses AtomicSafetyHandle to track:
// 1. Was Dispose() ever called?
// 2. Has the allocator scope (frame/temp/persistent) expired?
// 3. Is the container being accessed after Dispose?

// Example of properly managed native memory:
NativeArray<float> temps = new NativeArray<float>(
    1024, Allocator.TempJob);  // Temp = must dispose within 4 frames
try
{
    var job = new MyJob { Data = temps };
    job.Schedule(1024, 64).Complete();
}
finally
{
    temps.Dispose();  // If this is missed, leak detected!
}
```

### 6.5 Pattern: **Hierarchical Profiler with Zero-Allocation Hot Path**

The pattern: profiler markers are statically declared (compile-time cost), scope-based (RAII begin/end), and write to a lock-free ring buffer. The hot path (marker begin/end) costs ~50ns and performs zero allocations. Analysis, visualization, and transport happen asynchronously, never on the game thread.

### 6.6 UzEngine Implications

UzEngine's debug/profiling spec (`debug-profiling.md`) already uses Tracy with `UZ_PROFILE_ZONE("name")` macros. Tracy's architecture matches Unity's pattern closely: lock-free ring buffer, per-thread zones, sub-100ns overhead.

**Gap: No custom counters.** Tracy supports `TracyPlot()` for scalar values, but UzEngine's spec only wraps `ZoneScoped` / `ZoneScopedN`. Should also wrap:

```cpp
// Expose custom counter macros alongside zone macros:
#define UZ_PROFILE_COUNTER(name, val)    TracyPlot(name, val)
#define UZ_PROFILE_COUNTER_INT(name, val) TracyPlot(name, (int64_t)val)
```

**Gap: No memory snapshot capability.** UzEngine uses arena allocators (Frame/Level/Permanent) with debug tracking (`AllocRecord`), but there is no "take snapshot" command that dumps the full allocation state to a file for offline analysis.

**Recommendation**: Add a `engine.memory_snapshot` IPC command that:
1. Dumps all arena high-water marks and per-allocation records
2. Dumps GPU resource tracking (textures, buffers, their sizes)
3. Writes to a JSON file for analysis in VSCode
4. Total cost: <10ms (arena records are already tracked in debug builds)

**Gap: No native leak detection equivalent.** UzEngine's `Handle<T>` and arena system should have a debug mode that flags leaked handles (valid handles that were never released before the owning scope/arena was reset).

---

## 7. DOTS Job System Scheduler (Deep Dive)

### 7.1 IJobParallelFor Scheduling Granularity

Unity's `IJobParallelFor` splits work into batches distributed across worker threads. The `innerloopBatchCount` parameter controls granularity:

```csharp
[BurstCompile]
struct CullingJob : IJobParallelFor
{
    [ReadOnly] public NativeArray<Bounds> ObjectBounds;
    [ReadOnly] public NativeArray<Plane>  FrustumPlanes; // 6 planes
    public NativeArray<byte> VisibilityResults;           // 1 = visible

    public void Execute(int index)
    {
        // Test AABB against frustum planes
        Bounds b = ObjectBounds[index];
        bool visible = true;
        for (int p = 0; p < 6; p++)
        {
            float d = math.dot(FrustumPlanes[p].normal,
                               b.center) + FrustumPlanes[p].distance;
            float r = math.dot(math.abs(FrustumPlanes[p].normal),
                               b.extents);
            if (d + r < 0) { visible = false; break; }
        }
        VisibilityResults[index] = visible ? (byte)1 : (byte)0;
    }
}

// Schedule with batch size tuning:
// Small batch (32): more parallelism, more overhead per batch
// Large batch (256): less parallelism, less overhead, better cache
var cullingJob = new CullingJob {
    ObjectBounds = boundsArray,
    FrustumPlanes = frustumArray,
    VisibilityResults = visResults
};

// 64 is typical — balances parallelism with cache locality
JobHandle cullingHandle = cullingJob.Schedule(
    objectCount,           // total items
    innerloopBatchCount: 64  // items per work-steal unit
);
```

Internally, Unity creates `ceil(objectCount / batchCount)` work units. Each unit is a range `[batchStart, batchStart + batchCount)`. Worker threads steal units from each other using a work-stealing deque.

### 7.2 JobHandle Chain and Batch Dependencies

`JobHandle` is the core dependency primitive. Jobs form a DAG:

```csharp
// Phase 1: Independent jobs (run in parallel)
JobHandle cullingHandle = cullingJob.Schedule(objectCount, 64);
JobHandle animHandle    = animJob.Schedule(skeletonCount, 8);
JobHandle physicsHandle = physicsJob.Schedule(bodyCount, 32);

// Phase 2: Depends on ALL Phase 1 jobs completing
JobHandle combined = JobHandle.CombineDependencies(
    cullingHandle, animHandle, physicsHandle);

// Phase 3: Runs only after Phase 2 is complete
JobHandle renderPrepHandle = renderPrepJob.Schedule(combined);

// Phase 4: Two independent jobs after renderPrep
JobHandle shadowHandle = shadowJob.Schedule(renderPrepHandle);
JobHandle lightHandle  = lightJob.Schedule(renderPrepHandle);

// Final barrier: wait for everything before presenting
JobHandle.CompleteAll(ref shadowHandle, ref lightHandle);
```

Dependency graph visualization:

```
cullingJob ──┐
animJob    ──┼── CombineDependencies ── renderPrepJob ──┬── shadowJob
physicsJob ──┘                                          └── lightJob
                                                              │
                                              CompleteAll ◄───┘
```

### 7.3 Native Collections and Safety Handles

NativeCollections are the foundation of safe parallel data access. Each collection wraps a raw pointer + `AtomicSafetyHandle`:

```csharp
// NativeArray<T> internal structure (simplified)
public struct NativeArray<T> : IDisposable where T : struct
{
    // The actual data pointer (native memory, not managed heap)
    [NativeDisableUnsafePtrRestriction]
    internal unsafe void* m_Buffer;

    internal int m_Length;
    internal Allocator m_AllocatorLabel;

    // Safety system: tracks who can read/write and when
    internal AtomicSafetyHandle m_Safety;

    // Debug: tracks where this array was allocated (leak detection)
    internal DisposeSentinel m_DisposeSentinel;

    public T this[int index]
    {
        get
        {
            // In editor/debug: checks bounds + write permission
            AtomicSafetyHandle.CheckReadAndThrow(m_Safety);
            if ((uint)index >= (uint)m_Length)
                throw new IndexOutOfRangeException();
            return UnsafeUtility.ReadArrayElement<T>(m_Buffer, index);
        }
        set
        {
            AtomicSafetyHandle.CheckWriteAndThrow(m_Safety);
            if ((uint)index >= (uint)m_Length)
                throw new IndexOutOfRangeException();
            UnsafeUtility.WriteArrayElement(m_Buffer, index, value);
        }
    }
}
```

The `AtomicSafetyHandle` enforces these rules at runtime (editor/debug builds only):

| Scenario | Result |
|----------|--------|
| Write to `[ReadOnly]` array in job | Exception: "not writable" |
| Read array on main thread while job is running | Exception: "being written by job" |
| Access array after Dispose() | Exception: "has been deallocated" |
| Access Temp allocation after 4 frames | Exception: "temp allocation expired" |
| Two jobs writing to same array without dependency | Exception: "race condition" |

### 7.4 Burst Compiler Annotations

Burst uses attributes to control compilation behavior:

```csharp
[BurstCompile(
    FloatPrecision = FloatPrecision.Standard,  // IEEE 754
    FloatMode = FloatMode.Fast,                // Allow FMA, reorder
    CompileSynchronously = false,              // Async compilation in editor
    OptimizeFor = OptimizeFor.Performance      // vs. Size vs. Balanced
)]
struct PhysicsBroadPhaseJob : IJobParallelFor
{
    [ReadOnly]
    public NativeArray<AABB> AllBounds;

    // Disable safety checks for this specific access
    // (used when you guarantee thread-safety manually)
    [NativeDisableParallelForRestriction]
    public NativeArray<int> BroadPhasePairs;

    // Disable container safety checks (for performance-critical inner loops)
    [NativeDisableContainerSafetyRestriction]
    public NativeList<int> ThreadLocalBuffer;

    // Mark as no-alias (compiler can assume no pointer aliasing)
    [NoAlias]
    public NativeArray<float3> Positions;

    public void Execute(int index)
    {
        AABB a = AllBounds[index];

        // Burst compiles this to SIMD AABB overlap test
        for (int j = index + 1; j < AllBounds.Length; j++)
        {
            AABB b = AllBounds[j];
            if (Overlaps(a, b))
            {
                // Write pair to thread-local buffer
                // [NativeDisableParallelForRestriction] needed because
                // we're writing to indices outside our batch range
                int pairIndex = Interlocked.Increment(ref pairCount) - 1;
                BroadPhasePairs[pairIndex] = PackPair(index, j);
            }
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    static bool Overlaps(AABB a, AABB b)
    {
        // Burst auto-vectorizes this to 3-wide SIMD comparison
        return math.all(a.max >= b.min) && math.all(b.max >= a.min);
    }
}
```

### 7.5 ECS System Scheduling in DOTS

In the full DOTS stack, systems declare their read/write access and the scheduler automatically determines which systems can run in parallel:

```csharp
[BurstCompile]
partial struct TransformSystem : ISystem
{
    [BurstCompile]
    public void OnUpdate(ref SystemState state)
    {
        // EntityQuery automatically declared by the foreach pattern
        // Scheduler sees: reads LocalTransform, reads Parent, writes WorldTransform
        // → Can run parallel with any system that doesn't write LocalTransform or Parent

        new TransformJob
        {
            ParentLookup = SystemAPI.GetComponentLookup<Parent>(true),
            LocalTransformLookup = SystemAPI.GetComponentLookup<LocalTransform>(true),
        }.ScheduleParallel();
        // ScheduleParallel() returns JobHandle, automatically added to
        // system dependency chain
    }
}

[BurstCompile]
partial struct TransformJob : IJobEntity
{
    [ReadOnly] public ComponentLookup<Parent> ParentLookup;
    [ReadOnly] public ComponentLookup<LocalTransform> LocalTransformLookup;

    // IJobEntity: Execute is called per-entity matching the query
    void Execute(ref WorldTransform worldTransform,
                 in LocalTransform localTransform,
                 in Parent parent)
    {
        if (ParentLookup.HasComponent(parent.Value))
        {
            var parentWorld = SystemAPI.GetComponent<WorldTransform>(
                parent.Value);
            worldTransform.Value = math.mul(
                parentWorld.Value, localTransform.Value);
        }
        else
        {
            worldTransform.Value = localTransform.Value;
        }
    }
}
```

The scheduler builds a dependency graph based on read/write declarations:

```
Frame N scheduling:
┌─────────────────────────────────────────────────────────────────┐
│ SystemScheduler analyzes all systems' ComponentAccess:           │
│                                                                 │
│ TransformSystem: reads(LocalTransform, Parent)                  │
│                  writes(WorldTransform)                          │
│ PhysicsSystem:   reads(WorldTransform, Collider)                │
│                  writes(Velocity, WorldTransform)                │
│ AnimationSystem: reads(AnimState)                               │
│                  writes(LocalTransform)                          │
│ RenderSystem:    reads(WorldTransform, MeshRenderer)            │
│                  writes(nothing)                                 │
│                                                                 │
│ Computed parallel groups:                                       │
│   Group 1: [AnimationSystem]  (writes LocalTransform)           │
│   Group 2: [TransformSystem]  (reads LocalTransform → after 1)  │
│   Group 3: [PhysicsSystem, RenderSystem]  (both read only       │
│             WorldTransform, no write conflict)                   │
│                                                                 │
│ AnimationSystem ──► TransformSystem ──┬── PhysicsSystem         │
│                                      └── RenderSystem           │
└─────────────────────────────────────────────────────────────────┘
```

### 7.6 Pattern: **Declarative Access + Automatic Dependency Resolution**

The pattern: systems declare what components they read and write. The scheduler computes the dependency graph automatically. This eliminates manual `JobHandle` chains for system-level ordering and prevents data races by construction.

This is distinct from the manual `JobHandle.CombineDependencies()` pattern (which is for intra-system job chains). The system scheduler handles inter-system dependencies.

### 7.7 UzEngine Implications

UzEngine's job system spec (`job-system.md`) already defines:
- Work-stealing deque with Chase-Lev algorithm
- `submit_parallel()` equivalent to `IJobParallelFor`
- System dependency declaration via `ISystem::on_configure()` with `reads/writes` sets
- TickGroup ordering (PrePhysics → Physics → PostPhysics → Render)

**Gap: No automatic dependency resolution.** The current spec uses manual TickGroup ordering. Within a TickGroup, systems marked `parallelizable = true` run in parallel. But there is no automatic read/write analysis to determine which systems in the same TickGroup can actually run in parallel without races.

**Recommendation**: Implement the Unity DOTS pattern of component access declarations:

```cpp
class TransformSystem : public ISystem {
    void on_configure(SystemInfo& info) override {
        info.reads  = { ComponentId::NodeParent, ComponentId::LocalTransform };
        info.writes = { ComponentId::WorldTransform };
        // Scheduler computes: TransformSystem can run parallel with
        // any system that does NOT write NodeParent or LocalTransform
    }
};
```

The current spec already has `reads` and `writes` fields in `SystemInfo`. The missing piece is the scheduler logic that uses this information to build a dependency graph instead of relying solely on TickGroup ordering.

**Gap: No safety handle equivalent.** Unity's `AtomicSafetyHandle` catches data races at runtime in debug builds. UzEngine should add debug-mode checks:

```cpp
#ifdef UZ_DEBUG
// In debug builds, track which components are being written
// and assert if another system tries to read/write simultaneously
struct ComponentAccessTracker {
    std::atomic<uint32_t> active_writers[MAX_COMPONENT_TYPES];
    std::atomic<uint32_t> active_readers[MAX_COMPONENT_TYPES];

    void begin_write(ComponentId id) {
        ASSERT(active_writers[id]++ == 0,
            "Double-write detected on component %d", id);
        ASSERT(active_readers[id] == 0,
            "Read-write race detected on component %d", id);
    }
    void end_write(ComponentId id) { active_writers[id]--; }
    void begin_read(ComponentId id) {
        ASSERT(active_writers[id] == 0,
            "Write-read race detected on component %d", id);
        active_readers[id]++;
    }
    void end_read(ComponentId id) { active_readers[id]--; }
};
#endif
```

**Gap: No batch size tuning guidance.** Unity's `innerloopBatchCount` is critical for performance. UzEngine's `min_batch` serves the same purpose but the spec doesn't provide guidance on choosing values. Based on Unity's conventions:

| Work Type | Recommended Batch | Rationale |
|-----------|------------------|-----------|
| AABB culling | 64-128 | Light per-item work, cache-friendly |
| Bone palette computation | 4-8 | Heavy per-item (~67 bones × mat4 mul) |
| Transform propagation | 64 | Medium work, tree traversal |
| Particle update | 128-256 | Trivial per-item (position += velocity) |

---

## Summary of Patterns Identified

| # | Pattern Name | Source | UzEngine Relevance |
|---|-------------|--------|-------------------|
| 1 | Dual-Layer Identity Survival | Hot-Reload | game.dll hot-reload: ECS in exe, logic in dll |
| 2 | Tiered Compilation Boundary | Mono/IL2CPP/Burst | Lua (instant) → game.dll (fast) → engine (slow) |
| 3 | Capability Discovery Interface | IUnityInterfaces | GameContext → query_interface() evolution |
| 4 | Layered Evaluation Pipeline | Animation/Playables | AnimNode DAG for composable animation |
| 5 | Fixed-Step Accumulator + Interpolation | PhysX Integration | Missing render interpolation in physics spec |
| 6 | Address-Based Indirection + Ref-Counted Lifetime | Addressables | Missing async asset handle in GameContext |
| 7 | Hierarchical Profiler with Zero-Alloc Hot Path | Profiler | Tracy already matches; add custom counters |
| 8 | Declarative Access + Auto Dependency Resolution | DOTS Job System | reads/writes fields exist; scheduler logic missing |

---

## Top 3 Critical Insights for UzEngine

### Insight 1: Physics Render Interpolation is Non-Negotiable

Unity's fixed-step accumulator pattern with render interpolation is used by every major engine. Without it, characters and projectiles visually stutter when physics rate (60 Hz) differs from render rate (144 Hz or variable). UzEngine's physics spec must add `CPhysicsInterpolation` with prev/curr transforms and alpha-blended rendering. This is a Phase 3 must-have, not a nice-to-have.

### Insight 2: AnimNode DAG Replaces Monolithic State Machine

Unity's evolution from Animator Controller (monolithic) to Playables API (composable DAG) proves that animation systems must be node-based for extensibility. UzEngine's current `AnimStateMachine` struct should be refactored into an `AnimNode` DAG where state machines, blend trees, IK solvers, and procedural layers are all interchangeable graph nodes. This affects Phase 3 animation implementation directly.

### Insight 3: Automatic System Dependency Resolution Prevents Races

Unity DOTS's scheduler computes parallel groups from declared component reads/writes. UzEngine already has `reads` and `writes` in `SystemInfo` but no scheduler logic to use them. Implementing automatic dependency resolution in Phase 2 (alongside the job system) prevents data races without manual TickGroup management and enables maximum parallelism.

---

## References

- Unity Manual: [Script compilation and assembly definition files](https://docs.unity3d.com/Manual/ScriptCompilationAssemblyDefinitionFiles.html)
- Unity Manual: [Domain Reload](https://docs.unity3d.com/Manual/DomainReloading.html)
- Unity DOTS: [Job System](https://docs.unity3d.com/Packages/com.unity.jobs@latest/)
- Unity DOTS: [Burst Compiler](https://docs.unity3d.com/Packages/com.unity.burst@latest/)
- Unity Manual: [Playables API](https://docs.unity3d.com/Manual/Playables.html)
- Unity Manual: [Addressables](https://docs.unity3d.com/Packages/com.unity.addressables@latest/)
- Unity Manual: [Memory Profiler](https://docs.unity3d.com/Packages/com.unity.memoryprofiler@latest/)
- Unity Manual: [Physics](https://docs.unity3d.com/Manual/PhysicsSection.html)
