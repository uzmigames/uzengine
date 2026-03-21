/// DX12 Agility SDK Activation
///
/// Exports the required symbols for the D3D12 Agility SDK runtime.
/// The Agility SDK embeds a newer DX12 runtime than the OS provides,
/// enabling SM6.6 features on drivers that support the hardware but
/// have an older system runtime (e.g., Windows 10 without updates).
///
/// Requirements:
/// - D3D12Core.dll and d3d12SDKLayers.dll in a "D3D12/" subdirectory
///   next to the executable
/// - These exports must be linked into every executable (not the static lib)

#ifdef _WIN32

#include <d3d12.h>

/// Agility SDK version — must match the SDK in thirdparty/agility.
extern "C" __declspec(dllexport) extern const unsigned int D3D12SDKVersion = D3D12_SDK_VERSION;

/// Path to Agility SDK DLLs, relative to the executable.
/// DLLs are deployed to build/lib/D3D12/ (exe is in build/bin/).
extern "C" __declspec(dllexport) extern const char* D3D12SDKPath = "..\\lib\\D3D12\\";

#endif // _WIN32
