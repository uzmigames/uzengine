<!-- doc-meta
title: UE5 Agility SDK Implementation & DLL Hosting Limitations
slug: ue5-agility-sdk-research
author: Unreal Architect Agent
date: 2026-03-25
phase: analysis
topics: [D3D12, Agility-SDK, RHI, DLL-hosting, Windows-10-vs-11]
-->

# UE5 Agility SDK Implementation & DLL Hosting Limitations

## Executive Summary

Unreal Engine 5 (5.6.0) does **NOT** explicitly manage the Agility SDK. Instead:
1. **System-managed loading**: D3D12Core.dll is loaded by the OS when D3D12 calls are made
2. **No explicit initialization**: No D3D12SDKVersion/D3D12SDKPath exports, no ID3D12DeviceFactory calls
3. **Windows 10/11 parity**: Same code path on both; SDK availability depends on system installation
4. **DLL hosting limitations**: When UE5 runs as a DLL inside another process, the host process controls SDK loading
5. **No fallback mechanism**: If SDK isn't available, device creation fails; no CPU simulation

## 1. UE5's Agility SDK Detection (Runtime Check)

### Location
`W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12Device.cpp` (lines 683-688)

### Code Pattern
```cpp
#if D3D12_CORE_ENABLED
static bool CheckIfAgilitySDKLoaded()
{
    const TCHAR* AgilitySDKDllName = TEXT("D3D12Core.dll");
    HMODULE AgilitySDKDllHandle = ::GetModuleHandle(AgilitySDKDllName);
    return AgilitySDKDllHandle != NULL;
}
#endif
```

### Key Points

1. **Runtime Check Only**: UE5 calls `GetModuleHandle("D3D12Core.dll")` to check if the SDK is already loaded
2. **No Explicit Loading**: UE5 never calls `LoadLibrary("D3D12Core.dll")` or uses any dynamic SDK loading
3. **Conditional Logging**: At line 1012, UE5 logs SDK status:
   ```cpp
   UE_LOG(LogD3D12RHI, Log, TEXT("DirectX Agility SDK runtime %s."),
          CheckIfAgilitySDKLoaded() ? TEXT("found") : TEXT("not found"));
   ```
4. **D3D12_CORE_ENABLED** is a compile-time flag (not found in public headers; likely set by build config)
   - Gated feature level 12.2 and shader model 6.7/6.6 behind this flag
   - Indicates SDK support is optional at compile time

## 2. Device Creation Path (No SDK-Specific Logic)

### Location
`W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12Device.cpp:495-530`

### Code Pattern
```cpp
static bool SafeTestD3D12CreateDevice(IDXGIAdapter* Adapter, D3D_FEATURE_LEVEL MinFeatureLevel, FD3D12DeviceBasicInfo& OutInfo)
{
    __try
    {
        ID3D12Device* Device = nullptr;
        const HRESULT D3D12CreateDeviceResult = D3D12CreateDevice(Adapter, MinFeatureLevel, IID_PPV_ARGS(&Device));
        if (SUCCEEDED(D3D12CreateDeviceResult))
        {
            // Query feature levels, shader models, resource tiers
            OutInfo.MaxFeatureLevel = FindHighestFeatureLevel(Device, MinFeatureLevel);
            OutInfo.MaxShaderModel = FindHighestShaderModel(Device);
            GetResourceTiers(Device, OutInfo.ResourceBindingTier, OutInfo.ResourceHeapTier);
            // ... more queries
            Device->Release();
            return true;
        }
        else
        {
            UE_LOG(LogD3D12RHI, Log, TEXT("D3D12CreateDevice failed with code 0x%08X"),
                   static_cast<int32>(D3D12CreateDeviceResult));
        }
    }
    __except (IsDelayLoadException(GetExceptionInformation()))
    {
        // Delay-load exception (missing DLL)
    }
    return false;
}
```

### Key Points

1. **Standard D3D12CreateDevice**: UE5 uses the undecorated, system-provided `D3D12CreateDevice` export
2. **No ID3D12DeviceFactory**: UE5 does NOT use the modern SDK runtime factory pattern
3. **Feature Level Fallback**: If the requested feature level (12.2 with Agility SDK) fails, UE5 falls back to querying 12.1/12.0
4. **SEH Exception Handling**: Uses Windows SEH to catch delay-load failures (missing DXGI, D3D12 DLL)
5. **On Failure**: Returns false; adapter is marked as unsupported; caller tries next adapter or fails startup

## 3. DXGI Factory Creation (Standard Path)

### Location
`W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12Device.cpp:120-135`

### Code Pattern
```cpp
static void SafeCreateDXGIFactory(IDXGIFactory4** DXGIFactory)
{
#if !defined(D3D12_CUSTOM_VIEWPORT_CONSTRUCTOR) || !D3D12_CUSTOM_VIEWPORT_CONSTRUCTOR
    __try
    {
        bIsQuadBufferStereoEnabled = FParse::Param(FCommandLine::Get(), TEXT("quad_buffer_stereo"));

        CreateDXGIFactory(__uuidof(IDXGIFactory4), (void**)DXGIFactory);
    }
    __except (IsDelayLoadException(GetExceptionInformation()))
    {
        // Delay-load exception handling
    }
#endif
}
```

### Key Points

1. **Standard CreateDXGIFactory**: No custom factory creation
2. **No SDK Configuration**: No call to ID3D12SDKConfiguration1 or ID3D12DeviceFactory
3. **Delay-Load DLL**: Both CreateDXGIFactory and D3D12CreateDevice are delay-loaded imports
   - This is intentional: Windows XP compatibility wrapper, SEH catches missing DLL

## 4. Feature Level Support (Agility SDK Features)

### Location
`W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12Device.cpp:150-158, 179-189`

### Code Pattern
```cpp
static D3D_FEATURE_LEVEL FindHighestFeatureLevel(ID3D12Device* Device, D3D_FEATURE_LEVEL MinFeatureLevel)
{
    const D3D_FEATURE_LEVEL FeatureLevels[] =
    {
        #if D3D12_CORE_ENABLED
        D3D_FEATURE_LEVEL_12_2,  // Requires Agility SDK
        #endif
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    // Query device to find highest supported level
    D3D12_FEATURE_DATA_FEATURE_LEVELS FeatureLevelCaps{};
    FeatureLevelCaps.pFeatureLevelsRequested = FeatureLevels;
    FeatureLevelCaps.NumFeatureLevels = UE_ARRAY_COUNT(FeatureLevels);

    if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &FeatureLevelCaps, sizeof(FeatureLevelCaps))))
    {
        return FeatureLevelCaps.MaxSupportedFeatureLevel;
    }
    return MinFeatureLevel;
}
```

### Key Points

1. **Compile-Time Gating**: Feature Level 12.2 only included if `D3D12_CORE_ENABLED` is defined
2. **Runtime Query**: Queried via `CheckFeatureSupport`, not pre-determined
3. **Graceful Fallback**: If 12.2 not available, 12.1 is used
4. **No SDK Version Check**: Doesn't verify SDK version; just queries hardware capabilities

## 5. Windows 10 vs Windows 11 Behavior

### The Finding: **IDENTICAL**

UE5 source code has **NO branching** for Windows 10 vs Windows 11 regarding Agility SDK:

```cpp
// Windows version check (line 695)
if (!FPlatformMisc::VerifyWindowsVersion(10, 0, 15063))
{
    UE_LOG(LogD3D12RHI, Warning, TEXT("Missing full support for Direct3D 12. Update to Windows 1703 or newer for D3D12 support."));
    return false;
}
```

This check ensures **minimum Windows 10 (1703)** support, not different SDK handling.

### What Actually Differs (OS Level)

- **Windows 10**: Agility SDK is optional; D3D12Core.dll ships with the SDK binary package
- **Windows 11**: Agility SDK built into OS; D3D12Core.dll pre-loaded by system
- **Windows 10 + DLL Hosting**: **CRITICAL ISSUE** — see section 6

## 6. DLL Hosting Limitation (Why VSCode/Plugin Mode Fails)

### The Problem

When UE5 runs as a DLL inside another process (VSCode extension host, game engine plugin, etc.):
1. **Host process controls SDK loading**
2. **D3D12Core.dll loaded by first D3D12 caller**
3. **VSCode extension host (node.exe on Windows 10)** does NOT load Agility SDK
4. **Result**: When UzEngine.dll is loaded, D3D12Core.dll is NOT in the module list
5. **D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_2, ...)** fails because system D3D12 doesn't have feature level 12.2

### UE5's Behavior in DLL Mode

UE5 has **no special code for DLL hosting**. It:
1. Checks `GetModuleHandle("D3D12Core.dll")` → returns NULL (not loaded by host)
2. Attempts `D3D12CreateDevice(..., D3D_FEATURE_LEVEL_12_2, ...)` → **E_FAIL** (unsupported feature level)
3. Falls back to system D3D12 feature levels (12.1, 12.0, 11.1, 11.0)
4. Device creation succeeds with **system D3D12** (Windows 10 system library)

### Why UzEngine Sees This as Black Screen

UzEngine's `dx12_device.cpp` likely:
1. Requests Feature Level 12.2 hard-coded
2. Doesn't have graceful fallback like UE5 does
3. Fails device creation; returns nullptr
4. Render pipeline never initializes

## 7. What UE5 Does NOT Do

### Static SDK Exports

UE5 **does not** export static symbols like:
```cpp
// NOT in UE5 source
extern "C" uint32 D3D12SDKVersion = SOME_VERSION;
extern "C" const char* D3D12SDKPath = "...";
```

**Why**: These exports are used by **host applications** to tell their loaded D3D12 about SDK version. UE5 is a client of D3D12, not a host that provisions it.

### ID3D12DeviceFactory / ID3D12SDKConfiguration1

UE5 **does not** use:
```cpp
// NOT in UE5 code
ID3D12SDKConfiguration1* SDKConfig = nullptr;
D3D12GetSDKVersion(&SDKVersion);  // Not called
SDKConfig->SetSDKVersion(...);     // Not called
```

**Why**: These APIs are for applications that want to **load a specific SDK version at runtime**. UE5 relies on whatever D3D12 is available (system or Agility SDK, depending on Windows version and host).

### LoadLibrary for D3D12Core.dll

UE5 **does not** explicitly load the Agility SDK:
```cpp
// NOT in UE5 code
HMODULE h = LoadLibrary(L"D3D12Core.dll");  // UE5 does NOT do this
```

**Why**: Delay-load imports from the SDK would require special handling. UE5 relies on OS module loading.

## 8. Build System: D3D12_CORE_ENABLED

### Where It's Used

1. **Feature Level 12.2 gating** (line 152)
2. **Shader Model 6.7/6.6 gating** (line 180)
3. **Agility SDK check logging** (line 683, 1012)

### Where It's Defined

Not found in public headers. Likely:
- Set in build configuration
- Platform-specific compiler flag
- Or `#define D3D12_CORE_ENABLED 1` in `D3D12RHIPrivate.h` (not accessible in partial source dump)

### Implication

When compiling UE5 with `D3D12_CORE_ENABLED=1`:
- Feature Level 12.2 becomes available
- But still gracefully falls back if not supported
- **No hard requirement for Agility SDK**

## 9. UzEngine Adaptation Strategy

### What UzEngine Should Do

1. **Remove hard-coded Feature Level 12.2 requirement**
   ```cpp
   // WRONG (current UzEngine behavior)
   D3D_FEATURE_LEVEL minLevel = D3D_FEATURE_LEVEL_12_2;

   // CORRECT (UE5 pattern)
   const D3D_FEATURE_LEVEL requestedLevels[] = {
   #ifdef UZENGINE_SUPPORT_AGILITY_SDK
       D3D_FEATURE_LEVEL_12_2,
   #endif
       D3D_FEATURE_LEVEL_12_1,
       D3D_FEATURE_LEVEL_12_0,
   };
   ```

2. **Implement graceful fallback**
   - Don't fail device creation if 12.2 isn't available
   - Query device for highest supported level
   - Adjust rendering features based on actual support

3. **For DLL Hosting (VSCode)**
   - Accept that Agility SDK won't be loaded by host
   - Use whatever feature level is available
   - Document that Feature Level 12.1 is minimum
   - Feature Level 12.0 for Windows 10 system D3D12

4. **No Static Exports Needed**
   - UzEngine is **not** a D3D12 provider
   - No need for D3D12SDKVersion/D3D12SDKPath
   - Host process controls SDK availability

### Current UzEngine Bug

In `runtime/engine/rhi/dx12/dx12_device.cpp`:
- Likely hard-coding `D3D_FEATURE_LEVEL_12_2` without fallback
- When `D3D12CreateDevice` fails with E_UNSUPPORTED_LEVEL, device creation fails
- No retry with lower feature level

## 10. Data Flow: SDK Detection → Device Creation → Feature Selection

```
Windows 10 (DLL Hosted in VSCode)
│
├─ LoadLibrary("UzEngine.dll")
│  └─ rx_create_device()
│     ├─ GetModuleHandle("D3D12Core.dll") → NULL
│     │  └─ [Log: "Agility SDK not found"]
│     ├─ CreateDXGIFactory() → SUCCESS (system DXGI)
│     ├─ Enumerate adapters
│     └─ D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, ...)
│        └─ E_FAIL (12.2 requires Agility SDK or Win11)
│        └─ INCORRECT: Return error
│        └─ CORRECT: Retry with D3D_FEATURE_LEVEL_12_1
│           └─ SUCCESS (system D3D12 supports this)
│
Windows 11 (Native)
│
├─ D3D12Core.dll pre-loaded by OS
├─ GetModuleHandle("D3D12Core.dll") → HMODULE
│  └─ [Log: "Agility SDK found"]
├─ CreateDXGIFactory() → SUCCESS
├─ D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, ...)
└─ SUCCESS
```

## 11. Key Findings Summary

| Question | Answer | Evidence |
|----------|--------|----------|
| Does UE5 bundle Agility SDK? | No | No LoadLibrary calls; only runtime check |
| How does UE5 activate SDK? | Via system loading; no explicit activation | GetModuleHandle, no factory initialization |
| D3D12SDKVersion/Path exports? | No | Not found in source |
| Runtime API usage (DeviceFactory)? | No | Uses standard D3D12CreateDevice |
| Windows 10 vs 11 handling? | Identical code path | No branching; feature level 12.2 is optional |
| Fallback when SDK missing? | Yes, gracefully | Tries lower feature levels if 12.2 fails |
| DLL hosting limitation? | Critical | Host process controls SDK; VSCode doesn't load it |

## 12. References

- **File**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12Device.cpp`
  - Lines 683-688: Agility SDK check
  - Lines 495-530: Device creation with graceful fallback
  - Lines 150-158: Feature level selection
  - Line 1012: SDK status logging
- **Build**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\D3D12RHI.Build.cs`
  - Depends on "DX12" third-party (system D3D12/DXGI)
  - No explicit Agility SDK dependency
