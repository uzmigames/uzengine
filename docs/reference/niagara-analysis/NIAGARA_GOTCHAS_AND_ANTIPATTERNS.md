# Niagara System: Gotchas, Anti-Patterns, and What NOT To Do

This document captures architectural missteps found in Niagara that UzEngine should avoid.

---

## Anti-Pattern 1: Template Metaprogramming for Parameter Binding

### What Niagara Does (File: NiagaraDataInterface.h, lines 76-146)

```cpp
// Templated binder chain
template<int32 ParamIdx, typename DataType, typename NextBinder>
struct TNDIParamBinder {
    template<typename... ParamTypes>
    static void Bind(UNiagaraDataInterface* Interface,
                     const FVMExternalFunctionBindingInfo& BindingInfo,
                     void* InstanceData,
                     FVMExternalFunction &OutFunc) {
        if (BindingInfo.InputParamLocations[ParamIdx]) {
            NextBinder::template Bind<ParamTypes...,
                VectorVM::FExternalFuncConstHandler<DataType>>(Interface, BindingInfo, InstanceData, OutFunc);
        } else {
            NextBinder::template Bind<ParamTypes...,
                VectorVM::FExternalFuncRegisterHandler<DataType>>(Interface, BindingInfo, InstanceData, OutFunc);
        }
    }
};

#define DEFINE_NDI_FUNC_BINDER(ClassName, FuncName)\
struct NDI_FUNC_BINDER(ClassName, FuncName) {\
    template<typename ... ParamTypes>\
    static void Bind(UNiagaraDataInterface* Interface, ...) {\
        auto Lambda = [Interface](FVectorVMExternalFunctionContext& Context) {\
            static_cast<ClassName*>(Interface)->FuncName<ParamTypes...>(Context);\
        };\
        OutFunc = FVMExternalFunction::CreateLambda(Lambda);\
    }\
};
```

### Problems

1. **Compile Time**: Each new NDI function explodes template instantiations
   - Example: 100 NDIs x 10 functions each = 1000s of template specializations
   - Compile times can exceed 30 seconds for incremental build

2. **Error Messages**: Error in template instantiation produces 50+ lines of template noise
   ```
   error: no matching function for call to 'Bind'
   ...called from within 'TNDIParamBinder<2, float, TNDIParamBinder<1, int, ...'
   ... (20+ more lines of template context)
   ```

3. **Type Safety**: Compiler can't catch logic errors until instantiation
   ```cpp
   // This compiles until you actually call it with wrong types
   template<typename T> void BadFunction(T x) { return x.NonExistentMethod(); }
   ```

### What UzEngine Should Do

Use **runtime type discrimination** with a simple switch:

```c
// uz_ndi_binder.h - Fast, simple, type-safe

typedef enum {
    UZ_NDI_PARAM_FLOAT,
    UZ_NDI_PARAM_INT,
    UZ_NDI_PARAM_VECTOR3,
    UZ_NDI_PARAM_VECTOR4,
} uz_ndi_param_type_t;

typedef struct {
    uz_ndi_param_type_t type;
    union {
        float f_value;
        int32_t i_value;
        float v3_value[3];
        float v4_value[4];
    } value;
} uz_ndi_param_t;

// Simple dispatch function (no templates)
void uz_ndi_bind_param(
    uz_ndi_t* ndi,
    uint32_t param_index,
    const uz_ndi_param_t* param
) {
    // Type-safe, error checking at runtime
    uz_ndi_param_t* ndi_params = (uz_ndi_param_t*)ndi->instance_data;

    if (param_index >= ndi->param_count) {
        UZ_LOG(Particles, Error, "Parameter index {} out of range", param_index);
        return;
    }

    if (ndi_params[param_index].type != param->type) {
        UZ_LOG(Particles, Error, "Parameter type mismatch at index {}", param_index);
        return;
    }

    // Copy parameter
    ndi_params[param_index] = *param;
}
```

### Why This Is Better

- **Compile time**: 5 seconds, not 30 seconds
- **Error messages**: Clear, actionable
  ```
  Error: Parameter type mismatch at index 2: expected VECTOR3, got FLOAT
  ```
- **Maintenance**: Adding new param type = one line to enum, no template explosion
- **Debugging**: Stack traces actually point to real code, not template instantiations

---

## Anti-Pattern 2: Per-Particle Struct (AOS) Instead of SOA

### What Happens (Wrong Way)

```c
// ANTI-PATTERN: Per-particle struct
typedef struct {
    float3 position;      // 12 bytes
    float3 velocity;      // 12 bytes
    float lifetime;       // 4 bytes
    float age;           // 4 bytes
    uint32_t color;      // 4 bytes
    float size;          // 4 bytes
    // ... more fields
    // Total: 100+ bytes per particle
} uz_particle_t;

uz_particle_t particles[1000000];

// GPU simulation shader:
[numthreads(64, 1, 1)]
void Simulate(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    uz_particle_t p = particles[idx];  // Fetch 100 bytes
    p.position += p.velocity * deltaTime;
    p.age += deltaTime;
    particles[idx] = p;
}
```

### Why This Is Catastrophic

**GPU Memory Access Pattern**:
```
Thread 0: reads particles[0]   (offset 0)
Thread 1: reads particles[1]   (offset 100)
Thread 2: reads particles[2]   (offset 200)
...
Thread 31: reads particles[31] (offset 3100)
```

**Cache line (128 bytes)**:
```
Cache Line 0 (bytes 0-127):
  Thread 0: position, velocity, lifetime, age
  + part of Thread 1's data (wasted)

Cache Line 1 (bytes 128-255):
  Thread 1: rest of data
  + part of Thread 2's data
  ...
```

**Result**:
- Cache hit rate: ~50% (only reading 1 attribute, other 99 bytes wasted)
- Memory bandwidth: wasted on unused data
- Performance: 2-4x slower than necessary

### What Niagara Does Right (SOA)

```c
// CORRECT: Separate buffers per attribute
typedef struct {
    float3* positions;     // One SRV: 1M x float3
    float3* velocities;    // One SRV: 1M x float3
    float* lifetimes;      // One SRV: 1M x float
    float* ages;           // One SRV: 1M x float
    uint32_t* colors;      // One SRV: 1M x uint32
    float* sizes;          // One SRV: 1M x float
} uz_particle_buffer_pack_t;

// GPU simulation shader:
[numthreads(64, 1, 1)]
void Simulate(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    float3 pos = PositionBuffer[idx];
    float3 vel = VelocityBuffer[idx];
    float dt = deltaTime;

    pos += vel * dt;
    PositionBuffer[idx] = pos;
    AgeBuffer[idx] += dt;
}
```

**GPU Memory Access Pattern**:
```
Threads 0-63: read positions[0-63]
              positions buffer: consecutive float3 = 256 bytes = 2 cache lines
              Perfect coalescing!

Next iteration:
Threads 0-63: read velocities[0-63]
              velocities buffer: consecutive float3 = 256 bytes = 2 cache lines
              Perfect coalescing!
```

**Result**:
- Cache hit rate: 99%+ (every byte fetched is used)
- Memory bandwidth: 100% efficient
- Performance: Optimal for SIMD

### UzEngine Implementation

```c
// uz_particle_buffer.h

typedef struct {
    // Each attribute is a separate GPU buffer
    uz_gpu_buffer_t position_buffer;
    uz_gpu_buffer_t velocity_buffer;
    uz_gpu_buffer_t lifetime_buffer;
    uz_gpu_buffer_t age_buffer;
    uz_gpu_buffer_t color_buffer;
    uz_gpu_buffer_t size_buffer;

    // Metadata
    uint32_t particle_count;
    uint32_t max_particles;

    // SRVs for binding to shaders
    uz_gpu_srv_t position_srv;
    uz_gpu_srv_t velocity_srv;
    // ... etc
} uz_particle_buffer_pack_t;

// NEVER use this pattern:
// typedef struct { float3 pos; float3 vel; float life; ... } uz_particle_t;
// uz_particle_t particles[MAX];  // WRONG!
```

---

## Anti-Pattern 3: Embedding Indirect Args Generation in Simulation

### What NOT To Do

```hlsl
// ANTI-PATTERN: Simulation shader writes both simulation AND draw args
[numthreads(64, 1, 1)]
void SimulateAndGenerateArgs(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;

    // Simulate particle
    Particle p = LoadParticle(idx);
    p.age += deltaTime;
    p.position += p.velocity * deltaTime;

    if (p.age > p.lifetime) {
        // Mark dead
        uint deadIdx;
        InterlockedAdd(DeadParticleCount[0], 1, deadIdx);
        DeadParticleList[deadIdx] = idx;
    }

    StoreParticle(idx, p);

    // WRONG: Also generate draw args here
    if (idx == 0) {
        uint aliveCount = InitialCount - DeadParticleCount[0];
        IndirectArgsBuffer[0] = 6;              // IndexCountPerInstance
        IndirectArgsBuffer[1] = aliveCount;    // InstanceCount
        IndirectArgsBuffer[2] = 0;
        IndirectArgsBuffer[3] = 0;
        IndirectArgsBuffer[4] = 0;
    }
}
```

### Problems

1. **Race Condition**: Thread 0 might read `DeadParticleCount[0]` before other threads finish writing
   - Indirect args are stale, draw wrong count
   - Frame N-1 results, not frame N

2. **Profiling Nightmare**: Can't profile draw arg generation separately
   - Is it slow because of simulation or args generation?
   - Can't optimize one without hitting the other

3. **Debugging**: Hard to verify args are correct (mixed with simulation data)

4. **Scalability**: What if draw args generation becomes expensive (future feature)?
   - Now locked to simulation frequency
   - Can't decouple

### What Niagara Does Right

**Separate compute passes**:

```hlsl
// Pass 1: Simulation only
[numthreads(64, 1, 1)]
void Simulate(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    Particle p = LoadParticle(idx);
    p.age += deltaTime;
    if (p.age > p.lifetime) {
        uint deadIdx;
        InterlockedAdd(DeadParticleCount[0], 1, deadIdx);
        DeadParticleList[deadIdx] = idx;
    }
    StoreParticle(idx, p);
}

// Pass 2: Indirect args generation (separate pass, guaranteed after simulation)
[numthreads(1, 1, 1)]
void GenerateIndirectArgs(uint3 tid : SV_DispatchThreadID) {
    // Now DeadParticleCount[0] is guaranteed complete
    uint aliveCount = InitialCount - DeadParticleCount[0];

    IndirectArgsBuffer[0] = 6;
    IndirectArgsBuffer[1] = aliveCount;
    IndirectArgsBuffer[2] = 0;
    IndirectArgsBuffer[3] = 0;
    IndirectArgsBuffer[4] = 0;
}
```

### UzEngine Implementation

```c
// uz_particle_system.h

void uz_particle_system_simulate(uz_particle_system_t* sys, float dt, uz_command_list_t* cmd) {
    // Pass 1: Simulate particles
    uz_rhi_bind_compute_shader(cmd, sys->particle_update_shader);
    for (uint32_t i = 0; i < sys->emitter_count; ++i) {
        uz_rhi_dispatch_indirect(cmd, sys->count_mgr->count_buffer, i * 4);
    }

    // GPU PIPELINE BARRIER: Wait for simulation to complete
    uz_rhi_resource_barrier(cmd, sys->count_mgr->count_buffer, RHI_ACCESS_WRITE, RHI_ACCESS_READ);

    // Pass 2: Generate indirect args (separate pass, guaranteed after simulation)
    uz_rhi_bind_compute_shader(cmd, sys->indirect_args_gen_shader);
    uz_rhi_dispatch(cmd, sys->emitter_count, 1, 1);

    // GPU PIPELINE BARRIER: Wait for args generation
    uz_rhi_resource_barrier(cmd, sys->indirect_args_buffer, RHI_ACCESS_WRITE, RHI_ACCESS_INDIRECT_ARGUMENT);
}
```

### Benefits

- **Correctness**: No race conditions (explicit GPU barrier)
- **Profiling**: Can GPU-profile each pass independently
- **Debugging**: Can capture draw args buffer, inspect in debugger
- **Future-proof**: Can add new passes (dead particle compaction, LOD generation, etc.) without touching simulation

---

## Anti-Pattern 4: Exposing Game-Thread State to Render Thread

### What NOT To Do

```cpp
// ANTI-PATTERN: Renderer holds pointer to FNiagaraEmitterInstance
class FBadRenderer : public FNiagaraRenderer {
    void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, ...) const override {
        // Render thread code
        if (EmitterInstance->bLocalSpace) {  // WRONG! EmitterInstance may be deleted on game thread
            // ...
        }
    }

private:
    FNiagaraEmitterInstance* EmitterInstance;  // DANGEROUS
};
```

### Why This Fails

**Timeline**:
```
Frame N:
  Game Thread:        Render Thread:
  - Tick emitter
  - Call Render() -----> - Bind resources
                          - Read EmitterInstance->bLocalSpace

  - Delete emitter    <---- (race condition!)
  - Deallocate memory
```

**Symptoms**:
- Occasional crashes (depends on timing)
- Corrupted data (game thread modifies while render thread reads)
- Memory use-after-free
- Hard to reproduce in debugger

### What Niagara Does Right (Render Proxy Pattern)

```cpp
// Game thread (safe to access EmitterInstance)
FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraEmitterInstance* Emitter) const {
    FNiagaraDynamicDataSprites* DynData = new FNiagaraDynamicDataSprites(Emitter);
    DynData->bLocalSpace = Emitter->bLocalSpace;  // Copy to opaque blob
    DynData->ParticleData = Emitter->GetParticleDataBuffer();
    return DynData;  // Return COPY, not pointer
}

// Render thread (only receives opaque data)
void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                             const FSceneViewFamily& ViewFamily,
                             uint32 VisibilityMap,
                             FMeshElementCollector& Collector,
                             const FNiagaraSceneProxy *SceneProxy) const override {
    FNiagaraDynamicDataBase *DynData = GetDynamicData();  // Safe: opaque blob
    // Don't know about EmitterInstance, can't access it
    // Can't accidentally read game-thread data
}
```

### UzEngine Implementation

```c
// uz_particle_renderer.h

// Opaque dynamic data (game thread creates, render thread consumes)
typedef struct {
    uint32_t _padding;  // Intentionally opaque
    // Render thread MUST NOT know about uz_emitter_instance_t
} uz_particle_dynamic_data_t;

// Game thread only
uz_particle_dynamic_data_t* uz_sprite_renderer_generate_data(
    const uz_emitter_instance_t* emitter,
    const uz_renderer_properties_t* props
) {
    // Safe to access emitter (game thread)
    uz_particle_dynamic_data_t_impl_t* impl = uz_malloc(sizeof(*impl));

    // Copy relevant data into opaque structure
    impl->particle_count = emitter->alive_count;
    impl->particle_buffer_srv = emitter->particle_data.position_buffer.srv;
    impl->material = props->material;

    return (uz_particle_dynamic_data_t*)impl;
}

// Render thread only
void uz_sprite_renderer_draw(
    uz_particle_dynamic_data_t* dyn_data,  // Opaque!
    uz_view_t* view,
    uz_command_list_t* cmd
) {
    // Render thread code
    // CANNOT access uz_emitter_instance_t
    // CANNOT dereference emitter pointer (even if we had one)
    // Can only use opaque data blob

    uz_particle_dynamic_data_t_impl_t* impl =
        (uz_particle_dynamic_data_t_impl_t*)dyn_data;

    uz_rhi_set_shader_resource(cmd, "ParticleData", impl->particle_buffer_srv);
    uz_rhi_draw_indexed_indirect(cmd, impl->particle_count);
}

// Cleanup (called whenever, doesn't matter)
void uz_particle_dynamic_data_destroy(uz_particle_dynamic_data_t* data) {
    uz_free((uz_particle_dynamic_data_t_impl_t*)data);
}
```

**Rules**:
1. Render thread function signatures NEVER mention `uz_emitter_instance_t`
2. Dynamic data is always opaque (`void*` cast to local struct)
3. Game thread owns the copy, render thread consumes and releases
4. No back-pointers from dynamic data to game-thread objects

---

## Anti-Pattern 5: GPU Readback Without Frame Latency Handling

### What Niagara Does (Complex)

```cpp
// NiagaraGPUInstanceCountManager.h, lines 95-98
const uint32* GetGPUReadback();
void ReleaseGPUReadback();
void EnqueueGPUReadback(FRHICommandListImmediate& RHICmdList);
bool HasPendingGPUReadback() const;
```

**State Machine**:
```
Frame N:        Frame N+1:        Frame N+2:
EnqueueReadback ----> HasPending == true ----> GetGPUReadback()
                      (wait)
```

**Problems**:
1. Async, but not truly frame-latent (can cause stalls if not careful)
2. Complex state machine easy to get wrong
3. Debugging readback issues is painful

### What UzEngine Should Do

**Option A: Don't readback (preferred)**
```c
// If only using readback for debugging:
#ifdef UZ_EDITOR
void uz_particle_system_debug_readback(...) {
    // Sync readback for editor tools only
    // Not in hot path
}
#endif
```

**Option B: Simple ping-pong buffer**
```c
// uz_particle_readback.h

typedef struct {
    uz_gpu_buffer_t gpu_buffer;
    uint8_t* cpu_buffer[2];  // Double-buffered
    uint32_t read_frame_offset;    // Which frame we're reading
    uint32_t write_frame_offset;   // Which frame GPU writes to
} uz_particle_readback_t;

void uz_particle_readback_enqueue(uz_particle_readback_t* readback, uz_command_list_t* cmd) {
    // Copy GPU -> CPU (opposite of read_frame_offset)
    uint32_t write_offset = 1 - readback->read_frame_offset;
    uz_rhi_copy_buffer(cmd, readback->gpu_buffer, readback->cpu_buffer[write_offset]);

    // Next call to GetReadback() will swap
}

const void* uz_particle_readback_get(uz_particle_readback_t* readback) {
    // Swap to newly copied buffer
    readback->read_frame_offset = 1 - readback->read_frame_offset;
    return readback->cpu_buffer[readback->read_frame_offset];
}
```

**Benefits**:
- Simple state machine (2 states, not 10)
- Guaranteed 1-frame latency (predictable)
- No stalls (CPU and GPU work in parallel)

---

## Anti-Pattern 6: Tight Coupling Between Simulation and Rendering

### What NOT To Do

```c
// ANTI-PATTERN: Simulation code calls rendering functions
void uz_particle_simulate(uz_emitter_instance_t* emitter, float dt) {
    // Simulation
    for (uint32_t i = 0; i < emitter->alive_count; ++i) {
        // ... simulate particle
    }

    // WRONG: Render thread call in simulation code
    uz_sprite_renderer_generate_data(emitter, emitter->renderer_props);
    uz_render_queue_add_sprite_particles(...);
}
```

### Why This Breaks

1. **Simulation becomes render-dependent**: Can't test simulation without rendering
2. **Render thread ordering**: What if renderer runs on different thread? Deadlock.
3. **Perf profiling**: Can't tell if slow due to simulation or rendering

### What Niagara Does Right

**Separate concerns**:
```cpp
// Simulation (GPU compute dispatch)
void ExecuteTicks(FRDGBuilder& GraphBuilder) {
    // Pure simulation
}

// Rendering (separate from simulation)
void PostRenderOpaque(FRDGBuilder& GraphBuilder, TConstStridedView<FSceneView> Views) {
    // Pure rendering, reads simulation results
}
```

### UzEngine Implementation

```c
// uz_particle_system.h

// Stage 1: Simulate (GPU only)
void uz_particle_system_simulate(
    uz_particle_system_t* sys,
    float dt,
    uz_command_list_t* cmd
) {
    // No rendering code here!
    // Pure simulation
}

// Stage 2: Generate rendering data (CPU side, after simulation)
void uz_particle_system_prepare_render(
    uz_particle_system_t* sys,
    uz_render_queue_t* render_queue
) {
    // Call renderer to generate dynamic data
    for (uint32_t i = 0; i < sys->emitter_count; ++i) {
        uz_particle_dynamic_data_t* dyn_data =
            uz_sprite_renderer_generate_data(sys->emitters[i], sys->emitters[i]->renderer_props);
        uz_render_queue_add_batch(render_queue, dyn_data);
    }
}
```

**Benefits**:
- Simulation testable independently
- Rendering testable independently
- Can profile each part separately
- Easier to add new renderer types

---

## Summary: What To Avoid

| Anti-Pattern | Why Bad | UzEngine Solution |
|--------------|---------|------------------|
| **Templates for params** | Compile time explosion | Runtime type switch |
| **AOS particle storage** | Poor cache, wasted bandwidth | SOA separate buffers |
| **Indirect args in sim** | Race conditions, profiling hard | Separate compute pass |
| **Game state to render thread** | Race conditions, crashes | Render proxy pattern |
| **Complex GPU readback** | State machine error-prone | Ping-pong or no readback |
| **Simulation calls rendering** | Tight coupling, hard to test | Separate pipeline stages |
| **UObject on hot path** | Refcount overhead, vtable | POD data structures |
| **Type switching dispatch** | Maintainability, performance | Plugin vtable system |

---

## Checklist Before Code Review

- [ ] No AOS particle structs (only SOA)
- [ ] Simulation shader doesn't write draw args (separate pass)
- [ ] Render thread functions never mention `uz_emitter_instance_t`
- [ ] No templates in parameter binding (runtime switch only)
- [ ] NDI dispatch uses vtable (no type switch)
- [ ] Free list correctly tracks allocations (no leaks)
- [ ] GPU barriers between dispatch passes
- [ ] Simulation isolated from rendering (separate functions)
- [ ] Test simulation without renderer (unit test)
- [ ] Profile each pass independently (GPU counters)
