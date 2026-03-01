# Pipeline Specification - End-to-End Streaming Architecture

This specification defines the requirements for the UzEngine streaming pipeline integration, connecting the renderer, encoder, muxer, and network server into a complete real-time streaming system.

## ADDED Requirements

### Requirement: Lock-Free Frame Ring Buffer
The system MUST use a lock-free ring buffer to pass rendered frames from the render thread to the encode thread without blocking either thread.

#### Scenario: Frame transfer between threads
Given the render thread produces a FrameData
When tryPush() is called on the ring buffer
Then the frame is enqueued for the encode thread
And the render thread returns immediately without blocking

#### Scenario: Ring buffer overflow with frame dropping
Given the ring buffer is full (encoder behind)
When tryPush() is called with a new frame
Then the push fails (returns false)
And the frame is dropped
And a warning log is emitted
And the render thread continues without stalling

### Requirement: Three-Thread Pipeline Architecture
The system MUST run the rendering, encoding, and networking on separate threads to prevent any stage from blocking another.

#### Scenario: Render thread produces frames
Given the application is running
When the render thread executes
Then it runs the DX12 render loop at target frame rate
And after each frame readback, it pushes FrameData to the ring buffer
And it never blocks on encoding or network operations

#### Scenario: Encode thread consumes and broadcasts
Given frames are available in the ring buffer
When the encode thread executes
Then it pops a FrameData from the ring buffer
And encodes it to H.264 via the selected encoder
And muxes the NAL units into an fMP4 media segment
And broadcasts the segment via the WebSocket server

### Requirement: Pipeline Orchestration
The system MUST initialize all subsystems in the correct order and shut them down gracefully on application exit.

#### Scenario: Startup sequence
Given the application starts
When main() executes
Then the renderer is initialized first
And the encoder is created via the factory
And the fMP4 muxer is initialized with codec config from the encoder
And the WebSocket server is started
And the encode thread is launched
And the render loop begins

#### Scenario: Graceful shutdown sequence
Given the application receives a quit signal
When shutdown begins
Then the render loop stops
And the encode thread is signaled to exit and joined
And the WebSocket server is stopped
And the encoder is shut down
And the renderer releases all GPU resources
And no threads are left running
