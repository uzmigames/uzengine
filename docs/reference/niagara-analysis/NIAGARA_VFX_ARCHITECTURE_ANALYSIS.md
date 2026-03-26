# Unreal Engine Niagara VFX System: Architectural Analysis for UzEngine

**Analysis Date**: 2026-03-24
**Target Codebase**: Unreal Engine 5.6 Niagara Plugin
**Purpose**: Extract architectural patterns for UzEngine VFX system adaptation

---

## Executive Summary

Niagara's architecture succeeds through five core design decisions:

1. **Particle Data as SOA (Structure-of-Arrays)** with per-type buffers instead of AOS
2. **Render Proxy Pattern** to avoid game-thread/render-thread type knowledge coupling
3. **Data Interface Plugin System (NDI)** for extensible collision, sampling, grid operations without tight coupling
4. **GPU Compute Dispatch Batching** with per-instance shader permutation compilation and free list memory management
5. **Indirect Draw Buffer System** with per-renderer instance count slots and frame-local allocation

**Key Insight**: Niagara decouples problem domains at architectural boundaries using **plug-in data interfaces** and **render proxy objects**, not switch statements. This enables GPU simulation without tightly coupling simulation code to rendering code.

---

## 1. GPU Particle Simulation Architecture

### 1.1 Particle Data Storage: SOA (Structure-of-Arrays)

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataSet.h` (not fully shown, referenced in compile data)

**Pattern**: Particles are stored as multiple large buffers, **not individual particles**.

```cpp
// From NiagaraGPUInstanceCountManager.h, line 154-155
/** A buffer holding the each emitter particle count after a simulation tick. */
FRWBuffer CountBuffer;
FRWBuffer CulledCountBuffer;
```

**Why SOA?**
- Single GPU texture SRV lookup per attribute per frame
- Coalesced memory access in compute shaders (threads access same column)
- Avoids cache-line waste from AOS (where adjacent particles interfere)
- Enables vertex factory to bind SRVs directly per attribute type

**UzEngine Adaptation**: For a data-driven particle system, store particles as:
```c
// Proposed uz_particle_buffer.h structure
struct uz_particle_buffer_t {
    uz_gpu_buffer_t position_buffer;   // float4 x N particles
    uz_gpu_buffer_t velocity_buffer;   // float4 x N particles
    uz_gpu_buffer_t color_buffer;      // uint32 x N particles (packed RGBA)
    uz_gpu_buffer_t life_buffer;       // float2 x N particles (age, lifetime)
    // ... one SRV per attribute type
};
```

### 1.2 Free List & Dead Particle Compaction

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraGPUInstanceCountManager.h`, lines 47-56, 72-80

```cpp
struct FIndirectArgSlot {
    FBufferRHIRef Buffer;
    FShaderResourceViewRHIRef SRV;
    uint32 Offset = INDEX_NONE;
    inline bool IsValid() const { return Offset != INDEX_NONE; }
};

/** Acquire an entry from the free list, assumes this comes from being presized. */
uint32 AcquireEntry();

/** Acquire an entry, this will either come from the free list or reallocate the buffer. */
uint32 AcquireOrAllocateEntry(FRHICommandListImmediate& RHICmdList);

/** Free the entry and reset it to INDEX_NONE if valid. */
void FreeEntry(uint32& BufferOffset);
```

**Pattern**: Frame-local allocation with free list recycling:
- Allocate a "slot" (offset into the count buffer) per emitter instance per frame
- Return unused slots to `FreeEntries` array (line 156)
- Clear counts with compute shader pass before render (see `NiagaraClearCounts.usf`)
- No fragmentation: slots are reused, buffer is pre-sized and never grows mid-frame

**Why this works**:
- O(1) allocation/deallocation (free list pop/push)
- No garbage collection pressure on game thread
- Fixed memory footprint per emitter instance
- Render thread never allocates (only game thread calls `AcquireEntry()`)

**UzEngine Adaptation**: For UzEngine's arena-based allocator:
```c
// uz_particle_allocator.h
typedef struct {
    uint32_t* free_list;      // Stack of available indices
    uint32_t free_count;
    uint32_t max_particles;
    uint32_t allocated_count; // High water mark
} uz_particle_allocator_t;

// Acquire slot from emitter's per-instance allocator
uint32_t uz_particle_acquire_slot(uz_particle_allocator_t* alloc) {
    return alloc->free_list[--alloc->free_count];
}

// Return slot to free list
void uz_particle_free_slot(uz_particle_allocator_t* alloc, uint32_t slot) {
    alloc->free_list[alloc->free_count++] = slot;
}
```

### 1.3 Dead Particle List & Compaction

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Private/NiagaraGpuComputeDispatch.cpp` (NiagaraComputeFreeIDs.usf shader)

**Pattern**: Two-pass compaction:
1. **Pass 1 (Simulate Stage)**: Mark dead particles by writing index to "dead particle list" (UAV)
2. **Pass 2 (Dispatch Stage)**: Compute shader counts dead particles, builds free list from dead indices

**Shader Logic** (from `NiagaraComputeFreeIDs.usf`):
```hlsl
// GPU simulation shader: particles write dead particle indices to dead list
if (Particle.Age > Particle.Lifetime) {
    uint DeadIndex = InterlockedAdd(DeadParticleCount[0], 1);
    DeadParticles[DeadIndex] = ParticleIndex;
}

// Then: separate compute pass reads dead list, updates free list
```

**Why**: Avoids CPU readback. All dead particle detection happens on GPU.

**UzEngine Adaptation**:
```c
// uz_particle_simulator.hlsl (pseudo-HLSL)
RWBuffer<uint> deadParticleList;
RWStructuredBuffer<uint> deadParticleCount;

[numthreads(64, 1, 1)]
void SimulateParticles(uint3 tid : SV_DispatchThreadID) {
    Particle p = LoadParticle(tid.x);
    p.age += deltaTime;

    if (p.age > p.lifetime) {
        uint deadIdx = InterlockedAdd(deadParticleCount[0], 1);
        deadParticleList[deadIdx] = tid.x;
    }

    StoreParticle(tid.x, p);
}
```

---

## 2. GPU Compute Dispatch & Batching Architecture

### 2.1 Dispatch Grouping & Per-Instance Compilation

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Private/NiagaraGpuComputeDispatch.h`, lines 32-81

```cpp
struct FNiagaraGpuDispatchInstance {
    const FNiagaraGPUSystemTick& Tick;
    const FNiagaraComputeInstanceData& InstanceData;
    FNiagaraSimStageData SimStageData;
};

struct FNiagaraGpuDispatchGroup {
    TArray<FNiagaraGPUSystemTick*> TicksWithPerInstanceData;
    TArray<FNiagaraGpuDispatchInstance> DispatchInstances;
    TArray<FNiagaraGpuFreeIDUpdate> FreeIDUpdates;
};

struct FNiagaraGpuDispatchList {
    TArray<uint32> CountsToRelease;
    TArray<FNiagaraGpuDispatchGroup> DispatchGroups;
};
```

**Pattern**: Instances are grouped by **shader permutation** (same script, same spawn/update stages).

- One dispatch group = one compute shader permutation
- Multiple instances per group = one big dispatch (threaded by particle count)
- Reduces RHI overhead: 1 PSO bind per group instead of per instance

**Dispatch Flow** (from `FNiagaraGpuComputeDispatch::ExecuteTicks`, lines 194):
1. **Prepare**: Group instances by shader permutation
2. **Allocate**: Acquire count buffer slots for each instance
3. **Dispatch**: Launch compute shader with indirect draw count
4. **Finalize**: Update free ID lists

**Why**: Reduces pipeline state object (PSO) binds. If you have 100 emitter instances using the same script, that's 1 PSO bind, not 100.

**UzEngine Adaptation**: Batch particle emitters by simulation stage shader:
```c
// uz_particle_dispatch.h
typedef struct {
    uz_shader_t* stage_shader;      // e.g., uz_particle_spawn_shader
    uz_emitter_instance_t** instances;
    uint32_t instance_count;
} uz_dispatch_batch_t;

// Group instances by shader, then dispatch in one call
uz_dispatch_batch_t batches[UZ_MAX_EMITTER_TYPES];

// Dispatch entire batch
for (uint32_t b = 0; b < batch_count; ++b) {
    uz_rhi_bind_compute_shader(cmd, batches[b].stage_shader);
    uz_rhi_dispatch_indirect(cmd, instance_count);
}
```

### 2.2 Indirect Dispatch Arguments

**Location**: `Engine/Plugins/FX/Niagara/Shaders/Private/NiagaraEmitterInstanceShader.usf`, lines 88-95

```hlsl
#if NIAGARA_DISPATCH_INDIRECT
    static uint3 DispatchThreadIdBounds;
    Buffer<uint4> IndirectDispatchArgs;
    uint IndirectDispatchArgsOffset;
#else
    uint3 DispatchThreadIdBounds;
    uint3 DispatchThreadIdToLinear;
#endif
```

**Pattern**: GPU compute shader receives its thread count from a buffer (indirect dispatch).

Dispatch call (C++):
```cpp
RHICmdList.DispatchIndirectComputeShader(
    IndirectArgsBuffer,  // Contains actual count from simulation
    IndirectArgsOffset   // Offset into buffer
);
```

**Why**: Particle count changes every frame. Rather than readback (CPU stall), the count buffer itself contains the next frame's spawn count. The GPU compute command reads it.

**UzEngine Adaptation**:
```c
// uz_particle_commands.h
typedef struct {
    uz_gpu_buffer_t count_buffer;    // Writable by GPU, readable by compute dispatch
    uint32_t count_offset;           // Offset in DWORDS
} uz_indirect_dispatch_args_t;

// C code:
uz_rhi_dispatch_indirect_compute(cmd, count_buffer, count_offset, shader);
```

---

## 3. Emitter/System Hierarchy & Parameter Binding

### 3.1 System -> Emitter -> Renderer Composition

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraSystem.h` (partial structure)

```cpp
// NiagaraSystem.h contains:
struct FNiagaraEmitterHandle {
    // ID, name, version reference to a UNiagaraEmitter
};

// NiagaraSystem owns:
TArray<FNiagaraEmitterHandle> EmitterHandles;
TArray<FNiagaraEmitterCompiledData> EmitterCompiledData;

// Each emitter owns renderers:
struct FVersionedNiagaraEmitterData {
    TArray<UNiagaraRendererProperties*> RendererProperties;
};
```

**Hierarchy**:
```
FNiagaraSystem
├─ FNiagaraEmitterHandle [0] → UNiagaraEmitter
│  ├─ UNiagaraSpriteRendererProperties
│  ├─ UNiagaraMeshRendererProperties
│  └─ UNiagaraLightRendererProperties
├─ FNiagaraEmitterHandle [1] → UNiagaraEmitter
│  └─ ...
```

**Why no virtual dispatch**: Each component type (System, Emitter, Renderer) is a data structure with no virtual functions. Parameter binding is table-driven.

### 3.2 Parameter Binding via Tables (No Type Switch)

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraEmitter.h`, lines 38-100

```cpp
USTRUCT() struct FNiagaraEventReceiverProperties {
    FName Name;
    FName SourceEventGenerator;
    FName SourceEmitter;
    FNiagaraDataSetCompiledData DataSetCompiledData;  // Layout metadata, not instance data
};

USTRUCT() struct FNiagaraEventGeneratorProperties {
    int32 MaxEventsPerFrame = 64;
    FName ID;
    FNiagaraDataSetCompiledData DataSetCompiledData;
};
```

**Pattern**: Parameter binding is **metadata-driven**.
- Compile stage: introspect script, build parameter layout tables
- Runtime: use tables to pack/unpack data without knowing types

**UzEngine Adaptation**: Build parameter descriptor tables:
```c
// uz_shader_parameters.h
typedef struct {
    const char* name;
    uint32_t offset;          // Offset in CBuf or SRV
    uint32_t size;            // Size in bytes
    uz_shader_param_type_t type;  // FLOAT, INT, TEXTURE, etc.
} uz_shader_param_desc_t;

typedef struct {
    uz_shader_param_desc_t* params;
    uint32_t param_count;
} uz_shader_param_layout_t;

// Bind without knowing types:
void uz_bind_parameters(
    uz_shader_param_layout_t* layout,
    const void* param_data,
    uz_gpu_command_t* cmd
) {
    for (uint32_t i = 0; i < layout->param_count; ++i) {
        uz_shader_param_desc_t* p = &layout->params[i];
        const void* value = (const char*)param_data + p->offset;
        uz_rhi_set_shader_param(cmd, p->name, value, p->size);
    }
}
```

---

## 4. Data Interface System (NDI): Extensibility Without Type Coupling

### 4.1 Data Interface Base Class Architecture

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterface.h`, lines 264-350

```cpp
struct FNDIGpuComputeContext {
    FRDGBuilder& GraphBuilder;
    FNiagaraGpuComputeDispatchInterface& ComputeDispatchInterface;

    FRDGBuilder& GetGraphBuilder() const { return GraphBuilder; }
    const FNiagaraGpuComputeDispatchInterface& GetComputeDispatchInterface() const {
        return ComputeDispatchInterface;
    }
};

struct FNDIGpuComputePrePostStageContext : public FNDIGpuComputeContext {
    const FNiagaraGPUSystemTick& SystemTick;
    const FNiagaraComputeInstanceData& ComputeInstanceData;
    const FNiagaraSimStageData& SimStageData;
};
```

**Pattern**: Contexts are **opaque parameter bags** passed to data interface methods.

Niagara doesn't switch on data interface type. Instead:

```cpp
// From NiagaraGpuComputeDispatch.h, lines 151-155
void ResetDataInterfaces(FRDGBuilder& GraphBuilder, const FNiagaraGPUSystemTick& Tick,
                         const FNiagaraComputeInstanceData& InstanceData);

void SetDataInterfaceParameters(FRDGBuilder& GraphBuilder, const FNiagaraGPUSystemTick& Tick,
                                const FNiagaraComputeInstanceData& InstanceData,
                                const FNiagaraShaderRef& ComputeShader, ...);

void PreStageInterface(FRDGBuilder& GraphBuilder, ...);
void PostStageInterface(FRDGBuilder& GraphBuilder, ...);
void PostSimulateInterface(FRDGBuilder& GraphBuilder, ...);
```

**Key Insight**: Niagara has a small set of **fixed lifecycle hooks** (PreStage, PostStage, PostSimulate), and every NDI implements them. No switch statements, no type casting.

### 4.2 Collision Query Data Interface Example

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceCollisionQuery.h`, lines 24-82

```cpp
UCLASS(EditInlineNew, Category = "Collision")
class UNiagaraDataInterfaceCollisionQuery : public UNiagaraDataInterface {
public:
    // Per-instance CPU data
    virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance) override;
    virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* InSystemInstance) override;

    // CPU simulation hooks
    virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
    virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;

    // GPU binding
    virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
    virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

    // VM binding (CPU simulation)
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

    // CPU-side function implementations
    void PerformQuerySyncCPU(FVectorVMExternalFunctionContext& Context);
    void PerformQueryAsyncCPU(FVectorVMExternalFunctionContext& Context);
};
```

**Key Methods**:
- `InitPerInstanceData()` - allocate CPU-side cache (e.g., collision batch state)
- `PerInstanceTick()` - update CPU cache before simulation (collect scene traces)
- `SetShaderParameters()` - bind GPU resources (read-only access to collision data)
- `GetVMExternalFunction()` - CPU-mode fallback for non-GPU simulation

**Why this is extensible**: To add a new NDI (e.g., particle grid sampling):
1. Subclass `UNiagaraDataInterface`
2. Implement the hooks above
3. Register in module
4. No changes to core Niagara dispatch code

**UzEngine Adaptation**: Build a similar plugin interface:
```c
// uz_particle_interface.h
typedef enum {
    UZ_NDI_LIFECYCLE_PRE_STAGE,
    UZ_NDI_LIFECYCLE_POST_STAGE,
    UZ_NDI_LIFECYCLE_POST_SIMULATE,
} uz_ndi_lifecycle_t;

typedef struct uz_ndi_t uz_ndi_t;

typedef struct {
    // Lifecycle hooks
    bool (*pre_stage)(uz_ndi_t* self, uz_ndi_context_t* ctx);
    bool (*post_stage)(uz_ndi_t* self, uz_ndi_context_t* ctx);
    bool (*post_simulate)(uz_ndi_t* self, uz_ndi_context_t* ctx);

    // GPU parameter binding
    void (*build_shader_params)(uz_ndi_t* self, uz_shader_builder_t* builder);
    void (*set_shader_params)(uz_ndi_t* self, uz_shader_context_t* ctx);
} uz_ndi_vtable_t;

typedef struct uz_ndi_t {
    uz_ndi_vtable_t* vtable;
    void* instance_data;  // Per-system-instance private data
} uz_ndi_t;

// Register new NDI
uz_ndi_t* uz_ndi_collision_create();
uz_ndi_registry_add(uz_ndi_collision_create());
```

---

## 5. Render Proxy Pattern: CPU/GPU Decoupling

### 5.1 Scene Proxy & Dynamic Data Separation

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraRenderer.h`, lines 30-104

```cpp
/** Struct used to pass dynamic data from game thread to render thread */
struct FNiagaraDynamicDataBase {
    virtual ~FNiagaraDynamicDataBase();

    FNiagaraDataBuffer* GetParticleDataToRender(FRHICommandListBase& RHICmdList,
                                                 bool bIsLowLatencyTranslucent = false) const;
    FMaterialRelevance GetMaterialRelevance() const { return MaterialRelevance; }
    virtual void ApplyMaterialOverride(int32 MaterialIndex, UMaterialInterface* MaterialOverride) {};

protected:
    FMaterialRelevance MaterialRelevance;
    FNiagaraSystemInstanceID SystemInstanceID;
    FNiagaraDataBufferRef CPUParticleData;
    INiagaraComputeDataBufferInterface* ComputeDataBufferInterface = nullptr;
};

class FNiagaraRenderer {
public:
    // Core rendering interface
    virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy,
                                                          const UNiagaraRendererProperties* InProperties,
                                                          const FNiagaraEmitterInstance* Emitteride) const { return nullptr; }

    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                                        const FSceneViewFamily& ViewFamily,
                                        uint32 VisibilityMap,
                                        FMeshElementCollector& Collector,
                                        const FNiagaraSceneProxy *SceneProxy) const {}

    void SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData);

protected:
    FNiagaraDynamicDataBase *DynamicDataRender;
};
```

**Pattern**: Two-stage render call:

1. **Game Thread** (`GenerateDynamicData`):
   - Takes `FNiagaraEmitterInstance* Emitter` (game-thread only)
   - Returns opaque `FNiagaraDynamicDataBase` blob
   - No rendering code runs here; just CPU-side data packing

2. **Render Thread** (`GetDynamicMeshElements`):
   - Takes `FNiagaraDynamicDataBase* DynamicData` (opaque)
   - Does NOT know about `FNiagaraEmitterInstance`
   - Extracts only what it needs (particle buffer, material, count)
   - Can't accidentally access game-thread state

**Why**: Prevents render thread from accessing game-thread data (which causes race conditions).

### 5.2 Sprite Renderer: Concrete Example

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraRendererSprites.h`, lines 40-92

```cpp
class FNiagaraRendererSprites : public FNiagaraRenderer {
public:
    virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy,
                                                          const UNiagaraRendererProperties* InProperties,
                                                          const FNiagaraEmitterInstance* Emitter) const override;

    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                                        const FSceneViewFamily& ViewFamily,
                                        uint32 VisibilityMap,
                                        FMeshElementCollector& Collector,
                                        const FNiagaraSceneProxy *SceneProxy) const override;

private:
    struct FParticleSpriteRenderData {
        const FNiagaraDynamicDataSprites* DynamicDataSprites = nullptr;
        class FNiagaraDataBuffer* SourceParticleData = nullptr;

        FRHIShaderResourceView* ParticleFloatSRV = nullptr;
        FRHIShaderResourceView* ParticleHalfSRV = nullptr;
        FRHIShaderResourceView* ParticleIntSRV = nullptr;
    };

    void PrepareParticleSpriteRenderData(FRHICommandListBase& RHICmdList,
                                         FParticleSpriteRenderData& ParticleSpriteRenderData,
                                         const FSceneViewFamily& ViewFamily,
                                         FNiagaraDynamicDataBase* InDynamicData,
                                         const FNiagaraSceneProxy* SceneProxy) const;
};
```

**Sprite-specific flow**:
1. `GenerateDynamicData()`: Cache material relevance, particle data pointer (CPU side)
2. `GetDynamicMeshElements()`: Receive opaque dynamic data, set up vertex factory with SRVs

**UzEngine Adaptation**: For your particle renderer:
```c
// uz_particle_renderer.h
typedef struct {
    uz_particle_buffer_t* particle_data;
    uz_material_t* material;
    uint32_t particle_count;
    uint32_t count_buffer_offset;
} uz_dynamic_particle_data_t;

// Game thread: prepare data
uz_dynamic_particle_data_t* uz_sprite_renderer_generate_data(
    uz_emitter_instance_t* emitter,
    uz_renderer_properties_t* props
) {
    uz_dynamic_particle_data_t* data = uz_malloc(sizeof(*data));
    data->particle_data = emitter->particle_buffer;
    data->material = props->material;
    data->particle_count = emitter->alive_count;
    return data;
}

// Render thread: consume opaque data (no access to emitter_instance_t)
void uz_sprite_renderer_draw(
    uz_dynamic_particle_data_t* data,
    uz_view_t* view,
    uz_command_list_t* cmd
) {
    uz_set_vertex_factory_srv(cmd, "ParticleData", data->particle_data->position_buffer);
    uz_set_shader_param(cmd, "ParticleCount", data->particle_count);
    uz_draw_indirect(cmd, data->particle_data->indirect_args);
}
```

---

## 6. Indirect Draw Buffer System

### 6.1 Instance Count Manager & Slot Allocation

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraGPUInstanceCountManager.h`, lines 40-191

```cpp
class FNiagaraGPUInstanceCountManager {
public:
    struct FIndirectArgSlot {
        FBufferRHIRef Buffer;
        FShaderResourceViewRHIRef SRV;
        uint32 Offset = INDEX_NONE;
        inline bool IsValid() const { return Offset != INDEX_NONE; }
    };

    // Allocate slot for draw call
    uint32 AcquireOrAllocateEntry(FRHICommandListImmediate& RHICmdList);
    void FreeEntry(uint32& BufferOffset);

    // Add draw indirect task (batched, run at frame end)
    FIndirectArgSlot AddDrawIndirect(FRHICommandListBase& RHICmdList,
                                      uint32 InstanceCountBufferOffset,
                                      uint32 NumIndicesPerInstance,
                                      uint32 StartIndexLocation,
                                      bool bIsInstancedStereoEnabled,
                                      bool bCulled,
                                      ENiagaraGpuComputeTickStage::Type ReadyTickStage);

    // Generate all indirect args buffers from tasks
    void UpdateDrawIndirectBuffers(FNiagaraGpuComputeDispatchInterface* ComputeDispatchInterface,
                                    FRHICommandList& RHICmdList,
                                    ENiagaraGPUCountUpdatePhase::Type CountPhase);

protected:
    struct FIndirectArgsPoolEntry {
        FRWBuffer Buffer;
        uint32 AllocatedEntries = 0;
        uint32 UsedEntries[ENiagaraGPUCountUpdatePhase::Max] = {};
    };

    int32 UsedInstanceCounts = 0;
    int32 AllocatedInstanceCounts = 0;

    FRWBuffer CountBuffer;               // Instance counts per emitter
    TArray<uint32> FreeEntries;          // Free list of slots
    TArray<FNiagaraDrawIndirectArgGenTaskInfo> DrawIndirectArgGenTasks[ENiagaraGPUCountUpdatePhase::Max];
    TMap<FNiagaraDrawIndirectArgGenTaskInfo, FNiagaraDrawIndirectArgGenSlotInfo> DrawIndirectArgMap;
```

**Pattern**: Three-level hierarchy:

1. **Instance Count Buffer** (one global): Holds live particle count per emitter instance
2. **Indirect Args Slots**: Offsets into indirect args buffer (per renderer)
3. **Indirect Args Pools**: Multiple buffers (for pre/post-opaque phases)

**Lifetime**:
- **Game Thread** (`AcquireEntry`): Allocate slot for emitter instance
- **Frame Update** (`UpdateDrawIndirectBuffers`): Run compute shader to write draw args
- **Render Thread** (draw call): Use generated draw args
- **Next Frame** (`FreeEntry`): Return slot to free list

### 6.2 Draw Indirect Argument Generation

**Location**: `Engine/Plugins/FX/Niagara/Shaders/Private/NiagaraDrawIndirectArgsGen.usf`

```hlsl
// Compute shader runs in UpdateDrawIndirectBuffers
// Reads: InstanceCount buffer at offset X
// Writes: DrawIndirectArgs buffer at offset Y

void GenerateDrawIndirectArgs(uint3 tid : SV_DispatchThreadID) {
    uint InstanceCountBufferOffset = tid.x;  // Which emitter

    uint ParticleCount = InstanceCountBuffer[InstanceCountBufferOffset];

    // Write D3D12_DRAW_INDEXED_ARGUMENTS structure
    DrawIndirectArgsBuffer[DrawIndirectArgsOffset + 0] = NumIndicesPerInstance;
    DrawIndirectArgsBuffer[DrawIndirectArgsOffset + 1] = ParticleCount;      // InstanceCount
    DrawIndirectArgsBuffer[DrawIndirectArgsOffset + 2] = StartIndexLocation;
    DrawIndirectArgsBuffer[DrawIndirectArgsOffset + 3] = BaseVertexLocation;
    DrawIndirectArgsBuffer[DrawIndirectArgsOffset + 4] = StartInstanceLocation;
}
```

**Why**:
- ParticleCount is computed on GPU during simulation
- Rather than stall CPU waiting for readback, write the draw args directly
- Render thread reads this buffer, feeds to GPU draw call

**UzEngine Adaptation**:
```c
// uz_indirect_args_gen.hlsl
RWBuffer<uint> particleCountBuffer;  // InstanceCountManager
RWBuffer<uint> indirectArgsBuffer;   // Pre-allocated

[numthreads(64, 1, 1)]
void GenerateIndirectArgs(uint3 tid : SV_DispatchThreadID) {
    uint emitter_idx = tid.x;
    uint particle_count = particleCountBuffer[emitter_idx];

    uint args_offset = emitter_idx * 5;  // 5 args per draw call

    indirectArgsBuffer[args_offset + 0] = QUAD_INDEX_COUNT;     // IndexCountPerInstance
    indirectArgsBuffer[args_offset + 1] = particle_count;       // InstanceCount
    indirectArgsBuffer[args_offset + 2] = 0;                    // StartIndexLocation
    indirectArgsBuffer[args_offset + 3] = 0;                    // BaseVertexLocation
    indirectArgsBuffer[args_offset + 4] = 0;                    // StartInstanceLocation
}
```

---

## 7. Module/Script Compilation System

### 7.1 Script Execution Stages

**Location**: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraScript.h`, lines 38-118

```cpp
UENUM() enum class ENiagaraScriptUsage : uint8 {
    EmitterSpawn,              // Once per emitter lifetime
    EmitterUpdate,             // Every frame
    ParticleSpawn,             // When particle born
    ParticleUpdate,            // Every frame per particle
    ParticleEventScript,       // On event
};

UENUM() enum class EUnusedAttributeBehaviour : uint8 {
    Copy,        // Copy previous value
    Zero,        // Set to 0
    None,        // Don't touch
    MarkInvalid, // Debug: set to 0xBAADBEEF
    PassThrough, // No double buffering
};
```

**Execution Order**:
1. **Emitter Spawn** (game thread, CPU)
2. **Emitter Update** (game thread, CPU per-frame)
3. **Particle Spawn** (GPU compute, runs when particles born)
4. **Particle Update** (GPU compute, runs every frame)
5. **Event Scripts** (GPU compute, on collision/custom events)

**Compilation**: Each script stage compiles to a separate **compute shader permutation** or **VectorVM bytecode**.

### 7.2 No Monolithic Shader

**Key Insight**: Unlike traditional particle systems, each Niagara script stage is **independent**.

- Spawn script: Runs N threads (one per spawn slot)
- Update script: Runs M threads (one per alive particle)
- No unified "simulate all particles" shader

This allows:
- Adding particle attribute (e.g., "AliveFrameCount") without recompiling everything
- Spawn script can skip expensive operations
- Cache-friendly: each stage touches only relevant attributes

**UzEngine Adaptation**: Compile scripts as separate compute shaders:
```c
// uz_particle_scripts.h
typedef enum {
    UZ_SCRIPT_SPAWN = 0,
    UZ_SCRIPT_UPDATE = 1,
    UZ_SCRIPT_EVENT = 2,
} uz_script_stage_t;

// Each stage compiles to HLSL compute shader
typedef struct {
    uz_shader_t* spawn_shader;
    uz_shader_t* update_shader;
    uz_shader_t* event_shader;
} uz_particle_script_pack_t;

// Compile single stage (not monolithic)
uz_shader_t* uz_compile_script_stage(
    uz_script_ast_t* ast,
    uz_script_stage_t stage
);
```

---

## 8. Anti-Patterns Found in Niagara (Avoid in UzEngine)

### 8.1 Template Metaprogramming Explosion

**Issue**: Data interface binding uses heavy C++ templates (lines 76-146 of NiagaraDataInterface.h):

```cpp
template<int32 ParamIdx, typename DataType, typename NextBinder>
struct TNDIParamBinder {
    template<typename... ParamTypes>
    static void Bind(UNiagaraDataInterface* Interface, ...);
};
```

**Cost**: Compile times suffer, error messages are cryptic.

**UzEngine Avoidance**: Use runtime type discrimination:
```c
// uz_param_binder.h - simpler, no templates
typedef enum {
    UZ_PARAM_FLOAT,
    UZ_PARAM_INT,
    UZ_PARAM_VECTOR3,
} uz_param_type_t;

void uz_bind_param(uz_ndi_t* ndi, uz_param_type_t type, void* value) {
    // Simple if-switch, compiles fast
    switch (type) {
        case UZ_PARAM_FLOAT: *(float*)ndi->params[0] = *(float*)value; break;
        case UZ_PARAM_INT: *(int*)ndi->params[1] = *(int*)value; break;
        // ...
    }
}
```

### 8.2 UObject Overhead for Hot-Path Data

**Issue**: Niagara particle systems inherit from `UObject`, which adds:
- Virtual destructor
- Reference counting
- Serialization support
- Reflection metadata

Not all needed at runtime.

**UzEngine Improvement**: Separate data types:
```c
// uz_emitter.h
typedef struct {
    // POD data only - no vtable, no refcount
    uz_particle_buffer_t particles;
    uint32_t alive_count;
    float spawn_rate;
} uz_emitter_data_t;

// Separate object for editor only
typedef struct {
    UObject base;  // UE object, for serialization
    uz_emitter_data_t* runtime;
} uz_emitter_asset_t;
```

### 8.3 Deferred GPU Readback Complexity

**Issue**: Niagara's GPU readback system (NiagaraGPUInstanceCountManager) is complex:

```cpp
const uint32* GetGPUReadback();
void ReleaseGPUReadback();
void EnqueueGPUReadback(FRHICommandListImmediate& RHICmdList);
bool HasPendingGPUReadback() const;
```

Readback is **asynchronous** but **not frame-latent**. Can cause frame-latency issues.

**UzEngine Simplification**:
- Use compute shader writes directly (no CPU readback needed for draw args)
- If debugging is needed, add optional synchronous readback for editor-only tools

```c
// uz_particle_debug.h
#if UZ_EDITOR
void uz_readback_particle_data_sync(
    uz_emitter_instance_t* emitter,
    uz_arena_t* temp_arena
);
#endif
```

---

## 9. Critical Implementation Notes for UzEngine

### 9.1 GPU Memory Layout

**Particles must be SOA**:
```c
// WRONG (AOS): Wastes memory, poor cache
struct Particle {
    float3 position;
    float3 velocity;
    float lifetime;
    float age;
    // ... 256 bytes per particle
};
Particle particles[MAX_PARTICLES];

// RIGHT (SOA): Coalesced access
float3 positions[MAX_PARTICLES];
float3 velocities[MAX_PARTICLES];
float lifetimes[MAX_PARTICLES];
float ages[MAX_PARTICLES];
```

### 9.2 Compute Shader Thread Mapping

**Always use 1D dispatch** (like Niagara):
```hlsl
[numthreads(64, 1, 1)]
void SimulateParticles(uint3 tid : SV_DispatchThreadID) {
    uint particleIdx = tid.x;
    if (particleIdx < particleCount) {
        // Simulate particle
    }
}
```

Reason: Threads in same wavefront should operate on consecutive particles for cache locality.

### 9.3 Frame-Local Allocation Pattern

**Game thread submits "acquire slot" requests**:
```c
typedef struct {
    uint32_t* pending_slots;
    uint32_t pending_count;
} uz_frame_allocations_t;

// Game thread
uz_frame_allocations_t frame_alloc = {};
for (emitter in active_emitters) {
    uint32_t slot = uz_particle_allocator_acquire(&emitter->allocator);
    frame_alloc.pending_slots[frame_alloc.pending_count++] = slot;
}

// Render thread processes frame_alloc after simulation
uz_instance_count_manager_update_counts(&instance_count_mgr, &frame_alloc);
```

### 9.4 Separate Indirect Args Generation Pass

**Do NOT embed in simulation shader**:
```hlsl
// BAD: Simulation shader writes draw args
[numthreads(64, 1, 1)]
void SimulateAndGenerateArgs(uint3 tid) {
    // Simulate
    uint deadCount = ...;

    // WRONG: Also write draw args here
    IndirectArgsBuffer[tid.x * 5] = deadCount;
}
```

**DO**: Separate compute pass after simulation:
```hlsl
// Stage 1: Simulate
[numthreads(64, 1, 1)]
void Simulate(uint3 tid) {
    // Just simulate, write particle data
}

// Stage 2: Generate indirect args (different shader)
[numthreads(1, 1, 1)]
void GenerateIndirectArgs() {
    uint count = InstanceCountBuffer[tid.x];
    IndirectArgsBuffer[...] = count;
}
```

Reason: Decouples concern, easier to debug, can be profiled separately.

---

## 10. File Reference Guide

**Core GPU Simulation**:
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraGPUInstanceCountManager.h` - Slot allocation, instance counts
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Private\NiagaraGpuComputeDispatch.h` - Dispatch batching, lifecycle
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Shaders\Private\NiagaraEmitterInstanceShader.usf` - GPU simulation boilerplate

**Architecture**:
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraSystem.h` - System/emitter/renderer hierarchy
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraEmitter.h` - Emitter properties, parameters, event routing
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraScript.h` - Script stages, compilation metadata

**Rendering**:
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Public\NiagaraRenderer.h` - Render proxy pattern base class
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Public\NiagaraRendererSprites.h` - Concrete sprite renderer
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Shaders\Private\NiagaraDrawIndirectArgsGen.usf` - Indirect args generation

**Data Interfaces**:
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraDataInterface.h` - NDI base class, lifecycle contexts
- `W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\Source\Niagara\Classes\NiagaraDataInterfaceCollisionQuery.h` - Example: collision NDI

---

## Summary: Key Takeaways for UzEngine VFX

| Aspect | Niagara Pattern | UzEngine Adaptation |
|--------|-----------------|-------------------|
| **Particle Storage** | SOA (separate buffers per attribute) | Native C struct arrays, one SRV per type |
| **GPU Dispatch** | Batched by shader permutation | Group emitters by script stage shader |
| **Memory Management** | Free list per instance, pre-sized | Arena + free list stack (simpler) |
| **Rendering Coupling** | Render proxy (opaque dynamic data) | Separate `_generate_data()` / `_draw()` functions |
| **Extensibility** | Data Interface plugin system | vtable-based NDI registry |
| **Type Safety** | Metadata-driven parameter binding | Descriptor tables, no type switches |
| **Indirect Rendering** | Per-emitter slot in shared count buffer | One count buffer, allocate slots per frame |

**Next Steps**:
1. Implement `uz_particle_allocator_t` (frame-local free list)
2. Build `uz_ndi_t` plugin registry with lifecycle hooks
3. Design `uz_particle_script_pack_t` (spawn/update/event shaders separate)
4. Create indirect args generation compute shader
5. Implement render proxy pattern in sprite/mesh renderers
