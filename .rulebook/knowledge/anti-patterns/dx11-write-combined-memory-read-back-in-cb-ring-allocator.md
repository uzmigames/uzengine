# DX11 Write-Combined Memory Read-Back in CB Ring Allocator

**Category**: rhi
**Tags**: dx11, constant-buffer, write-combined, black-screen, rhi, dynamic-buffer

## Description

DX11 DYNAMIC buffers mapped with D3D11_MAP_WRITE_DISCARD use write-combined (WC) memory. WC memory is write-only — reads return stale/zero data. If SetRootCBV reads from the mapped pointer (alloc.cpuPtr) to copy into a scratch CB, all constant buffer data arrives as zeros (matrices, camera position, lighting params). This causes geometry to transform to degenerate (0,0,0,0) clip-space positions — resulting in a completely black screen even though draw calls execute correctly.

## Example

// WRONG: Reading from WC mapped pointer
void* mappedPtr = MapBuffer(dynamicBuffer); // Returns WC pointer
memcpy(mappedPtr, &cbData, size); // Write OK
memcpy(scratchCB, mappedPtr, size); // READ FAILS — returns zeros!

// CORRECT: Use CPU-side shadow copy for ConstantBuffer DYNAMIC buffers
if (desc.usage == ConstantBuffer) {
    shadowCopy = malloc(size);
    return shadowCopy; // Reads from cacheable CPU memory work
}
// On Unmap, copy shadow → actual GPU buffer

## When to Use

Any time a DX11 DYNAMIC buffer is mapped and the mapped pointer is read back (not just written to). Especially common in CB ring allocators where the same allocation is written by the caller and then read by SetRootCBV to copy into a per-draw scratch CB.

## When NOT to Use

DX12 and Vulkan don't have this issue — their upload heaps use cacheable memory by default. Also not an issue for staging/readback buffers which use HOST_VISIBLE memory.
