# Renderer Specification - Frame Readback and FPS Overlay

This specification extends the DirectX 12 renderer with frame readback capabilities for CPU-side access to rendered pixels and an FPS overlay for visual performance monitoring.

## ADDED Requirements

### Requirement: GPU Frame Readback
The system MUST copy each rendered frame from the GPU render target to a CPU-accessible readback buffer using DX12 fence-based synchronization.

#### Scenario: Successful frame readback to CPU memory
Given the renderer has completed drawing a frame
When the readback operation executes
Then the render target is transitioned to D3D12_RESOURCE_STATE_COPY_SOURCE
And CopyTextureRegion copies the frame to a readback buffer
And a fence signal ensures the GPU copy is complete before CPU access
And the mapped readback buffer contains valid RGBA pixel data matching the rendered frame

#### Scenario: Double-buffered readback pipelining
Given the renderer uses double-buffered readback
When frame N is being copied by the GPU
Then the CPU can simultaneously read frame N-1
And no frame data corruption occurs from concurrent access

### Requirement: FPS Counter Measurement
The system MUST measure frame rate using a high-resolution timer and report a rolling average FPS value.

#### Scenario: Accurate FPS calculation
Given the render loop is running
When 60 frames have been rendered
Then the FPS value represents the rolling average over the last 60 frames
And the measurement uses QueryPerformanceCounter for sub-millisecond accuracy

### Requirement: FPS Overlay Rendering
The system MUST render an FPS counter label directly onto the rendered frame using a CPU-side bitmap font renderer.

#### Scenario: FPS text visible on rendered frame
Given the renderer is producing frames
When each frame is composited
Then the FPS overlay displays "FPS: XX" in the top-left corner
And the text is rendered using a bitmap font with alpha blending
And the overlay has a contrasting background for readability
And the overlay is composited before the readback copy so it appears in captured frames

### Requirement: Frame Data Export
The system MUST expose captured frame data via a FrameData structure containing RGBA pixels, dimensions, timestamp, and frame number.

#### Scenario: Complete frame data available for encoder
Given a frame has been captured via readback
When getLatestFrame() is called
Then a FrameData struct is returned with width=1280, height=720
And the pixels array contains RGBA data in row-major order
And the timestamp reflects microseconds since stream start
And the frameNumber increments monotonically
