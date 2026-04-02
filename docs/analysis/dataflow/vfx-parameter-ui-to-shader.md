<!-- doc-meta
Title: Data Flow - VFX Parameter UI to Shader
Path: docs/analysis/dataflow/vfx-parameter-ui-to-shader.md
Type: Data Flow Analysis
Tags: vfx, data-flow, parameter-pipeline, constant-buffer, shader
Status: Complete
Date: 2026-03-30
-->

# Data Flow: VFX Parameter UI → Constant Buffer → Shader

Complete tracing of how a user parameter change in the VFX viewport editor flows through the engine to affect GPU rendering.

---

## Overview: Four-Stage Pipeline

```
┌──────────────────────────────────────────────────────────────────┐
│ STAGE 1: USER INPUT (ImGui/Viewport)                             │
│  ImGui slider change → VfxEmitterParameters struct updated       │
└─────────────────────────┬──────────────────────────────────────────┘
                          │
                          ↓
┌──────────────────────────────────────────────────────────────────┐
│ STAGE 2: CPU SIDE LOGIC (Emitter Instance)                       │
│  Parameter applied to VfxEmitterInstance, may trigger restart    │
└─────────────────────────┬──────────────────────────────────────────┘
                          │
                          ↓
┌──────────────────────────────────────────────────────────────────┐
│ STAGE 3: GPU SETUP (Render Graph Pass)                           │
│  Constant buffer struct populated from emitter parameters       │
│  Buffer uploaded to GPU via RHI command list                    │
└─────────────────────────┬──────────────────────────────────────────┘
                          │
                          ↓
┌──────────────────────────────────────────────────────────────────┐
│ STAGE 4: GPU EXECUTION (Compute/Pixel Shader)                    │
│  Shader reads constant buffer, applies parameter to particles   │
│  Final pixel rendered to swapchain                              │
└──────────────────────────────────────────────────────────────────┘
```

---

## Stage 1: ImGui Viewport Editor

### Input Source

**File:** `demos/vfx_sandbox/main.cpp`  
**Component:** ImGui parameter panel  
**Trigger:** Mouse/keyboard changes to slider widgets  

**Code Flow:**

```cpp
// In vfx_viewport.cpp or similar
if (ImGui::SliderFloat("Spawn Rate", &emitter_params.spawn_rate, 0, 500)) {
    // Event fires here → parameter changed
    emitter_instance.apply_parameters(emitter_params);
    emitter_instance.mark_for_restart();
}
```

### Parameter Storage at This Stage

| Where | What | Type |
|-------|------|------|
| ImGui widget state | Current slider value | float (CPU memory) |
| `VfxEmitterParameters` struct | All 20+ parameters | Struct on stack |
| Local variable | One changed value | float |

### Decision Point

**Does this parameter require emitter restart?**

```cpp
bool needs_restart = (
    spawn_rate_changed ||
    lifetime_changed ||
    size_changed ||
    blend_mode_changed
);

bool live_update_only = (
    gravity_changed ||
    drag_changed ||
    wind_velocity_changed
);
```

If `needs_restart = true`, go to restart path. Otherwise, live update.

---

## Stage 2: CPU-Side Emitter Instance

### Emitter Restart Path (Most Parameters)

**File:** `runtime/src/engine/vfx/emitter_instance.h/.cpp`  
**Classes:** `VfxEmitterInstance`, `VfxEmitterController`

**Timeline:**

```
Frame N (user adjusts parameter)
    ├─ ImGui detects change
    ├─ emitter.apply_parameters(new_params)
    ├─ mark_for_restart() sets flag
    └─ Returns immediately
    
Frame N+1 (engine tick)
    ├─ VfxSystem::update()
    │  ├─ For each emitter_instance:
    │  │  ├─ If needs_restart:
    │  │  │  ├─ release_particle_buffers()  // Kill existing particles
    │  │  │  ├─ reset_simulation_state()     // Time = 0, count = 0
    │  │  │  ├─ spawn_rate = new_spawn_rate // Apply new param
    │  │  │  └─ needs_restart = false
    │  │  │
    │  │  └─ simulate(delta_time)
    │  │     ├─ Spawn phase:
    │  │     │  ├─ spawn_count = spawn_rate * delta_time
    │  │     │  ├─ Create that many particles
    │  │     │  └─ Initialize with new_lifetime, new_color, etc
    │  │     │
    │  │     └─ Update phase:
    │  │        ├─ Integrate velocity
    │  │        ├─ Apply gravity (if live-updated)
    │  │        └─ Update particle attributes
    │  │
    │  └─ render_emitter(emitter_instance, render_graph)
    │
    └─ Returns
```

### Parameter Storage at This Stage

| Where | What | Type | Lifetime |
|-------|------|------|----------|
| `VfxEmitterInstance::parameters` | Current param snapshot | VfxEmitterParameters | Frame-persistent |
| `VfxEmitterInstance::particle_buffer` | Position, velocity, age, color, etc | GPU structured buffer (indirect) | Frame-persistent |
| GPU CB (staging) | About to upload | ConstBufferEmitter | This frame only |

### Live Update Path (Forces Only)

For parameters like gravity/drag that don't require restart:

```cpp
void VfxEmitterInstance::apply_live_parameter_update(const VfxEmitterParameters& new_params) {
    // Just update the member
    parameters.gravity = new_params.gravity;
    parameters.drag = new_params.drag;
    parameters.wind_velocity = new_params.wind_velocity;
    
    // No particle reset needed
    // CB will be updated normally next frame
}
```

---

## Stage 3: Constant Buffer Setup (Render Graph)

### Constant Buffer Struct Definition

**File:** `runtime/src/engine/vfx/constant_buffers.h`

```cpp
struct ConstBufferEmitterSimulation {
    // Spawn parameters
    float spawn_rate;                  // offset 0, particles per second
    float spawn_spread_angle;          // offset 4, degrees
    uz::Vec3 spawn_offset;             // offset 8, cm
    
    // Particle parameters
    float lifetime_min;                // offset 20, seconds
    float lifetime_max;                // offset 24, seconds
    uz::Vec3 initial_velocity;         // offset 28, cm/s
    float size_initial;                // offset 40, scale [0-10]
    
    // Physics
    uz::Vec3 gravity;                  // offset 44, cm/s²
    float drag;                        // offset 56, [0-1]
    uz::Vec3 wind_velocity;            // offset 60, cm/s
    float vortex_strength;             // offset 72, scale
    
    // Rendering
    uint32_t blend_mode;               // offset 76, enum {Opaque=0, Translucent=1, Additive=2, ...}
    float size_over_life_scale;        // offset 80, multiplier
    uz::Vec4 color_initial;            // offset 84, linear RGBA
    uz::Vec4 color_over_life_end;      // offset 100, linear RGBA at lifetime=1
    
    // Simulation state
    uint32_t time_ms;                  // offset 116, current emitter time in ms
    uint32_t particle_count;           // offset 120, live particles this frame
    uint32_t max_particles;            // offset 124, max capacity
    uint32_t flags;                    // offset 128, bitpack (local_space, determinism, etc)
    
    // Padding to 256 bytes (D3D12 requirement)
    // (rest of struct pads to alignment)
};
```

**Layout Check (D3D12):**

The struct MUST be:
- Multiple of 16 bytes (float4 alignment)
- Total size 256 bytes or less (constant buffer limit, usually larger is fine)
- Float3 fields followed by padding (no implicit packing)

### Render Graph Pass: Update Constant Buffer

**File:** `runtime/src/engine/renderer/render_graph/vfx_emitter_pass.cpp`

```cpp
void VfxEmitterPass::execute(RenderGraph& graph, const VfxEmitterInstance& emitter) {
    // Create CB allocation
    void* cb_data = graph.alloc_cb_data(sizeof(ConstBufferEmitterSimulation));
    
    // Populate from emitter parameters
    ConstBufferEmitterSimulation* cb = static_cast<ConstBufferEmitterSimulation*>(cb_data);
    
    // ┌─ Parameter -> CB mapping ─┐
    cb->spawn_rate = emitter.parameters.spawn_rate;              // Direct copy
    cb->spawn_spread_angle = emitter.parameters.spawn_spread_angle;
    cb->spawn_offset = emitter.parameters.spawn_offset;
    
    cb->lifetime_min = emitter.parameters.lifetime_min;
    cb->lifetime_max = emitter.parameters.lifetime_max;
    cb->initial_velocity = emitter.parameters.velocity_initial;
    cb->size_initial = emitter.parameters.size_initial;
    
    cb->gravity = emitter.parameters.gravity;
    cb->drag = emitter.parameters.drag;
    cb->wind_velocity = emitter.parameters.wind_velocity;
    cb->vortex_strength = emitter.parameters.vortex_strength;
    
    // Clamp blend mode to valid range
    cb->blend_mode = static_cast<uint32_t>(emitter.parameters.blend_mode);
    
    cb->size_over_life_scale = emitter.parameters.size_over_life_scale;
    cb->color_initial = emitter.parameters.color_initial;
    cb->color_over_life_end = emitter.parameters.color_over_life_end;
    
    // Emitter simulation state
    cb->time_ms = emitter.elapsed_time_ms;
    cb->particle_count = emitter.live_particle_count;
    cb->max_particles = emitter.max_particles;
    
    // Bit flags
    uint32_t flags = 0;
    if (emitter.parameters.local_space) flags |= (1 << 0);
    if (emitter.parameters.use_determinism) flags |= (1 << 1);
    cb->flags = flags;
    
    // Add to render graph
    graph.add_constant_buffer(RenderGraphResourceType::Emitter, cb);
}
```

### RHI Command List Upload

**File:** `runtime/src/engine/renderer/rhi/command_list.cpp` (or render graph backend)

Once render graph is executed:

```cpp
void RenderGraph::execute_uploads() {
    for (auto& cb_alloc : constant_buffers) {
        // Map CB to GPU memory
        D3D12_RESOURCE_DESC desc = {
            .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
            .Width = sizeof(ConstBufferEmitterSimulation),
            .Height = 1,
            ...
        };
        
        ID3D12Resource* cb_resource = create_upload_buffer(desc);
        
        // Copy CPU data to upload heap
        void* mapped = map_buffer(cb_resource);
        memcpy(mapped, &cb_alloc.data, sizeof(ConstBufferEmitterSimulation));
        unmap_buffer(cb_resource);
        
        // Submit to GPU command list
        command_list->SetComputeRootConstantBufferView(0, cb_resource->GetGPUVirtualAddress());
        // or for pixel shader:
        command_list->SetGraphicsRootConstantBufferView(0, cb_resource->GetGPUVirtualAddress());
    }
}
```

### Parameter Storage at This Stage

| Where | What | Type | Lifetime |
|-------|------|------|----------|
| Stack (execute fn) | CB data copy | ConstBufferEmitterSimulation (256B) | This frame |
| GPU upload heap | Mapped CB data | GPU virtual memory | Until CB swapped |
| GPU constant buffer | Active CB for draw | GPU VRAM (CB cache) | Until next draw overrides |

---

## Stage 4: GPU Execution (Compute/Pixel Shader)

### Shader Usage

**File:** `runtime/shaders/vfx/particle_simulation.hlsl` (compute shader)  
**File:** `runtime/shaders/vfx/particle_rendering.hlsl` (pixel shader)

#### Compute Shader (Particle Update)

```hlsl
// In particle_simulation.hlsl

cbuffer ConstBufferEmitterSimulation : register(b0) {
    float spawn_rate;
    float spawn_spread_angle;
    float3 spawn_offset;
    // ... rest of struct
};

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint particle_idx = DTid.x;
    if (particle_idx >= max_particles) return;
    
    // Read particle from VRAM
    Particle p = particle_buffer[particle_idx];
    
    // APPLY PARAMETER: gravity
    p.velocity += gravity * delta_time;
    
    // APPLY PARAMETER: drag
    p.velocity *= exp(-drag * delta_time);
    
    // APPLY PARAMETER: wind_velocity
    p.velocity += wind_velocity * delta_time * 0.1;
    
    // APPLY PARAMETER: lifetime_max for fade calculation
    float normalized_age = p.age / lifetime_max;
    
    // APPLY PARAMETER: size_over_life_scale
    p.size = size_initial * size_over_life_scale * normalized_age;
    
    // Write back
    particle_buffer[particle_idx] = p;
}
```

#### Pixel Shader (Rendering)

```hlsl
// In particle_rendering.hlsl

cbuffer ConstBufferEmitterSimulation : register(b0) {
    float spawn_rate;
    // ... all params
};

float4 main(PSInput input) : SV_Target {
    float4 particle_color = input.color;
    
    // APPLY PARAMETER: blend_mode affects how we composite
    if (blend_mode == BLEND_ADDITIVE) {
        return particle_color;  // Alpha is additive intensity
    } else if (blend_mode == BLEND_TRANSLUCENT) {
        // Normal premultiplied alpha
        return float4(particle_color.rgb * particle_color.a, particle_color.a);
    }
    
    // APPLY PARAMETER: color_over_life_end for tinting
    float normalized_age = input.age / lifetime_max;
    float4 color_end = lerp(color_initial, color_over_life_end, normalized_age);
    
    return particle_color * color_end;
}
```

### GPU Memory State at This Stage

| Where | What | Type | Persistence |
|-------|------|------|---|
| CB register slot | Emitter parameters | ConstBufferEmitterSimulation | Bound for this draw/dispatch |
| Particle buffer (SRV) | Current particle data | StructuredBuffer<Particle> | Read during compute |
| Particle buffer (UAV) | Output particle data | RWStructuredBuffer<Particle> | Written during compute |
| Framebuffer | Final pixel output | RWTexture2D<float4> | Rendered to swapchain |

---

## Complete Parameter Journey Example

### Walkthrough: User Changes Spawn Rate from 10 to 50 particles/second

```
┌─ FRAME N ─────────────────────────────────────────────────────────┐
│ User Input:                                                         │
│   ImGui slider: spawn_rate 10 → 50                                │
│   Location: Stack (ImGui internal state)                          │
│   Type: float                                                      │
└────────────────────┬───────────────────────────────────────────────┘
                     ↓
┌─ Call Path ───────────────────────────────────────────────────────┐
│ emitter_viewport.cpp:                                             │
│   ImGui::SliderFloat("Spawn Rate", &params.spawn_rate, 0, 500)   │
│   → params.spawn_rate = 50.0f                                     │
│   → emitter_instance.apply_parameters(params)                     │
│   → emitter_instance.mark_for_restart()                           │
│                                                                     │
│ Location: VfxEmitterParameters struct (stack)                     │
│ Type: float (50.0f)                                               │
└────────────────────┬───────────────────────────────────────────────┘
                     ↓
┌─ FRAME N+1 ───────────────────────────────────────────────────────┐
│ VfxSystem::update():                                               │
│   emitter_instance.simulate(delta_time=0.016)                     │
│                                                                     │
│   if (needs_restart) {                                            │
│       release_particle_buffers()                                  │
│       reset_state()                                               │
│       needs_restart = false                                       │
│   }                                                                │
│                                                                     │
│   spawn_count = spawn_rate * delta_time                           │
│               = 50 * 0.016 = 0.8 particles                        │
│       (accumulated per frame, may spawn 1 particle this frame)    │
│                                                                     │
│   Location: VfxEmitterInstance.parameters.spawn_rate = 50.0f      │
│   Type: float (CPU memory)                                        │
└────────────────────┬───────────────────────────────────────────────┘
                     ↓
┌─ Render Graph Setup ──────────────────────────────────────────────┐
│ VfxEmitterPass::execute():                                        │
│   ConstBufferEmitterSimulation* cb = alloc_cb_data(...)          │
│   cb->spawn_rate = emitter.parameters.spawn_rate  // = 50.0f     │
│   graph.add_constant_buffer(cb)                                   │
│                                                                     │
│   Location: Staging memory (malloc'd by render graph)             │
│   Type: ConstBufferEmitterSimulation.spawn_rate (float, offset 0) │
└────────────────────┬───────────────────────────────────────────────┘
                     ↓
┌─ RHI Upload ──────────────────────────────────────────────────────┐
│ Command List execution:                                           │
│   ID3D12Resource* cb = create_upload_buffer(256 bytes)           │
│   memcpy(mapped_ptr, cb_data, 256)                               │
│   command_list->SetComputeRootConstantBufferView(0, gpu_va)      │
│                                                                     │
│   Location: GPU upload heap → GPU constant buffer cache          │
│   Type: D3D12 constant buffer (256B, register b0)                │
└────────────────────┬───────────────────────────────────────────────┘
                     ↓
┌─ GPU Compute ─────────────────────────────────────────────────────┐
│ Shader: particle_simulation.hlsl                                  │
│   [numthreads(256,1,1)]                                           │
│   void main(uint3 DTid) {                                         │
│       ...                                                          │
│       float spawn_amount = spawn_rate * delta_time                │
│       // = 50 * 0.016 = 0.8                                      │
│       ...                                                          │
│   }                                                                │
│                                                                     │
│   Location: GPU register cache (constant buffer b0)               │
│   spawn_rate value: 50.0f (loaded from CB during kernel execute) │
└────────────────────┬───────────────────────────────────────────────┘
                     ↓
┌─ Result ──────────────────────────────────────────────────────────┐
│ Framebuffer shows:                                                │
│   More particles spawning (50/sec instead of 10/sec)              │
│   Effect appears denser/more intense                              │
│                                                                     │
│ Next frame (N+2), user sees the change on screen                 │
└───────────────────────────────────────────────────────────────────┘
```

---

## Latency & Synchronization Points

### Timeline with Actual Latencies

```
Frame N:
  t=0ms   User moves slider
  t=1ms   apply_parameters() called
  t=2ms   mark_for_restart() sets flag
  t=16ms  Frame ends

Frame N+1:
  t=16ms  Update() phase starts
  t=17ms  Emitter restart detected, particles killed
  t=18ms  New particles spawned at new rate
  t=19ms  Render graph builds, CB uploaded to GPU
  t=20ms  GPU executes compute (async)
  t=21ms  GPU renders pixel (async)
  t=32ms  Frame ends, swapchain presents
          ** User SEES change here (frame N+1) **

Frame N+2:
  t=32ms  All particles now spawned at new rate (50/sec)
  t=48ms  User sees stable effect at new rate
```

**Total latency: ~32ms (one frame)**

---

## Validation Checklist

When implementing this data flow:

- [ ] Parameter bounds are enforced (clamp min/max in Stage 2)
- [ ] CB struct is padded to 256-byte multiple (Stage 3)
- [ ] All struct fields are declared in shader (Stage 4)
- [ ] CB register slot matches in C++ and HLSL (register(b0))
- [ ] GPU readback (if needed) uses async/deferred pattern, not sync on main thread
- [ ] Restart logic doesn't leak particle buffers (check release in Stage 2)
- [ ] Live update parameters bypass particle reset (Stage 2)
- [ ] ImGui changes are detected every frame (Stage 1)
- [ ] CB upload happens AFTER emitter state is finalized (Stage 3)

---

## See Also

- `docs/analysis/niagara-editor-ui-patterns.md` — Complete Niagara UI architecture
- `docs/guides/vfx-parameter-ui-quickstart.md` — ImGui implementation examples
