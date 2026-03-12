# RHI — Device Resources

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/core/rhi/rhi.h` (IRHIDevice), `runtime/core/rhi/dx12/dx12_resources.h`, `dx12_resource_creation.h/.cpp`
> **Depends on**: RHI types (`rhi_types.h`), D3D12MA

## Purpose

`IRHIDevice` is the central resource factory. It creates and destroys all GPU resources — buffers, textures, shaders, pipelines, samplers, swap chains, fences, and command lists. Every resource flows through this interface. The backend (DX12, Vulkan, Metal) implements the actual GPU allocation.

## Resource Types

### Buffers

GPU buffers for vertex data, indices, constants, structured data, and indirect arguments.

```cpp
RHIBufferDesc desc{};
desc.sizeBytes = sizeof(Vertex) * vertexCount;
desc.stride    = sizeof(Vertex);
desc.usage     = RHIBufferUsage::VertexBuffer;
desc.memory    = RHIMemoryType::Default;       // GPU-only (fastest)
desc.debugName = "MeshVertexBuffer";

RHIBufferHandle buffer = device->CreateBuffer(desc);
```

**Usage flags** (combinable via `|`):

| Flag | Purpose | Creates |
|------|---------|---------|
| `VertexBuffer` | Input assembler vertex data | — |
| `IndexBuffer` | Input assembler index data | — |
| `ConstantBuffer` | Per-draw/per-frame constants (256-byte aligned) | — |
| `StructuredBuffer` | Array of typed elements (per-element stride) | SRV |
| `UnorderedAccess` | Read-write from shaders | UAV |
| `IndirectArgs` | Indirect draw/dispatch arguments | — |
| `CopySrc` / `CopyDst` | GPU copy source/destination | — |

**Memory types:**

| Type | CPU Access | GPU Access | Use Case |
|------|-----------|------------|----------|
| `Default` | None | Fast read/write | Meshes, textures, GPU-only data |
| `Upload` | Write (persistent map) | Read-only | Staging for GPU upload |
| `Readback` | Read (persistent map) | Write-only | GPU → CPU data transfer |

**Bindless access:**
- `GetBufferSRVIndex(handle)` → index in global CBV_SRV_UAV heap (for structured buffers)
- `GetBufferGPUAddress(handle)` → GPU virtual address (for root CBV binding)
- `MapBuffer(handle)` → persistent CPU pointer (Upload/Readback only)

### Textures

2D textures, 2D arrays, and cube maps with automatic descriptor creation.

```cpp
RHITextureDesc desc{};
desc.width            = 1024;
desc.height           = 1024;
desc.mipLevels        = 10;
desc.format           = RHIFormat::RGBA8Srgb;
desc.usage            = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
desc.debugName        = "Albedo";

RHITextureHandle texture = device->CreateTexture(desc);
uint32_t srvIndex = device->GetTextureSRVIndex(texture); // For bindless access
```

The backend automatically creates descriptors based on usage flags:

| Usage Flag | Descriptor Created |
|------------|-------------------|
| `ShaderResource` | SRV (shader read) |
| `RenderTarget` | RTV (color attachment) |
| `DepthStencil` | DSV (depth attachment) |
| `UnorderedAccess` | UAV (shader read/write) |

### Shaders

Compiled shader bytecode (DXIL for DX12, SPIR-V for Vulkan, MSL for Metal). The bytecode is copied internally — the caller's buffer does not need to persist.

```cpp
RHIShaderDesc desc{};
desc.bytecode     = dxilData.data();
desc.bytecodeSize = dxilData.size();
desc.stage        = RHIShaderStage::Vertex;
desc.entryPoint   = "main";
desc.debugName    = "MeshVS";

RHIShaderHandle vs = device->CreateShader(desc);
```

### Pipelines

Graphics pipeline state objects (PSOs) combine shaders, rasterizer state, blend state, depth-stencil state, and render target formats into an immutable object.

```cpp
RHIGraphicsPipelineDesc desc{};
desc.vertexShader          = vs;
desc.pixelShader           = ps;
desc.topology              = RHIPrimitiveTopology::TriangleList;
desc.rasterizer.cullMode   = RHICullMode::Back;
desc.depthStencil.depthEnable = true;
desc.depthStencil.depthFunc   = RHICompareFunc::Less;
desc.renderTargetFormats[0]   = RHIFormat::RGBA8Unorm;
desc.renderTargetCount        = 1;
desc.depthStencilFormat       = RHIFormat::D32Float;
desc.debugName                = "MeshPipeline";

// SM6.6 bindless: no input layout needed (vertex data from SRV buffer)
// SM5 fallback: provide inputElements + inputElementCount + vertexStride

RHIPipelineHandle pipeline = device->CreateGraphicsPipeline(desc);
```

**SM6.6 vs SM5 pipeline differences:**

| Aspect | SM6.6 Bindless | SM5 Fallback |
|--------|---------------|--------------|
| Vertex input | None (`inputElementCount = 0`) | `RHIInputElementDesc[]` with semantics |
| Resource binding | Push constants with heap indices | Descriptor tables + root CBV |
| Vertex fetch | Shader pulls from SRV buffer | Input Assembler fetches vertices |

### Samplers

Texture sampling state — filter mode, address mode, LOD, anisotropy.

```cpp
RHISamplerDesc desc{};
desc.filter      = RHIFilterMode::Anisotropic;
desc.addressU    = RHIAddressMode::Wrap;
desc.addressV    = RHIAddressMode::Wrap;
desc.maxAnisotropy = 16;
desc.debugName   = "TextureSampler";

RHISamplerHandle sampler = device->CreateSampler(desc);
uint32_t idx = device->GetSamplerIndex(sampler); // For bindless
```

## Resource Pool (DX12 Implementation)

The DX12 backend stores resources in flat `ResourcePool<T>` arrays. Each slot has a generation counter matching the `RHIHandle::generation`. This provides O(1) lookup with use-after-free detection:

```
ResourcePool<DX12Buffer>
┌──────┬──────────┬────────────┬──────────┐
│ Slot │ gen=3    │ D3D12MA*   │ SRV=42   │  ← Live resource (gen matches handle)
├──────┼──────────┼────────────┼──────────┤
│ Slot │ gen=7    │ nullptr    │ —        │  ← Freed slot (gen bumped, in free list)
├──────┼──────────┼────────────┼──────────┤
│ Slot │ gen=1    │ D3D12MA*   │ SRV=105  │  ← Live resource
└──────┴──────────┴────────────┴──────────┘
```

When a resource is destroyed:
1. Native GPU resource is released (D3D12MA allocation freed)
2. Descriptor indices returned to the descriptor allocator
3. Slot pushed to free list for reuse
4. Generation stays bumped — old handles with the previous generation are now stale

**Engine Comparison:**
- **UE5**: `FD3D12Adapter` owns typed resource arrays (`TArray<FD3D12Resource>`). Similar flat-array concept but with `AddRef/Release` reference counting instead of generational handles.
- **Godot**: `RenderingDeviceVulkan` stores resources in `HashMap<RID, Resource>` — hash lookup per access vs our O(1) array index.
- **UzEngine**: Flat array with free list + generational handle. Fastest possible lookup (single array index) with O(1) stale-handle detection.

## Formats

The `RHIFormat` enum covers all commonly used GPU formats:

| Category | Formats |
|----------|---------|
| Color (8-bit) | R8Unorm, RG8Unorm, RGBA8Unorm, RGBA8Srgb, BGRA8Unorm, BGRA8Srgb |
| Color (16-bit float) | R16Float, RG16Float, RGBA16Float |
| Color (32-bit float) | R32Float, RG32Float, RGB32Float, RGBA32Float |
| Color (32-bit uint) | R32Uint, RG32Uint, RGBA32Uint |
| Depth/Stencil | D16Unorm, D32Float, D24UnormS8Uint, D32FloatS8Uint |

`BytesPerTexel(format)` returns the size per texel (1–16 bytes). Block-compressed formats (BC1–BC7) are not yet supported.

## Rules

1. **ALWAYS** set `debugName` on every resource — it appears in GPU debuggers (PIX, RenderDoc) and leak reports
2. **ALWAYS** check `handle.IsValid()` after `Create*()` — creation can fail (out of memory, invalid params)
3. **NEVER** access a resource after calling `Destroy*()` — the handle becomes stale
4. **ALWAYS** use `Upload` memory type for CPU→GPU data staging, `Default` for GPU-only data
5. **NEVER** call `MapBuffer()` on `Default` memory buffers — returns nullptr
