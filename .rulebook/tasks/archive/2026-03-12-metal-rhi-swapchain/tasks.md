## 1. Implementation

### CAMetalLayer Setup
- [ ] 1.1 Create `metal_swapchain.h` — `MetalSwapChain : public IRHISwapChain` declaration (C++ header)
- [ ] 1.2 Create `metal_swapchain.mm` — Objective-C++ implementation
- [ ] 1.3 Accept `void* nativeWindowHandle` (NSWindow*) in constructor
- [ ] 1.4 Create `CAMetalLayer` and attach to NSWindow's content view
- [ ] 1.5 Configure layer — pixel format (BGRA8Unorm), maximum drawable count (3), framebufferOnly
- [ ] 1.6 Set initial drawable size from window dimensions

### Drawable Management
- [ ] 1.7 Implement `GetCurrentBackBuffer()` — acquire `nextDrawable`, wrap `.texture` as RHITextureHandle
- [ ] 1.8 Implement `GetCurrentBackBufferIndex()` — return frame counter % 3
- [ ] 1.9 Handle drawable acquisition timeout — log warning if `nextDrawable` returns nil
- [ ] 1.10 Track current drawable reference for present

### Presentation
- [ ] 1.11 Implement `Present(bool vsync)` — `commandBuffer->presentDrawable()` + `commit()`
- [ ] 1.12 Control VSync — set `layer.displaySyncEnabled = vsync` (macOS 10.13+)
- [ ] 1.13 Increment frame counter after present

### Resize & Queries
- [ ] 1.14 Implement `Resize(w, h)` — update `layer.drawableSize`
- [ ] 1.15 Implement `GetWidth()` — return `layer.drawableSize.width`
- [ ] 1.16 Implement `GetHeight()` — return `layer.drawableSize.height`
- [ ] 1.17 Implement `GetFormat()` — map `layer.pixelFormat` to `RHIFormat`

### Cleanup
- [ ] 1.18 Implement destructor — release CAMetalLayer, clear drawable references

## 2. Testing

- [ ] 2.1 Test swap chain creation from NSWindow handle
- [ ] 2.2 Test `GetCurrentBackBuffer()` returns valid texture handle
- [ ] 2.3 Test `GetCurrentBackBufferIndex()` rotates 0, 1, 2, 0, ...
- [ ] 2.4 Test `Resize()` updates drawable size
- [ ] 2.5 Test `GetWidth()/GetHeight()` return correct values after resize
- [ ] 2.6 Test `GetFormat()` returns BGRA8Unorm (default)
- [ ] 2.7 Test `Present()` with vsync on and off
- [ ] 2.8 Test drawable nil handling doesn't crash
- [ ] 2.9 Verify 95%+ coverage

## 3. Documentation

- [ ] 3.1 Document CAMetalLayer integration in `docs/engine/03-rhi/15-metal-backend.md`
- [ ] 3.2 Document why CAMetalLayer over MTKView
- [ ] 3.3 Document NSWindow → CAMetalLayer attachment flow
- [ ] 3.4 Document VSync control via displaySyncEnabled
