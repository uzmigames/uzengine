# RHI Core Interface Specification

## Overview
The RHI (Rendering Hardware Interface) provides a unified GPU abstraction layer. All game code accesses GPU via the RHI interface. Backend implementations (DX12, Vulkan, Metal) are swappable without game code changes.

## File Structure
```
runtime/src/rhi/
├── rhi.h                   # Pure virtual C interface
├── rhi_backend.h           # Backend registration
├── rhi_dx12.c              # DX12 SM6.6 backend
└── rhi_vulkan.c            # Vulkan backend (future)
```

## Key Types & Operations

### RHI Device Interface
```cpp
/// Rendering device abstraction
struct rhi_device_t { /* opaque */ };
typedef rhi_device_t* RHIDevice;

/// Device creation parameters
struct rhi_device_desc {
    uint32_t feature_level;     // DX12: 60 = SM6.0, 61 = SM6.1, 62 = SM6.2, 63 = SM6.3, 64 = SM6.4, 65 = SM6.5, 66 = SM6.6
    bool validation_enabled;
    bool gpu_based_validation;
};

/// Create rendering device
RHIDevice rhi_device_create(const rhi_device_desc* desc, uz_log_category category);

/// Destroy device and all resources
void rhi_device_destroy(RHIDevice dev);

/// Get device vendor string (e.g., "NVIDIA RTX 4090")
const char* rhi_device_get_name(RHIDevice dev);
```

### Command List Interface
```cpp
struct rhi_command_list_t { /* opaque */ };
typedef rhi_command_list_t* RHICommandList;

/// Begin recording commands
RHICommandList rhi_command_list_begin(RHIDevice dev);

/// Clear render target to color
void rhi_command_list_clear(RHICommandList cmd, float r, float g, float b, float a);

/// Execute recorded commands (GPU fence created)
void rhi_command_list_execute(RHIDevice dev, RHICommandList cmd);

/// Wait for GPU execution
void rhi_device_wait_idle(RHIDevice dev);
```

### Resource Interface
```cpp
struct rhi_buffer_t { /* opaque */ };
typedef rhi_buffer_t* RHIBuffer;

enum rhi_buffer_usage {
    RHI_BUFFER_VERTEX    = 0x1,
    RHI_BUFFER_INDEX     = 0x2,
    RHI_BUFFER_CONSTANT  = 0x4,
    RHI_BUFFER_STORAGE   = 0x8,
};

/// Create GPU buffer
RHIBuffer rhi_buffer_create(RHIDevice dev, size_t size, enum rhi_buffer_usage usage);

/// Upload data to buffer
void rhi_buffer_upload(RHIDevice dev, RHIBuffer buf, const void* data, size_t size);

/// Release buffer
void rhi_buffer_destroy(RHIDevice dev, RHIBuffer buf);
```

### Swapchain Interface
```cpp
struct rhi_swapchain_t { /* opaque */ };
typedef rhi_swapchain_t* RHISwapchain;

struct rhi_swapchain_desc {
    void* hwnd;              // Win32 HWND
    uint32_t width;
    uint32_t height;
    uint32_t buffer_count;   // 2 = double, 3 = triple
    bool vsync;
};

/// Create swapchain for window
RHISwapchain rhi_swapchain_create(RHIDevice dev, const rhi_swapchain_desc* desc);

/// Present backbuffer to display
void rhi_swapchain_present(RHISwapchain sc);

/// Handle resize (e.g., from WM_SIZE)
void rhi_swapchain_resize(RHISwapchain sc, uint32_t width, uint32_t height);

/// Destroy swapchain
void rhi_swapchain_destroy(RHIDevice dev, RHISwapchain sc);
```

## DX12 Backend Requirements

### ADDED
Device initialization with:
- Feature level auto-detection (try SM6.6, fallback to SM6.0)
- Debug layer registration
- GPU-based validation (if requested)
- Swapchain with DXGI
- Fence and event creation for synchronization

Command list recording:
- Allocator per frame
- Reset on frame boundary
- Barrier insertion for resource transitions

## Design Decisions

### Why Pure C Interface?
- Easy binding to N-API and other languages
- No C++ ABI compatibility issues
- Clear ownership (caller allocates/frees via opaque pointers)

### Why VTable Pattern?
- Single backend active per session (no multi-instance)
- Vtable allows backend swapping at runtime
- Minimal overhead (one pointer dereference per call)

### Resource Lifetime
- Creator is responsible for destruction
- Double-free checks in debug builds
- GPU sync happens on device_wait_idle() or destruction

## Testing Requirements

### Unit Tests
- Device creation/destruction succeeds
- Resource creation with various sizes/alignments
- Swapchain resize doesn't crash
- Validation catches invalid operations

### Integration Tests
- Clear screen renders correctly
- Frame pacing maintains 60fps target
- No GPU hangs or TDR events
- Memory profiler shows no leaks

### Validation
- All operations pass D3D12 debug layer
- GPU hangs detected via timeout
- Invalid state caught at runtime
