---
name: DX12 Per-Frame Descriptor Ring Buffer
description: SM5 SetFragmentTexture uses a per-frame ring in the shader-visible CBV_SRV_UAV heap to avoid cross-frame descriptor races
type: project
---

## Per-Frame Linear Descriptor Allocator (2026-03-27)

**Problem**: `m_fragmentTexStagingBase` was a single block of 16 descriptors allocated once in `Init()`, shared across all frames and passes. When frame N writes descriptors, frames N-1/N-2 on the GPU may still read from those same locations.

**Solution**: Pre-allocate `kDescriptorsPerFrame (256) * kFramesInFlight (3) = 768` contiguous descriptors at `Init()`. Each frame uses slice `m_currentFrame * kDescriptorsPerFrame`. Within a frame, each `SetPipeline` call triggers a fresh `kMaxFragmentTextures (16)` block allocation via bump pointer (`m_frameDescriptorOffset`). This gives 16 passes per frame budget.

**Key fields** in `DX12CommandList`:
- `m_frameRingBase` — absolute heap index of the ring start
- `m_frameDescriptorOffset` — bump offset within current frame's slice (reset in `Begin()`)
- `m_currentPassDescBase` — absolute heap index of current pass's block (reset in `SetPipeline()`)

**Why**: Microsoft docs state "descriptors cannot be changed while a command list submitted for execution might reference that location." The GPU fence wait before `Begin()` guarantees frame N-3 is done, making slice reuse safe.

**How to apply**: If descriptor budget (256/frame) is ever exhausted, increase `kDescriptorsPerFrame`. Current 16-pass limit covers GBuffer + shadow + lighting + post-process + UI + debug.
