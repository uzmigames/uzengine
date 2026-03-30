<!-- doc-meta
title: UE5 Source Code Reference Map for D3D12/Agility SDK
slug: ue5-code-reference-map
author: Architecture Analysis
date: 2026-03-25
phase: reference
topics: [UE5, D3D12, code-locations, RHI]
-->

# UE5 Source Code Reference Map for D3D12 & Agility SDK

## Complete File List with Function Locations

### Main Device Creation & Agility SDK

| Function | File | Lines | Purpose |
|----------|------|-------|---------|
| `CheckIfAgilitySDKLoaded()` | `WindowsD3D12Device.cpp` | 683-688 | Runtime check for D3D12Core.dll |
| `SafeTestD3D12CreateDevice()` | `WindowsD3D12Device.cpp` | 495-530 | Device creation with SEH exception handling |
| `SafeCreateDXGIFactory()` | `WindowsD3D12Device.cpp` | 120-135 | DXGI factory creation with fallback |
| `FindHighestFeatureLevel()` | `WindowsD3D12Device.cpp` | 146-171 | Query device for max supported feature level |
| `FindHighestShaderModel()` | `WindowsD3D12Device.cpp` | 173-200 | Query device for max shader model |
| `FindAdapter()` | `WindowsD3D12Device.cpp` | 790+ | Enumerate adapters and find best one |

### Feature Level Selection

| Code Block | File | Lines | What It Does |
|-----------|------|-------|--------------|
| Feature level array (with 12_2 gate) | `WindowsD3D12Device.cpp` | 150-158 | Defines `D3D_FEATURE_LEVEL[]` with conditional 12_2 |
| Shader model array (with 6.7/6.6 gate) | `WindowsD3D12Device.cpp` | 179-189 | Defines `D3D_SHADER_MODEL[]` with conditional 6_7/6_6 |
| Feature level to string conversion | `WindowsD3D12Device.cpp` | 750-770 | Converts feature level enum to text for logging |

### Initialization & Logging

| Code Block | File | Lines | Purpose |
|-----------|------|-------|---------|
| Agility SDK status logging | `WindowsD3D12Device.cpp` | 1011-1013 | Logs "SDK found" or "not found" |
| Adapter chosen logging | `WindowsD3D12Device.cpp` | 1015-1024 | Logs adapter name, vendor ID, device ID |
| Windows version check | `WindowsD3D12Device.cpp` | 695-699 | Requires Windows 10 1703 (build 15063) minimum |

## Detailed Code Snippets for Reference

### 1. Feature Level Array with Compile-Time Gating

**File**: `WindowsD3D12Device.cpp:146-171`

```cpp
static D3D_FEATURE_LEVEL FindHighestFeatureLevel(ID3D12Device* Device, D3D_FEATURE_LEVEL MinFeatureLevel)
{
    // Add new feature levels that the app supports here.
#if D3D12_CORE_ENABLED
    D3D_FEATURE_LEVEL_12_2,  // ← Agility SDK required
#endif
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0
};

// Rest of function queries device via CheckFeatureSupport
D3D12_FEATURE_DATA_FEATURE_LEVELS FeatureLevelCaps{};
FeatureLevelCaps.pFeatureLevelsRequested = FeatureLevels;
FeatureLevelCaps.NumFeatureLevels = UE_ARRAY_COUNT(FeatureLevels);

if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &FeatureLevelCaps, sizeof(FeatureLevelCaps))))
{
    return FeatureLevelCaps.MaxSupportedFeatureLevel;
}

return MinFeatureLevel;
```

### 2. Device Creation with Exception Handling

**File**: `WindowsD3D12Device.cpp:495-530`

```cpp
static bool SafeTestD3D12CreateDevice(IDXGIAdapter* Adapter, D3D_FEATURE_LEVEL MinFeatureLevel, FD3D12DeviceBasicInfo& OutInfo)
{
    __try
    {
        ID3D12Device* Device = nullptr;
        const HRESULT D3D12CreateDeviceResult = D3D12CreateDevice(Adapter, MinFeatureLevel, IID_PPV_ARGS(&Device));
        if (SUCCEEDED(D3D12CreateDeviceResult))
        {
            OutInfo.MaxFeatureLevel = FindHighestFeatureLevel(Device, MinFeatureLevel);
            OutInfo.MaxShaderModel = FindHighestShaderModel(Device);
            GetResourceTiers(Device, OutInfo.ResourceBindingTier, OutInfo.ResourceHeapTier);
            OutInfo.NumDeviceNodes = Device->GetNodeCount();

            OutInfo.bSupportsWaveOps = GetSupportsWaveOps(Device);
            OutInfo.bSupportsAtomic64 = GetSupportsAtomic64(Adapter, Device);

            OutInfo.MaxRHIFeatureLevel = FindMaxRHIFeatureLevel(OutInfo.MaxFeatureLevel, OutInfo.MaxShaderModel, OutInfo.ResourceBindingTier, OutInfo.bSupportsWaveOps, OutInfo.bSupportsAtomic64);

            OutInfo.bUMA = IsDeviceUMA(Device);

            Device->Release();
            return true;
        }
        else
        {
            UE_LOG(LogD3D12RHI, Log, TEXT("D3D12CreateDevice failed with code 0x%08X"), static_cast<int32>(D3D12CreateDeviceResult));
        }
    }
    __except (IsDelayLoadException(GetExceptionInformation()))
    {
        CA_SUPPRESS(6322);  // Suppress "empty except block" warning
    }

    return false;
}
```

**Key Pattern**:
- Wraps D3D12CreateDevice in SEH (__try/__except)
- Catches delay-load exceptions (missing DLL)
- Queries feature support AFTER device creation succeeds
- Returns false if creation fails; caller tries next adapter

### 3. Agility SDK Runtime Detection

**File**: `WindowsD3D12Device.cpp:683-688`

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

**Key Pattern**:
- Simple `GetModuleHandle` check (NO LoadLibrary)
- Checks if D3D12Core.dll is already loaded by system
- Gated by `D3D12_CORE_ENABLED` compile flag

### 4. Agility SDK Status Logging

**File**: `WindowsD3D12Device.cpp:1011-1013`

```cpp
#if D3D12_CORE_ENABLED
    UE_LOG(LogD3D12RHI, Log, TEXT("DirectX Agility SDK runtime %s."), CheckIfAgilitySDKLoaded() ? TEXT("found") : TEXT("not found"));
#endif
```

**Key Pattern**:
- Informational logging only
- No action taken based on result
- Device creation proceeds regardless

### 5. DXGI Factory Creation with Fallback

**File**: `WindowsD3D12Device.cpp:120-135`

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
        CA_SUPPRESS(6322);
    }
#endif
}
```

**Key Pattern**:
- Standard `CreateDXGIFactory` (no custom SDK factory)
- Uses SEH for delay-load exception handling
- No D3D12SDKConfiguration calls

### 6. Shader Model Array with Compile-Time Gating

**File**: `WindowsD3D12Device.cpp:173-200`

```cpp
static D3D_SHADER_MODEL FindHighestShaderModel(ID3D12Device* Device)
{
    const D3D_SHADER_MODEL ShaderModelsToCheck[] =
    {
#if D3D12_CORE_ENABLED
        D3D_SHADER_MODEL_6_7,  // ← Agility SDK required
        D3D_SHADER_MODEL_6_6,  // ← Agility SDK required
#endif
        D3D_SHADER_MODEL_6_5,
        D3D_SHADER_MODEL_6_4,
        D3D_SHADER_MODEL_6_3,
        D3D_SHADER_MODEL_6_2,
        D3D_SHADER_MODEL_6_1,
        D3D_SHADER_MODEL_6_0,
    };

    // Query in descending order, return first that succeeds
    for (D3D_SHADER_MODEL ShaderModel : ShaderModelsToCheck)
    {
        D3D12_FEATURE_DATA_SHADER_MODEL ShaderModelCaps = { ShaderModel };
        if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &ShaderModelCaps, sizeof(ShaderModelCaps))) && ShaderModelCaps.HighestShaderModel >= ShaderModel)
        {
            return ShaderModel;
        }
    }

    return D3D_SHADER_MODEL_6_0;
}
```

## Patterns to Implement in UzEngine

### Pattern 1: Feature Level Array with Fallback

```cpp
// Source: UE5 WindowsD3D12Device.cpp:150-158
const D3D_FEATURE_LEVEL requestedLevels[] = {
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
    D3D_FEATURE_LEVEL_12_2,
#endif
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
};
```

### Pattern 2: Query Device After Creation

```cpp
// Source: UE5 WindowsD3D12Device.cpp:495-516
ID3D12Device* device = nullptr;
HRESULT hr = D3D12CreateDevice(adapter, requestedLevels[0], IID_PPV_ARGS(&device));
if (SUCCEEDED(hr)) {
    D3D12_FEATURE_DATA_FEATURE_LEVELS caps = {};
    caps.pFeatureLevelsRequested = requestedLevels;
    caps.NumFeatureLevels = std::size(requestedLevels);

    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(caps)))) {
        actual_level = caps.MaxSupportedFeatureLevel;
    }
}
```

### Pattern 3: SEH Exception Handling

```cpp
// Source: UE5 WindowsD3D12Device.cpp:497-527
__try
{
    hr = D3D12CreateDevice(...);
}
__except (IsDelayLoadException(GetExceptionInformation()))
{
    // Missing DLL, etc.
}
```

### Pattern 4: Agility SDK Detection (Logging Only)

```cpp
// Source: UE5 WindowsD3D12Device.cpp:683-688
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
HMODULE sdk = ::GetModuleHandle(L"D3D12Core.dll");
if (sdk != nullptr) {
    UZ_LOG(Render, Log, "Agility SDK found");
} else {
    UZ_LOG(Render, Log, "Agility SDK not found (using system D3D12)");
}
#endif
```

## Build System Integration

**File**: `D3D12RHI/D3D12RHI.Build.cs`

```csharp
public D3D12RHI(ReadOnlyTargetRules Target) : base(Target)
{
    // Dependencies
    PublicDependencyModuleNames.AddAll(
        "Core",
        "RHI"
    );

    // Third-party (system D3D12/DXGI, NOT custom SDK distribution)
    AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
}
```

**Key Point**: UE5 depends on system D3D12/DXGI libraries, not a custom Agility SDK distribution.

## No Code Patterns Found

### D3D12SDKVersion / D3D12SDKPath Exports
- **Not used in UE5 RHI**
- These are for D3D12 **hosts** to declare their version
- UE5 is a D3D12 **client**, not a host

### ID3D12DeviceFactory / ID3D12SDKConfiguration1
- **Not used in UE5 RHI**
- These are for applications that want to **dynamically load SDK versions**
- UE5 uses whatever is available (system or Agility SDK)

### LoadLibrary for D3D12Core.dll
- **Not done in UE5**
- Delay-load imports handle this automatically
- OS module loading is automatic and cleaner

### Explicit SDK Version Management
- **Not done in UE5**
- No calls to `D3D12GetSDKVersion()` or `SetSDKVersion()`
- Adapts to available SDK at runtime via feature level queries

## Summary

UE5's approach is:
1. **Request multiple feature levels** in order (12.2, 12.1, 12.0, ...)
2. **Let D3D12CreateDevice pick the best one** it supports
3. **Query device afterwards** to confirm capabilities
4. **Gate advanced features** (shaders, mesh shaders, etc.) based on actual support
5. **Log SDK detection** for debugging (informational only)
6. **Handle delays-load exceptions** gracefully

This allows UE5 to work on:
- Windows 11 (Agility SDK always available)
- Windows 10 + SDK installed (Agility SDK available)
- Windows 10 + SDK not installed (falls back to system D3D12)
- DLL hosted scenarios (host controls SDK, not the DLL)

**All with the same code, no branches, no hard requirements.**
