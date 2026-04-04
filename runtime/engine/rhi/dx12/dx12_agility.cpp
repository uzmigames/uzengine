/// DX12 Agility SDK Activation (EXE builds only).
///
/// Exports D3D12SDKVersion and D3D12SDKPath so the Windows D3D12 loader
/// picks up D3D12Core.dll from the executable directory. This enables
/// SM6.6+ and Feature Level 12_2 on Windows 10.
///
/// This file is compiled ONLY into standalone executables (tools, demos)
/// via explicit addCSourceFiles in build.zig / build_common.zig.
/// It is NEVER included in the static lib or DLL — the addCppDir helper
/// skips it, and the DLL build does not reference it.

#ifdef _WIN32

#include <d3d12.h>

extern "C" __declspec(dllexport) const unsigned int D3D12SDKVersion = D3D12_SDK_VERSION;
extern "C" __declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";

#endif // _WIN32
