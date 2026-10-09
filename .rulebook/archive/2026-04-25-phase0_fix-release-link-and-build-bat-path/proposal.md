# Proposal: phase0_fix-release-link-and-build-bat-path

## Why

`zig build -Doptimize=ReleaseFast` failed with two unrelated link errors plus
an environment bug in `scripts/build.bat` that prevented the runtime build
from running through the documented entry point at all.

1. **`uz::VFXHotReload::*` undefined symbols (test_runtime + uzengine).**
   `runtime/engine/vfx/asset/vfx_hot_reload.cpp` was wrapped in
   `#if defined(UZ_DEV_BUILD) || !defined(NDEBUG)`. Zig's `ReleaseFast`
   defines `NDEBUG`, so the implementation TU compiled to an empty object.
   The header still declared every method, and `scene_renderer_impl.h:726`
   declares `std::unique_ptr<VFXHotReload> m_vfxHotReload`. `unique_ptr`
   requires `T`'s destructor to be visible at the *enclosing* class's
   destruction site — even if the unique_ptr is always null in release —
   so the linker emitted a reference to `~VFXHotReload()` that nothing
   defined. Tests under `tests/engine/vfx/test_vfx_hot_reload.cpp` made
   it worse: they call `Initialize`, `Shutdown`, `Poll`, `OnReload`
   unconditionally, producing five additional unresolved symbols.

2. **`__mingw_uuidof<ID3D12Fence>()` undefined symbol (test_runtime + uzengine).**
   Zig on Windows compiles via Clang against MinGW headers, where
   `IID_PPV_ARGS()` expands to `__mingw_uuidof<T>()`. The required
   `__CRT_UUID_DECL` specialisations live in
   `runtime/engine/rhi/dx12/dx12_guids.h`, and the project's stated
   convention is to include that header in every DX12 `.cpp`.
   `dx12_fence.cpp` was the only DX12 TU that did not include it
   (directly or transitively via `dx12_device.h`), so its
   `IID_PPV_ARGS(&m_fence)` call generated an external reference instead
   of inlining the GUID.

3. **`scripts/build.bat` lost `powershell.exe` from PATH.**
   Lines 21–23 read `Path` from registry via `reg query` and assigned
   the raw value to `%PATH%`. The registry key is `REG_EXPAND_SZ`, so
   `%SystemRoot%` was never expanded — the new `PATH` contained the
   literal token instead of `C:\Windows`. `runtime/build.zig:961` calls
   `powershell` for the post-build shader integrity check. With the
   bogus PATH, `zig build` failed with
   `failed to spawn and capture stdio from powershell: FileNotFound`.

Every Zig release build from the documented pipeline is broken by these
three regressions until they are fixed.

## What Changes

- `runtime/engine/vfx/asset/vfx_hot_reload.cpp` — drop the
  `#if defined(UZ_DEV_BUILD) || !defined(NDEBUG)` guard so the
  destructor and all method bodies are always emitted. Document that
  the class is still only *instantiated* in dev builds (callers gate
  the `make_unique<VFXHotReload>()` call), and that DCE strips
  unreferenced bodies in release.
- `runtime/engine/rhi/dx12/dx12_fence.cpp` — add
  `#include "dx12_guids.h"` after `dx12_fence.h`, matching the
  existing convention.
- `scripts/build.bat` — after the `reg query` blocks that capture
  `SYSPATH` / `USRPATH`, insert two `call set "VAR=%VAR%"` lines so
  cmd re-expands embedded environment tokens (`%SystemRoot%` → `C:\Windows`)
  before assigning the combined value to `PATH`.

## Impact

- Affected specs: `runtime/build/release-link-integrity` (new, this task);
  `scripts/build/path-refresh` (new, this task).
- Affected code:
  - `runtime/engine/vfx/asset/vfx_hot_reload.cpp`
  - `runtime/engine/rhi/dx12/dx12_fence.cpp`
  - `scripts/build.bat`
- Breaking change: NO. Release-build behaviour is *restored*; debug
  builds and external API are unchanged.
- User benefit: `scripts/build.bat` and `zig build -Doptimize=ReleaseFast`
  succeed end-to-end on a clean machine, producing
  `runtime/build/bin/uzengine.exe`, `test_runtime.exe`, and the
  `uzcook` / `uzpack` / `uzmesh` / `uzvalidate` / `uzvfxinspect` tools.
