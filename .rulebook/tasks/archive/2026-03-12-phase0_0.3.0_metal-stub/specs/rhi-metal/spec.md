# Metal RHI Backend Stub — Spec Delta

## ADDED Requirements

### Requirement: Metal Stub Full Interface Coverage
The Metal stub SHALL implement all 4 RHI interfaces with every method present: 29 Device, 28 CommandList, 7 SwapChain, 4 Fence methods. Every method MUST compile without Metal SDK or Objective-C dependencies. The stub is pure C++.

#### Scenario: Stub compiles on Windows
Given a Windows 10 build environment without Metal SDK
When the Metal stub is compiled with UZ_BACKEND_METAL
Then compilation succeeds with zero errors

### Requirement: Safe Default Returns
Every stub method MUST return a safe default value: null/invalid handles for Create methods, zero for numeric queries, empty string for string queries, false for boolean queries. No method SHALL crash, invoke undefined behavior, or return uninitialized data.

#### Scenario: CreateBuffer returns invalid handle
Given the Metal stub is active
When CreateBuffer is called with any valid desc
Then it returns RHIBufferHandle::Invalid()
And logs an error "Metal backend not available on this platform"

#### Scenario: GetAdapterName returns empty string
Given the Metal stub is active
When GetAdapterName is called
Then it returns "Metal (stub — not available)"

### Requirement: Error Logging on Every Call
Every stub method MUST log exactly one `UZ_LOG(RHI, Error, ...)` message indicating the Metal backend is not available. The message MUST include the method name for debugging.

#### Scenario: Method name in error log
Given the Metal stub is active
When DrawIndexed is called
Then the log contains "Metal backend not available: DrawIndexed"

### Requirement: Metal API Mapping Comments
Every stub method MUST contain a comment documenting the expected Metal API call for future implementation. This serves as a porting guide.

#### Scenario: CreateBuffer has Metal mapping
Given the MetalDevice stub source code
When reviewing the CreateBuffer method
Then it contains a comment like "// Metal: id<MTLBuffer> = [device newBufferWithLength:options:]"

### Requirement: Platform Rejection
The backend factory SHALL reject `--rhi metal` on non-Apple platforms with a clear error message. It MUST NOT crash or silently fall back — the user must be informed that Metal requires macOS/iOS.

#### Scenario: Metal rejected on Windows
Given the engine is launched on Windows with --rhi metal
When the factory attempts to create the backend
Then it returns an error "Metal backend requires macOS or iOS"
And the engine does not create a device

### Requirement: Backend Auto-Detection Priority
The factory auto-detection MUST use platform-dependent priority. On Windows: DX12 → Vulkan → DX11. On Linux: Vulkan only. On macOS: Metal → Vulkan (via MoltenVK). The priority order MUST be documented and configurable via `.uzproject`.

#### Scenario: Windows auto-detection prefers DX12
Given a Windows 10 machine with DX12, Vulkan, and DX11 available
When --rhi auto (default) is used
Then DX12 is selected as the backend

#### Scenario: DX12 failure falls back to Vulkan
Given DX12 device creation fails
When auto-detection runs
Then Vulkan is attempted next
And if Vulkan succeeds, it is used with a warning logged
