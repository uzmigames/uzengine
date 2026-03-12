# DX11 RHI Backend — Spec Delta

## ADDED Requirements

### Requirement: DX11 Full Interface Parity
The DX11 backend SHALL implement all 4 RHI interfaces (IRHIDevice, IRHICommandList, IRHISwapChain, IRHIFence) with complete method coverage: 29 Device methods, 28 CommandList methods, 7 SwapChain methods, 4 Fence methods. Every method MUST produce functionally equivalent results to the DX12 backend.

#### Scenario: All interfaces implemented
Given the DX11 backend is compiled
When each RHI interface method is called
Then no method throws "not implemented" and all produce correct GPU behavior

### Requirement: DX11 Device Creation
The DX11Device SHALL create a D3D11.1+ device with feature level 11_0 minimum. It MUST enable the debug layer when validation is requested. Adapter selection MUST prefer high-performance GPU (discrete over integrated).

#### Scenario: Device created on Windows 10
Given a Windows 10 machine with DX11-capable GPU
When CreateRHIDevice is called with DX11 backend
Then a D3D11 device is created successfully
And GetAdapterName returns the GPU name
And GetFeatureLevel returns "D3D_FEATURE_LEVEL_11_x"

#### Scenario: Debug layer enabled
Given validation is enabled in RHIDeviceDesc
When the DX11 device is created
Then D3D11 debug layer is active
And API errors produce debug output messages

### Requirement: SM5 Binding Model
The DX11 backend MUST use SM5 slot-based binding. SetDescriptorIndex SHALL map to SRV/CBV slot assignments. SetRootConstants SHALL update a constant buffer bound to the appropriate slot. CreateGraphicsPipeline MUST create D3D11 input layouts from RHIInputElementDesc arrays.

#### Scenario: Vertex input layout created
Given a pipeline desc with 3 input elements (Position, Normal, UV)
When CreateGraphicsPipeline is called on DX11
Then a D3D11 input layout is created with 3 elements
And drawing with this pipeline reads vertex data correctly

#### Scenario: SetDescriptorIndex maps to SRV slot
Given a texture is bound via SetDescriptorIndex(0, srvIndex)
When a draw call samples from the texture
Then the texture is visible in the pixel shader at the mapped SRV slot

### Requirement: DX11 Implicit Barrier Model
The DX11 backend SHALL implement ResourceBarrier as a no-op. D3D11 manages resource state transitions implicitly. The method MUST NOT crash or produce errors regardless of the barrier parameters.

#### Scenario: Barrier is no-op
Given any combination of RHIResourceState transitions
When ResourceBarrier is called on DX11
Then the call completes without error
And no state tracking is required

### Requirement: DX11 Fence Emulation
The DX11 backend SHALL emulate timeline fences using D3D11 event queries and a CPU-side monotonic counter. Signal MUST issue a query. WaitCPU MUST poll the query until complete. IsComplete MUST return the current completion state without blocking.

#### Scenario: Fence synchronization works
Given a DX11 fence with value 0
When Signal(1) is called after GPU work
And WaitCPU(1) is called on the CPU
Then WaitCPU blocks until the GPU work completes
And GetCompletedValue returns 1

#### Scenario: IsComplete is non-blocking
Given GPU work is still in-flight
When IsComplete(pendingValue) is called
Then it returns false without blocking the CPU

### Requirement: DX11 Command List via Deferred Context
The DX11 CommandList SHALL use ID3D11DeviceContext deferred contexts for recording. Begin() MUST create or reset a deferred context. End() MUST finalize the command list. Execute() MUST submit via the immediate context's ExecuteCommandList().

#### Scenario: Command list records and executes
Given a DX11 command list
When Begin, SetPipeline, Draw, End, Execute are called in sequence
Then the draw call is executed on the GPU via immediate context

### Requirement: DX11 SwapChain
The DX11 SwapChain SHALL use DXGI with FLIP_DISCARD on Windows 10+ and FLIP_SEQUENTIAL as fallback. Triple-buffering MUST be supported. Present MUST respect the vsync flag. Tearing MUST be enabled where the system supports it.

#### Scenario: Present with vsync off
Given tearing is supported
When Present(false) is called
Then the frame is presented immediately without waiting for VBlank

#### Scenario: Resize updates back buffers
Given a swapchain at 800x600
When Resize(1920, 1080) is called
Then GetWidth returns 1920 and GetHeight returns 1080
And GetCurrentBackBuffer returns a valid texture handle

### Requirement: Backend Factory Selection
The RHI backend factory SHALL support `--rhi dx11` CLI argument to select the DX11 backend. When DX12 is unavailable, the factory MUST automatically fall back to DX11 without user intervention.

#### Scenario: CLI selects DX11
Given the engine is launched with --rhi dx11
When the RHI device is created
Then the DX11 backend is used regardless of DX12 availability

#### Scenario: Automatic fallback
Given a machine where DX12 device creation fails
When the factory attempts to create an RHI device with auto-detection
Then it falls back to DX11 and logs a warning

### Requirement: Cross-Backend Visual Equivalence
The same RHI draw calls on DX11 and DX12 backends MUST produce visually equivalent rendering output. Minor floating-point differences are acceptable; structural differences (missing geometry, wrong colors, missing textures) are NOT.

#### Scenario: Clear screen matches
Given the same clear color RGBA(0.2, 0.4, 0.6, 1.0)
When rendered on DX11 and DX12
Then both produce the same pixel values (within 1/255 tolerance)
