## 1. Frame Readback Implementation
- [x] 1.1 Create readback buffer (D3D12_HEAP_TYPE_READBACK) sized for 1280x720 RGBA
- [x] 1.2 Add resource state transitions for render target to copy source
- [x] 1.3 Implement CopyTextureRegion from render target to readback buffer after rendering
- [x] 1.4 Implement fence-based synchronization to wait for GPU copy completion
- [x] 1.5 Map readback buffer to CPU memory and copy pixels into FrameData struct
- [x] 1.6 Double-buffer readback buffers (one GPU-filling, one CPU-reading) for pipelining

## 2. FPS Counter
- [x] 2.1 Implement high-resolution timer using QueryPerformanceCounter/QueryPerformanceFrequency
- [x] 2.2 Implement rolling average FPS calculation (average over last 60 frames)
- [x] 2.3 Track frame time delta for smooth animation timing

## 3. FPS Overlay Rendering
- [x] 3.1 Create src/renderer/fps_overlay.h with FPSOverlay class interface
- [x] 3.2 Create hardcoded bitmap font data (digits 0-9, letters F/P/S, colon, space) as constant arrays
- [x] 3.3 Implement CPU-side text rasterization: sprintf FPS value, render glyphs to RGBA buffer
- [x] 3.4 Upload rasterized text to a GPU staging texture each frame
- [x] 3.5 Composite FPS overlay onto render target using alpha-blended screen-space quad
- [x] 3.6 Position overlay in top-left corner with readable size and contrasting background

## 4. Integration
- [x] 4.1 Update FrameData struct in types.h with pixel data, dimensions, timestamp, frame number
- [x] 4.2 Integrate FPS overlay into the render pipeline (after triangle draw, before readback copy)
- [x] 4.3 Expose getLatestFrame() method on renderer for downstream encoder consumption

## 5. Testing
- [x] 5.1 Verify readback buffer contains valid RGBA pixel data (not zeroed or garbage)
- [x] 5.2 Verify FPS counter displays reasonable values (25-60+ FPS)
- [x] 5.3 Verify FPS overlay is visible and readable in the rendered frame
- [x] 5.4 Verify readback does not cause significant frame rate drop (< 2ms overhead at 720p)
- [x] 5.5 Verify no GPU validation errors from debug layer during readback operations

## 6. Documentation
- [x] 6.1 Document frame capture pipeline in docs/architecture.md
- [x] 6.2 Document FPS overlay design decisions
