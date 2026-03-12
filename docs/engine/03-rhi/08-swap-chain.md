# RHI — Swap Chain & Presentation

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/core/rhi/rhi.h` (IRHISwapChain), `dx12/dx12_swapchain.h/.cpp`
> **Depends on**: RHI device, command lists, fences

## Purpose

The swap chain manages back buffers and presentation to a window surface. It's the final link between GPU rendering and what the user sees on screen. UzEngine creates its own HWND with a direct DX12 swap chain — no webview, no frame encoding, no intermediate copies for the primary viewport.

## Configuration

```cpp
RHISwapChainDesc desc{};
desc.windowHandle = hwnd;                    // Native HWND
desc.width        = 1280;
desc.height       = 720;
desc.bufferCount  = 3;                       // Triple-buffered
desc.format       = RHIFormat::RGBA8Unorm;   // Back buffer format
desc.vsync        = true;                    // Vertical sync

IRHISwapChain* swapChain = device->CreateSwapChain(desc);
```

## DX12 Implementation Details

### FLIP_DISCARD Presentation

The DX12 backend uses `DXGI_SWAP_EFFECT_FLIP_DISCARD` — the modern presentation model recommended by Microsoft. It:

- Integrates directly with the Desktop Window Manager (DWM) compositor
- Allows the compositor to discard old frames instead of copying them
- Supports variable refresh rate (VRR) via `DXGI_PRESENT_ALLOW_TEARING`
- Is the only model supported for `IDXGISwapChain4` and newer

### Tearing Support (VRR)

When vsync is disabled and the display supports variable refresh rate (FreeSync/G-Sync), the swap chain queries `DXGI_FEATURE_PRESENT_ALLOW_TEARING` and sets the `DXGI_PRESENT_ALLOW_TEARING` flag on `Present()`. This eliminates the compositor's vsync wait, allowing true uncapped frame rates.

```cpp
// Present behavior:
if (vsync)
    swapChain->Present(1, 0);                     // Sync interval 1 = wait for vblank
else if (tearingSupported)
    swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING); // No wait, tear allowed
else
    swapChain->Present(0, 0);                     // No wait, no tearing flag
```

### Back Buffer Rotation

Triple-buffering means 3 back buffers rotating through the GPU:

```
Frame 0: Render → Buffer[0], Present Buffer[0]
Frame 1: Render → Buffer[1], Present Buffer[1]
Frame 2: Render → Buffer[2], Present Buffer[2]
Frame 3: Render → Buffer[0], Present Buffer[0]  ← Buffer[0] recycled
```

After `Present()`, `GetCurrentBackBufferIndex()` automatically advances. The render loop must re-query the current back buffer each frame:

```cpp
RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
// ... render to backBuffer ...
swapChain->Present(vsync);
```

### Resize

When the window resizes, all back buffers must be recreated:

```cpp
device->WaitIdle();  // GPU must be completely idle
swapChain->Resize(newWidth, newHeight);
// All previous back buffer handles are now INVALID
```

`Resize(0, 0)` is a no-op — handles minimized windows gracefully.

## Frame Lifecycle

A complete frame using the swap chain:

```cpp
// 1. Get current back buffer
RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

// 2. Transition to render target
RHIBarrierDesc barrier{};
barrier.texture     = backBuffer;
barrier.stateBefore = RHIResourceState::Present;
barrier.stateAfter  = RHIResourceState::RenderTarget;
cmdList->ResourceBarrier(&barrier, 1);

// 3. Clear and render
cmdList->ClearRenderTarget(backBuffer, clearColor);
// ... draw calls ...

// 4. Transition to present
barrier.stateBefore = RHIResourceState::RenderTarget;
barrier.stateAfter  = RHIResourceState::Present;
cmdList->ResourceBarrier(&barrier, 1);

// 5. Submit and present
cmdList->End();
cmdList->Execute();
swapChain->Present(vsync);

// 6. Signal fence for this frame
fence->Signal(++fenceValue);
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Swap effect | FLIP_DISCARD | FLIP_DISCARD (Vulkan: FIFO/Mailbox) | FLIP_DISCARD |
| Buffer count | 2 (double) or 3 (triple) configurable | 2 (double) | 3 (triple) fixed |
| HDR | Yes (`DXGI_FORMAT_R16G16B16A16_FLOAT`) | Planned | Planned (RGBA16Float back buffer) |
| VRR/Tearing | Yes | Yes (Vulkan PRESENT_MODE_IMMEDIATE) | Yes (`DXGI_PRESENT_ALLOW_TEARING`) |

## Rules

1. **ALWAYS** transition back buffer: `Present → RenderTarget` before rendering, `RenderTarget → Present` before `Present()`
2. **ALWAYS** call `WaitIdle()` before `Resize()` — the GPU must not be using the back buffers
3. **NEVER** cache back buffer handles across `Resize()` calls — they become invalid
4. **NEVER** call `Present()` from a thread other than the one that created the swap chain
