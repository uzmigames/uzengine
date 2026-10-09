# Release Build Integrity — Link Symbols and PATH Refresh

## ADDED Requirements

### Requirement: VFXHotReload Implementation Visible in Release Builds
The runtime SHALL emit the full implementation of `uz::VFXHotReload`
(constructor, destructor, `Initialize`, `Shutdown`, `Poll`, `OnReload`,
`ReloadGraph`, `ReloadDescendants`, `ScanDirectory`, `UpdateInheritanceLink`,
`NormalizePath`) in every build configuration, including
`-Doptimize=ReleaseFast` (which defines `NDEBUG`). The implementation
file `runtime/engine/vfx/asset/vfx_hot_reload.cpp` MUST NOT be guarded by
`#if defined(UZ_DEV_BUILD) || !defined(NDEBUG)` or any equivalent
preprocessor predicate that would elide its contents in release.

#### Scenario: Release link of test_runtime
Given Zig is invoked with `-Doptimize=ReleaseFast`
When the linker resolves `runtime/tests/engine/vfx/test_vfx_hot_reload.cpp`
Then every `VFXHotReload::*` symbol referenced by the tests MUST resolve
And the link step MUST exit 0

#### Scenario: Release link of uzengine.exe
Given Zig is invoked with `-Doptimize=ReleaseFast`
When the linker resolves the SceneRenderer destructor
Then `uz::VFXHotReload::~VFXHotReload()` MUST be defined
And the implicit `unique_ptr<VFXHotReload>::~unique_ptr()` chain MUST link

#### Scenario: Dev-only instantiation gating preserved
Given a release build of the runtime
When `SceneRenderer::SetupForRendering` runs
Then `m_vfxHotReload = std::make_unique<VFXHotReload>()` MUST remain inside
its existing `#if !defined(NDEBUG)` block
And the unique_ptr MUST stay null in release so DCE can strip the unused
method bodies from the final binary

### Requirement: DX12 .cpp Files Include dx12_guids.h
Every `runtime/engine/rhi/dx12/*.cpp` translation unit that uses
`IID_PPV_ARGS()` or any `__uuidof<T>` expansion against a D3D12 / DXGI
interface SHALL include `dx12_guids.h` either directly or transitively
through `dx12_device.h`. Translation units that do not transitively
receive `dx12_guids.h` MUST include it explicitly.

#### Scenario: dx12_fence.cpp on a non-MSVC toolchain
Given Zig (Clang + MinGW headers) compiles `dx12_fence.cpp`
When `IID_PPV_ARGS(&m_fence)` expands inside `DX12Fence::Init`
Then the `__CRT_UUID_DECL(ID3D12Fence, ...)` specialisation from
`dx12_guids.h` MUST be visible to the compiler
And the resulting object MUST NOT contain an unresolved
`__mingw_uuidof<ID3D12Fence>()` reference

#### Scenario: Convention enforcement during code review
Given a new `runtime/engine/rhi/dx12/<name>.cpp` is added
When the file uses `IID_PPV_ARGS()` or `ComPtr<...>::As()`
Then the file MUST include `dx12_guids.h` after `d3d12.h` and `dxgi1_6.h`
And reviewers MUST flag the omission before merge

### Requirement: build.bat PATH Refresh Expands Registry Tokens
`scripts/build.bat` SHALL expand `REG_EXPAND_SZ` tokens (e.g. `%SystemRoot%`,
`%USERPROFILE%`) when refreshing `PATH` from
`HKLM\...\Session Manager\Environment` and `HKCU\Environment`. After
the refresh, the resulting `PATH` MUST allow child processes (notably
`zig.exe` and the Zig build graph) to spawn `powershell.exe`,
`cmd.exe`, and other system binaries without `FileNotFound` errors.

#### Scenario: PATH refresh on a fresh shell
Given `scripts/build.bat` is invoked from a `cmd.exe` whose `PATH` does
not yet include `C:\Windows\System32\WindowsPowerShell\v1.0`
When the registry refresh block at the top of `build.bat` runs
Then the resulting `PATH` MUST contain a fully expanded
`C:\Windows\System32\WindowsPowerShell\v1.0` (or equivalent
`%SystemRoot%`-derived directory)
And subsequent `zig build` invocations MUST resolve `powershell` for
the post-build shader integrity check

#### Scenario: Empty user PATH
Given the current user has no `HKCU\Environment\Path` entry
When the registry refresh block runs
Then `USRPATH` MUST be empty (or absent) without aborting the script
And `PATH` MUST still contain the expanded system PATH
