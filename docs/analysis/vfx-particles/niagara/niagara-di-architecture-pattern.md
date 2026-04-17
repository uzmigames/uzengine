<!-- doc-meta
type: architecture
subsystem: vfx
tags: [data-interface, pattern, gpu-sim]
created: 2026-03-30
-->

# Niagara Data Interface Architecture Pattern for UzEngine

## Overview

UE5's Niagara Data Interfaces (DIs) are a modular system for extending GPU particle simulation without modifying core simulation code. This document describes the architectural pattern and how to adapt it to UzEngine.

## Key Concept: Separation of Concerns

```
┌─────────────────────────────────────────────────────────────┐
│ Niagara System (manages emitters, lifetime, effects)        │
└──────────────────────┬──────────────────────────────────────┘
                       │
    ┌──────────────────┴──────────────────┐
    │                                     │
    ▼                                     ▼
┌──────────────┐                  ┌──────────────┐
│ Simulation   │                  │ Renderers    │
│ (GPU compute)│                  │ (GPU draw)   │
│              │                  │              │
│ - Spawn      │ ◄─── Data        │ - Meshes     │
│ - Update     │      Interface   │ - Lights     │
│ - Velocity   │      (DI)        │ - Decals     │
│ - Position   │   ◄─── DI        │ - Ribbons    │
│              │                  │              │
└──────────────┘                  └──────────────┘

Data Interface = modular data provider/consumer
```

## UE5's Data Interface Architecture (Reference)

### 1. **CPU-Side (UObject Class)**
```cpp
UCLASS(EditInlineNew, Category = "MyDI")
class UNiagaraDataInterfaceMyFeature : public UNiagaraDataInterface
{
public:
    // Properties (serialized, editable)
    UPROPERTY(EditAnywhere)
    float SomeParameter = 1.0f;
    
    // Per-instance data (GT)
    struct FInstanceData {
        FVector CachedValue;
        FRHIShaderResourceView* SomeBuffer;
    };
    
    virtual int32 PerInstanceDataSize() const { return sizeof(FInstanceData); }
    virtual bool InitPerInstanceData(void* Data, FNiagaraSystemInstance* Sys);
    virtual void DestroyPerInstanceData(void* Data, FNiagaraSystemInstance* Sys);
    virtual bool PerInstanceTick(void* Data, FNiagaraSystemInstance* Sys, float DeltaSeconds);
};
```

### 2. **GPU-Side (Proxy Class)**
```cpp
struct FNiagaraDataInterfaceProxyMyFeature : public FNiagaraDataInterfaceProxy
{
    virtual void PreStage(const FNDIGpuComputePreStageContext& Context);
    virtual void PostSimulate(const FNDIGpuComputePostSimulateContext& Context);
    
    // Per-instance data (RT)
    TMap<FNiagaraSystemInstanceID, FInstanceData_RT> InstanceData_RT;
};
```

### 3. **Shader Side (HLSL Functions)**
```hlsl
// Auto-generated from C++ DI definition
float3 DIMyFeature_GetSomeValue(uint InstanceIndex);
```

### 4. **Integration Point**
```cpp
// In simulation compute shader
void SimulateParticles(uint ParticleID)
{
    // Call DI function
    float3 force = DIMyFeature_GetSomeValue(DispatchID.x);
    velocity += force * DeltaTime;
}
```

---

## UzEngine Adaptation Pattern

Since UzEngine uses a simpler compute-shader-based approach, adapt as follows:

### **Option A: Compute Module Approach (Recommended)**

Each DI becomes an optional compute module included in the main simulation.

```
VFX System Architecture:
┌──────────────────────────────────────────┐
│  Particle Simulation                     │
│                                          │
│  include "simulation/core.hlsl"          │
│  include "simulation/forces.hlsl"        │
│  include "simulation/di_vector_field.hlsl" ◄── Data Interface Module
│  include "simulation/di_skeletal_mesh.hlsl" ◄── Data Interface Module
│                                          │
│  [numthreads(256, 1, 1)]                │
│  void main(uint3 DTid : SV_DispatchThreadID) {
│    Particle p = LoadParticle(DTid.x);  │
│    p.velocity += DIVectorField_Sample(p.position);
│    p.position += p.velocity * dt;      │
│  }                                       │
└──────────────────────────────────────────┘
```

### **Option B: Callback/Virtual Dispatch Approach**

Use function pointers or jump tables in CB to call DI functions conditionally.

```hlsl
// Constant buffer with DI function lookup
cbuffer DICallbacks : register(b5) {
    uint NumActiveDIs;
    uint DITypes[16];  // Type IDs of active DIs
    uint DIDataOffsets[16];  // Where DI data starts in SRV
};

void ApplyDIForces(inout Particle p, uint DIIndex) {
    switch(DITypes[DIIndex]) {
        case DI_TYPE_VECTOR_FIELD:
            p.velocity += DIVectorField_Sample(...);
            break;
        case DI_TYPE_AUDIO_SPECTRUM:
            float band = DIAudioSpectrum_GetBand(...);
            p.scale *= 1.0 + band * 0.5;
            break;
        // ... etc
    }
}
```

---

## Pattern: Minimal Data Interface Implementation

### **Step 1: Define C++ Properties**
```cpp
// vfx_vector_field_di.h
struct VectorFieldDI {
    ComPtr<ID3D11ShaderResourceView> vectorFieldSRV;
    Vec3 tileAxes = {1, 1, 1};  // X/Y/Z tiling enabled
    float padding;
};
```

### **Step 2: Bind to Compute CB**
```cpp
// vfx_simulation.cpp
struct SimulationCB {
    // ... existing fields ...
    VectorFieldDI vectorFieldDI;  // Offset 192
};

void UpdateSimulationCB(const VectorFieldComponent* component) {
    SimulationCB cb;
    // ... fill CB ...
    cb.vectorFieldDI.vectorFieldSRV = component->vectorFieldTexture->GetSRV();
    commandList->SetComputeShaderResources(0, {cb.vectorFieldDI.vectorFieldSRV});
}
```

### **Step 3: Call from Simulation Shader**
```hlsl
// vfx_simulation.hlsl
Texture3D g_VectorField : register(t10);  // Bind SRV
SamplerState g_VectorFieldSampler : register(s2);

float3 SampleVectorField(float3 worldPos) {
    float3 samplePos = worldPos * g_SimulationCB.vectorFieldDI.tileAxes;
    return g_VectorField.SampleLevel(g_VectorFieldSampler, samplePos, 0).xyz;
}

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    Particle p = LoadParticle(DTid.x);
    float3 vectorFieldForce = SampleVectorField(p.position);
    p.velocity += vectorFieldForce * g_SimulationCB.deltaTime;
    StoreParticle(DTid.x, p);
}
```

---

## Three DI Integration Patterns

### **Pattern 1: Read-Only Query DI (Simplest)**
Example: Vector Field, Camera, Landscape

```
GPU Simulation                    DI Data (SRV)
┌─────────────────┐             ┌─────────────┐
│ Query position  │──────────►  │ Vector Field│
│ Get velocity    │◄────────────│ Texture     │
└─────────────────┘             └─────────────┘
```

**Implementation**:
- Bind texture SRV to compute shader
- Sample directly in simulation loop

### **Pattern 2: Particle-Driven Write DI (Medium Complexity)**
Example: Render Target, Spatial Hash

```
GPU Simulation                    DI Data (UAV)
┌─────────────────┐             ┌─────────────┐
│ Write particle  │──────────►  │ Render      │
│ position to RT  │             │ Target UAV  │
│ or grid cell    │             └─────────────┘
└─────────────────┘
```

**Implementation**:
- Pre-allocate UAV (render target or grid buffer)
- Use atomic operations to accumulate particle data
- Barrier after compute to sync UAV

### **Pattern 3: Feedback Loop DI (Complex)**
Example: Render Target Read + Write

```
GPU Simulation                    DI Data (SRV + UAV)
┌─────────────────┐             ┌──────────────┐
│ Read from RT    │──────────►  │ Texture SRV  │
│ Modify particle │             │ (input)      │
│ Write to RT     │◄────────────│ Texture UAV  │
│                 │             │ (output)     │
└─────────────────┘             └──────────────┘
```

**Implementation**:
- Bind SRV for reading (previous frame data)
- Bind UAV for writing (this frame output)
- Ensure proper sync between frames (double-buffer or explicit fence)

---

## Data Interface Checklist (Per Feature)

Use this for each new DI:

```markdown
## MyFeature Data Interface

### Phase 1: Design
- [ ] Define C++ properties and defaults
- [ ] Identify GPU data (textures, buffers, constants)
- [ ] Sketch shader functions needed
- [ ] List dependencies (other DIs, components, systems)

### Phase 2: CPU Implementation
- [ ] Create property struct (UProperties)
- [ ] Implement per-instance data initialization
- [ ] Hook into component lifecycle (OnComponentCreated, etc.)
- [ ] CPU→GPU sync (fill constant buffer)

### Phase 3: GPU Implementation
- [ ] Add CB fields (alignment-safe)
- [ ] Bind SRVs/UAVs to compute shader
- [ ] Write HLSL query functions
- [ ] Test with dummy values first (solid color, constant)

### Phase 4: Integration
- [ ] Wire into simulation loop (update order)
- [ ] Add to simulation CB
- [ ] Test with particles (visual inspection)
- [ ] Performance profiling

### Phase 5: Documentation
- [ ] Parameter descriptions
- [ ] Use case examples
- [ ] Performance notes
- [ ] Interaction with other DIs
```

---

## Concrete Example: Vector Field DI (Complete)

### **C++ (Header)**
```cpp
// runtime/engine/vfx/vector_field_di.h
#pragma once
#include "engine/core/math.h"
#include "engine/rhi/rhi_device.h"

struct VectorFieldDI {
    ComPtr<ID3D11ShaderResourceView> texture;  // 3D texture
    Vec3 tilingAxes = {1, 1, 1};
    float padding = 0;
    
    // World space bounds (for debug, optional)
    Vec3 minBounds = {-1000, -1000, -1000};
    Vec3 maxBounds = {1000, 1000, 1000};
};

class VectorFieldComponent {
public:
    VectorFieldDI GetDIData() const { return m_diData; }
    void SetTexture(ID3D11ShaderResourceView* srv) { m_diData.texture = srv; }
    
private:
    VectorFieldDI m_diData;
};
```

### **C++ (Loading)**
```cpp
// Load from file during scene init
void VectorFieldComponent::LoadTexture(const char* path) {
    // Load 3D texture file (e.g., DDS volume texture)
    ComPtr<ID3D11Texture3D> tex;
    // ... DirectX loading code ...
    
    ComPtr<ID3D11ShaderResourceView> srv;
    device->CreateShaderResourceView(tex.Get(), nullptr, &srv);
    SetTexture(srv.Get());
}
```

### **CB Setup**
```cpp
// runtime/engine/vfx/simulation_cb.h
struct SimulationCB {
    // ... existing fields ...
    VectorFieldDI vectorFieldDI;  // Offset 192
};

// Fill CB each frame
void UpdateSimulationCB(const VectorFieldComponent* vfComp) {
    SimulationCB cb = {};
    cb.vectorFieldDI = vfComp->GetDIData();
    // ... upload CB ...
}
```

### **Shader**
```hlsl
// runtime/shaders/vfx/simulation.hlsl

// Data Interface: Vector Field
Texture3D g_VectorFieldTexture : register(t10);
SamplerState g_VectorFieldSampler : register(s2);

float3 DIVectorField_Sample(float3 worldPos, float3 tilingAxes) {
    // Normalize position to [0, 1]
    float3 uv = (worldPos + 1000.0) / 2000.0;  // Assumes [-1000, 1000] bounds
    uv *= tilingAxes;  // Apply tiling
    uv = frac(uv);     // Wrap
    
    float3 velocity = g_VectorFieldTexture.SampleLevel(
        g_VectorFieldSampler, uv, 0).xyz;
    
    // Denormalize from [0, 1] to [-1, 1]
    return velocity * 2.0 - 1.0;
}

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint particleID = DTid.x;
    if (particleID >= g_SimulationCB.numParticles) return;
    
    Particle p = LoadParticle(particleID);
    
    // Apply vector field force
    float3 vfVelocity = DIVectorField_Sample(p.position, 
        g_SimulationCB.vectorFieldDI.tilingAxes);
    p.velocity += vfVelocity * g_SimulationCB.deltaTime;
    
    StoreParticle(particleID, p);
}
```

---

## Scaling to Multiple DIs

As you implement more DIs, organize by category:

```
vfx/
├── di_spawn/
│   ├── skeletal_mesh_di.h
│   ├── landscape_di.h
│   └── grid_spawn_di.h
├── di_forces/
│   ├── vector_field_di.h
│   ├── audio_spectrum_di.h
│   └── turbulence_di.h
├── di_query/
│   ├── camera_di.h
│   ├── landscape_query_di.h
│   └── collision_query_di.h
└── di_output/
    ├── render_target_di.h
    └── light_descriptor_di.h
```

Each DI file follows the template:
1. Property struct (CPU)
2. Buffer/texture definitions
3. Shader include file (HLSL)
4. Integration point (CB setup, binding)

---

## Performance Considerations

### **Memory Budget per DI**
- CB space: 16–64 bytes (typical)
- SRV/UAV slots: 1–4 per DI
- VRAM texture: depends on quality (512³ = 512MB for float3)

### **Compute Cost**
- Sample DI: 1–10 GPU cycles per query
- Multiply DIs: O(n) queries per particle per DI
- Total sim time: ~1–2ms for 1M particles + 4 DIs (mid-range GPU)

### **Optimization Strategies**
1. **Lazy binding**: Only bind active DIs
2. **Mip levels**: Use coarser mips for large volumes
3. **LOD DIs**: Lower-quality DI data for distant particles
4. **Batching**: Collect DI queries, avoid redundant sampling

---

## Testing Pattern

### **Shader-Only Test**
```hlsl
// Bypass simulation, output DI query results directly
float3 testColor = DIVectorField_Sample(float3(0, 0, 0), float3(1, 1, 1));
return float4(testColor * 0.5 + 0.5, 1.0);  // Visualize velocity
```

### **Particle Visualization**
```cpp
// In billboard shader: show DI data per particle
float diValue = LoadParticle(particleID).diQuery;
outputColor = float4(diValue.xxx, 1.0);  // Heatmap
```

### **CPU Validation**
```cpp
// Read back DI texture, validate CPU-side computation
void ValidateDI() {
    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(vectorFieldSRV, 0, D3D11_MAP_READ, 0, &mapped);
    float3* data = (float3*)mapped.pData;
    // Compare with CPU reference implementation
    context->Unmap(vectorFieldSRV, 0);
}
```

---

## Next Steps for Implementation

1. **Pick one P0 feature** (Mesh Renderer, Light Renderer, or Dynamic Params)
2. **Follow the template above** for C++, CB, HLSL
3. **Test in isolation** (shader-only, then integration)
4. **Add to simulation loop** (update CB, bind SRV/UAV)
5. **Document and iterate**

Each subsequent DI becomes faster as you refine the pattern.
