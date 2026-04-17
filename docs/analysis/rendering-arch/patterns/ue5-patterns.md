<!-- doc-meta: ue5-rendering-architecture-patterns, UE5 rendering architecture patterns for system design, 2026-04-04 -->

# UE5 Rendering Architecture Patterns & Design Principles

**Reference Version**: Unreal Engine 5.6  
**Focus**: Architectural patterns, data flow, and implementation strategies  
**Purpose**: Guide UzEngine architectural decisions

---

## Table of Contents

1. [Thread Model & Proxy Pattern](#thread-model--proxy-pattern)
2. [Data Flow: Component → Renderer](#data-flow-component--renderer)
3. [Render Graph Architecture](#render-graph-architecture)
4. [Shader System Architecture](#shader-system-architecture)
5. [Constant Buffer Strategy](#constant-buffer-strategy)
6. [Material System Pipeline](#material-system-pipeline)
7. [Visibility & Culling](#visibility--culling)
8. [RHI Abstraction Design](#rhi-abstraction-design)
9. [Key Architectural Insights](#key-architectural-insights)

---

## Thread Model & Proxy Pattern

### UE5 Threading Model

**Three Concurrent Threads**:

```
Game Thread                   Render Thread                GPU
├─ Update components          ├─ Record commands           ├─ Execute
├─ Physics simulation         ├─ Visibility culling        ├─ Rasterize
├─ Script execution           ├─ Sort draw calls           ├─ Compute
└─ Scene management           ├─ Bind resources
                              └─ Submit to GPU
```

**Decoupling**:
- Game thread **never** calls render thread functions
- Render thread **never** accesses game objects directly
- Data flows one-way: Game → Render (via proxies)

### Render Proxy Pattern

**Problem**: Render thread needs current component state without locking.

**Solution**: **FPrimitiveSceneProxy** — Render thread copy of component state.

**Example: Light Component**

```cpp
// Game Thread (Engine/Classes/Lights/LightComponent.h)
class ULightComponent : public USceneComponent
{
public:
    UPROPERTY(EditAnywhere) float Intensity = 1000.0f;
    UPROPERTY(EditAnywhere) FLinearColor Color = FLinearColor::White;
    
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    
    void SetIntensity(float NewIntensity)
    {
        Intensity = NewIntensity;
        // Notify renderer of change
        SendRenderDynamicData_Concurrent();
    }
};

// Render Thread (Renderer/Private/LightSceneInfo.h)
class FLightSceneProxy : public FPrimitiveSceneProxy
{
public:
    float Intensity;           // Copied from game thread
    FLinearColor Color;        // Snapshot in time
    FVector WorldPosition;     // Immutable on render thread
    
    void GetDynamicMeshElements(...) override
    {
        // Render thread ONLY uses proxy data, never accesses original component
        DrawLight(Intensity, Color, WorldPosition);
    }
};
```

**Lifecycle**:

```
Frame N: Game thread updates Intensity
         │
         └─ Calls SendRenderDynamicData_Concurrent()
            │
            ├─ Enqueues proxy update command
            │
            Render thread (Frame N+1):
            └─ Executes update: proxy.Intensity = game.Intensity
               │
               └─ Records render commands using updated proxy
                  │
                  GPU (Frame N+2): Executes draw calls
```

**Key Insight**: 1-2 frame latency is **acceptable** for visual quality (not perceived by user).

### SendRenderDynamicData Pattern

**When component state changes at runtime**:

```cpp
void MyComponent::OnPropertyChanged()
{
    // Modify game-thread copy
    Intensity = 2000.0f;
    
    // Notify render thread
    if (SceneProxy)
    {
        ENQUEUE_RENDER_COMMAND(UpdateLightCommand)
        (
            [this](FRHICommandList& CmdList)
            {
                // Render thread: update proxy
                static_cast<FMyComponentProxy*>(SceneProxy)->Intensity = Intensity;
            }
        );
    }
}
```

**Without proxy pattern** (naive):
```cpp
// ❌ WRONG: Render thread accesses game object
void MyComponent::Render()
{
    Intensity = ...;  // Game thread reading/writing
    if (bSomethingChanged)  // Race condition!
    {
        // Render thread trying to read game state
    }
}
```

---

## Data Flow: Component → Renderer

### Example: SkyLight Component with Cubemap

**Goal**: Capture a sky cubemap at runtime, apply IBL.

**Data Flow** (simplified):

```
1. Game Thread (Update)
   └─ SkyLightComponent::UpdateCaptureContent()
      ├─ Determine cubemap resolution (128×128)
      ├─ Add render task to queue
      └─ Enqueue: "Capture cubemap"

2. Render Thread (Main Render Pass)
   └─ SkyLightSceneInfo::UpdateCapture()
      ├─ If time to recapture:
      │  ├─ Render 6 faces (cube map faces)
      │  ├─ Store in SkyLightSceneInfo::CubemapTexture
      │  └─ Update IrradianceTexture (SH or irradiance cubemap)
      │
      └─ During deferred lighting:
         └─ Sample IrradianceTexture → IBL ambient

3. Constant Buffer
   └─ Scene.cbuffer
      ├─ SkyLightColor (float3)
      ├─ SkyLightIntensity (float)
      └─ SkyLightCubemapIndex (uint)  // Into texture array

4. Pixel Shader (Deferred Light Pass)
   └─ float3 iblAmbient = SampleIrradiance(skyLightIndex, normal);
      float finalColor = diffuse * iblAmbient + specular * reflection;
```

**Files Involved**:

| Stage | File | Class | Function |
|-------|------|-------|----------|
| Component (Game) | Engine/Classes/Lights/SkyLightComponent.h | USkyLightComponent | SetCubemap(), UpdateCaptureContent() |
| Scene Info (Game) | Engine/Source/Runtime/Engine/Private/Lights/SkyLightComponent.cpp | FSkyLightSceneInfo | ctor/dtor manage lifetime |
| Scene Proxy (Render) | Engine/Source/Runtime/Renderer/Private/SkyLightSceneInfo.h | FSkyLightSceneProxy | GetDynamicMeshElements() |
| Capture (Render) | Engine/Source/Runtime/Renderer/Private/SkyLightSceneCapture.cpp | FSkyLightCapturePass | Render() — render 6 faces |
| CB Update (Render) | Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp | FSceneRenderer::UpdateSkyLight() | Populate constant buffer |
| Shader (GPU) | Engine/Shaders/Private/BasePassCommon.ush | main() | Sample IBL, compute lighting |

---

## Render Graph Architecture

### Purpose

**Render graph** == declarative frame description (not imperative API calls).

**Traditional API** (imperative, error-prone):
```cpp
// ❌ Manual management, easy to get wrong
FTexture* ColorTarget = CreateTexture(...);
FTexture* NormalTarget = CreateTexture(...);

// Manual dependency tracking
SetRenderTarget(ColorTarget);
BindShader(GBufferShader);
DrawMesh();  // Hope GPU isn't still using it...

// Manual readback scheduling
ReadbackNormalData(NormalTarget);
```

**Render Graph API** (declarative):
```cpp
// ✓ Graph system tracks lifetimes & dependencies
FRenderGraphBuilder Builder(GraphName);

FGBufferTextures GBuffer = AddGBufferPass(Builder, ...);
FReflectionData Reflections = AddSSRPass(Builder, GBuffer);
AddCompositePass(Builder, GBuffer, Reflections);

Builder.Execute();  // Graph resolves all dependencies
```

### Render Graph Concepts

**Pass** = atomic rendering operation:
```cpp
class FMyPass : public FRenderGraphPass
{
    // Input resources (read-only)
    FRDGTextureRef GBufferAlbedo;
    FRDGTextureRef DepthBuffer;
    
    // Output resources (write-only)
    FRDGTextureRef OutputColor;
    
    void Execute(FRHICommandListImmediate& RHICmdList) override
    {
        // Record GPU commands
        RHICmdList.SetRenderTarget(OutputColor);
        RHICmdList.DrawIndexedPrimitive(...);
    }
};
```

**Resource Lifetime Tracking**:
- Graph knows which passes read/write each resource
- Allocates GPU memory only when needed
- Deallocates after last reader finishes
- Automatically inserts pipeline barriers

**Example: Shadow Map Flow**

```
CSMDepthPass
├─ Inputs: Mesh geometry, light matrix
├─ Output: ShadowDepthTexture
└─ Dependencies: None (early in frame)
   │
   └─ Used by LightingPass
      │
      ├─ Inputs: GBuffer, ShadowDepthTexture
      ├─ Output: HDRColor
      └─ Barrier: ShadowDepth (read-only)
         │
         └─ Used by PostProcessPass
            │
            ├─ Inputs: HDRColor
            ├─ Output: FinalColor
            └─ Barrier: HDRColor (read-only)
               │
               └─ Present to screen
```

**Without render graph**: Manual barrier insertion, easy to miss.

### Integration with RHI

**Render Graph ↔ RHI Decoupling**:

```cpp
// Render Graph (high-level)
FRDGPass* MyPass = Builder.AddPass<FMyPass>(...);

// Converted to RHI (low-level) during Execute()
MyPass->Execute(RHICmdList);
├─ SetRenderTarget(RHI texture handle)
├─ SetPipelineState(RHI PSO)
├─ DrawIndexedPrimitive(...)
└─ Barrier(ReadOnly)
```

---

## Shader System Architecture

### Shader Compilation Pipeline

**UE5 Shader Compilation** (multi-stage):

```
1. Material Definition (.uasset material)
   │
   ├─ Nodes: TextureSample, Multiply, Add, ...
   ├─ Graph edges (connections)
   └─ Material properties (blend mode, etc.)

2. Material Shader Generation
   │
   ├─ Traverse graph (DFS)
   ├─ Emit HLSL code per node
   │  └─ TextureSample(UV) → "tex2D(Sampler_0, UV)"
   │  └─ Multiply(A, B) → "A * B"
   ├─ Generate base pass shader template
   └─ Inject generated code into template

3. Pre-Processing
   │
   ├─ Macro substitution (#define FEATURE_X 1)
   ├─ Platform-specific features (SM5, ES3, etc.)
   ├─ Quality level (scalability)
   └─ Permutation key selection

4. Compilation (DXC or other)
   │
   ├─ Compile each permutation
   │  ├─ Vertex shader
   │  ├─ Pixel shader
   │  ├─ Compute shader (optional)
   │  └─ Hull/Domain for tessellation
   ├─ Profile targets (PS_5_0, CS_6_0, etc.)
   └─ Optimization (-O3)

5. Pipeline State Object (PSO) Creation
   │
   ├─ Vertex layout (position, UV, normal)
   ├─ Compiled shaders
   ├─ Blend state (opaque, transparent, etc.)
   ├─ Depth state (compare, write, etc.)
   ├─ Rasterizer state (cull, fill, etc.)
   └─ Root signature (parameter bindings)

6. Caching
   │
   ├─ PSO serialized to disk
   ├─ Next run: deserialized (skip compilation)
   └─ Hash: content + platform + settings
```

### Permutation System

**Permutation Key** = bitmask of enabled features.

```cpp
uint64 PermutationKey = 0;

enum class EShaderFeature : uint32
{
    VERTEX_ANIMATED = (1 << 0),     // 0x0001
    NORMAL_MAPPED = (1 << 1),        // 0x0002
    PARALLAX_MAPPING = (1 << 2),     // 0x0004
    RECEIVE_SHADOWS = (1 << 3),      // 0x0008
    // ... up to 64 bits
};

// Material X: VERTEX_ANIMATED | NORMAL_MAPPED | RECEIVE_SHADOWS
PermutationKey = 0x000B;

// Material Y: VERTEX_ANIMATED only
PermutationKey = 0x0001;

// Same shader code compiled 2 ways
// Compiler optimizes away unused branches
```

**Advantage**: Dead-code elimination by compiler (unused branches never execute).

**Disadvantage**: Combinatorial explosion (2^N permutations).

**UE5 Mitigation**:
- Skip invalid combinations (e.g., tessellation + mobile)
- Cache compiled PSOs to disk
- Async compilation queue
- Shader cook-on-load (console commands)

---

## Constant Buffer Strategy

### Constant Buffer Hierarchy

UE5 uses **multiple constant buffers** for different update frequencies:

```
Frequency: Per-Frame (Updated every frame)
├─ View CB (camera, view-projection matrix)
├─ Primitive CB (per-object transform)
├─ Light CB (light data)
└─ Fog CB (volumetric fog parameters)

Frequency: Per-Material (Updated per material change)
├─ Material CB (texture scales, colors)
├─ Custom data CB (material-specific parameters)

Frequency: Per-Pass (Once per render pass)
├─ Postprocess CB (bloom threshold, DOF distance)

Frequency: Static (Compiled in)
├─ Math constants (PI, etc.)
├─ Platform limits (max lights, etc.)
```

### Root Signature Design

**Root Signature** = GPU parameter layout (DX12 terminology).

```cpp
// UE5 Root Signature (typical deferred)
enum class ERootSignatureSlot : uint32
{
    ViewTransforms = 0,     // b0 (View/Proj matrices)
    PrimitiveData = 1,      // b1 (World transform)
    MaterialParams = 2,     // b2 (Material constants)
    LightData = 3,          // b3 (Light CB)
    DynamicSamplers = 4,    // t0-t15 (Textures)
    StaticSamplers = 5,     // s0-s3 (Point, Linear, etc.)
};

// Root signature space: 64 DWORDs total
// b0: 16 DWORDs (4×4 matrix)
// b1: 16 DWORDs (transform data)
// b2: 16 DWORDs (material data)
// b3: 16 DWORDs (light data)
// Total: 64 DWORDS ✓ At capacity
```

**Bindless Resources** (advanced):
- Single descriptor heap
- Resource index passed via constant buffer
- Eliminates descriptor table changes

---

## Material System Pipeline

### Material Class Hierarchy

```cpp
UMaterial (Base)
├─ Opaque material
│  ├─ Diffuse + Specular parameters
│  └─ Compiled to deferred + forward variants
├─ Transparent material
│  ├─ Blend mode (additive, modulate, etc.)
│  └─ Compiled to forward path only
├─ UI material
│  └─ Special rendering for UI
└─ Post-process material
   └─ Applied after rendering (bloom, etc.)

UMaterialInstance (Runtime override)
├─ Inherits from parent material
├─ Overrides scalar/vector/texture parameters
└─ No shader recompilation (PSO cached)

UDynamicMaterialInstance (Temporary)
├─ Created at runtime
├─ One-off overrides
└─ Deallocated after use
```

### Material Graph Compilation Example

**Graph**:
```
TextureSample (BaseColor) ──┐
                            ├─ Multiply ──┐
Constant (1, 0, 0)  ────────┘            │
                                         ├─ Output
Constant (Roughness)  ───────────────────┤
                                         │
Constant (Metallic)  ──────────────────┘
```

**Generated HLSL**:
```hlsl
float3 BaseColor = tex2D(BaseColorSampler, UV).rgb;
float3 Tint = float3(1.0, 0.0, 0.0);
float3 FinalAlbedo = BaseColor * Tint;

float Roughness = 0.5;
float Metallic = 0.0;

// Output to GBuffer
GBufferOut.Albedo = FinalAlbedo;
GBufferOut.Roughness = Roughness;
GBufferOut.Metallic = Metallic;
```

### Material Properties → Shader Parameters

**Mapping**:

| Material Property | Shader Constant | Frequency |
|-------------------|-----------------|-----------|
| Base Color | cb_MaterialParams.Color | Per-material |
| Roughness | cb_MaterialParams.Roughness | Per-material |
| Metallic | cb_MaterialParams.Metallic | Per-material |
| Custom Data | cb_MaterialParams.Custom[0-3] | Per-material |
| World Position Offset | Computed per-vertex | Per-vertex |
| Normal Map | Sampled from t0 | Per-pixel |

---

## Visibility & Culling

### Multi-Stage Visibility Pipeline

```
Frame Setup
│
├─ Frustum Culling (CPU)
│  ├─ AABB vs view frustum (6 planes)
│  ├─ Rough cull (fast, some over-inclusion)
│  └─ Output: "Potentially visible" set
│
├─ Precomputed Visibility (if available)
│  ├─ PVS (Potentially Visible Set) from level designer
│  ├─ Indoor levels: cell-based visibility
│  └─ Reduces CPU cull cost
│
├─ GPU Culling (per-instance fine-grained)
│  ├─ Frustum cull compute (accurate, per-instance)
│  ├─ HZB occlusion test (GPU depth-based cull)
│  └─ Output: Visible instance bitmask
│
├─ Draw Call Sorting
│  ├─ Material-based (minimize state changes)
│  ├─ Distance-based (from-to-back for transparency)
│  └─ Stateful vs stateless (dynamic vs static)
│
└─ Rendering
   └─ Execute sorted draw calls
```

### Occlusion Culling (HZB)

**Data Structure**:

```
Original Depth Buffer (2560×1440)
│
Mip 0: 2560×1440 (original)
Mip 1: 1280×720  (2×2 max reduction)
Mip 2: 640×360   (4×4 max reduction)
Mip 3: 320×180   (8×8 max reduction)
...
Mip 10: 2×2      (single pixel = max depth)
```

**Query Algorithm**:

```cpp
bool IsOccluded(AABB instanceBounds)
{
    // Conservative: if entire AABB could be occluded, cull
    
    float maxZ = instanceBounds.MaxZ;  // Farthest point
    float minZ = instanceBounds.MinZ;  // Nearest point
    
    // Project to screen
    float2 screenMin = Project(instanceBounds.MinXY);
    float2 screenMax = Project(instanceBounds.MaxXY);
    
    // Find mip level that covers AABB
    uint mip = SelectMipLevel(screenMin, screenMax);
    
    // Sample HZB at that mip
    float hzbDepth = SampleHZB(mip, screenCenter);
    
    // If HZB closer than instance far point: CULLED
    if (hzbDepth < maxZ)
        return true;  // Instance is occluded
    else
        return false; // Instance may be visible
}
```

**Conservative**: May include some occluded instances (false negatives), but never misses visible ones.

---

## RHI Abstraction Design

### RHI Purpose

**RHI** = Renderer Hardware Interface (API abstraction layer).

```
High-level Rendering Code
    ↓
RHI (Abstract API)
├─ DX12 Implementation
├─ Vulkan Implementation
├─ OpenGL Implementation
└─ Console APIs (PS5, XSX)
    ↓
GPU Hardware
```

### RHI Class Hierarchy (simplified)

```cpp
class FRHIDevice (Abstract)
{
public:
    virtual FRHITextureRef CreateTexture(const FRHITextureDesc&) = 0;
    virtual FRHIPipelineRef CreateGraphicsPipeline(...) = 0;
    virtual void ExecuteCommandList(FRHICommandList*) = 0;
};

class FDX12Device : public FRHIDevice
{
private:
    ID3D12Device* Device;
    IDXGIAdapter* Adapter;
    
public:
    FRHITextureRef CreateTexture(const FRHITextureDesc&) override
    {
        // Create ID3D12Resource, wrap in FD3D12Texture
        ID3D12Resource* D3DTexture = Device->CreateCommittedResource(...);
        return new FD3D12Texture(D3DTexture);
    }
};
```

### Resource Handle Strategy

**Problem**: Exposing D3D12 types everywhere couples code to DX12.

**Solution**: Opaque handles (FRHITextureRef, FRHIBufferRef, etc.).

```cpp
// ❌ WRONG: Directly expose D3D12
void MyRenderer::BindTexture(ID3D12Resource* Texture)
{
    // Now tied to DX12, can't port to Vulkan
}

// ✓ CORRECT: Use RHI handles
void MyRenderer::BindTexture(FRHITextureRef Texture)
{
    // Implementation independent
    GetRHI()->BindTexture(Texture);  // RHI picks right backend
}
```

### Command List Abstraction

**Deferred Command Recording**:

```cpp
class FRHICommandList
{
    // Commands not executed immediately
    // Queued for later batch execution
    
    void SetRenderTarget(FRHITextureRef RenderTarget)
    {
        // Record: "SetRenderTarget(RenderTarget)"
        Commands.push_back(SetRenderTargetCmd{RenderTarget});
    }
    
    void DrawIndexedPrimitive(...)
    {
        Commands.push_back(DrawIndexedCmd{...});
    }
};

// Later (GPU thread):
for (auto& cmd : CommandList.Commands)
{
    cmd->Execute(Device);  // Virtual dispatch per command
}
```

**Benefit**: 
- Deferred execution (batch optimization)
- Multi-threaded recording (N threads → 1 GPU thread merge)
- API-agnostic command format

---

## Key Architectural Insights

### 1. Proxy Pattern Enables Async Rendering

**Design Decision**: Render thread never touches game objects.

**Impact**:
- Eliminates locks (lock-free architecture)
- Enables deterministic 3-frame pipeline
- Allows game/render to run independently
- Easy to add compute operations (async shadows, etc.)

**Cost**: 1-2 frame input latency (imperceptible).

### 2. Shader Permutations > Single Mega-Shader

**Design Decision**: Generate unique shader per material.

**Impact**:
- Unlimited material complexity (no uniform limit)
- Compiler optimizes dead code (unused features removed)
- Per-material optimization possible
- Difficult debugging (N shader variants)

**Alternative** (not used): Mega-shader with all features compiled in (bloated, slow).

### 3. Deferred + Forward Hybrid > Pure Forward or Deferred

**Design Decision**: Deferred for opaque (many lights), forward for translucent.

**Impact**:
- Scalable to 1000+ lights (deferred)
- Correct translucency (forward)
- Minimum state changes
- More complex renderer (two paths)

**Alternative** (not used): Pure forward (slow with many lights) or pure deferred (bad translucency).

### 4. Temporal Reprojection Amortizes Expensive Effects

**Design Decision**: Accumulate noisy effects over multiple frames.

**Impact**:
- SSR/SSGI run at 1/2 or 1/4 res, reproject to full
- Effective 4× performance (1/4 res = 1/16 pixels, done 4 frames = 1/4 work per frame)
- Temporal artifacts if not filtered properly
- Cheap high-quality effects

**Alternative** (not used): Full-res every frame (too expensive, rarely attempted).

### 5. Render Graph Solves Resource Lifetime & Barriers

**Design Decision**: Declarative frame description (not imperative API calls).

**Impact**:
- Graph automatically inserts barriers
- Allocates resources only when needed
- Tracks dependencies (prevents use-before-write)
- More boilerplate upfront, but fewer runtime bugs

**Alternative** (old UE3): Manual barrier insertion, easy to deadlock.

---

## Porting to UzEngine

### Recommended Patterns

| Pattern | UE5 Implementation | UzEngine Adaptation |
|---------|-------------------|-------------------|
| Proxy Pattern | FPrimitiveSceneProxy | Mirror scene data to render thread, no game access |
| Shader Compilation | Per-material code gen | Generate unique shader per material |
| Permutations | Bitmask feature flags | Compile variants for each combination |
| Constant Buffers | Frequency-based hierarchy | Group by update rate (per-frame, per-material, static) |
| Render Graph | Declarative pass ordering | Explicit dependency graph, auto barriers |
| GPU Culling | Compute shader + indirect | Instance frustum cull, occlusion cull (HZB) |
| RHI Abstraction | Handle-based, opaque types | Hide backend behind virtual interface |

### Anti-Patterns (Avoid)

| Anti-Pattern | Why Bad | UE5 Avoids How |
|--------------|--------|-----------------|
| Render thread touching game objects | Race conditions, deadlocks | All state copied to proxies |
| Hardcoded shader logic | Inflexible, brittle | Material graph compilation |
| Single mega-shader | Bloated, poor optimization | Per-material permutations |
| Pure forward rendering | O(N*M) lights, slow | Deferred for opaque |
| Manual barrier insertion | Easy to deadlock | Render graph auto-barriers |
| Direct DX12 API calls in renderer | Tightly coupled, unportable | All calls through RHI abstraction |

---

## Summary

UE5's rendering architecture achieves **AAA visual quality + platform portability** through:

1. **Thread-safe proxy pattern** — Eliminates locks, enables async rendering
2. **Per-material shader compilation** — Unlimited complexity, compiler optimizes
3. **Deferred + forward hybrid** — Scales to many lights, correct translucency
4. **Temporal reprojection** — Amortizes expensive effects across frames
5. **Render graph abstraction** — Automatic barrier insertion, resource lifetime tracking
6. **RHI abstraction layer** — Supports DX12, Vulkan, OpenGL, consoles
7. **Multi-stage visibility** — CPU frustum → GPU culling → HZB occlusion
8. **Constant buffer hierarchy** — Groups by update frequency (efficiency)

Adopting these patterns in UzEngine will significantly improve rendering quality, maintainability, and platform coverage.
