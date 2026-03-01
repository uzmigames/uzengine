# Proposal: Add Frame Readback and FPS Overlay

## Why
To stream the rendered content to VSCode, we need to capture the GPU-rendered frames and bring them to CPU-accessible memory. This phase adds the DX12 readback buffer mechanism that copies the render target to system memory after each frame. Additionally, an FPS overlay is critical for the PoC because it provides visual proof that the content is being rendered natively on the GPU in real-time, not pre-recorded or static. The FPS counter rendered directly into the frame proves live GPU rendering and gives performance visibility.

## What Changes
- Add a readback buffer (D3D12_HEAP_TYPE_READBACK) to copy the render target to CPU-readable memory each frame
- Add fence-based synchronization to ensure the GPU copy is complete before CPU reads
- Implement a high-resolution timer for measuring frame rate with rolling average
- Implement a CPU-side bitmap font renderer that rasterizes FPS text into RGBA pixels
- Composite the FPS overlay onto the render target before the readback copy
- Expose the captured frame data (RGBA pixels + metadata) via a FrameData structure for downstream consumption by the encoder

## Impact
- Affected specs: renderer/spec.md (modified)
- Affected code: src/renderer/renderer.h, src/renderer/renderer.cpp, src/renderer/fps_overlay.h, src/renderer/fps_overlay.cpp, src/common/types.h, src/common/timer.h
- Breaking change: NO (additive changes to existing renderer)
- User benefit: Frame capture ready for encoding pipeline, visual FPS counter proves live GPU rendering
