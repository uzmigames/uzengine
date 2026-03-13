# RHI — Overview

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/rhi/rhi.h`, `rhi_types.h`, `rhi_backend.h`, `rhi_config.h`
> **Depends on**: Core (logging, assertions, config, paths)

## Purpose

The Rendering Hardware Interface (RHI) is a backend-agnostic GPU abstraction layer. Application code — renderer, scene, materials, post-processing — uses only the RHI interfaces. No DX12, Vulkan, or Metal types appear above the RHI boundary.

This is the single most critical architectural boundary in the engine. Every rendering feature flows through the RHI. Getting it wrong means rewriting everything when adding a new backend.

```
Application Layer (Renderer, Scene, Materials)
        │
        ▼
    ┌───────────────────────────────────────┐
    │       RHI Interface Layer             │
    │  IRHIDevice · IRHICommandList         │
    │  IRHISwapChain · IRHIFence            │
    │  RHIHandle<Tag> typed handles         │
    ├───────────────────────────────────────┤
    │ DX12 (Done) │ Vulkan (Done) │ Metal   │
    │(rhi/dx12/)  │(rhi/vulkan/)  │(WIP)    │
    └───────────────────────────────────────┘
        │
        ▼
    GPU Hardware (D3D12, Vulkan, Metal APIs)
```

## Design Principles

### 1. No Leaky Abstractions

The RHI interface headers (`rhi.h`, `rhi_types.h`) include zero platform-specific headers. No `d3d12.h`, no `vulkan.h`, no `<windows.h>`. All types are engine-defined enums and structs. This means:

- Code above the RHI compiles on any platform without modification
- Adding a new backend requires zero changes to application code
- The compiler enforces the boundary — accidental DX12 usage won't compile

### 2. Typed Generational Handles

GPU resources are referenced via `RHIHandle<Tag>` — a lightweight 8-byte value type combining a slot index with a generation counter:

```cpp
template <typename Tag>
struct RHIHandle {
    uint32_t index = 0;       // Slot in backend's resource pool
    uint32_t generation = 0;  // 0 = invalid/never-allocated
    bool IsValid() const { return generation != 0; }
};

using RHIBufferHandle   = RHIHandle<BufferTag>;
using RHITextureHandle  = RHIHandle<TextureTag>;
using RHIPipelineHandle = RHIHandle<PipelineTag>;
using RHIShaderHandle   = RHIHandle<ShaderTag>;
using RHISamplerHandle  = RHIHandle<SamplerTag>;
```

**Why not raw pointers?**

| Approach | Pros | Cons |
|----------|------|------|
| Raw pointers | Direct access, no lookup | Use-after-free, no type safety across backends |
| `std::shared_ptr` | Automatic lifetime | Reference counting overhead, hidden ownership |
| **Generational handles** (chosen) | O(1) validation, type-safe, copyable, 8 bytes | One indirection on access |

The generation counter catches use-after-free at O(1) cost: when a resource is destroyed, its slot's generation increments. Any handle still holding the old generation gets rejected on access.

**Engine Comparison:**
- **UE5**: Uses `FRHIResource*` raw pointers with reference counting (`AddRef/Release`). Type safety via separate pointer types (`FRHIBuffer*`, `FRHITexture*`).
- **Godot**: Uses `RID` (Resource ID) — a uint64 handle to `RenderingServer` resources. Similar concept to our handles but untyped (no compile-time tag safety).
- **UzEngine**: Typed generational handles — combines Godot's handle indirection with compile-time type safety via C++ template tags. No reference counting.

### 3. Compile-Time Backend Selection

The active backend is determined at compile time via `UZ_BACKEND_DX12`, `UZ_BACKEND_VULKAN`, `UZ_BACKEND_METAL` defines. There is no runtime backend switching — this simplifies the factory pattern and eliminates virtual dispatch overhead for backend selection:

```cpp
enum class RHIBackend : uint8_t {
    DX12,    // Windows only, SM6.6 bindless
    Vulkan,  // Cross-platform (Linux/Windows)
    Metal,   // macOS/iOS only
    Null,    // Headless testing (all operations are no-ops)
};
```

**Why not runtime switching?** GPU device, descriptor heaps, command queues, and shader bytecode are all backend-specific. Switching mid-session would require destroying and recreating everything — effectively a restart. Compile-time selection eliminates dead code and lets the compiler optimize backend-specific paths.

### 4. SM6.6 Bindless as Primary Path

The RHI is designed around SM6.6 bindless resource access as the primary rendering path:

- Shaders access resources via `ResourceDescriptorHeap[index]` — no descriptor tables, no input layouts
- A global CBV_SRV_UAV heap (1M descriptors) holds all resource descriptors
- Push constants (`SetDescriptorIndex`) pass resource indices to shaders
- SM5.x fallback uses traditional Input Assembler + register-based binding

This mirrors the modern approach used by UE5's bindless renderer and eliminates the descriptor management complexity that plagues traditional DX12 rendering.

## Core Interfaces

| Interface | Purpose | Thread Safety |
|-----------|---------|---------------|
| `IRHIDevice` | Resource factory (buffers, textures, shaders, pipelines, samplers) | Create/Destroy: thread-safe |
| `IRHICommandList` | GPU command recording (draw, dispatch, barriers, copies) | NOT thread-safe (one recorder) |
| `IRHISwapChain` | Back buffer management and presentation | Present: creator thread only |
| `IRHIFence` | CPU/GPU synchronization (monotonic values) | Poll: any thread; Wait: single thread |

### Ownership Model

```
CreateRHIDevice(desc)  → IRHIDevice* (caller owns)
  └── device->CreateBuffer(desc)      → RHIBufferHandle
  └── device->CreateTexture(desc)     → RHITextureHandle
  └── device->CreateShader(desc)      → RHIShaderHandle
  └── device->CreateGraphicsPipeline  → RHIPipelineHandle
  └── device->CreateSampler(desc)     → RHISamplerHandle
  └── device->CreateSwapChain(desc)   → IRHISwapChain* (caller owns)
  └── device->CreateFence()           → IRHIFence* (caller owns)
  └── device->CreateCommandList()     → IRHICommandList* (caller owns)
DestroyRHIDevice(device)  // calls WaitIdle() internally
```

Every resource created from a device must be destroyed before the device. `DestroyRHIDevice` calls `WaitIdle()` internally for safety.

## Configuration Resolution

RHI configuration follows the engine's cascading precedence:

```
CLI flags (highest priority)
  └── --rhi dx12|vulkan|metal|null
  └── --shader-model sm5|sm6.6|...
  └── --gpu-validation
  └── --gpu-breadcrumbs
  └── --adapter N
          ▼
.uzproject config
  └── render.rhi: "dx12"
  └── render.shader_model: "sm6.6"
          ▼
Platform auto-detect (lowest priority)
  └── Windows → DX12
  └── Linux → Vulkan
  └── macOS → Metal
```

Resolved via `ResolveRHIConfig(config)` → `RHIConfig` → `ToDeviceDesc()` → `RHIDeviceDesc`.

## Shader Model Negotiation

At device creation, the backend probes GPU capabilities and negotiates the highest supported shader model:

1. Try SM6.7 → SM6.6 → SM6.5 → ... → SM5.0
2. If SM6.6+ is available: enable bindless rendering (`ResourceDescriptorHeap[]`)
3. If SM5.x only: use Input Assembler + traditional register binding
4. The negotiated shader model determines which precompiled shader variants to load

```cpp
ShaderModel sm = device->GetShaderModel();
bool bindless = device->IsBindless();  // true for SM6.6+
```

## Backend Status

| Backend | Shader Model | Status | Notes |
|---------|-------------|--------|-------|
| **DX12 SM6.6** | SM6.6+ bindless | **Done** (6069 FPS clear screen, RTX 4090) | Primary development path, multithread-safe |
| DX12 SM5.0 | SM5.0 (FXC → DXBC) | Planned | Traditional IA + register binding. Requires FXC compiler (DXC can't compile SM5.0) |
| **DX11** | **SM5.0 (FXC → DXBC)** | **Planned** | Legacy GPU support via Direct3D 11.1. Deferred context command model, implicit memory management. See [11-dx11-backend](11-dx11-backend.md) |
| **Vulkan** | **SPIR-V (SM6.0)** | **Done** (5187 FPS triangle, RTX 4090) | Cross-platform (Linux/Windows/Steam Deck). Uses volk for dynamic dispatch, zero shader changes needed. See [vulkan-backend.md](vulkan-backend.md) |
| **Metal** | **MSL (Shader-Slang transpilation)** | **Phase 4** (Core impl. done; build + factory in progress) | macOS / iOS via argument buffers (bindless). metal-cpp for C++ safety. MetalDevice, MetalCommandList, MetalSwapChain, MetalFence complete. See [15-metal-backend.md](15-metal-backend.md) |
| Null | N/A | **Done** | Headless testing, all ops are no-ops |

## Rules

1. **NEVER** include `d3d12.h`, `vulkan.h`, or `metal.h` above the `rhi/dx12/`, `rhi/vulkan/`, `rhi/metal/` directories
2. **NEVER** store `RHIHandle` without checking `IsValid()` before use
3. **ALWAYS** destroy resources explicitly — don't rely on device destructor cleanup (it works but masks leaks)
4. **ALWAYS** use `ResourceBarrier()` for state transitions — the backend tracks states and validates transitions in debug builds
5. **ALWAYS** fence-synchronize before reusing a command list — Begin() on a busy allocator is undefined behavior
