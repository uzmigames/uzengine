# Proposal: Metal RHI Backend Stub (Phase 0.3)

## Why

Metal is required for macOS and iOS support. While we cannot test Metal on the current Windows 10 dev machine, creating a compile-ready stub NOW ensures the RHI interface remains compatible with Metal's paradigms (command encoders, MTLLibrary, argument buffers). The stub compiles on all platforms as a placeholder, implements all interface methods with structured "not available" errors, and will be filled in when a Mac is available for testing. This prevents accumulating DX12/Vulkan-only assumptions that would be painful to fix later.

## What Changes

### New Stub Backend

1. **MetalDevice** — Stub implementing all 29 `IRHIDevice` methods. Every method logs a clear error (`UZ_LOG(RHI, Error, "Metal backend not available on this platform")`) and returns a safe default (null handle, zero, empty string). No Objective-C or Metal SDK dependency — pure C++ that compiles on Windows.

2. **MetalCommandList** — Stub implementing all 28 `IRHICommandList` methods. All methods are no-ops with error logging.

3. **MetalSwapChain** — Stub implementing all 7 `IRHISwapChain` methods.

4. **MetalFence** — Stub implementing all 4 `IRHIFence` methods.

5. **Metal TODO Markers** — Each method stub includes a comment block documenting the expected Metal API call (e.g., `// Metal: MTLDevice.makeBuffer(length:options:)`) to guide future implementation.

### Build System

6. **Backend Selection** — `UZ_BACKEND_METAL` compile-time flag. Factory returns error on non-Apple platforms. Stub compiles but selection is rejected at runtime on Windows/Linux.

## Impact

- Affected specs: `docs/engine/03-rhi/` (Metal backend placeholder docs)
- Affected code: `runtime/core/rhi/metal/` (new stubs), `runtime/core/rhi/rhi_backend.h` (factory)
- Breaking change: NO — stub only, no functional changes
- User benefit: RHI interface validated against all 3 major GPU APIs; future Mac port ready
- Enables: macOS/iOS implementation when hardware is available; documents Metal API mapping
