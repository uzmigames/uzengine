## 1. Frame Readback Implementation
- [ ] 1.1 Create readback buffer (D3D12_HEAP_TYPE_READBACK) sized for 1280x720 RGBA
- [ ] 1.2 Add resource state transitions for render target to copy source
- [ ] 1.3 Implement CopyTextureRegion from render target to readback buffer after rendering
- [ ] 1.4 Implement fence-based synchronization to wait for GPU copy completion
- [ ] 1.5 Map readback buffer to CPU memory and copy pixels into FrameData struct
- [ ] 1.6 Double-buffer readback buffers (one GPU-filling, one CPU-reading) for pipelining

## 2. FPS Counter
- [ ] 2.1 Implement high-resolution timer using QueryPerformanceCounter/QueryPerformanceFrequency
- [ ] 2.2 Implement rolling average FPS calculation (average over last 60 frames)
- [ ] 2.3 Track frame time delta for smooth animation timing

## 3. FPS Overlay Rendering
- [ ] 3.1 Create src/renderer/fps_overlay.h with FPSOverlay class interface
- [ ] 3.2 Create hardcoded bitmap font data (digits 0-9, letters F/P/S, colon, space) as constant arrays
- [ ] 3.3 Implement CPU-side text rasterization: sprintf FPS value, render glyphs to RGBA buffer
- [ ] 3.4 Upload rasterized text to a GPU staging texture each frame
- [ ] 3.5 Composite FPS overlay onto render target using alpha-blended screen-space quad
- [ ] 3.6 Position overlay in top-left corner with readable size and contrasting background

## 4. Integration
- [ ] 4.1 Update FrameData struct in types.h with pixel data, dimensions, timestamp, frame number
- [ ] 4.2 Integrate FPS overlay into the render pipeline (after triangle draw, before readback copy)
- [ ] 4.3 Expose getLatestFrame() method on renderer for downstream encoder consumption

## 5. Testing
- [ ] 5.1 Verify readback buffer contains valid RGBA pixel data (not zeroed or garbage)
- [ ] 5.2 Verify FPS counter displays reasonable values (25-60+ FPS)
- [ ] 5.3 Verify FPS overlay is visible and readable in the rendered frame
- [ ] 5.4 Verify readback does not cause significant frame rate drop (< 2ms overhead at 720p)
- [ ] 5.5 Verify no GPU validation errors from debug layer during readback operations

## 6. Documentation
- [ ] 6.1 Document frame capture pipeline in docs/architecture.md
- [ ] 6.2 Document FPS overlay design decisions
