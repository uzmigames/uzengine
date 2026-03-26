# SRP File Split — Spec Delta

## ADDED Requirements

### Requirement: File Size Limit Enforcement
Every source file in `runtime/engine/` and `runtime/demos/common/` MUST NOT exceed 1500 lines. Files that grow beyond this limit SHALL be split into cohesive units following the Single Responsibility Principle.

#### Scenario: No engine file exceeds 1500 lines
Given the SRP file split task is complete
When counting lines in all .cpp/.h files under `runtime/engine/` and `runtime/demos/common/` (excluding `thirdparty/`)
Then no file SHALL exceed 1500 lines

#### Scenario: Split files compile identically
Given a source file has been split into multiple files
When building with `zig build` (and msvc, clang)
Then compilation SHALL succeed with zero errors
And zero new warnings introduced by the split

### Requirement: Umbrella Header Backward Compatibility
The umbrella `rhi.h` header MUST include all four sub-headers (`rhi_fence.h`, `rhi_command_list.h`, `rhi_swapchain.h`, `rhi_device.h`) so that existing `#include "rhi/rhi.h"` directives continue to work without modification.

#### Scenario: Existing includes remain valid
Given `rhi.h` has been split into four sub-headers plus an umbrella
When any translation unit includes `"rhi/rhi.h"`
Then all four interfaces (IRHIFence, IRHICommandList, IRHISwapChain, IRHIDevice) SHALL be available
And no source file outside `engine/rhi/` requires include path changes

### Requirement: Zero Behavioral Changes
Every file split MUST be a pure mechanical refactoring. No function signatures, default values, algorithms, or control flow SHALL change during the split.

#### Scenario: Test suite passes unchanged
Given all 16 files have been split
When running the full test suite via `zig build test`
Then all existing tests SHALL pass with zero regressions
And no test file requires modification due to the split

#### Scenario: Visual output unchanged
Given all renderer files have been split
When running `demos/pbr_test` and capturing a screenshot
Then the output SHALL be pixel-identical to the pre-split screenshot (PSNR > 50 dB)

### Requirement: Build System Integration
Every new `.cpp` file created by a split MUST be added to `build.zig` in the correct module's source list. New header files do NOT need build.zig changes but MUST use the project's include guard convention.

#### Scenario: New cpp files registered in build system
Given `scene_lifecycle.cpp` has been split into 5 new .cpp files
When `build.zig` is read
Then all 5 new file paths SHALL appear in the renderer module's source list
And the original `scene_lifecycle.cpp` entry SHALL be removed

#### Scenario: Include guards follow convention
Given a new header `scene_renderer_frame.h` is created
When reading the file
Then it SHALL use `#pragma once` as its include guard (matching existing project convention)

### Requirement: Internal Headers Not Exposed
Headers suffixed with `_impl.h` (e.g., `scene_renderer_impl.h`, `pipeline_library_impl.h`) MUST only be included by `.cpp` files within the same module. They SHALL NOT be included by files outside their module directory.

#### Scenario: Impl header not leaked to consumers
Given `pipeline_library_impl.h` exists with private member declarations
When searching for `#include.*pipeline_library_impl.h` across the codebase
Then matches SHALL only exist in `engine/renderer/core/pipeline/pipeline_*.cpp` files
And no file outside `engine/renderer/core/pipeline/` SHALL include it
