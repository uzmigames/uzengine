# Proposal: Metal RHI Backend Test Suite

## Why

All RHI backends must pass the same test suite to validate interface compliance. The Metal backend needs a comprehensive test suite covering device creation, resource management, command recording, swap chain presentation, and fence synchronization. Tests must achieve 95%+ coverage per CLAUDE.md requirements. The test suite should reuse the existing RHI test infrastructure used by DX12 and Vulkan backends.

## What Changes

### New Test Files

Following the test structure pattern (`tests/` mirrors `engine/`):

1. **`tests/engine/rhi/metal/test_metal_device.cpp`** — Device creation, resource creation/destruction, capability queries
2. **`tests/engine/rhi/metal/test_metal_command_list.cpp`** — Command recording, encoder switching, draw/dispatch
3. **`tests/engine/rhi/metal/test_metal_swapchain.cpp`** — Drawable acquisition, present, resize
4. **`tests/engine/rhi/metal/test_metal_fence.cpp`** — Signal/wait, timeline values, completion queries
5. **`tests/engine/rhi/metal/test_metal_resources.cpp`** — Format conversion, storage mode mapping, resource type validation
6. **`tests/engine/rhi/metal/test_metal_shader_slang.cpp`** — HLSL→MSL compilation, binding translation, error handling

### Test Strategy

- **Unit tests**: Validate individual Metal API wrappers and format conversions
- **Integration tests**: End-to-end render pipeline (create device → create resources → record commands → execute → present)
- **Cross-backend parity tests**: Same test logic runs against DX12, Vulkan, and Metal to verify identical behavior
- **Platform conditional**: Tests compile and run only on macOS (`#if defined(__APPLE__)`)

## Impact

- Affected specs: none (tests are not spec'd)
- Affected code: `tests/engine/rhi/metal/` (new directory, 6 test files)
- Breaking change: NO
- User benefit: Confidence that Metal backend implements all RHI methods correctly
- Dependencies: All other `metal-rhi-*` tasks must be complete
