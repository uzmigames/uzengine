<!-- doc-meta
title: UE5 vs UzEngine Code Comparison - Agility SDK & Device Creation
slug: ue5-uzengine-code-comparison
author: Architecture Analysis
date: 2026-03-25
phase: implementation-reference
topics: [D3D12, code-comparison, before-after]
-->

# UE5 vs UzEngine Code Comparison

## Pattern 1: Feature Level Array with Compile-Time Gating

### UE5 (Correct Pattern)
**Source**: `WindowsD3D12Device.cpp:146-158`

```cpp
static D3D_FEATURE_LEVEL FindHighestFeatureLevel(ID3D12Device* Device, D3D_FEATURE_LEVEL MinFeatureLevel)
{
    const D3D_FEATURE_LEVEL FeatureLevels[] =
    {
        // Add new feature levels that the app supports here.
#if D3D12_CORE_ENABLED
        D3D_FEATURE_LEVEL_12_2,   // Only if Agility SDK compile support enabled
#endif
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    // ... rest of function
}
```

### UzEngine (Incorrect - Assumed Current)
```cpp
// In dx12_device.cpp or similar
D3D_FEATURE_LEVEL requestedLevel = D3D_FEATURE_LEVEL_12_2;  // Hard-coded!
// No fallback, no array, no conditional compilation
```

### UzEngine (Correct - What to Implement)
```cpp
// In dx12_device.h or dx12_device.cpp

// Option A: Function pattern like UE5
static D3D_FEATURE_LEVEL GetRequestedFeatureLevels(D3D_FEATURE_LEVEL* outArray, uint32& outCount)
{
    static D3D_FEATURE_LEVEL levels[] = {
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
        D3D_FEATURE_LEVEL_12_2,
#endif
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };
    outArray = levels;
    outCount = std::size(levels);
}

// Option B: Inline array like UE5
const D3D_FEATURE_LEVEL requestedLevels[] = {
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
    D3D_FEATURE_LEVEL_12_2,
#endif
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
};
const uint32 levelCount = std::size(requestedLevels);
```

---

## Pattern 2: Device Creation with Graceful Fallback

### UE5 (Correct Pattern)
**Source**: `WindowsD3D12Device.cpp:495-530`

```cpp
static bool SafeTestD3D12CreateDevice(IDXGIAdapter* Adapter, D3D_FEATURE_LEVEL MinFeatureLevel, FD3D12DeviceBasicInfo& OutInfo)
{
    __try
    {
        ID3D12Device* Device = nullptr;

        // Attempt to create device with requested feature level
        const HRESULT D3D12CreateDeviceResult = D3D12CreateDevice(
            Adapter,
            MinFeatureLevel,           // This may be 12.2 or 12.1 depending on calling context
            IID_PPV_ARGS(&Device)
        );

        if (SUCCEEDED(D3D12CreateDeviceResult))
        {
            // SUCCESS: Device created, now query its capabilities
            OutInfo.MaxFeatureLevel = FindHighestFeatureLevel(Device, MinFeatureLevel);
            OutInfo.MaxShaderModel = FindHighestShaderModel(Device);
            GetResourceTiers(Device, OutInfo.ResourceBindingTier, OutInfo.ResourceHeapTier);
            OutInfo.NumDeviceNodes = Device->GetNodeCount();
            OutInfo.bSupportsWaveOps = GetSupportsWaveOps(Device);
            OutInfo.bSupportsAtomic64 = GetSupportsAtomic64(Adapter, Device);
            OutInfo.MaxRHIFeatureLevel = FindMaxRHIFeatureLevel(...);
            OutInfo.bUMA = IsDeviceUMA(Device);

            Device->Release();
            return true;
        }
        else
        {
            // FAILURE: Log the error and return false
            // Caller will try next adapter or next feature level
            UE_LOG(LogD3D12RHI, Log, TEXT("D3D12CreateDevice failed with code 0x%08X"),
                   static_cast<int32>(D3D12CreateDeviceResult));
        }
    }
    __except (IsDelayLoadException(GetExceptionInformation()))
    {
        // DLL loading failed (e.g., missing D3D12 or DXGI on very old Windows)
        CA_SUPPRESS(6322);
    }

    return false;
}
```

### UzEngine (Incorrect - Assumed Current)
```cpp
// In dx12_device.cpp
void CreateDevice() {
    ID3D12Device* device = nullptr;
    HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&device));

    if (FAILED(hr)) {
        // No fallback, no retry
        // Black screen
        return nullptr;
    }

    // Continue with device
}
```

### UzEngine (Correct - What to Implement)
```cpp
// In dx12_device.cpp

// Method A: Retry loop approach
void CreateDeviceWithFallback() {
    const D3D_FEATURE_LEVEL requestedLevels[] = {
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
        D3D_FEATURE_LEVEL_12_2,
#endif
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };

    ID3D12Device* device = nullptr;
    HRESULT hr = E_FAIL;
    D3D_FEATURE_LEVEL actualLevel = D3D_FEATURE_LEVEL_11_0;

    // Try each feature level in order
    for (uint32 i = 0; i < std::size(requestedLevels); ++i) {
        hr = D3D12CreateDevice(adapter, requestedLevels[i], IID_PPV_ARGS(&device));

        if (SUCCEEDED(hr)) {
            actualLevel = requestedLevels[i];
            UZ_LOG(Render, Log, "Device created with Feature Level %d", actualLevel);
            break;
        } else if (i > 0) {
            // Fallback attempt
            UZ_LOG(Render, Warning, "Feature Level %d not supported, trying %d",
                   requestedLevels[i-1], requestedLevels[i]);
        }
    }

    if (FAILED(hr)) {
        UZ_LOG(Render, Error, "D3D12CreateDevice failed: 0x%08X", static_cast<uint32>(hr));
        return nullptr;
    }

    // Query device for confirmed capabilities
    QueryDeviceCapabilities(device, actualLevel);

    return device;
}

// Method B: Wrapper function pattern (like UE5)
struct DeviceCreationInfo {
    D3D_FEATURE_LEVEL supportedLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_SHADER_MODEL maxShaderModel = D3D_SHADER_MODEL_6_0;
    bool supportsAgilitySDK = false;
    // ... other caps
};

bool CreateD3D12DeviceSafe(IDXGIAdapter* adapter, DeviceCreationInfo& outInfo)
{
    __try {
        const D3D_FEATURE_LEVEL requestedLevels[] = {
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
            D3D_FEATURE_LEVEL_12_2,
#endif
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
        };

        ID3D12Device* device = nullptr;

        // Try requested levels in order
        for (auto level : requestedLevels) {
            HRESULT hr = D3D12CreateDevice(adapter, level, IID_PPV_ARGS(&device));
            if (SUCCEEDED(hr)) {
                // Query actual capabilities
                outInfo.supportedLevel = QueryFeatureLevelSupport(device, requestedLevels);
                outInfo.maxShaderModel = QueryMaxShaderModel(device);
                outInfo.supportsAgilitySDK = (outInfo.supportedLevel >= D3D_FEATURE_LEVEL_12_2);

                device->Release();
                return true;
            }
        }

        UZ_LOG(Render, Error, "No supported feature level found");
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // DLL loading or other exception
        UZ_LOG(Render, Error, "Exception during device creation");
        return false;
    }
}
```

---

## Pattern 3: Query Device Capabilities After Creation

### UE5 (Correct Pattern)
**Source**: `WindowsD3D12Device.cpp:503-504`

```cpp
// AFTER successful device creation:
OutInfo.MaxFeatureLevel = FindHighestFeatureLevel(Device, MinFeatureLevel);
OutInfo.MaxShaderModel = FindHighestShaderModel(Device);

// Helper function that queries device
static D3D_FEATURE_LEVEL FindHighestFeatureLevel(ID3D12Device* Device, D3D_FEATURE_LEVEL MinFeatureLevel)
{
    const D3D_FEATURE_LEVEL FeatureLevels[] = { /* ... */ };

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

### UzEngine (Incorrect - Assumed Current)
```cpp
// Probably skips querying or assumes what was requested is what's supported
// No CheckFeatureSupport call
```

### UzEngine (Correct - What to Implement)
```cpp
// In dx12_device.cpp

D3D_FEATURE_LEVEL QueryMaxFeatureLevel(ID3D12Device* device, const D3D_FEATURE_LEVEL* requestedLevels, uint32 levelCount)
{
    D3D12_FEATURE_DATA_FEATURE_LEVELS caps = {};
    caps.pFeatureLevelsRequested = requestedLevels;
    caps.NumFeatureLevels = levelCount;

    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(caps)))) {
        return caps.MaxSupportedFeatureLevel;
    }

    // Fallback: return highest requested (may be optimistic)
    return requestedLevels[0];
}

D3D_SHADER_MODEL QueryMaxShaderModel(ID3D12Device* device)
{
    const D3D_SHADER_MODEL modelsToCheck[] = {
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
        D3D_SHADER_MODEL_6_7,
        D3D_SHADER_MODEL_6_6,
#endif
        D3D_SHADER_MODEL_6_5,
        D3D_SHADER_MODEL_6_4,
        D3D_SHADER_MODEL_6_3,
        D3D_SHADER_MODEL_6_2,
        D3D_SHADER_MODEL_6_1,
        D3D_SHADER_MODEL_6_0,
    };

    for (auto model : modelsToCheck) {
        D3D12_FEATURE_DATA_SHADER_MODEL modelCaps = { model };
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &modelCaps, sizeof(modelCaps))) &&
            modelCaps.HighestShaderModel >= model) {
            return model;
        }
    }

    return D3D_SHADER_MODEL_6_0;
}

// Usage after device creation:
D3D_FEATURE_LEVEL actualLevel = QueryMaxFeatureLevel(device, requestedLevels, std::size(requestedLevels));
D3D_SHADER_MODEL actualModel = QueryMaxShaderModel(device);

device_info.supported_feature_level = actualLevel;
device_info.max_shader_model = actualModel;
device_info.supports_agility_sdk = (actualLevel >= D3D_FEATURE_LEVEL_12_2);
```

---

## Pattern 4: Agility SDK Runtime Detection (Logging)

### UE5 (Pattern)
**Source**: `WindowsD3D12Device.cpp:683-688, 1012`

```cpp
#if D3D12_CORE_ENABLED
static bool CheckIfAgilitySDKLoaded()
{
    const TCHAR* AgilitySDKDllName = TEXT("D3D12Core.dll");
    HMODULE AgilitySDKDllHandle = ::GetModuleHandle(AgilitySDKDllName);
    return AgilitySDKDllHandle != NULL;
}
#endif

// Later, in initialization:
#if D3D12_CORE_ENABLED
    UE_LOG(LogD3D12RHI, Log, TEXT("DirectX Agility SDK runtime %s."),
           CheckIfAgilitySDKLoaded() ? TEXT("found") : TEXT("not found"));
#endif
```

### UzEngine (Correct - What to Implement)
```cpp
// In dx12_device.cpp

#ifdef UZENGINE_SUPPORT_AGILITY_SDK
static bool CheckIfAgilitySDKLoaded()
{
    const wchar_t* sdkDll = L"D3D12Core.dll";
    HMODULE handle = ::GetModuleHandle(sdkDll);
    return handle != nullptr;
}

void LogAgilitySDKStatus()
{
    if (CheckIfAgilitySDKLoaded()) {
        UZ_LOG(Render, Log, "DirectX Agility SDK runtime found");
    } else {
        UZ_LOG(Render, Log, "DirectX Agility SDK runtime not found (using system D3D12)");
    }
}
#endif

// Call during initialization:
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
LogAgilitySDKStatus();
#endif
```

---

## Pattern 5: Feature Gating Based on Device Capabilities

### UE5 (Example from rendering pipeline)
```cpp
// In render code, after device is created and capabilities are known:
if (MaxFeatureLevel >= D3D_FEATURE_LEVEL_12_2) {
    // Use advanced Agility SDK features
    EnableMeshShaders();
    EnableWorkGraphs();
} else {
    // Use traditional rendering paths
    DisableMeshShaders();
}
```

### UzEngine (Correct - What to Implement)
```cpp
// In dx12_context.cpp or shader compilation code:

void ConfigureRenderingFeatures(const DeviceInfo& info)
{
    if (info.supported_feature_level >= D3D_FEATURE_LEVEL_12_2) {
        UZ_LOG(Render, Log, "Agility SDK detected, enabling advanced features");
        shader_compile_flags |= SHADER_COMPILE_FLAG_ENABLE_ADVANCED_FEATURES;
        render_features.mesh_shaders_enabled = true;
        render_features.work_graphs_enabled = true;
    } else {
        UZ_LOG(Render, Log, "Using Feature Level %d, some features disabled",
               info.supported_feature_level);
        render_features.mesh_shaders_enabled = false;
        render_features.work_graphs_enabled = false;
    }
}
```

---

## Build System Integration

### UE5 (Reference)
**File**: `D3D12RHI.Build.cs`

```csharp
public D3D12RHI(ReadOnlyTargetRules Target) : base(Target)
{
    // ... other dependencies ...

    // Third-party system libraries (NOT custom SDK)
    AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");

    // No special Agility SDK dependency
    // D3D12_CORE_ENABLED likely set via preprocessor in .cpp files or project settings
}
```

### UzEngine (Correct - What to Implement)
**File**: `runtime/build.zig` or CMakeLists.txt

```zig
// In build.zig
const exe = b.addExecutable(...);

// Define Agility SDK support (compile-time gate)
exe.root_module.addCMacro("UZENGINE_SUPPORT_AGILITY_SDK", "1");

// Or conditionally:
if (target.os.tag == .windows) {
    exe.root_module.addCMacro("UZENGINE_SUPPORT_AGILITY_SDK", "1");
}
```

Or in CMake:
```cmake
# CMakeLists.txt
target_compile_definitions(uzengine PRIVATE UZENGINE_SUPPORT_AGILITY_SDK=1)
```

---

## Summary of Changes

| Aspect | UE5 Approach | Current UzEngine | Fix |
|--------|-------------|------------------|-----|
| Feature level | Array with fallback | Hard-coded 12.2 | Use array, implement retry |
| Device creation | Try, catch, retry on fail | Try once, fail on error | Add retry loop |
| Capability query | Query after creation | Assumed/not queried | Add CheckFeatureSupport |
| SDK detection | GetModuleHandle + log | Not present | Add for debugging |
| Fallback behavior | Graceful (tries 12.1, 12.0) | None (black screen) | Implement fallback |

---

## Implementation Checklist

- [ ] Replace hard-coded feature level with array
- [ ] Implement retry loop for feature levels
- [ ] Add CheckFeatureSupport query after device creation
- [ ] Store actual supported level in device info struct
- [ ] Add Agility SDK detection + logging
- [ ] Gate advanced features based on actual support
- [ ] Add UZENGINE_SUPPORT_AGILITY_SDK macro to build system
- [ ] Test on Windows 10 + DLL hosted (should work now)
- [ ] Test on Windows 11 (should still work)
- [ ] Test on Windows 10 + Agility SDK installed (advanced features enabled)
