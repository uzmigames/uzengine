# Security — RAII Ownership Conventions

> **Status**: Reference
> **Source**: `runtime/engine/renderer/rhi/` (RHI interface), `runtime/engine/renderer/core/dx12_*.cpp` (DX12 implementations)
> **Related**: [Core — Memory Management](../01-core/05-memory.md), [RHI — Device & Resources](../03-rhi/03-device-resources.md)

## Purpose

UzEngine uses RAII (Resource Acquisition Is Initialization) patterns to manage GPU resource lifetimes safely. The engine distinguishes between two ownership models:

1. **Smart pointer model** — for CPU-owned objects and temporary COM objects (`ComPtr<T>`, `Own<T>`)
2. **Struct + explicit destroy model** — for GPU resources stored in C-style structs that cross API boundaries (`DX12Buffer`, `DX12Texture`, `DX12Pipeline`)

This document explains the conventions, the rationale, and the safety rules that prevent resource leaks and use-after-free bugs.

## Part 1: Smart Pointers for Temporary Objects

### ComPtr\<T\> for COM Objects

All temporary COM objects (those not stored in C-style structs) must be wrapped in `ComPtr<T>`:

```cpp
// ✅ CORRECT — temporary blob from shader compilation
ComPtr<ID3DBlob> errorBlob;
D3DCompileFromFile(L"shader.hlsl", nullptr, nullptr,
    "main", "cs_6_0", flags, 0, shader.GetAddressOf(), errorBlob.GetAddressOf());
if (!shader) {
    // errorBlob is automatically released when it goes out of scope
    UZ_LOG(Render, Error, "Shader compile failed: {}", GetHresultMessage(hr));
    return false;
}

// ✅ CORRECT — temporary info queue for debugging
ComPtr<ID3D12InfoQueue> infoQueue;
device->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf()));
if (infoQueue) {
    UINT64 count = infoQueue->GetNumStoredMessages();
    // infoQueue released automatically
}
```

### Why ComPtr?

- **Automatic Release()** — `ComPtr<T>` calls `Release()` in its destructor
- **Multiple return paths** — early returns don't leak if wrapped in `ComPtr`
- **Exception safety** — stack unwinding destroys `ComPtr`, releasing the COM object
- **Zero runtime cost in shipping** — `ComPtr` is just RAII bookkeeping

### Own\<T\> for CPU Objects

For CPU-side objects (scene nodes, render proxies, etc.), use `Own<T>`:

```cpp
// ✅ CORRECT — heap-allocated mesh stored in a scene node
Own<Mesh> mesh = MakeOwn<Mesh>(vertices, indices);
node->SetMesh(std::move(mesh));

// Early return — mesh is freed automatically
if (!IsValidGeometry(vertices)) {
    return false;  // mesh destructor called, memory freed
}
```

## Part 2: GPU Resources in C-Style Structs

Some GPU resources are stored in C-style structs for C ABI compatibility or because they're stored in containers that don't support move semantics:

```cpp
/// Raw GPU buffer handle for RHI ABI boundary.
struct DX12Buffer {
    ID3D12Resource* resource = nullptr;  // COM object (manual release required)
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    size_t sizeInBytes = 0;
};

/// Raw GPU texture handle for RHI ABI boundary.
struct DX12Texture {
    ID3D12Resource* resource = nullptr;  // COM object (manual release required)
    D3D12_RESOURCE_DESC desc = {};
};

/// Raw GPU pipeline state handle for RHI ABI boundary.
struct DX12Pipeline {
    ID3D12PipelineState* pso = nullptr;  // COM object (manual release required)
};
```

### Rationale

Why not wrap these in `ComPtr<T>`?

1. **C ABI boundary** — Structures crossing C boundaries cannot contain C++ templates (some build configurations or language bindings)
2. **Container storage** — Some containers (custom pools, pre-allocated arrays) expect trivially copyable structs
3. **Explicit lifetime** — Raw pointers make it explicit that you must call `Destroy()` yourself

### Explicit Destroy Pattern

Every function that creates a GPU resource returns a struct with a raw pointer, and the caller **must** call `Destroy()`:

```cpp
// ✅ CORRECT — create buffer with explicit destroy
DX12Buffer buffer = device->CreateBuffer(desc);

// Use the buffer...
device->CopyDataToBuffer(buffer, data);

// Explicit cleanup — Destroy() releases the COM object
device->DestroyBuffer(buffer);
// ❌ DO NOT: buffer.resource is now dangling
```

### Implement Destroy Functions Carefully

Every `Destroy()` function is responsible for calling `Release()` on the COM object:

```cpp
void RHIDeviceDX12::DestroyBuffer(DX12Buffer& buffer) {
    if (buffer.resource) {
        // Release() decrements refcount
        // If refcount == 0, the COM object is freed
        SAFE_RELEASE(buffer.resource);
        buffer.resource = nullptr;
        buffer.gpuAddress = 0;
        buffer.sizeInBytes = 0;
    }
}

void RHIDeviceDX12::DestroyTexture(DX12Texture& texture) {
    if (texture.resource) {
        SAFE_RELEASE(texture.resource);
        // Clear desc fields to catch use-after-free
        texture.desc = {};
    }
}
```

The macro `SAFE_RELEASE` is defined as:

```cpp
#define SAFE_RELEASE(com) \
    if (com) { \
        (com)->Release(); \
        (com) = nullptr; \
    }
```

## Part 3: Descriptor Handles & Bounds

### Descriptor Heap Allocation

Descriptors (texture views, sampler views, etc.) are allocated from a shared heap. Every `Allocate()` call returns a handle that **must** be checked:

```cpp
// ✅ CORRECT — check for allocation failure
DescriptorHandle handle = descriptorHeap.Allocate();
if (handle.IsInvalid()) {
    UZ_LOG(Render, Error, "Descriptor heap exhausted");
    return false;
}

// Use the handle...
device->SetGraphicsRootDescriptorTable(0, handle.GpuHandle());

// Explicit free (when no longer needed)
descriptorHeap.Free(handle);
```

### Check for kInvalidIndex

After calling `Allocate()`, always check against `DescriptorAllocator::kInvalidIndex`:

```cpp
uint32_t index = allocator.Allocate();
if (index == DescriptorAllocator::kInvalidIndex) {
    // Allocation failed — heap is full
    UZ_CHECKF(false, "Descriptor heap exhausted (capacity: {})",
              allocator.Capacity());
    return nullptr;
}
```

### Bounds Checking on Use

The descriptor heap **asserts** that indices are within bounds when accessed:

```cpp
// In DescriptorAllocator::GetCpuHandle(uint32_t index):
UZ_CHECKF(index < m_capacity,
    "Descriptor index {} out of bounds (capacity: {})",
    index, m_capacity);
```

This catches:
- Out-of-bounds writes (heap overflow)
- Use-after-free (stale indices reused after free)
- Allocation overflow (tried to allocate past capacity)

## Part 4: Double-Free & Use-After-Free Prevention

### Pattern 1: Reset to Null After Release

Always set the pointer to `nullptr` after calling `Release()`:

```cpp
// ✅ CORRECT
if (resource) {
    resource->Release();
    resource = nullptr;  // Mark as released
}

// ❌ WRONG — can double-free or use-after-free
if (resource) {
    resource->Release();
    // resource is dangling — next access is undefined behavior
}
```

### Pattern 2: Guard Delete Operations

For GPU resources stored in containers, guard the delete:

```cpp
class TexturePool {
public:
    void FreeTexture(TextureHandle handle) {
        DX12Texture* tex = m_textures[handle.index];
        if (tex && tex->resource) {
            // Resource exists and hasn't been freed yet
            tex->resource->Release();
            tex->resource = nullptr;
            m_textures[handle.index] = nullptr;
        } else if (tex == nullptr) {
            UZ_LOG(Render, Warning, "Attempt to free already-freed texture");
            return;
        }
    }

private:
    std::vector<DX12Texture*> m_textures;
};
```

### Pattern 3: Validate Preconditions

Before using a resource, validate that it exists:

```cpp
void RHICommandList::BindBuffer(RHIBufferHandle handle) {
    DX12Buffer* buffer = m_device->GetBuffer(handle);
    UZ_CHECKF(buffer && buffer->resource,
        "Attempt to bind freed or invalid buffer (handle: {})", handle.id);

    m_commandList->SetGraphicsRootConstantBufferView(
        0, buffer->gpuAddress);
}
```

## Part 5: GPU Fence & Memory Ordering

### Fence Overflow Safety

The GPU keeps track of completed frames via fence values (uint64_t). The engine never allows fence values to overflow:

```cpp
class RHIDeviceDX12 {
public:
    void WaitIdle() {
        // Get current fence value
        UINT64 completedValue = m_fence->GetCompletedValue();

        // Prevent overflow — if we're about to wrap, log and stall
        if (m_nextFenceValue == UINT64_MAX) {
            UZ_LOG(Render, Warning,
                "GPU fence value nearing max ({}), resetting...", UINT64_MAX);
            m_commandQueue->Wait(m_fence, m_nextFenceValue);
            // Wait for GPU to catch up
            while (m_fence->GetCompletedValue() < m_nextFenceValue) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
            // Reset counters
            m_nextFenceValue = 0;
            completedValue = 0;
        }
    }
};
```

This ensures:
- Fence values never wrap around (uint64_t range is huge — ~585 billion years of frames at 60 FPS)
- GPU-to-CPU synchronization remains correct across boundaries
- No misinterpretation of "older" vs "newer" fences

### Memory Barriers

Always use proper barrier semantics for GPU memory ordering:

```cpp
// ✅ CORRECT — barrier with transition and hazard tracking
D3D12_RESOURCE_BARRIER barrier = {
    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Transition = {
        .pResource = buffer.resource,
        .StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
        .StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    },
};
m_commandList->ResourceBarrier(1, &barrier);

// Use buffer in pixel shader...
```

## Rules

1. **ALWAYS** wrap temporary COM objects in `ComPtr<T>` — never rely on manual `Release()` for temporaries
2. **ALWAYS** explicitly call `Destroy()` for GPU resources in C-style structs — do not rely on implicit cleanup
3. **ALWAYS** check descriptor allocation results against `DescriptorAllocator::kInvalidIndex` — exhaustion is not a fatal error, handle it gracefully
4. **ALWAYS** assert bounds when accessing descriptor heaps via `CpuHandle` or `GpuHandle` — bounds violations indicate memory corruption
5. **ALWAYS** set pointers to `nullptr` after calling `Release()` — prevents double-free and catches use-after-free earlier
6. **ALWAYS** validate preconditions before using GPU resources — defensive coding catches bugs in error paths
7. **NEVER** call `Release()` on a COM object that's `nullptr` — guard with null checks
8. **NEVER** use a GPU resource after calling `Destroy()` — treat the pointer as dangling immediately
9. **NEVER** assume GPU resources are freed immediately — actual destruction may be deferred to preserve in-flight frame semantics
10. **NEVER** let fence values overflow — `WaitIdle()` resets if approaching `UINT64_MAX`

## See Also

- [RHI — Device & Resources](../03-rhi/03-device-resources.md) — detailed resource creation/deletion lifecycle
- [RHI — Descriptors](../03-rhi/05-descriptors.md) — descriptor heap management and bounds
- [Observability — CPU Profiling](../04-observability/02-cpu-profiling.md) — performance profiling and assertion patterns
