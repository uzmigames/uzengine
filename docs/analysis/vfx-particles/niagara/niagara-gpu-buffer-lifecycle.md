<!-- doc-meta
title: Niagara GPU Buffer Lifecycle and Compute Dispatch Architecture
author: Claude Code (via team-lead analysis request)
date: 2026-04-02
status: analysis
subsystem: vfx
tags: [gpu-sim, buffer-management, compute-dispatch, resource-states, render-graph]
-->

# Niagara GPU Buffer Lifecycle and Compute Dispatch Architecture

This document provides a detailed analysis of UE5 Niagara's GPU buffer management, compute dispatch pipeline, and resource state handling. This is critical context for fixing UzEngine's particle system crashes during compute dispatch.

## 1. GPU Buffer Lifecycle

### 1.1 Data Buffer Structure (FNiagaraDataBuffer)

UE5's particle simulation uses a triple-buffered system to decouple CPU and GPU work. Each frame's particles are stored in **FNiagaraDataBuffer**, a shared object with three GPU RW buffers:

```cpp
// Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataSet.h:85-260

class FNiagaraDataBuffer : public FNiagaraSharedObject
{
public:
    // Three separate GPU buffers per data type
    FRWBuffer GPUBufferFloat;    // Float particle attributes (position, velocity, color, etc.)
    FRWBuffer GPUBufferInt;      // Int32 attributes (indices, flags, spawning info)
    FRWBuffer GPUBufferHalf;     // FP16 attributes (for memory savings)
    
    // Per-particle ID → index mapping (critical for free list)
    FRWBuffer GPUIDToIndexTable;
    
    // Immutable per-frame state
    uint32 NumInstances;              // Current live particle count
    uint32 NumInstancesAllocated;     // Max capacity
    uint32 FloatStride;               // Bytes between float components
    uint32 Int32Stride;               // Bytes between int32 components
    uint32 HalfStride;                // Bytes between half components
    uint32 NumSpawnedInstances;       // Newly spawned this frame
    uint32 IDAcquireTag;              // For allocating fresh particle IDs
    
    // Resource state tracking
    uint32 GPUInstanceCountBufferOffset;           // Offset into shared count buffer
    ENiagaraGpuComputeTickStage::Type GPUDataReadyStage;  // When data becomes valid
};

// Triple-buffered access pattern
class FNiagaraComputeExecutionContext
{
public:
    FNiagaraDataBuffer* DataBuffers_RT[2];  // Double-buffered (ping-pong)
    uint32 BufferSwapsThisFrame_RT = 0;     // Tracks which buffer is "current"
    
    FORCEINLINE FNiagaraDataBuffer* GetPrevDataBuffer() { 
        return DataBuffers_RT[(BufferSwapsThisFrame_RT & 1) ^ 1]; 
    }
    FORCEINLINE FNiagaraDataBuffer* GetNextDataBuffer() { 
        return DataBuffers_RT[(BufferSwapsThisFrame_RT & 1)]; 
    }
    FORCEINLINE void AdvanceDataBuffer() { ++BufferSwapsThisFrame_RT; }
};
```

**Key invariant**: At any point in time, there are exactly 2 data buffers in play:
- **Previous data buffer** (read-only SRV) — contains last frame's simulation results, used for rendering
- **Next data buffer** (UAV) — destination for current frame's compute writes

### 1.2 GPU Buffer Allocation Sequence

#### Step 1: Allocate at Emitter Spawn
When a particle emitter is first spawned, Niagara allocates GPU memory:

```cpp
// NiagaraDataSet.h:103-106
void FNiagaraDataBuffer::AllocateGPU(
    FRHICommandListBase& RHICmdList, 
    uint32 InNumInstances,              // Max particles for this emitter
    ERHIFeatureLevel::Type FeatureLevel,
    const TCHAR* DebugSimName           // For PIX markers
);
```

This creates three FRWBuffer objects (one per data type). Each FRWBuffer is an RHI abstraction that allocates both SRV and UAV views automatically.

**Memory layout per buffer**:
```
FRWBuffer GPUBufferFloat
├─ Allocation: NumInstancesAllocated * FloatStride bytes
│
├─ Component 0 (Position.X):  [float0_inst0, float0_inst1, ..., float0_instN]
├─ Component 1 (Position.Y):  [float1_inst0, float1_inst1, ..., float1_instN]
├─ Component 2 (Position.Z):  [float2_inst0, float2_inst1, ..., float2_instN]
│
├─ ... more components ...
│
└─ Stride alignment: Each component starts at offset (i * FloatStride)
   where FloatStride = Align(NumInstancesAllocated * sizeof(float), VECTOR_WIDTH_BYTES) + VECTOR_WIDTH_BYTES
```

**Why strides?** This "structure of arrays" (SoA) layout is **GPU-friendly** because:
- Same component across all particles is contiguous in memory → cache efficiency
- SIMD operations on a component iterate over contiguous addresses
- Example: `for (i = 0; i < NumParticles; ++i) position[i].x = ...` → sequential memory access

#### Step 2: Free List Initialization

UE5 uses a **free list allocator** for particle IDs. This is **critical** for understanding UzEngine's crash:

```cpp
// NiagaraComputeExecutionContext.h:26-57
struct FNiagaraGpuSpawnInfo
{
    uint32 EventSpawnTotal;     // Total particles to spawn this frame
    uint32 SpawnRateInstances;  // New particles from spawn rate
    uint32 MaxParticleCount;    // Max allowed live particles
    
    // Multiple spawn groups (different spawn rate intervals)
    int32 SpawnInfoStartOffsets[NIAGARA_MAX_GPU_SPAWN_INFOS];
    FNiagaraGpuSpawnInfoParams SpawnInfoParams[NIAGARA_MAX_GPU_SPAWN_INFOS];
};
```

**The free list works as follows**:

1. **Initialization**: Compute shader allocates initial free list
   ```hlsl
   // Pseudo-code for free list init compute shader
   [numthreads(256, 1, 1)]
   void InitFreeList(uint3 DTid : SV_DispatchThreadID) {
       uint idx = DTid.x;
       if (idx < MaxParticleCount) {
           // Each index gets added to free list stack
           g_FreeIDsStack[idx] = idx;
           g_FreeIDsCount = MaxParticleCount;
       }
   }
   ```

2. **Spawn phase**: Allocate particle IDs from free list
   ```hlsl
   // Each spawn thread atomically pops an ID
   uint ParticleID;
   InterlockedExchange(g_FreeIDsStack[g_FreeIDsCount-1], -1, ParticleID);
   InterlockedAdd(g_FreeIDsCount, -1);
   
   // Map ID → index in data buffer
   g_IDToIndexTable[ParticleID] = NewBufferIndex;
   ```

3. **Update phase**: Particles simulate independently
4. **Kill phase**: Dead particles return IDs to free list
   ```hlsl
   InterlockedAdd(g_FreeIDsCount, 1);
   g_FreeIDsStack[g_FreeIDsCount-1] = DeadParticleID;
   ```

**Critical issue**: The free list **must be initialized with valid data before any spawn dispatch**. If the initialization fails, subsequent atomicexchange/add operations read garbage, allocate invalid IDs, and crash when writing to `g_IDToIndexTable[InvalidID]` (UAV out-of-bounds).

### 1.3 Buffer Swap Timing

Data buffers swap at the **start of each emitter's simulation phase**:

```cpp
// NiagaraDataSet.h:287-291
void FNiagaraDataSet::EndSimulate(bool SetCurrentData = true)
{
    // After all compute stages, mark the "next" buffer as current
    if (SetCurrentData) {
        SetCurrentData(&CurrentDestinationDataBuffer);
    }
}
```

**Timeline**:
```
Frame N:
├─ RenderThread Start
│  ├─ DataBuffers_RT[0] = Previous (for rendering)
│  ├─ DataBuffers_RT[1] = Next (destination for compute)
│  │
│  ├─ PreInitViews: Prep data interfaces
│  ├─ Initialize: Allocate particle IDs from free list ← **CRITICAL**
│  ├─ Spawn: Run spawn compute shader
│  ├─ Update: Run simulation compute shader
│  ├─ Kill: Run kill shader, return dead IDs to free list
│  │
│  ├─ AdvanceDataBuffer() → BufferSwapsThisFrame_RT++
│  │   Now DataBuffers_RT[0] = Previous simulation
│  │       DataBuffers_RT[1] = Next (destination) for next frame
│  │
│  └─ Rendering: Uses GetPrevDataBuffer() for vertex data
│
Frame N+1:
└─ Same as Frame N, with buffers rotated
```

### 1.4 Resource State Transitions

Niagara carefully manages resource states to avoid GPU validation errors. The state machine is:

```cpp
// NiagaraGpuComputeDispatch.cpp:144-162
template<typename TTransitionArrayType>
static void AddDataBufferTransitions(
    TTransitionArrayType& BeforeTransitionArray, 
    TTransitionArrayType& AfterTransitionArray, 
    FNiagaraDataBuffer* DestinationData, 
    ERHIAccess BeforeState = ERHIAccess::SRVMask,     // Default: readable
    ERHIAccess AfterState = ERHIAccess::UAVCompute    // Default: writable
)
{
    // Each buffer has three RW buffers that need state transitions
    if (FRHIUnorderedAccessView* FloatUAV = DestinationData->GetGPUBufferFloat().UAV)
    {
        // Before compute: SRV → UAV
        BeforeTransitionArray.Emplace(FloatUAV, BeforeState, AfterState);
        // After compute: UAV → SRV
        AfterTransitionArray.Emplace(FloatUAV, AfterState, BeforeState);
    }
    // Same for Half and Int buffers...
}
```

**State diagram per buffer**:
```
┌─────────────────┐
│   SRV Read      │  (Rendering, or previous compute read)
│  ERHIAccess:    │
│  SRVMask        │
└────────┬────────┘
         │ Transition before compute
         ▼
┌─────────────────┐
│   UAV Write     │  (Compute shader writes particles)
│  ERHIAccess:    │
│  UAVCompute     │  (atomics allowed)
└────────┬────────┘
         │ Transition after compute
         ▼
┌─────────────────┐
│   SRV Read      │  (Rendering next frame, or billing data interface reads)
└─────────────────┘
```

## 2. Compute Dispatch Pipeline

### 2.1 Dispatch Structure

Niagara's compute dispatch happens in **stages**, corresponding to simulation phases:

```cpp
// NiagaraGpuComputeDispatch.h:59-65
struct FNiagaraGpuDispatchGroup
{
    TArray<FNiagaraGPUSystemTick*>          TicksWithPerInstanceData;
    TArray<FNiagaraGpuDispatchInstance>     DispatchInstances;  // Emitters to dispatch
    TArray<FNiagaraGpuFreeIDUpdate>         FreeIDUpdates;      // Free list sync
};

struct FNiagaraGpuDispatchList
{
    TArray<uint32>                          CountsToRelease;
    TArray<FNiagaraGpuDispatchGroup>        DispatchGroups;     // Batched by shader
};
```

### 2.2 Dispatch Phases

The render thread executes compute dispatches in this sequence per frame:

```
Phase 1: PRE_INIT_VIEWS (if enabled)
├─ Data interface setup
├─ Constant buffer allocation
└─ No simulation yet

Phase 2: FIRST (main simulation)
├─ Initialize: Clear free lists, allocate particle IDs
├─ Spawn: Create new particles (reads spawn rate, reads free list, writes ID table)
├─ Update: Simulate all living particles (reads/writes position, velocity, etc.)
├─ Kill: Mark dead particles (reads lifetime, returns IDs to free list)
└─ Finalize: Compact particles (optional, defragmentation)

Phase 3: POST_OPAQUE (optional, for late-frame effects)
├─ Re-simulate some particles with depth buffer
├─ Collision detection against scene depth
└─ Soft particle fading

Phase 4: POST_RENDER (optional, for next-frame prep)
└─ Cleanup, readbacks, data channel updates
```

### 2.3 Root Signature and Descriptor Binding

Niagara compute shaders use a **structured root signature** (DX12):

```cpp
// Typical Niagara compute root signature:
[0] CBV: Global params (NumParticles, DeltaTime, EmitterSpawnInfo, etc.)
[1] CBV: Data interface parameters
[2] CBV: Shader-specific module parameters
[3] SRV Table: 64+ entries (textures, buffers, other emitter outputs)
[4] UAV Table: 64+ entries (particle data buffers, output targets)
[5] Sampler: Linear/Nearest samplers (reused across all dispatches)
```

**Critical difference from raster passes**:
- Raster: Root signature bound once at pipeline creation
- Compute: Root signature bound **per-dispatch**, because descriptor tables change per emitter

### 2.4 SRV vs UAV Access for Same Buffer

This is the **core source of UzEngine's crash**. In a multi-stage dispatch, the same buffer transitions between SRV and UAV:

```
Spawn Stage (initialize free list):
├─ Input SRV:  (none, reads only CB)
├─ Output UAV: g_FreeIDsStack (write)
└─ Barrier: After spawn, transition g_FreeIDsStack to SRV

Update Stage (allocate from free list):
├─ Input SRV:  g_FreeIDsStack (read)
├─ Output UAV: g_ParticleData_Position (write)
├─ Output UAV: g_IDToIndexTable (write) ← accesses g_FreeIDsStack[idx]
└─ Barrier: After update, transition buffers to SRV
```

**Barrier types** (from RenderGraph):

```cpp
// FRDGBarrier models resource state change
struct FRDGBarrier {
    FRDGResourceHandle Resource;
    ERHIAccess AccessBefore;  // What was happening before
    ERHIAccess AccessAfter;   // What will happen after
};

// Common access modes:
ERHIAccess::SRVMask        // Read-only in any shader
ERHIAccess::UAVCompute     // Read-write in compute shader
ERHIAccess::CopySrc        // Source for GPU copy
ERHIAccess::CopyDst        // Destination for GPU copy
ERHIAccess::IndirectArgs   // Read as indirect dispatch args
```

**Manual barrier insertion** (when RenderGraph isn't used):

```cpp
// NiagaraGpuComputeDispatch uses explicit barriers in some paths:
FRHITransitionInfo TransitionBefore[] = {
    FRHITransitionInfo(IDToIndexTableUAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
};
RHICmdList->Transition(MakeArrayView(TransitionBefore));

// Dispatch compute
RHICmdList->DispatchComputeShader(ThreadsX, ThreadsY, ThreadsZ);

// Transition back
FRHITransitionInfo TransitionAfter[] = {
    FRHITransitionInfo(IDToIndexTableUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
};
RHICmdList->Transition(MakeArrayView(TransitionAfter));
```

### 2.5 Constant Buffer Management

Each compute dispatch receives a per-frame **unified constant buffer**:

```cpp
// Pseudo C++ structure (actual layout is generated)
struct NiagaraComputeCB {
    // Core params
    uint32  NumParticles;
    uint32  NumParticlesToSpawn;
    float   DeltaTime;
    float   TotalSimulatedTime;
    
    // Spawn info (multiple spawn groups possible)
    uint32  SpawnGroupCount;
    FNiagaraGpuSpawnInfoParams SpawnInfoParams[8];
    
    // Camera data (for distance-based effects, soft particles)
    float4x4 ViewMatrix;
    float4x4 InvViewMatrix;
    float3   CameraPosition;
    
    // Data interfaces (see section 3)
    // ... 100+ more fields depending on modules enabled
};
```

**Upload pattern**:
```cpp
void UploadComputeCB(FRHICommandList& RHICmdList, const FNiagaraComputeCB& CB) {
    void* CBData = RHICmdList.LockBuffer(ComputeCBHandle, 0, sizeof(CB), RLM_WriteOnly);
    FMemory::Memcpy(CBData, &CB, sizeof(CB));
    RHICmdList.UnlockBuffer(ComputeCBHandle);
}
```

## 3. Billboard Rendering Pipeline

### 3.1 Camera Data Flow

Billboard particles require camera parameters at **render-time**. UE5 passes this through:

```cpp
// For each billboard render pass:
struct FBillboardComputeParams {
    float4x4 ViewProj;           // Camera perspective matrix
    float4x4 InvView;            // Inverse view for per-particle camera direction
    float3   CameraRight;        // Right vector in world space
    float3   CameraUp;           // Up vector in world space
    float3   CameraPosition;     // Camera origin
};

// Passed via vertex shader constant buffer slot 1:
cbuffer CameraData : register(b1) {
    float4x4 ViewProj;
    float4x4 InvView;
    float3   CameraData[4];  // Camera vectors packed
};
```

### 3.2 Billboard Vertex Shader

The vertex shader **expands particles into quads** using camera orientation:

```hlsl
// NiagaraSpriteVertexFactory.ush (lines 1-150)

struct FVertexFactoryInput {
    float2 TexCoord : ATTRIBUTE0;        // 0,0 to 1,1 (quad corner)
    uint VertexId : SV_VertexID;         // 0-3 (quad corners)
    uint InstanceId : SV_InstanceID;     // Particle index
};

float4 main(FVertexFactoryInput Input) : SV_Position {
    uint ParticleID = Input.InstanceId;
    
    // Load particle data from GPU buffer via SRV
    Particle p = LoadParticle(ParticleID);
    
    // Build billboard quad (2 triangles, 4 vertices)
    float3 CameraDir = normalize(CameraPosition - p.Position);
    float3 Right = normalize(cross(CameraUp, CameraDir)) * p.Size * Input.TexCoord.x;
    float3 Up = normalize(cross(CameraDir, Right)) * p.Size * Input.TexCoord.y;
    
    float3 VertexPosition = p.Position + Right + Up;
    
    // Transform to screen space
    return mul(float4(VertexPosition, 1), ViewProj);
}
```

**Facing modes** (from `NiagaraSpriteVertexFactory.ush`):

```hlsl
#define FACING_MODE_FACECAMERA              0  // Look at camera, preserve camera up
#define FACING_MODE_FACECAMERAPLANE         1  // Completely parallel to camera plane (flat)
#define FACING_MODE_CUSTOMFACINGVECTOR      2  // Face toward SpriteFacing attribute
#define FACING_MODE_FACECAMERAPOSITION      3  // Face camera but use fixed up (0,0,1)
#define FACING_MODE_FACECAMERADISTANCEBLEND 4  // Blend modes 0 and 3 by distance

// Pipeline determines facing mode at shader compilation time
// Different permutations for each mode
```

### 3.3 Rendering Pass Structure

```cpp
// Pseudo-code for billboard rendering pass
void RenderBillboards(FRDGBuilder& GraphBuilder, FNiagaraDataBuffer* DataBuffer) {
    // 1. Prep render target (color + depth)
    FRDGTextureRef ColorTarget = GraphBuilder.RegisterExternalTexture(...);
    FRDGTextureRef DepthTarget = GraphBuilder.RegisterExternalTexture(...);
    
    // 2. Add pass to graph
    GraphBuilder.AddPass(
        RDG_EVENT_NAME("Niagara Billboards"),
        [&](FRHICommandList& RHICmdList) {
            // 3. Set render targets
            RHICmdList.SetRenderTargets(ColorTarget, DepthTarget);
            
            // 4. Set pipeline state
            RHICmdList.SetPipelineState(BillboardPSO);
            
            // 5. Set root signature
            RHICmdList.SetRootSignature(BillboardRootSig);
            
            // 6. Bind data buffer SRV (for vertex fetch)
            RHICmdList.SetComputeShaderResources(3, {
                DataBuffer->GetGPUBufferFloat().SRV,
                DataBuffer->GetGPUBufferInt().SRV,
                // ...
            });
            
            // 7. Bind camera CB
            RHICmdList.SetRootConstants(0, sizeof(CameraData), &CameraData);
            
            // 8. Draw indexed (quad per particle)
            RHICmdList.DrawIndexedInstanced(
                6,                      // 6 indices per quad (2 triangles)
                DataBuffer->GetNumInstances(),  // Instance count = particle count
                0,                      // Index offset
                0,                      // Vertex offset
                0                       // Instance offset
            );
        }
    );
}
```

## 4. Ping-Pong Buffer Strategy

### 4.1 Double-Buffering

Niagara uses **double-buffering (NOT triple-buffering)** on the GPU:

```cpp
// Two buffers rotate each frame
FNiagaraDataBuffer* DataBuffers_RT[2];

// Buffer 0: Previous (last frame's result)
// Buffer 1: Next (this frame's destination)

// After simulation completes:
AdvanceDataBuffer();  // Swap: (BufferSwapsThisFrame_RT & 1) flips which is which
```

**Rationale**: GPU work is **immediately submitted** to the command queue, so by the time rendering happens, compute has finished.

**Timeline**:
```
Frame N Render Thread:
├─ BufferSwapsThisFrame_RT = 0
│  └─ GetPrevDataBuffer() = DataBuffers_RT[1]  (frame N-1 result)
│  └─ GetNextDataBuffer() = DataBuffers_RT[0]  (frame N destination)
│
├─ Run compute → writes to DataBuffers_RT[0]
│
├─ AdvanceDataBuffer() → BufferSwapsThisFrame_RT = 1
│
└─ Render billboards using GetPrevDataBuffer() = DataBuffers_RT[1]
   (frame N-1 data, which is valid by now since compute finished)

Frame N+1 Render Thread:
├─ BufferSwapsThisFrame_RT = 1
│  └─ GetPrevDataBuffer() = DataBuffers_RT[0]  (frame N result, just written)
│  └─ GetNextDataBuffer() = DataBuffers_RT[1]  (frame N+1 destination)
│
└─ ... repeat
```

### 4.2 When Swaps Happen

The **swap is ordered explicitly** in the dispatch sequence:

```cpp
// NiagaraGpuComputeDispatch.cpp (conceptual)
void ExecuteTicks(FRDGBuilder& GraphBuilder, ...) {
    for (auto& DispatchInstance : DispatchInstances) {
        // 1. Initialize (spawn/kill) → writes to "Next"
        DispatchStage(..., SimStageData.Initialize);
        
        // 2. Main update → writes to "Next"
        DispatchStage(..., SimStageData.Update);
        
        // 3. Finalize, prepare for next frame
        DispatchInstance.ComputeContext->AdvanceDataBuffer();
        // ↑ After this, "Next" becomes "Prev" for *next* frame
    }
}
```

**Important**: The swap happens **per emitter**, not per-frame. If a system has 5 emitters, each one's buffer swaps when its simulation completes.

## 5. Critical Implementation Patterns for UzEngine

### 5.1 Free List Initialization (MUST DO FIRST)

This is **the** most critical piece. UzEngine's crash likely stems from uninitialized free lists.

**Pattern from Niagara** (pseudo-HLSL):

```hlsl
// Free ID Stack (UAV)
RWStructuredBuffer<uint> g_FreeIDsStack : register(u0);
RWBuffer<uint> g_FreeIDsCount : register(u1);

// FIRST dispatch of the frame (must run exactly once before spawn)
[numthreads(256, 1, 1)]
void InitFreeList(uint3 DTid : SV_DispatchThreadID) {
    uint idx = DTid.x;
    if (idx < g_MaxParticleCount) {
        // Initialize free list: all particles start as available
        g_FreeIDsStack[idx] = idx;
    }
    
    if (idx == 0) {
        // One thread initializes the count
        g_FreeIDsCount[0] = g_MaxParticleCount;
    }
}

// Then, spawn dispatch
[numthreads(256, 1, 1)]
void SpawnParticles(uint3 DTid : SV_DispatchThreadID) {
    uint SpawnIdx = DTid.x;
    if (SpawnIdx >= g_NumParticlesToSpawn) return;
    
    // Allocate a particle ID from free list
    uint FreeSlot;
    InterlockedAdd(g_FreeIDsCount[0], -1, FreeSlot);
    // Now FreeSlot is the count BEFORE decrement, i.e., valid index
    
    uint ParticleID = g_FreeIDsStack[FreeSlot];  // Safe read
    
    // Initialize particle
    g_ParticlePosition[ParticleID] = SpawnPosition;
    g_ParticleVelocity[ParticleID] = SpawnVelocity;
    
    // Map ID → index
    g_IDToIndexTable[ParticleID] = ParticleID;  // Or compaction index if used
}

// Finally, kill dispatch
[numthreads(256, 1, 1)]
void KillParticles(uint3 DTid : SV_DispatchThreadID) {
    uint idx = DTid.x;
    if (g_ParticleLifetime[idx] <= 0) {
        // Return dead particle to free list
        uint FreeSlot;
        InterlockedAdd(g_FreeIDsCount[0], 1, FreeSlot);
        g_FreeIDsStack[FreeSlot] = idx;
    }
}
```

**Order is CRITICAL**:
1. `InitFreeList` (once per frame) → initializes stacks and count
2. `SpawnParticles` → pops from free list
3. `UpdateParticles` → uses particle data
4. `KillParticles` → pushes back to free list

If steps are out of order or missing, you get:
- Uninitialized reads from free list → invalid particle IDs
- Out-of-bounds UAV writes → GPU crash ("Device Removed")
- Stale ID table entries → rendering artifacts

### 5.2 Resource State Transitions

**Before EVERY compute dispatch**:

```cpp
// Transition particle buffers from SRV (if read) to UAV (for write)
FRDGBarrier BeforeBarriers[] = {
    FRDGBarrier(ParticleDataUAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
    FRDGBarrier(IDToIndexTableUAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
    FRDGBarrier(FreeListStackUAV, ERHIAccess::SRVMask, ERHIAccess::UAVCompute),
};
RHICmdList->Transition(MakeArrayView(BeforeBarriers));

// Dispatch
RHICmdList->DispatchComputeShader(ThreadGroupsX, ThreadGroupsY, 1);

// Transition back to SRV for next pass or rendering
FRDGBarrier AfterBarriers[] = {
    FRDGBarrier(ParticleDataUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
    FRDGBarrier(IDToIndexTableUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
    FRDGBarrier(FreeListStackUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
};
RHICmdList->Transition(MakeArrayView(AfterBarriers));
```

### 5.3 Double-Buffer Swap Ordering

```cpp
// Per emitter, after simulation completes:
void FinalizeEmitterSimulation(FNiagaraComputeExecutionContext* Context) {
    // All compute dispatches done
    // Now safe to swap buffers
    
    // Option 1: Swap immediately
    Context->AdvanceDataBuffer();
    // GetPrevDataBuffer() now returns freshly-written data for rendering
    
    // Option 2: Defer to next frame's prep
    Context->bPendingBufferSwap = true;
    // (handled in next frame's initialization)
}
```

### 5.4 Constant Buffer Packing

Niagara carefully packs CB data to avoid gaps (costly padding):

```cpp
// Bad: relies on compiler packing (varies by platform)
struct BadCB {
    float3 Position;        // 12 bytes
    // 4 byte padding inserted here by compiler!
    uint ParticleID;        // 4 bytes → offset 16, not 12
};

// Good: explicit alignment
struct GoodCB {
    float3 Position;        // 12 bytes
    uint ParticleID;        // 4 bytes → offset 12
    // Total 16 bytes, aligned to float4
    
    // Or use packing macro:
    // #define PACK_2xF32_1xU32 float4
    float4 Data;  // Stores Position.xy and Position.z, ParticleID in .w
};
```

Niagara generates CB layouts **automatically** from module declarations to ensure tight packing.

## 6. Common Failure Modes and Solutions

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| Device Removed crash on spawn | Free list uninitialized or bounds check missing | Run InitFreeList dispatch FIRST; add bounds checks to ID allocation |
| Particles spawn at wrong positions | Buffer swap happened before compute finished | Ensure AdvanceDataBuffer() happens AFTER all dispatches, not before |
| Rendering shows previous frame's particles | Prev/Next buffer pointers flipped | Check `GetPrevDataBuffer()` and `GetNextDataBuffer()` logic; verify `BufferSwapsThisFrame_RT` increments |
| Constant buffer stale data | CB uploaded before data interfaces were updated | Call PreStageInterface() BEFORE setting CB params |
| Rendering uses uninitialized particles | Transition UAV → SRV not inserted | Add explicit barriers between dispatch and rendering |
| Particle count jumps to max every frame | Free list count not reset between frames | InitFreeList must run once per frame, not per-spawn |

## 7. Recommended UzEngine Implementation Plan

Based on this analysis, here's the order to implement:

1. **Phase 1: Core GPU Buffer Management** (Week 1)
   - Implement FNiagaraDataBuffer equivalent (see section 1.1)
   - Implement AllocateGPU() with SRV/UAV creation
   - Test: Allocate 10K particles, verify GPU memory usage

2. **Phase 2: Free List Allocator** (Week 1-2)
   - Implement free list init compute shader (section 5.1)
   - Implement spawn allocator (InterlockedAdd pattern)
   - Test: Spawn 1000 particles, verify no out-of-bounds

3. **Phase 3: Compute Dispatch Pipeline** (Week 2)
   - Implement DispatchGroup batching (section 2.2)
   - Add InitFreeList dispatch (must be first)
   - Add resource state transitions (section 5.2)
   - Test: Dispatch all phases, verify no GPU validation errors

4. **Phase 4: Double-Buffer Swap** (Week 2-3)
   - Implement DataBuffers_RT[2] pattern
   - Implement AdvanceDataBuffer() at correct timing
   - Test: Verify GetPrevDataBuffer() returns frame-old data

5. **Phase 5: Billboard Rendering** (Week 3)
   - Implement billboard vertex shader (section 3.2)
   - Wire camera data through CB
   - Test: Render particles, verify correct billboard orientation

## References

- **UE5 Niagara Source**: `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara`
- **Core Classes**:
  - `NiagaraDataSet.h` (lines 85-260): FNiagaraDataBuffer structure
  - `NiagaraComputeExecutionContext.h` (lines 66-225): Execution context and buffer management
  - `NiagaraGpuComputeDispatch.cpp` (lines 144-162): Resource state transitions
- **Shader Code**:
  - `NiagaraSpriteVertexFactory.ush` (lines 1-150): Billboard vertex shader and facing modes
- **Documentation**:
  - Previous analysis files: `niagara-di-architecture-pattern.md`, `ue5-niagara-source-map.md`

---

**Document Generated**: 2026-04-02  
**Analysis Scope**: GPU buffer lifecycle, compute dispatch, resource states  
**Confidence Level**: HIGH — sourced from actual UE5 5.6 Niagara implementation

