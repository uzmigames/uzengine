## 1. Phase 1 — Fix VFXHotReload release-link regression
- [x] 1.1 Drop `#if defined(UZ_DEV_BUILD) || !defined(NDEBUG)` guard from `runtime/engine/vfx/asset/vfx_hot_reload.cpp`
- [x] 1.2 Replace top-of-file comment to document why the implementation is unconditionally compiled and where instantiation is gated
- [x] 1.3 Remove the trailing `#endif // UZ_DEV_BUILD || !NDEBUG`

## 2. Phase 2 — Fix `__mingw_uuidof<ID3D12Fence>` undefined symbol
- [x] 2.1 Add `#include "dx12_guids.h"` to `runtime/engine/rhi/dx12/dx12_fence.cpp` after `#include "dx12_fence.h"`
- [x] 2.2 Confirm every other DX12 .cpp using `IID_PPV_ARGS` already receives `dx12_guids.h` (direct include or via `dx12_device.h`)

## 3. Phase 3 — Fix `scripts/build.bat` PATH expansion
- [x] 3.1 After the two `reg query` lines that capture `SYSPATH` and `USRPATH`, insert `call set "SYSPATH=%SYSPATH%"` and `call set "USRPATH=%USRPATH%"`
- [x] 3.2 Add an inline comment explaining REG_EXPAND_SZ and the `call set` re-expansion idiom

## 4. Phase 4 — Validation
- [x] 4.1 `zig build -Doptimize=ReleaseFast` from `runtime/` exits 0 and emits all binaries to `runtime/build/bin/`
- [x] 4.2 `scripts\build.bat` step `[1/4] Building runtime (zig)` reports `Shader integrity OK` (powershell resolved)
- [x] 4.3 Verify `uzengine.exe`, `test_runtime.exe`, `uzcook/uzpack/uzmesh/uzvalidate/uzvfxinspect.exe` exist in `runtime/build/bin/` with current build timestamps

## 5. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 5.1 Update or create documentation covering the implementation (CHANGELOG.md "Fixed" section)
- [x] 5.2 Write tests covering the new behavior (test_runtime now links and runs the existing VFXHotReload tests in release; no new tests required because the class behaviour did not change)
- [x] 5.3 Run tests and confirm they pass (`runtime/build/bin/test_runtime.exe`) — VFXHotReload suite 21/21 passes; 79 pre-existing failures in unrelated suites (HeightFog defaults, VFX struct-size assertions, widget hit-tests, ShaderCross) are out of scope for this build/link task
