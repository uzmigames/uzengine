# Proposal: Metal RHI SwapChain (MetalSwapChain)

## Why

The swap chain manages back buffer presentation to the display. On Metal, this is handled via `CAMetalLayer` — a Core Animation layer that vends `MTLDrawable` objects. This component requires Objective-C++ (`.mm` file) because `CAMetalLayer` is an Objective-C class with no metal-cpp equivalent. This is the only Metal RHI file that requires Objective-C.

## What Changes

### New Files

1. **`metal_swapchain.h`** — `MetalSwapChain : public IRHISwapChain` declaration (pure C++ header)
2. **`metal_swapchain.mm`** — Implementation using Objective-C++ for `CAMetalLayer` access

### Metal API Mapping

| IRHISwapChain Method | Metal API |
|---|---|
| `GetCurrentBackBuffer()` | `layer.nextDrawable.texture` → wrap in RHITextureHandle |
| `GetCurrentBackBufferIndex()` | Internal frame counter % buffer count |
| `Present(vsync)` | `commandBuffer->presentDrawable(drawable)` + `commit()` |
| `Resize(w, h)` | `layer.drawableSize = CGSizeMake(w, h)` |
| `GetWidth()` | `layer.drawableSize.width` |
| `GetHeight()` | `layer.drawableSize.height` |
| `GetFormat()` | `layer.pixelFormat` → map to `RHIFormat` |

### Design Decisions

- **CAMetalLayer over MTKView**: `MTKView` is higher-level and assumes UIKit/AppKit integration. `CAMetalLayer` gives direct control over drawable acquisition and present timing, matching our HWND-based approach on Windows.
- **Triple buffering**: `layer.maximumDrawableCount = 3` matches DX12/Vulkan triple-buffered swapchain.
- **VSync control**: `layer.displaySyncEnabled = vsync` on macOS 10.13+.
- **NSWindow integration**: Swap chain receives `NSWindow*` (cast from `void* nativeWindowHandle`), creates a `CAMetalLayer`, and sets it as the window's content view layer.

## Impact

- Affected specs: `docs/engine/03-rhi/15-metal-backend.md`
- Affected code: `runtime/engine/rhi/metal/metal_swapchain.h/.mm` (new)
- Breaking change: NO
- User benefit: Display presentation on macOS/iOS
- Dependencies: `metal-rhi-device`, `metal-rhi-resources`
