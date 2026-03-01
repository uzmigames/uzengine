# DirectX 12 Renderer Specification

This specification defines the requirements for the UzEngine DirectX 12 rendering subsystem, responsible for GPU-accelerated 3D rendering of a spinning triangle with HLSL shaders as a proof of concept for the streaming engine.

## ADDED Requirements

### Requirement: DX12 Device Initialization
The system MUST initialize a DirectX 12 device with DXGI factory, hardware adapter enumeration, and feature level 12_0 support.

#### Scenario: Successful device creation on compatible GPU
Given a system with a DirectX 12 compatible GPU
When the renderer initializes
Then a D3D12 device is created with feature level 12_0
And a direct command queue is created
And a command allocator is created for each back buffer

#### Scenario: Debug layer activation in debug builds
Given the application is built in Debug configuration
When the renderer initializes
Then the D3D12 debug layer is enabled for validation

### Requirement: Swap Chain Management
The system MUST create a double-buffered swap chain with DXGI_FORMAT_R8G8B8A8_UNORM format bound to the application window.

#### Scenario: Swap chain creation and presentation
Given a valid Win32 window handle at 1280x720 resolution
When the swap chain is created
Then two back buffers are allocated in DXGI_FORMAT_R8G8B8A8_UNORM
And an RTV descriptor heap is created with two descriptors
And frames can be presented to the window

### Requirement: HLSL Shader Pipeline
The system MUST compile and use HLSL vertex and pixel shaders at runtime via D3DCompile for rendering a colored triangle with MVP transformation.

#### Scenario: Runtime shader compilation
Given embedded HLSL source strings for vertex and pixel shaders
When the renderer initializes the pipeline
Then the vertex shader is compiled with target vs_5_0
And the pixel shader is compiled with target ps_5_0
And compilation errors are reported with descriptive messages

#### Scenario: Vertex shader transforms positions
Given a vertex with position and color attributes
When the vertex shader executes
Then the position is transformed by the worldViewProj matrix from constant buffer b0
And the color is passed through to the pixel shader

### Requirement: Triangle Rendering with Rotation
The system MUST render a triangle with three colored vertices (red, green, blue) that rotates continuously based on elapsed time.

#### Scenario: Spinning triangle at consistent speed
Given the render loop is running
When each frame is rendered
Then the triangle rotation angle is calculated from elapsed time (not frame count)
And the MVP matrix is updated with the current rotation
And the triangle vertices are rendered with per-vertex color interpolation

#### Scenario: Frame synchronization
Given double-buffered rendering
When a frame is submitted
Then a fence is used to synchronize CPU and GPU
And the next frame does not begin until the previous frame's GPU work is complete

### Requirement: Win32 Window Management
The system MUST create and manage a Win32 window at 1280x720 resolution for DX12 rendering output.

#### Scenario: Window creation and message processing
Given the application starts
When the main function executes
Then a window is created at 1280x720 resolution with title "UzEngine PoC"
And the window message pump processes messages without blocking the render loop
And WM_DESTROY triggers application shutdown

### Requirement: Graceful Shutdown
The system MUST cleanly release all DirectX 12 resources on application exit without memory leaks or device errors.

#### Scenario: Clean resource release
Given the application is shutting down
When the shutdown sequence executes
Then the GPU is flushed (all pending commands complete)
And all COM objects (device, swap chain, command queue, PSO, buffers) are released
And no D3D12 debug layer warnings or errors are reported
