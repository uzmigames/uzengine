<!-- doc-meta
title: DX12 Non-Bindless Rendering Issues Analysis
author: claude
date: 2026-03-26
tags: [dx12, descriptors, non-bindless, sm6.0, rendering-bugs]
-->

# DX12 Non-Bindless (SM6.0) Rendering Issues Analysis

Research-backed analysis of three rendering bugs encountered on the DX12 SM6.0
(non-bindless) fallback path. Each section identifies the root cause, cites
Microsoft documentation and engine precedents, and proposes a concrete fix.

---

## Problem 1: Fullscreen PS Draws Nothing on Non-Bindless DX12

### Symptoms

- Pipeline creation succeeds (no `E_INVALIDARG`).
- DX12 debug layer reports zero errors/warnings.
- `BeginRenderPass` binds a valid RTV.
- `SetPipeline` binds the correct graphics PSO.
- `IASetPrimitiveTopology(TriangleList)` is called.
- `Draw(3)` is issued.
- Root param 1 (b1 CBV) and root param 2 (b2 CBV) are bound.
- Root param 5 (SRV table) is bound via `SetFragmentTexture` for t0-t2.
- **No visible output.** The same shader works on SM6.8 (bindless).

### Root Cause Analysis

#### 1.1 Descriptor Race Condition: Reusing the Same Staging Block Across Frames

**The primary suspect is that the staging block is allocated once at Init() and
reused every frame, creating a GPU/CPU race condition.**

From Microsoft's official documentation on descriptor heap synchronization:

> "Descriptor heap contents can be changed before, during and after recording
> command lists that reference it. However, descriptors **cannot be changed
> while a command list submitted for execution might reference that location**,
> as this could invoke a race condition."
>
> -- [Descriptor Heaps Overview, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps-overview)

UzEngine's `DX12CommandList::Init()` allocates a single contiguous block of 16
descriptors (`m_fragmentTexStagingBase`) in the shader-visible heap. This block
is reused across all frames. When frame N is recording and copies descriptors
into this block, frame N-1 or N-2 may still be executing on the GPU and
reading from the **same heap locations**. This is a textbook race condition.

From Microsoft's Copying Descriptors documentation:

> "`CopyDescriptorsSimple` uses the CPU to **immediately** copy descriptors.
> They can be called free threaded as long as multiple threads on the CPU or
> GPU do not perform any potentially conflicting writes."
>
> -- [Copying Descriptors, Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3d12/copying-descriptors)

The key insight: `CopyDescriptorsSimple` is a **CPU-side immediate operation**.
It overwrites descriptor memory instantly. If the GPU from a previous frame is
still reading those same descriptors, the result is undefined -- which can
manifest as the shader reading garbage SRV descriptors and producing no output.

#### 1.2 Why Bindless (SM6.8) Works

On the bindless path, `SetFragmentTexture` is a no-op (returns immediately).
The shader uses `ResourceDescriptorHeap[index]` to access textures via root
constants at param 0. Root constants are **versioned per-draw** by the D3D12
runtime (they are stored in the command list itself, not in a shared heap), so
there is no cross-frame aliasing.

#### 1.3 Why the Debug Layer Does Not Catch This

The DX12 debug layer does not detect all descriptor heap race conditions. From
Microsoft's documentation:

> "DX12 has removed some runtime and driver error checks for performance
> reasons, so some issues may not generate debug warnings."
>
> -- [GameDev.net: D3D12 Triangle Not Rendering](https://github.com/Microsoft/DirectX-Graphics-Samples/issues/99)

Descriptor heap race conditions are among the hardest to detect because the
debug layer cannot track GPU execution progress relative to CPU descriptor
writes without full GPU-based validation enabled.

#### 1.4 Additional Suspects: Pipeline State Defaults

Several forum threads document cases where DX12 draws produce no output with a
clean debug layer due to:

- **`SampleMask = 0`** in the PSO descriptor -- must be `0xFFFFFFFF`.
- **`RenderTargetWriteMask = 0`** in the blend state -- prevents any pixel
  output.
- **Cull mode mismatch** -- fullscreen triangle with wrong winding order gets
  culled.

These should be verified but are less likely given the same PSO works on
bindless.

### Fix Recommendation

**Replace the single shared staging block with a per-frame bump allocator
(ring buffer) in the shader-visible heap.**

```
Frame layout in shader-visible CBV_SRV_UAV heap:

  [0 .. 4095]    : Persistent descriptors (textures, IBL, etc.)
  [4096 .. 4096 + kRingSize - 1] : Ring buffer for per-draw staging

  Each frame gets its own region:
    frame 0: [ringBase + 0*frameSlice .. ringBase + 1*frameSlice - 1]
    frame 1: [ringBase + 1*frameSlice .. ringBase + 2*frameSlice - 1]
    frame 2: [ringBase + 2*frameSlice .. ringBase + 3*frameSlice - 1]
```

The pattern used by Diligent Engine, BGFX, and the approach recommended by
NVIDIA:

> "Before each draw that changes descriptors, call
> `SetGraphicsRootDescriptorTable()` with a GPU descriptor handle where the
> offset is ever increasing with the amount of descriptor sizes that are
> copied. The offset will be reset to zero when a new frame starts."
>
> -- [GameDev.net: Descriptor Heap Strategies](https://gamedev.net/forums/topic/686440-d3d12-descriptor-heap-strategies/)

**Concrete implementation:**

```cpp
// In DX12CommandList:
struct FrameDescriptorRing {
    uint32_t baseIndex;       // Start of this frame's region in shader-visible heap
    uint32_t capacity;        // Max descriptors per frame
    uint32_t currentOffset;   // Bump pointer within the frame
};

// At Begin(): reset currentOffset to 0 for the current frame slot
// At SetFragmentTexture(): allocate from ring, copy, bind
uint32_t AllocateStagingBlock(uint32_t count) {
    uint32_t start = m_frameRing.baseIndex + m_frameRing.currentOffset;
    m_frameRing.currentOffset += count;
    return start;
}
```

Each pass calls `AllocateStagingBlock(kMaxFragmentTextures)` to get a fresh
contiguous block. Descriptors are copied into this block and the table is bound
to it. Previous frames' regions are never touched until the GPU fence confirms
completion.

### Alternative: Per-Pass Fresh Allocation (Simpler)

If ring buffer complexity is undesirable, a simpler fix is to allocate a fresh
block of `kMaxFragmentTextures` descriptors from the bump allocator at the
start of every pass. This wastes more heap space but eliminates the race
condition entirely. With 16 descriptors per pass and ~20 passes per frame
across 3 frames in flight, this requires only ~960 descriptors of ring space.

---

## Problem 2: Stale Descriptors Between Passes

### Symptoms

The tonemap pass reads a transmittance LUT (from the sky atmosphere pass)
instead of the bloom texture at slot t1. This is because `SetFragmentTexture`
copies ONE descriptor at a time, and descriptors from prior passes persist in
unwritten slots.

### Root Cause

The staging block at `m_fragmentTexStagingBase` is a single 16-slot region
that persists across the entire frame. When the sky atmosphere pass binds:

- t0 = transmittance LUT
- t1 = scattering LUT
- t2 = irradiance LUT

And the tonemap pass later binds:

- t0 = scene color
- t2 = exposure

Slot t1 **still contains the scattering LUT** from the sky pass. The tonemap
shader reads this stale descriptor at t1 (where it expects the bloom texture).

### How Other Engines Handle This

#### Diligent Engine

Diligent Engine uses a two-tier approach with per-frame linear suballocators:

> "Every command context recording commands allocates a chunk of descriptors
> from the shared dynamic part of the GPU descriptor heap."
>
> -- [Diligent Engine: Managing Descriptor Heaps](https://diligentgraphics.com/diligent-engine/architecture/d3d12/managing-descriptor-heaps/)

Dynamic descriptors are allocated fresh for each binding, never reused within
the same frame. Stale descriptor carryover is impossible because each
draw/dispatch gets its own contiguous block.

#### Godot Engine (D3D12 Driver)

Godot's D3D12 rendering device driver (`drivers/d3d12/rendering_device_driver_d3d12.cpp`)
uses a descriptor heap pooling system based on uniform sets. Each uniform set
(equivalent to a Vulkan descriptor set) allocates its own block from a
`DescriptorsHeap` allocator. When a uniform set is bound, the full set of
descriptors is populated -- there is no partial update pattern.

> Godot allocates "a single CPUDescriptorHeap with 1024 entries and
> distributes these" per binding object, ensuring no cross-contamination.
>
> -- [Godot PR #106809: DirectX DescriptorsHeap pooling](https://github.com/godotengine/godot/pull/106809)

#### UE5 (D3D12 RHI)

UE5's non-bindless DX12 path uses `FD3D12DescriptorCache` which maintains an
"online heap" with a ring-buffer allocator. Each draw call's descriptor
requirements are gathered into a fresh contiguous region. The previous draw's
descriptors are never reused.

### Fix Recommendation

**Option A (Best): Per-pass fresh allocation from ring buffer.**

This is the same fix as Problem 1. Each pass gets a fresh 16-slot block from
the per-frame ring allocator. No descriptors from prior passes can leak through.

**Option B (Quick fix): Zero-fill the staging block at pass start.**

At the beginning of each pass (in `BeginRenderPass`), copy a null SRV
descriptor into all 16 staging slots:

```cpp
void DX12CommandList::BeginRenderPass(...) {
    // ... existing code ...

    // Clear fragment texture staging block to prevent stale descriptors
    if (m_fragmentTexStagingBase != UINT32_MAX) {
        auto& alloc = m_device->GetDescriptorAllocator(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto nullSrvHandle = alloc.CpuHandle(m_device->GetNullSrvIndex());
        for (uint32_t i = 0; i < kMaxFragmentTextures; ++i) {
            auto dstHandle = alloc.CpuHandle(m_fragmentTexStagingBase + i);
            m_device->GetDevice()->CopyDescriptorsSimple(
                1, dstHandle, nullSrvHandle,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
}
```

This still has the cross-frame race condition from Problem 1, so Option A is
strictly superior. Option B is only acceptable as an interim hotfix.

---

## Problem 3: Font Atlas Renders Garbled Red (Root Param Index Confusion)

### Symptoms

The text overlay (font atlas) renders as garbled red on SM6.0. The fix was to
change from `SetDescriptorTable(4, ...)` to `SetFragmentTexture(0, ...)`.

### Root Cause

Root parameter 4 is the **UAV descriptor table** (u0-u3), not the SRV table.
The SRV table is at root parameter 5. When the font atlas SRV was bound at
param 4, the GPU interpreted the SRV descriptor as a UAV, leading to undefined
reads that manifested as garbled red pixels.

UzEngine SM5 root signature layout:

| Root Param | Type | Registers | Purpose |
|------------|------|-----------|---------|
| 0 | Root Constants | b0 | Descriptor indices / algorithm params |
| 1 | Root CBV | b1 | Per-frame data |
| 2 | Root CBV | b2 | Per-object data |
| 3 | Root CBV | b4 | Shadow data |
| **4** | **Descriptor Table** | **u0-u3** | **UAV (compute outputs)** |
| **5** | **Descriptor Table** | **t0-unbounded** | **SRV (textures)** |

Binding an SRV at param 4 instead of param 5 is a silent error -- the debug
layer does not validate that the descriptor type matches the root parameter
expectation in all cases.

### How Common Is This Mistake?

Extremely common. From Simon's Tech Blog on DX12 root signature management:

> "When iterating on shader code, modifying some resources binding (e.g.
> adding a texture variable / removing a constant buffer), the root signature
> may be changed, which causes the change of root parameter index. This will
> need to update all function calls like `SetGraphicsRootDescriptorTable()`
> with new root parameter index, which is tedious and error-prone."
>
> -- [Simon's Tech Blog: D3D12 Root Signature Management](https://simonstechblog.blogspot.com/2019/06/d3d12-root-signature-management.html)

This is one of the top motivations for bindless rendering (SM6.6+), which
eliminates root parameter index management entirely.

### How Other Engines Avoid This

Both UE5 and Godot use **named binding enums** or **compile-time constants**
for root parameter indices rather than raw integer literals:

```cpp
// UE5 pattern (simplified)
enum class ERootParameterIndex {
    RootConstants = 0,
    PerFrameCBV   = 1,
    PerObjectCBV  = 2,
    ShadowCBV     = 3,
    UAVTable      = 4,
    SRVTable      = 5,
    Count
};
```

This makes the mistake immediately visible in code review:

```cpp
// WRONG: SetGraphicsRootDescriptorTable(4, ...)  -- 4 = UAV, not SRV
// RIGHT: SetGraphicsRootDescriptorTable(ERootParameterIndex::SRVTable, ...)
```

### Fix Recommendation

1. **Define root parameter index constants** in `dx12_pso_creation.h` or a
   shared header:

```cpp
namespace RootParam {
    constexpr uint32_t kRootConstants = 0;
    constexpr uint32_t kPerFrameCBV   = 1;
    constexpr uint32_t kPerObjectCBV  = 2;
    constexpr uint32_t kShadowCBV     = 3;
    constexpr uint32_t kUAVTable      = 4;
    constexpr uint32_t kSRVTable      = 5;  // SM5 only
}
```

2. **Replace all raw integer root param indices** with these constants.

3. **Add a static assertion** that `kSM5SrvTableRootParam` in the command list
   matches `RootParam::kSRVTable`.

The existing `kSM5SrvTableRootParam = 5` in `SetFragmentTexture` is correct,
but the text renderer was using a raw `4` instead of going through
`SetFragmentTexture`. The fix of routing through `SetFragmentTexture` is
correct and should be the only path for binding SRVs on SM5.

---

## Summary of Descriptor Management Best Practices (from Microsoft + Industry)

### Microsoft Official Guidance

1. **Use a single shader-visible heap per type** (CBV/SRV/UAV and Sampler).
   Set it once per frame via `SetDescriptorHeaps`. Never switch heaps
   mid-frame unless absolutely necessary.
   ([Descriptor Heaps Overview](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps-overview))

2. **`CopyDescriptorsSimple` is immediate on the CPU.** The GPU reads
   descriptors from the heap at draw/dispatch time. You must not overwrite
   descriptors that an in-flight command list may still reference.
   ([Copying Descriptors](https://learn.microsoft.com/en-us/windows/win32/direct3d12/copying-descriptors))

3. **Root Signature 1.1 descriptor range flags** control when the GPU reads
   descriptors:
   - `DESCRIPTORS_VOLATILE` (used by UzEngine): descriptors can change during
     recording but **must not change while the command list is executing**.
   - Default (no flag): descriptors are **static** -- must be set before the
     descriptor table is bound and cannot change until execution completes.
   ([Root Signature Version 1.1](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1))

4. **Source descriptors for `CopyDescriptorsSimple` must come from a
   non-shader-visible heap.** The destination can be any CPU-visible heap
   (shader-visible or not). UzEngine currently copies from one region of the
   shader-visible heap to another region of the same heap. While technically
   the source can be shader-visible if also CPU-visible, the standard pattern
   is to stage in a CPU-only heap first.
   ([Copying Descriptors](https://learn.microsoft.com/en-us/windows/win32/direct3d12/copying-descriptors))

### Industry Standard Pattern: Per-Frame Ring Allocator

The consensus across NVIDIA, Diligent Engine, BGFX, and UE5 is:

1. Reserve a large region of the shader-visible heap as a ring buffer.
2. Partition it into `kFramesInFlight` slices.
3. Each frame uses a bump allocator within its slice.
4. Reset the bump pointer at frame start (after the frame's fence is signaled).
5. Each draw/pass allocates a fresh contiguous block for its descriptor table.
6. Never overwrite descriptors from a previous frame until the fence confirms
   GPU completion.

This eliminates both cross-frame race conditions (Problem 1) and stale
descriptor carryover (Problem 2) in a single architectural change.

---

## Recommended Fix Priority

| Priority | Fix | Addresses |
|----------|-----|-----------|
| **P0** | Per-frame ring allocator for descriptor staging | Problems 1 + 2 |
| **P1** | Named root param index constants | Problem 3 prevention |
| **P2** | All SRV binding routed through `SetFragmentTexture` | Problem 3 prevention |
| **P3** | Debug validation: assert in `SetDescriptorTable` that param != kUAVTable when binding SRV | Problem 3 detection |

---

## References

- [Descriptor Heaps Overview - Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptor-heaps-overview)
- [Copying Descriptors - Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3d12/copying-descriptors)
- [Root Signature Version 1.1 - Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1)
- [Resource Binding Functional Spec - DirectX Specs](https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html)
- [Managing Descriptor Heaps - Diligent Graphics](https://diligentgraphics.com/diligent-engine/architecture/d3d12/managing-descriptor-heaps/)
- [Advanced API Performance: Descriptors - NVIDIA Developer Blog](https://developer.nvidia.com/blog/advanced-api-performance-descriptors/)
- [D3D12 Descriptor Heap Management - Simon's Tech Blog](https://simonstechblog.blogspot.com/2019/06/d3d12-descriptor-heap-management.html)
- [D3D12 Root Signature Management - Simon's Tech Blog](https://simonstechblog.blogspot.com/2019/06/d3d12-root-signature-management.html)
- [D3D12 Descriptor Heap Strategies - GameDev.net](https://gamedev.net/forums/topic/686440-d3d12-descriptor-heap-strategies/)
- [Do We Need to Rebind After CopyDescriptors? - GameDev.net](https://www.gamedev.net/forums/topic/692250-do-we-need-to-rebind-a-descriptor-table-after-copydescriptors-operation/)
- [Godot D3D12 Driver - GitHub](https://github.com/godotengine/godot/blob/master/drivers/d3d12/rendering_device_driver_d3d12.cpp)
- [Godot PR #106809: DirectX DescriptorsHeap Pooling](https://github.com/godotengine/godot/pull/106809)
- [Bindless Descriptors - Wicked Engine](https://wickedengine.net/2021/04/bindless-descriptors/)
- [D3D12 No Output with DXC - GameDev.net](https://www.gamedev.net/forums/topic/710769-d3d12-no-output-on-screen-when-using-shader-compiled-with-dxc/)
- [Learning D3D12 from D3D11 Part 2 - Alex Tardif](https://alextardif.com/D3D11To12P2.html)
