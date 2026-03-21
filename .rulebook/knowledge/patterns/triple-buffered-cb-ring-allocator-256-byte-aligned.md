# Triple-Buffered CB Ring Allocator (256-byte aligned)

**Category**: architecture
**Tags**: rendering, constant-buffer, ring-allocator, gpu-upload, dx12

## Description

Constant buffer uploads use a pre-allocated ring buffer: 3 frames × 2MB = 6MB total on upload heap. BeginFrame(frameIndex) rotates ring position. Allocate(sizeBytes) returns CBAllocation with cpuPtr, gpuAddr, bufferHandle, offsetBytes. 256-byte alignment per allocation (DX12 CBV requirement). Frame N records while Frame N-1 executes on GPU, Frame N-2's fence is awaited for reclaim. Zero GPU/CPU sync stalls.

## Example

// Per-frame usage:\nm_cbRing->BeginFrame(frameIndex);\nauto alloc = m_cbRing->Allocate(sizeof(PerFrameCB));\nmemcpy(alloc.cpuPtr, &perFrameData, sizeof(PerFrameCB));\ncmdList->SetRootCBV(kSlotPerFrame, alloc.gpuAddr);

## When to Use

For any per-frame or per-object constant buffer upload. When implementing new render passes that need CB data.

## When NOT to Use

For persistent GPU resources (use default heap). For large data uploads (use staging batch/upload ring).
