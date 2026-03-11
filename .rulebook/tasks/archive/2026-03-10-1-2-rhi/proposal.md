# Milestone 1.2 — RHI (Rendering Hardware Interface)

## Why

The engine's rendering must be abstracted from GPU API specifics. A unified RHI interface allows:
- Multiple backend support (DX12 SM6.6, Vulkan, Metal fallbacks)
- Easy testing with mock backends
- Clean separation of high-level rendering from GPU details
- Vendor-specific optimizations without affecting game code

## What Changes

### Core RHI Abstraction Layer
- **`rhi.h`**: Pure virtual C interface (vtable)
  - Device creation/destruction
  - Command list recording and execution
  - Resource management (buffers, textures)
  - Pipeline state objects (PSO)
  - Synchronization (fences, events)

### DX12 SM6.6 Backend
- **`rhi_dx12.c`**: Concrete DX12 implementation
  - DX12Device wrapping IDXGIDevice, ID3D12Device6
  - Command lists via ID3D12CommandList
  - Resource heaps (descriptor, shader-visible)
  - Pipeline state caching (PSO)
  - Fence synchronization for frame pacing

### GPU Resource Management
- **D3D12MemoryAllocator integration**: Named GPU memory tracking
- **Descriptor heap management**: Bindless descriptor support
- **Swapchain**: Double/triple buffered present with resize handling

### Minimal Rendering (Validation)
- Clear screen to animated color (sine wave)
- Validate GPU command execution
- Measure GPU frame time (no stutters)

## Test Coverage
- All RHI operations compile and link
- GPU executes clear color without validation errors
- Frame pacing is consistent (60fps target)
- Swapchain resize works
- No GPU leaks or hangs on shutdown

## Dependencies
- ✅ **1.0** (Logging, Assertions, Platform Window) — DONE
- ✅ **1.1** (Math & Containers) — DONE

## Success Metric
Solid color animates smoothly in engine window at 60fps. No validation errors.
