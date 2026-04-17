<!-- doc-meta
title: DX12 Frame Capture for H.264 Encoding — Research & Recommended Architecture
author: claude
created: 2026-03-28
tags: [dx12, readback, h264, streaming, swapchain, vscode]
-->

# DX12 Frame Capture for H.264 Encoding

Research into the correct DX12 pattern for capturing rendered frames for
H.264 encoding in a real-time streaming scenario (UzEngine viewport inside
VSCode via N-API).

## Problem Statement

The current `uz_encode_frame()` in `runtime/engine/app/runtime.cpp` (line 1073)
uses a blocking fallback path:

```
WaitIdle() -> Begin encode cmdList -> CaptureFromPresent() -> End -> Execute -> WaitIdle() -> Map -> Encode -> Unmap
```

This has three critical issues:

1. **Double WaitIdle** stalls the GPU pipeline completely (two full drain
   points per frame).
2. **Post-Present backbuffer ambiguity** -- after `Present()`,
   `GetCurrentBackBufferIndex()` returns the *next* buffer for rendering,
   not the one just presented. `CaptureFromPresent` reads from
   `swapChain->GetCurrentBackBuffer()`, which may return the wrong buffer.
3. **The triple-buffered FrameReadback path is disabled** (line 1083 comment)
   because it caused `DXGI_ERROR_DEVICE_REMOVED` from command list conflicts
   between the render and encode command lists.

## Root Cause Analysis: Why the Current Approaches Fail

### Why separate command list readback causes DEVICE_REMOVED

DX12 requires that resource state transitions are consistent across all
command lists submitted to the same queue. When the render command list
leaves the backbuffer in `Present` state, and a *separate* encode command
list transitions it to `CopySrc` *after* `Present()` has been called:

- The DXGI runtime may have already flipped the buffer internally.
- The backbuffer index has rotated -- `GetCurrentBackBuffer()` now returns
  a *different* buffer than the one just rendered.
- If the encode command list transitions buffer N while the display engine
  is scanning out buffer N, the DX12 runtime detects an invalid state
  and removes the device.

### Why WaitIdle + CaptureFromPresent is fragile

Even with `WaitIdle()` draining all GPU work:

- After `Present()`, the swapchain has advanced its index. The buffer that
  was just rendered to is no longer "current."
- `CaptureFromPresent` calls `swapChain->GetCurrentBackBuffer()` which
  returns the *next* buffer (index advanced by Present), not the rendered one.
- On FLIP_DISCARD swapchains (the default for DX12), the previous
  backbuffer content is explicitly *undefined* after Present.

## Industry Approaches to Frame Capture

### 1. Render to Intermediate Texture (Recommended)

The standard pattern used by game engines with streaming/recording support:

```
Scene rendering -> Intermediate RT (engine-owned texture)
                     |
                     +---> Copy to Readback Buffer (for H.264 encode)
                     |
                     +---> Copy/Blit to Swapchain Backbuffer (for Present)
```

The engine never reads back from the swapchain backbuffer. The intermediate
RT is a regular `D3D12_RESOURCE_DIMENSION_TEXTURE2D` with
`D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET`, fully under engine control.

**Advantages:**
- No swapchain index ambiguity -- the intermediate is always valid.
- Readback copy and present blit can happen in the same command list.
- No WaitIdle needed if triple-buffered readback fences are used.
- Works with all swapchain models (FLIP_DISCARD, FLIP_SEQUENTIAL, etc.).
- Decouples encode resolution from display resolution (encode at 720p,
  display at native).

**This is what UzEngine already almost does.** The render graph writes to
an HDR texture (`hdrRes`), tonemaps to either `LDR_Intermediate` or the
backbuffer (`bbRes`). The fix is to *always* tonemap to an engine-owned
LDR texture, then copy that to both the readback buffer and the backbuffer.

### 2. OBS Studio Game Capture (Hook-Based)

OBS hooks `IDXGISwapChain::Present` via DLL injection. Inside the hook,
*before* calling the real `Present`, OBS copies the backbuffer (which is
still in a known state since Present has not yet executed) to a shared
texture. This works because:

- The copy happens *before* Present, not after.
- The backbuffer is still the "current" rendered buffer.
- OBS uses `ID3D12Device::CreateSharedHandle` for cross-process sharing.

**Not applicable to UzEngine** -- we own the render loop, so we do not
need to hook Present. We can simply add the readback copy to our own
command list before calling Present.

### 3. NVIDIA NvFBC / NvIFR (Hardware Capture)

NVIDIA's proprietary APIs capture frames at the driver level:

- **NvFBC** captures the composited framebuffer directly (full desktop).
- **NvIFR** injects into the graphics API to capture from render targets.

Both bypass the swapchain entirely. NvFBC is what ShadowPlay uses.
These APIs are NVIDIA-only, require an enterprise license for non-GeForce
GPUs, and add a vendor dependency. Not suitable for a cross-vendor engine.

### 4. IDXGIOutputDuplication (Desktop Duplication API)

Captures the *composited desktop* output, not the application's render
target. Adds window-manager composition latency (1+ frame). Only captures
what is visible on screen. Fails for occluded/minimized windows. Not
suitable for a headless or embedded viewport.

## Recommended Architecture for UzEngine

### Pattern: Always-Render-to-Intermediate + Dual Copy

```
Render Graph
  |
  v
[HDR RT] --tonemap--> [LDR Intermediate*] --copy--> [Readback Buffer N]
                              |
                              +---copy/blit---> [Swapchain Backbuffer] --> Present()
```

*The LDR Intermediate already exists in the codebase (`m_fxaaLdrIntermediate`)
when CAS or FXAA is active. The change is to always allocate it and always
tonemap to it, regardless of post-process settings.*

### Resource State Transitions (Single Command List)

All transitions happen in one command list, in one submission, eliminating
the cross-command-list synchronization bug:

```
1. [LDR Intermediate: RenderTarget]  -- tonemap writes here
2. Barrier: LDR Intermediate  RenderTarget -> CopySrc
3. CopyTextureToBuffer(LDR Intermediate -> Readback[N])
4. Barrier: Backbuffer  Present -> CopyDst
5. CopyResource(LDR Intermediate -> Backbuffer)   (or fullscreen blit)
6. Barrier: Backbuffer  CopyDst -> Present
7. Barrier: LDR Intermediate  CopySrc -> RenderTarget  (ready for next frame)
8. End + Execute command list
9. Present()
10. Signal fence for Readback[N]
11. Advance write index
```

### Triple-Buffered Readback (Zero-Stall Encode)

The existing `FrameReadback` class (`runtime/engine/ipc/frame_readback.h`)
is correctly designed for this. The reason it failed before is that it was
reading from the *swapchain backbuffer* via a separate command list. With
the intermediate texture approach, the readback copy is recorded in the
*same* command list as the render work, eliminating the conflict.

```
Frame N:   GPU copies LDR Intermediate -> Readback[0], signals fence[0]
Frame N+1: GPU copies LDR Intermediate -> Readback[1], signals fence[1]
Frame N+2: GPU copies LDR Intermediate -> Readback[2], signals fence[2]
           CPU maps Readback[0] (fence[0] guaranteed complete), encodes H.264
Frame N+3: GPU writes Readback[0] again (recycled)
           CPU maps Readback[1], encodes
           ...
```

2-frame encode latency is acceptable for viewport streaming (33ms at 60fps).
The CPU never stalls waiting for the GPU.

### API Calls (DX12-Specific, Abstracted via RHI)

**Creating the LDR Intermediate (once, on init or resize):**
```cpp
RHITextureDesc desc{};
desc.width  = viewportWidth;
desc.height = viewportHeight;
desc.format = RHIFormat::RGBA8Unorm;  // or BGRA8Unorm to match swapchain
desc.usage  = RHITextureUsage::RenderTarget | RHITextureUsage::CopySrc;
desc.debugName = "LDR_Encode_Intermediate";
m_ldrIntermediate = device->CreateTexture(desc);
```

**Recording the dual copy (per frame, in the main command list):**
```cpp
// After tonemap writes to m_ldrIntermediate...

// 1. Transition LDR intermediate to copy source
RHIBarrierDesc toCopySrc{};
toCopySrc.texture    = m_ldrIntermediate;
toCopySrc.stateBefore = RHIResourceState::RenderTarget;
toCopySrc.stateAfter  = RHIResourceState::CopySrc;
cmdList->ResourceBarrier(&toCopySrc, 1);

// 2. Copy to readback buffer (for H.264 encode)
uint32_t readbackSlot = frameReadback.GetWriteSlot();
cmdList->CopyTextureToBuffer(m_ldrIntermediate, 0,
                              frameReadback.GetBuffer(readbackSlot),
                              0, frameReadback.GetRowPitch());

// 3. Transition backbuffer to copy destination
RHIBarrierDesc bbToCopyDst{};
bbToCopyDst.texture    = backBuffer;
bbToCopyDst.stateBefore = RHIResourceState::Present;
bbToCopyDst.stateAfter  = RHIResourceState::CopyDst;
cmdList->ResourceBarrier(&bbToCopyDst, 1);

// 4. Copy LDR intermediate to backbuffer for display
cmdList->CopyResource(m_ldrIntermediate, backBuffer);

// 5. Transition backbuffer back to present
RHIBarrierDesc bbToPresent{};
bbToPresent.texture    = backBuffer;
bbToPresent.stateBefore = RHIResourceState::CopyDst;
bbToPresent.stateAfter  = RHIResourceState::Present;
cmdList->ResourceBarrier(&bbToPresent, 1);

// 6. Transition LDR intermediate back to render target for next frame
RHIBarrierDesc toRT{};
toRT.texture    = m_ldrIntermediate;
toRT.stateBefore = RHIResourceState::CopySrc;
toRT.stateAfter  = RHIResourceState::RenderTarget;
cmdList->ResourceBarrier(&toRT, 1);

// 7. End + Execute + Present
cmdList->End();
cmdList->Execute();
swapChain->Present(false);

// 8. Signal fence for the readback buffer
frameReadback.SignalFence();
frameReadback.AdvanceWrite();
```

**Reading back on CPU (from render thread or dedicated encode thread):**
```cpp
if (frameReadback.IsReadReady()) {
    const uint8_t* data;
    uint32_t stride;
    frameReadback.MapRead(&data, &stride);

    encoder.SubmitFrameAsync(data, stride);  // copies internally

    frameReadback.UnmapRead();
    frameReadback.AdvanceRead();
}
```

### Integration with Existing Render Graph

The cleanest integration point is in `frame_postprocess.cpp` and
`scene_render_frame.cpp`:

1. **`frame_postprocess.cpp`**: Change `RegisterPostProcessPasses` to
   *always* write tonemap output to `m_ldrIntermediate` (currently it
   writes directly to `bbRes` when CAS/FXAA are inactive).

2. **`scene_render_frame.cpp`**: After `rgBuilder.Execute(cmdList)`, add
   the dual copy (intermediate -> readback + intermediate -> backbuffer)
   and the backbuffer Present transition. This replaces the current
   render graph's final backbuffer write.

3. **`runtime.cpp`**: Remove the `uz_encode_frame()` WaitIdle path. Instead,
   read from the triple-buffered readback in `uz_encode_frame()` using
   `FrameReadback::IsReadReady()` / `MapRead()` -- zero stall.

4. **`render_thread.cpp`**: The render loop becomes:
   ```
   uz_render_frame()    // renders + copies to readback + presents
   uz_encode_frame()    // maps readback[N-2], encodes, unmaps -- no stall
   ```

### Performance Budget

| Operation                     | Cost (1080p)   | Notes                     |
|-------------------------------|----------------|---------------------------|
| CopyTextureToBuffer (readback)| ~0.1-0.3 ms    | GPU memory bandwidth only |
| CopyResource (to backbuffer)  | ~0.1-0.2 ms    | Same-device copy          |
| H.264 encode (NVENC)          | ~1-3 ms        | Async on encode engine    |
| H.264 encode (x264 ultrafast) | ~3-8 ms        | CPU, on encoder thread    |
| Triple-buffer readback stall  | 0 ms            | Fence-gated, no wait      |

Total overhead vs current path: saves 2x WaitIdle (each ~2-16ms depending
on GPU workload), adds ~0.3ms of copy work. Net improvement: 4-30ms per
frame.

### Resolution Independence

A bonus of the intermediate texture approach: the encode resolution can
differ from the display resolution. For a VSCode viewport panel:

- Display at panel pixel size (e.g., 1920x1080 on a high-DPI monitor)
- Encode at a lower resolution (e.g., 1280x720) for bandwidth savings
- Use a separate, smaller LDR intermediate for encoding
- Blit (with scaling) from the full-res intermediate to the backbuffer

This is a future optimization -- for the initial fix, encode and display
at the same resolution.

## Summary of Required Changes

| File | Change |
|------|--------|
| `scene_renderer_impl.h` | Add `m_ldrEncodeIntermediate` texture + SRV |
| `scene_renderer_init.cpp` | Create/destroy the LDR intermediate on init/resize |
| `frame_postprocess.cpp` | Always tonemap to intermediate, never directly to bbRes |
| `scene_render_frame.cpp` | After render graph execute: copy intermediate to readback + backbuffer |
| `runtime.cpp` | Remove WaitIdle encode path; use FrameReadback::IsReadReady/MapRead |
| `frame_readback.h/cpp` | Add `GetWriteSlot()`, `GetBuffer()`, `AdvanceWrite()` public methods |

## References

- [Microsoft: Swap Chains (DX12)](https://learn.microsoft.com/en-us/windows/win32/direct3d12/swap-chains)
- [Microsoft: IDXGISwapChain3::GetCurrentBackBufferIndex](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiswapchain3-getcurrentbackbufferindex)
- [Microsoft: Read back data via a buffer](https://learn.microsoft.com/en-us/windows/win32/direct3d12/readback-data-using-heaps)
- [Microsoft: CopyTextureRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion)
- [Microsoft: DRED (Device Removed Extended Data)](https://microsoft.github.io/DirectX-Specs/d3d/DeviceRemovedExtendedData.html)
- [Microsoft: Enhanced Barriers](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html)
- [OBS Studio DX12 capture](https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/graphics-hook/d3d12-capture.cpp) -- hooks Present, copies *before* Present
- [OBS Studio DXGI capture](https://github.com/obsproject/obs-studio/blob/master/plugins/win-capture/graphics-hook/dxgi-capture.cpp)
- [NVIDIA Capture SDK](https://developer.nvidia.com/capture-sdk) -- NvFBC/NvIFR, hardware-level capture
- [GameDev.net: Copy texture from back buffer in DX12](https://gamedev.net/forums/topic/679771-copy-texture-from-back-buffer-in-directx12/)
- [GameDev.net: DX12 Fences and swap chain Present](https://www.gamedev.net/forums/topic/677527-dx12-fences-and-swap-chain-present/)
- [Asawicki: Secrets of Direct3D 12 Copies](https://asawicki.info/news_1722_secrets_of_direct3d_12_copies_to_the_same_buffer)
