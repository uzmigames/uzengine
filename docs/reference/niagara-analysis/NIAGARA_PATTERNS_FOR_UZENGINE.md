# Niagara Architectural Patterns: Direct Application to UzEngine

This document maps each Niagara pattern to proposed UzEngine implementations.

---

## Pattern 1: SOA Particle Storage

### Niagara Implementation
- Multiple `FRWBuffer` instances (one per attribute)
- Each attribute gets separate GPU buffer
- Vertex factory binds SRVs to shader resources

### UzEngine Adaptation

```c
// uz_particle_attributes.h
typedef enum {
    UZ_ATTR_POSITION = 0,
    UZ_ATTR_VELOCITY = 1,
    UZ_ATTR_ACCELERATION = 2,
    UZ_ATTR_COLOR = 3,
    UZ_ATTR_LIFE = 4,
    UZ_ATTR_ROTATION = 5,
    UZ_ATTR_SIZE = 6,
    // ... user-defined attributes
    UZ_ATTR_COUNT_MAX = 32,
} uz_particle_attr_t;

typedef struct {
    uz_gpu_buffer_t buffers[UZ_ATTR_COUNT_MAX];
    uint32_t attr_count;
    uint32_t max_particles;
    uint32_t stride[UZ_ATTR_COUNT_MAX];  // Bytes per particle per attribute
} uz_particle_buffer_pack_t;

// Usage in simulation shader:
// pos_srv = buffers[UZ_ATTR_POSITION].srv
// vel_srv = buffers[UZ_ATTR_VELOCITY].srv
// Can fetch independently without AOS overhead
```

**Files to Create**:
- `src/engine/particles/particle_attributes.h` - Attribute registry
- `src/engine/particles/particle_buffer.h` - SOA buffer management
- `src/engine/particles/particle_buffer.cpp` - Allocation/release

---

## Pattern 2: Free List Allocation

### Niagara Implementation
```cpp
TArray<uint32> FreeEntries;  // Stack of available slots
uint32 AcquireEntry();       // Pop from stack
void FreeEntry(uint32& offset);  // Push to stack
```

### UzEngine Adaptation

```c
// uz_particle_allocator.h
typedef struct {
    uint32_t* free_list;      // Dynamically sized stack
    uint32_t free_top;        // Current stack pointer
    uint32_t max_particles;   // Capacity
    uint32_t used_count;      // High water mark
} uz_particle_allocator_t;

uz_particle_allocator_t* uz_particle_allocator_create(uint32_t max_particles) {
    uz_particle_allocator_t* alloc = uz_malloc(sizeof(*alloc));
    alloc->free_list = uz_malloc(max_particles * sizeof(uint32_t));
    alloc->free_top = max_particles;
    alloc->max_particles = max_particles;
    alloc->used_count = 0;

    // Initialize free list: [max-1, max-2, ..., 0]
    for (uint32_t i = 0; i < max_particles; ++i) {
        alloc->free_list[i] = max_particles - 1 - i;
    }
    return alloc;
}

// O(1) acquire
uint32_t uz_particle_allocator_acquire(uz_particle_allocator_t* alloc) {
    if (alloc->free_top == 0) {
        UZ_LOG(Particles, Warning, "Particle allocator full: {}", alloc->max_particles);
        return UINT32_MAX;  // Allocation failed
    }
    uint32_t slot = alloc->free_list[--alloc->free_top];
    alloc->used_count = UZ_MAX(alloc->used_count, alloc->max_particles - alloc->free_top);
    return slot;
}

// O(1) free
void uz_particle_allocator_free(uz_particle_allocator_t* alloc, uint32_t slot) {
    if (alloc->free_top >= alloc->max_particles) {
        UZ_LOG(Particles, Error, "Particle allocator double-free: slot={}", slot);
        return;
    }
    alloc->free_list[alloc->free_top++] = slot;
}

void uz_particle_allocator_destroy(uz_particle_allocator_t* alloc) {
    if (alloc) {
        uz_free(alloc->free_list);
        uz_free(alloc);
    }
}

// Usage:
typedef struct {
    uz_particle_buffer_pack_t particle_data;
    uz_particle_allocator_t allocator;
    uint32_t alive_count;
} uz_emitter_instance_t;

// Game thread:
void uz_emitter_spawn(uz_emitter_instance_t* emitter, uint32_t spawn_count) {
    for (uint32_t i = 0; i < spawn_count; ++i) {
        uint32_t slot = uz_particle_allocator_acquire(&emitter->allocator);
        if (slot != UINT32_MAX) {
            // Initialize particle at slot
            emitter->alive_count++;
        }
    }
}
```

**Files to Create**:
- `src/engine/particles/particle_allocator.h`
- `src/engine/particles/particle_allocator.cpp`

---

## Pattern 3: Instance Count Manager (Per-Instance Slot Allocation)

### Niagara Implementation
```cpp
FRWBuffer CountBuffer;               // Global count buffer
TArray<uint32> FreeEntries;          // Free slots in CountBuffer
uint32 AcquireOrAllocateEntry();     // Allocate slot for emitter instance
```

### UzEngine Adaptation

```c
// uz_instance_count_manager.h
typedef struct {
    uz_gpu_buffer_t count_buffer;    // uint32 x N offsets
    uint32_t* cpu_mirror;            // CPU copy for readback (if needed)
    uint32_t* free_slots;            // Stack of available offsets
    uint32_t free_count;
    uint32_t used_count;
    uint32_t capacity;
} uz_instance_count_manager_t;

uz_instance_count_manager_t* uz_instance_count_manager_create(
    uint32_t max_instances
) {
    uz_instance_count_manager_t* mgr = uz_malloc(sizeof(*mgr));
    mgr->capacity = max_instances;
    mgr->used_count = 0;
    mgr->free_count = max_instances;

    // Allocate GPU buffer
    mgr->count_buffer = uz_rhi_create_buffer(
        RHI_BUFFER_STRUCTURED,
        sizeof(uint32_t),
        max_instances,
        NULL
    );

    // Allocate CPU mirror (for diagnostics)
    mgr->cpu_mirror = uz_malloc(max_instances * sizeof(uint32_t));

    // Initialize free list
    mgr->free_slots = uz_malloc(max_instances * sizeof(uint32_t));
    for (uint32_t i = 0; i < max_instances; ++i) {
        mgr->free_slots[i] = max_instances - 1 - i;
    }

    return mgr;
}

// Called per-emitter-instance during setup
uint32_t uz_instance_count_manager_acquire_slot(
    uz_instance_count_manager_t* mgr
) {
    if (mgr->free_count == 0) {
        UZ_LOG(Particles, Error, "Instance count manager full");
        return UINT32_MAX;
    }
    uint32_t offset = mgr->free_slots[--mgr->free_count];
    mgr->used_count++;
    return offset;
}

void uz_instance_count_manager_free_slot(
    uz_instance_count_manager_t* mgr,
    uint32_t offset
) {
    if (mgr->free_count >= mgr->capacity) {
        UZ_LOG(Particles, Error, "Instance count manager double-free");
        return;
    }
    mgr->free_slots[mgr->free_count++] = offset;
    mgr->used_count--;
}

// Called once per frame to reset count buffer
void uz_instance_count_manager_clear_counts(
    uz_instance_count_manager_t* mgr,
    uz_command_list_t* cmd
) {
    uz_rhi_clear_buffer_uint(cmd, mgr->count_buffer, 0, mgr->used_count, 0);
}
```

**Files to Create**:
- `src/engine/particles/instance_count_manager.h`
- `src/engine/particles/instance_count_manager.cpp`

---

## Pattern 4: Render Proxy (CPU/GPU Decoupling)

### Niagara Implementation
```cpp
// Game thread
FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraEmitterInstance* Emitter);

// Render thread
void GetDynamicMeshElements(const FNiagaraDynamicDataBase* DynamicData);
```

### UzEngine Adaptation

```c
// uz_particle_renderer_types.h

// Opaque dynamic data (game thread creates)
typedef struct {
    uint32_t emitter_id;                 // For debugging only
    uz_gpu_buffer_t particle_position_srv;
    uz_gpu_buffer_t particle_color_srv;
    uz_gpu_buffer_t particle_size_srv;
    uint32_t instance_count_offset;      // Offset into instance count buffer
    uz_material_t* material;
    uint32_t particle_count;             // Cached from game thread
    uz_matrix4_t transform;
} uz_particle_dynamic_data_t;

// Game thread: prepare opaque data (no render thread state exposure)
uz_particle_dynamic_data_t* uz_sprite_renderer_generate_dynamic_data(
    const uz_emitter_instance_t* emitter,
    const uz_renderer_properties_t* props
) {
    uz_particle_dynamic_data_t* data = uz_malloc(sizeof(*data));
    data->emitter_id = emitter->id;
    data->particle_position_srv = emitter->particle_data.buffers[UZ_ATTR_POSITION].srv;
    data->particle_color_srv = emitter->particle_data.buffers[UZ_ATTR_COLOR].srv;
    data->particle_size_srv = emitter->particle_data.buffers[UZ_ATTR_SIZE].srv;
    data->instance_count_offset = emitter->count_buffer_offset;
    data->material = props->material;
    data->particle_count = emitter->alive_count;
    data->transform = emitter->transform;
    return data;
}

// Render thread: consume opaque data only (no emitter_instance_t access!)
void uz_sprite_renderer_draw(
    uz_sprite_renderer_t* renderer,
    const uz_particle_dynamic_data_t* dyn_data,
    const uz_view_t* view,
    uz_render_list_t* render_list
) {
    // Setup mesh batch
    uz_mesh_batch_t batch = {};
    batch.vertex_factory = renderer->vertex_factory;
    batch.material = dyn_data->material;
    batch.primitive_data_buffer_offset = dyn_data->instance_count_offset;

    // Bind particle data SRVs
    uz_rhi_set_shader_resource(renderer->sprite_shader, "ParticlePositions",
                                dyn_data->particle_position_srv);
    uz_rhi_set_shader_resource(renderer->sprite_shader, "ParticleColors",
                                dyn_data->particle_color_srv);
    uz_rhi_set_shader_resource(renderer->sprite_shader, "ParticleSizes",
                                dyn_data->particle_size_srv);

    // Draw with indirect count
    batch.draw_call = (uz_draw_call_t) {
        .index_buffer = renderer->quad_index_buffer,
        .indices_per_instance = 6,
        .indirect_args_buffer = renderer->instance_count_mgr->count_buffer,
        .indirect_args_offset = dyn_data->instance_count_offset,
    };

    uz_render_list_add_batch(render_list, &batch);
}

// Cleanup
void uz_particle_dynamic_data_destroy(uz_particle_dynamic_data_t* data) {
    uz_free(data);
}
```

**Files to Create**:
- `src/engine/particles/renderer_proxy.h`
- `src/engine/particles/sprite_renderer.h`
- `src/engine/particles/sprite_renderer.cpp`

---

## Pattern 5: Data Interface Plugin System (NDI)

### Niagara Implementation
```cpp
class UNiagaraDataInterface {
    virtual void PreStageInterface(FRDGBuilder& GraphBuilder, ...);
    virtual void PostStageInterface(FRDGBuilder& GraphBuilder, ...);
    virtual void PostSimulateInterface(FRDGBuilder& GraphBuilder, ...);
};
```

### UzEngine Adaptation

```c
// uz_data_interface.h

typedef enum {
    UZ_NDI_LIFECYCLE_RESET,
    UZ_NDI_LIFECYCLE_PRE_STAGE,
    UZ_NDI_LIFECYCLE_POST_STAGE,
    UZ_NDI_LIFECYCLE_POST_SIMULATE,
} uz_ndi_lifecycle_t;

typedef struct uz_ndi_context_t {
    uz_render_graph_t* graph;
    uz_emitter_instance_t* emitter;
    uz_command_list_t* cmd;
} uz_ndi_context_t;

typedef struct uz_ndi_t uz_ndi_t;

typedef struct {
    // Lifecycle callbacks
    bool (*reset)(uz_ndi_t* self, uz_ndi_context_t* ctx);
    bool (*pre_stage)(uz_ndi_t* self, uz_ndi_context_t* ctx);
    bool (*post_stage)(uz_ndi_t* self, uz_ndi_context_t* ctx);
    bool (*post_simulate)(uz_ndi_t* self, uz_ndi_context_t* ctx);

    // GPU shader integration
    void (*build_shader_parameters)(uz_ndi_t* self, uz_shader_builder_t* builder);
    void (*set_shader_parameters)(uz_ndi_t* self, uz_shader_context_t* ctx);

    // Cleanup
    void (*destroy)(uz_ndi_t* self);
} uz_ndi_vtable_t;

typedef struct uz_ndi_t {
    uz_ndi_vtable_t* vtable;
    void* instance_data;  // Per-emitter-instance private data
    const char* name;
} uz_ndi_t;

// Collision Query NDI (Example Implementation)

typedef struct {
    uz_collision_batch_t batch;
    uint32_t query_count;
} uz_ndi_collision_instance_data_t;

bool uz_ndi_collision_reset(uz_ndi_t* self, uz_ndi_context_t* ctx) {
    uz_ndi_collision_instance_data_t* data = (uz_ndi_collision_instance_data_t*)self->instance_data;
    data->query_count = 0;
    return true;
}

bool uz_ndi_collision_pre_stage(uz_ndi_t* self, uz_ndi_context_t* ctx) {
    uz_ndi_collision_instance_data_t* data = (uz_ndi_collision_instance_data_t*)self->instance_data;
    // Pre-stage: gather collision data from scene
    uz_collision_batch_update(&data->batch, ctx->cmd);
    return true;
}

bool uz_ndi_collision_post_stage(uz_ndi_t* self, uz_ndi_context_t* ctx) {
    // No-op for collision
    return true;
}

bool uz_ndi_collision_post_simulate(uz_ndi_t* self, uz_ndi_context_t* ctx) {
    // Post-sim: read back collision results if needed
    return true;
}

void uz_ndi_collision_build_shader_params(uz_ndi_t* self, uz_shader_builder_t* builder) {
    uz_shader_builder_add_resource(builder, "CollisionBuffer", UZ_SHADER_RESOURCE_BUFFER);
    uz_shader_builder_add_resource(builder, "CollisionCount", UZ_SHADER_RESOURCE_CBUFFER);
}

void uz_ndi_collision_set_shader_params(uz_ndi_t* self, uz_shader_context_t* ctx) {
    uz_ndi_collision_instance_data_t* data = (uz_ndi_collision_instance_data_t*)self->instance_data;
    uz_shader_set_resource(ctx->shader, "CollisionBuffer", data->batch.buffer);
    uz_shader_set_cbuffer(ctx->shader, "CollisionCount", &data->query_count, sizeof(uint32_t));
}

void uz_ndi_collision_destroy(uz_ndi_t* self) {
    uz_ndi_collision_instance_data_t* data = (uz_ndi_collision_instance_data_t*)self->instance_data;
    uz_collision_batch_destroy(&data->batch);
    uz_free(data);
    uz_free(self);
}

uz_ndi_t* uz_ndi_collision_create() {
    uz_ndi_t* ndi = uz_malloc(sizeof(*ndi));
    ndi->instance_data = uz_malloc(sizeof(uz_ndi_collision_instance_data_t));
    ndi->name = "CollisionQuery";

    static uz_ndi_vtable_t vtable = {
        .reset = uz_ndi_collision_reset,
        .pre_stage = uz_ndi_collision_pre_stage,
        .post_stage = uz_ndi_collision_post_stage,
        .post_simulate = uz_ndi_collision_post_simulate,
        .build_shader_parameters = uz_ndi_collision_build_shader_params,
        .set_shader_parameters = uz_ndi_collision_set_shader_params,
        .destroy = uz_ndi_collision_destroy,
    };
    ndi->vtable = &vtable;
    return ndi;
}

// NDI Registry

typedef struct {
    uz_ndi_t** ndis;
    uint32_t count;
} uz_ndi_registry_t;

uz_ndi_registry_t* uz_ndi_registry_create() {
    uz_ndi_registry_t* registry = uz_malloc(sizeof(*registry));
    registry->count = 0;
    registry->ndis = uz_malloc(16 * sizeof(uz_ndi_t*));
    return registry;
}

void uz_ndi_registry_register(uz_ndi_registry_t* registry, uz_ndi_t* ndi) {
    if (registry->count >= 16) {
        UZ_LOG(Particles, Error, "NDI registry full");
        return;
    }
    registry->ndis[registry->count++] = ndi;
    UZ_LOG(Particles, Info, "Registered NDI: {}", ndi->name);
}

// Core simulation dispatch (NO TYPE SWITCHING!)
void uz_particle_simulate(
    uz_ndi_registry_t* ndi_registry,
    uz_emitter_instance_t* emitter,
    float dt,
    uz_command_list_t* cmd
) {
    uz_ndi_context_t ctx = { .emitter = emitter, .cmd = cmd };

    // Pre-stage: all NDIs prepare data
    for (uint32_t i = 0; i < ndi_registry->count; ++i) {
        ndi_registry->ndis[i]->vtable->pre_stage(ndi_registry->ndis[i], &ctx);
    }

    // Run simulation compute shader
    uz_rhi_bind_compute_shader(cmd, emitter->simulate_shader);
    uz_rhi_dispatch_indirect(cmd, emitter->particle_data.buffers[0], 0);

    // Post-stage: all NDIs finalize
    for (uint32_t i = 0; i < ndi_registry->count; ++i) {
        ndi_registry->ndis[i]->vtable->post_stage(ndi_registry->ndis[i], &ctx);
    }
}
```

**Files to Create**:
- `src/engine/particles/data_interface.h`
- `src/engine/particles/data_interface_collision.cpp`
- `src/engine/particles/data_interface_registry.h`

---

## Pattern 6: Indirect Draw Arguments Generation

### Niagara Implementation
- Simulation computes `InstanceCountBuffer[offset]`
- Separate compute pass reads count, writes `D3D12_DRAW_INDEXED_ARGUMENTS`

### UzEngine Adaptation

```hlsl
// uz_particle_indirect_args_gen.hlsl

RWStructuredBuffer<uint> InstanceCountBuffer : register(u0);  // From simulation
RWStructuredBuffer<uint> IndirectArgsBuffer : register(u1);   // Output

[numthreads(1, 1, 1)]
void GenerateIndirectArgs(uint3 tid : SV_DispatchThreadID) {
    uint emitter_idx = tid.x;

    // Read particle count written by simulation
    uint particle_count = InstanceCountBuffer[emitter_idx];

    // Compute indirect args offset
    uint args_offset = emitter_idx * 5;  // 5 args per draw call

    // Write D3D12_DRAW_INDEXED_ARGUMENTS structure:
    // IndexCountPerInstance, InstanceCount, StartIndexLocation,
    // BaseVertexLocation, StartInstanceLocation
    IndirectArgsBuffer[args_offset + 0] = QUAD_INDEX_COUNT;      // 6 for quad
    IndirectArgsBuffer[args_offset + 1] = particle_count;        // Dynamic count
    IndirectArgsBuffer[args_offset + 2] = 0;                     // StartIndexLocation
    IndirectArgsBuffer[args_offset + 3] = 0;                     // BaseVertexLocation
    IndirectArgsBuffer[args_offset + 4] = 0;                     // StartInstanceLocation
}
```

```c
// uz_particle_indirect_gen.h

typedef struct {
    uz_gpu_buffer_t instance_count_buffer;
    uz_gpu_buffer_t indirect_args_buffer;
    uz_shader_t* gen_shader;
} uz_indirect_args_generator_t;

uz_indirect_args_generator_t* uz_indirect_args_generator_create(
    uint32_t max_emitters
) {
    uz_indirect_args_generator_t* gen = uz_malloc(sizeof(*gen));

    gen->instance_count_buffer = uz_rhi_create_buffer(
        RHI_BUFFER_STRUCTURED,
        sizeof(uint32_t),
        max_emitters,
        NULL
    );

    gen->indirect_args_buffer = uz_rhi_create_buffer(
        RHI_BUFFER_STRUCTURED | RHI_BUFFER_INDIRECT,
        sizeof(uint32_t),
        max_emitters * 5,  // 5 args per draw call
        NULL
    );

    gen->gen_shader = uz_shader_compile_from_file("uz_particle_indirect_args_gen.hlsl");
    return gen;
}

void uz_indirect_args_generator_update(
    uz_indirect_args_generator_t* gen,
    uint32_t emitter_count,
    uz_command_list_t* cmd
) {
    uz_rhi_bind_compute_shader(cmd, gen->gen_shader);
    uz_rhi_set_uav(cmd, 0, gen->instance_count_buffer);
    uz_rhi_set_uav(cmd, 1, gen->indirect_args_buffer);
    uz_rhi_dispatch(cmd, emitter_count, 1, 1);
}
```

**Files to Create**:
- `src/engine/particles/indirect_args_gen.h`
- `src/engine/particles/indirect_args_gen.cpp`
- `src/shaders/particle/uz_particle_indirect_args_gen.hlsl`

---

## Pattern 7: Script Stage Separation (Compile Each Stage Separately)

### Niagara Implementation
- Spawn script → separate compute shader
- Update script → separate compute shader
- Event script → separate compute shader

### UzEngine Adaptation

```c
// uz_particle_scripts.h

typedef enum {
    UZ_SCRIPT_SPAWN = 0,
    UZ_SCRIPT_UPDATE = 1,
    UZ_SCRIPT_EVENT = 2,
    UZ_SCRIPT_STAGE_COUNT,
} uz_script_stage_t;

typedef struct {
    uz_shader_t* shaders[UZ_SCRIPT_STAGE_COUNT];
    const char* source[UZ_SCRIPT_STAGE_COUNT];
} uz_particle_script_pack_t;

// Compile spawn script ONLY (not monolithic)
uz_shader_t* uz_compile_particle_spawn_script(const char* script_source) {
    char hlsl[4096];
    snprintf(hlsl, sizeof(hlsl),
        "#include \"uz_particle_common.hlsl\"\n"
        "#define STAGE_SPAWN 1\n"
        "%s\n",
        script_source
    );
    return uz_shader_compile_compute(hlsl, "SimulateSpawn", 64, 1, 1);
}

// Compile update script ONLY (independent)
uz_shader_t* uz_compile_particle_update_script(const char* script_source) {
    char hlsl[4096];
    snprintf(hlsl, sizeof(hlsl),
        "#include \"uz_particle_common.hlsl\"\n"
        "#define STAGE_UPDATE 1\n"
        "%s\n",
        script_source
    );
    return uz_shader_compile_compute(hlsl, "SimulateUpdate", 64, 1, 1);
}

// Load/save (no recompile if only spawn changed)
uz_particle_script_pack_t* uz_particle_script_pack_load(const char* path) {
    // Load source
    // Compile each stage independently
    // Return pack
}
```

**Why**: Adding new attribute doesn't force full recompile. Only affected stages recompile.

**Files to Create**:
- `src/engine/particles/script_compiler.h`
- `src/engine/particles/script_compiler.cpp`
- `src/shaders/particle/uz_particle_common.hlsl`

---

## Integration: Full Simulation Pipeline

```c
// uz_particle_system.h

typedef struct {
    uz_instance_count_manager_t* count_mgr;
    uz_ndi_registry_t* ndi_registry;
    uz_indirect_args_generator_t* indirect_gen;
    uz_emitter_instance_t** emitters;
    uint32_t emitter_count;
} uz_particle_system_t;

uz_particle_system_t* uz_particle_system_create() {
    uz_particle_system_t* sys = uz_malloc(sizeof(*sys));
    sys->count_mgr = uz_instance_count_manager_create(1024);
    sys->ndi_registry = uz_ndi_registry_create();
    sys->indirect_gen = uz_indirect_args_generator_create(1024);

    // Register default NDIs
    uz_ndi_registry_register(sys->ndi_registry, uz_ndi_collision_create());

    return sys;
}

void uz_particle_system_simulate(
    uz_particle_system_t* sys,
    float dt,
    uz_command_list_t* cmd
) {
    // Stage 1: Reset count buffer (all counts = 0)
    uz_instance_count_manager_clear_counts(sys->count_mgr, cmd);

    // Stage 2: Per-emitter simulation
    for (uint32_t i = 0; i < sys->emitter_count; ++i) {
        uz_emitter_instance_t* emitter = sys->emitters[i];

        // Run spawn script (if any particles spawning)
        uz_rhi_bind_compute_shader(cmd, emitter->spawn_shader);
        uz_rhi_dispatch(cmd, emitter->spawn_count, 1, 1);

        // Run update script (for all alive particles)
        uz_rhi_bind_compute_shader(cmd, emitter->update_shader);
        uz_rhi_dispatch_indirect(cmd, sys->count_mgr->count_buffer,
                                  emitter->count_buffer_offset);

        // Run NDI lifecycle
        uz_ndi_context_t ctx = { .emitter = emitter, .cmd = cmd };
        for (uint32_t j = 0; j < sys->ndi_registry->count; ++j) {
            sys->ndi_registry->ndis[j]->vtable->post_stage(
                sys->ndi_registry->ndis[j], &ctx
            );
        }
    }

    // Stage 3: Generate indirect args for drawing
    uz_indirect_args_generator_update(sys->indirect_gen, sys->emitter_count, cmd);

    // Stage 4: Render particles (uses pre-generated indirect args)
    for (uint32_t i = 0; i < sys->emitter_count; ++i) {
        uz_emitter_instance_t* emitter = sys->emitters[i];
        uz_particle_dynamic_data_t* dyn_data =
            uz_sprite_renderer_generate_dynamic_data(emitter, emitter->renderer_props);

        // Render thread will use dyn_data (opaque, game-thread state hidden)
        uz_render_queue_add_sprite_particles(dyn_data);
    }
}
```

---

## Summary of Files to Create

| Component | Header | Implementation |
|-----------|--------|-----------------|
| Attributes | `particle_attributes.h` | (inline) |
| SOA Buffers | `particle_buffer.h` | `particle_buffer.cpp` |
| Allocator | `particle_allocator.h` | `particle_allocator.cpp` |
| Instance Counts | `instance_count_manager.h` | `instance_count_manager.cpp` |
| Render Proxy | `renderer_proxy.h` | `sprite_renderer.cpp` |
| Data Interfaces | `data_interface.h` | `data_interface_*.cpp` |
| Indirect Args | `indirect_args_gen.h` | `indirect_args_gen.cpp` |
| Script Compiler | `script_compiler.h` | `script_compiler.cpp` |
| System | `particle_system.h` | `particle_system.cpp` |

Plus shaders:
- `uz_particle_common.hlsl` - Shared utilities
- `uz_particle_spawn.hlsl` - Spawn stage
- `uz_particle_update.hlsl` - Update stage
- `uz_particle_indirect_args_gen.hlsl` - Indirect args generation

---

## Testing Strategy

1. **Unit Tests**:
   - Allocator: test acquire/free cycles, no fragmentation
   - Count Manager: test slot allocation, concurrent access
   - Data Interface: test lifecycle hooks, vtable dispatch

2. **Integration Tests**:
   - Single emitter 10K particles → correct count buffer
   - Multiple emitters → correct dispatch batching
   - NDI collision → pre/post stage callbacks fire
   - Indirect drawing → correct draw args generated

3. **Performance Benchmarks**:
   - SOA vs AOS particle fetch latency
   - Dispatch grouping overhead reduction
   - Free list allocation speed

4. **Regression**:
   - Ensure particle data matches CPU simulation
   - Frame-to-frame consistency with fixed seed
