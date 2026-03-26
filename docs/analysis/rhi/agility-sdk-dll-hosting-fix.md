<!-- doc-meta
title: UzEngine Agility SDK & DLL Hosting Fix Strategy
slug: uzengine-agility-sdk-dll-hosting-fix
author: Architecture Analysis
date: 2026-03-25
phase: implementation-plan
topics: [D3D12, Agility-SDK, DLL-hosting, feature-levels, Windows-10]
-->

# UzEngine Agility SDK & DLL Hosting Fix Strategy

## Problem Statement

**Current State (UzEngine on Windows 10 + DLL Hosted in VSCode)**

1. UzEngine runs as UzEngine.dll inside VSCode extension host (node.exe)
2. VSCode host process does NOT load Agility SDK (D3D12Core.dll)
3. UzEngine `dx12_device.cpp` hard-codes `D3D_FEATURE_LEVEL_12_2` request
4. `D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_2, ...)` fails with E_UNSUPPORTED_FEATURE_LEVEL
5. Device creation returns nullptr → black screen

**Root Cause**

Feature Level 12.2 requires Agility SDK to be loaded. On Windows 10 with DLL hosting, the SDK is not loaded, so the request fails. Unlike UE5, UzEngine has no fallback.

## Solution: Implement UE5-Style Feature Level Fallback

### Step 1: Replace Hard-Coded Feature Level with Query-Based Selection

**File**: `runtime/engine/rhi/dx12/dx12_device.h`

**BEFORE** (current UzEngine):
```cpp
// Somewhere in device creation
D3D_FEATURE_LEVEL requestedLevel = D3D_FEATURE_LEVEL_12_2;
HRESULT hr = D3D12CreateDevice(adapter, requestedLevel, IID_PPV_ARGS(&device));
if (FAILED(hr)) {
    // Device creation failed → black screen
    return nullptr;
}
```

**AFTER** (UE5 pattern):
```cpp
// Define request list with optional Agility SDK features
const D3D_FEATURE_LEVEL requestedLevels[] = {
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
    D3D_FEATURE_LEVEL_12_2,  // Requires Agility SDK (Windows 11 or Windows 10 + SDK)
#endif
    D3D_FEATURE_LEVEL_12_1,   // Fallback (always available on Windows 10+)
    D3D_FEATURE_LEVEL_12_0,
};
const uint32 levelCount = std::size(requestedLevels);

// Query adapter for highest supported level
D3D12_FEATURE_DATA_FEATURE_LEVELS caps{};
caps.pFeatureLevelsRequested = requestedLevels;
caps.NumFeatureLevels = levelCount;

// Try to create device with requested levels
HRESULT hr = D3D12CreateDevice(adapter, requestedLevels[0], IID_PPV_ARGS(&device));

// If feature level 12.2 failed, D3D12CreateDevice will fail with E_UNSUPPORTED_FEATURE_LEVEL
// Retry with 12.1
if (FAILED(hr) && levelCount > 1) {
    for (uint32 i = 1; i < levelCount; ++i) {
        hr = D3D12CreateDevice(adapter, requestedLevels[i], IID_PPV_ARGS(&device));
        if (SUCCEEDED(hr)) {
            UZ_LOG(Render, Log, "Device created with Feature Level %d (fallback from 12_2)", requestedLevels[i]);
            break;
        }
    }
}

if (FAILED(hr)) {
    UZ_LOG(Render, Error, "D3D12CreateDevice failed: 0x%08X", static_cast<uint32>(hr));
    return nullptr;
}
```

### Step 2: Query Device Capabilities After Creation

**File**: `runtime/engine/rhi/dx12/dx12_device.cpp` (in device initialization)

After successful device creation, query the actual capabilities:

```cpp
// Query supported feature level
D3D_FEATURE_LEVEL supportedLevel;
ID3D12Device* device = /* created above */;

D3D12_FEATURE_DATA_FEATURE_LEVELS caps{};
caps.pFeatureLevelsRequested = requestedLevels;
caps.NumFeatureLevels = levelCount;

if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(caps)))) {
    supportedLevel = caps.MaxSupportedFeatureLevel;
    UZ_LOG(Render, Log, "Device supports Feature Level %d (max requested: %d)",
           supportedLevel, requestedLevels[0]);
} else {
    supportedLevel = requestedLevels[0];  // Assume what was requested
}

// Store for later feature queries
device_info.supported_feature_level = supportedLevel;
device_info.supports_agility_sdk = (supportedLevel >= D3D_FEATURE_LEVEL_12_2);
```

### Step 3: Gate Agility SDK Features in Shaders & Rendering

**File**: `runtime/engine/rhi/dx12/dx12_context.cpp` (or equivalent)

When compiling shaders or setting up rendering features:

```cpp
// Only enable Agility SDK features if device supports them
if (device_info.supports_agility_sdk) {
    // Enable: Mesh shaders, Work graphs, Super-sampling, etc.
    shader_compile_flags |= D3D12_SHADER_COMPILATION_FLAG_ENABLE_EXPERIMENTAL_FEATURES;
} else {
    // Use traditional rendering paths
    // Shader model 6.5 vs 6.7, no mesh shaders, etc.
}
```

### Step 4: Agility SDK Detection (Optional Logging)

**File**: `runtime/engine/rhi/dx12/dx12_device.cpp` (during initialization)

```cpp
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
static bool CheckIfAgilitySDKLoaded() {
    const wchar_t* AgilitySDKDllName = L"D3D12Core.dll";
    HMODULE AgilitySDKDllHandle = ::GetModuleHandle(AgilitySDKDllName);
    return AgilitySDKDllHandle != nullptr;
}

// In device init
if (CheckIfAgilitySDKLoaded()) {
    UZ_LOG(Render, Log, "DirectX Agility SDK runtime found");
} else {
    UZ_LOG(Render, Log, "DirectX Agility SDK runtime not found (will use system D3D12)");
}
#endif
```

## Why This Works

### Windows 11 (Agility SDK Pre-Loaded)
1. OS loads D3D12Core.dll automatically
2. UE5-style request: `[12_2, 12_1, 12_0]`
3. `D3D12CreateDevice(..., 12_2, ...)` succeeds
4. Device uses SDK features

### Windows 10 (No SDK, DLL Hosted)
1. VSCode host (node.exe) doesn't load SDK
2. UE5-style request: `[12_2, 12_1, 12_0]`
3. `D3D12CreateDevice(..., 12_2, ...)` fails
4. UzEngine retries with `12_1` → succeeds
5. Device uses system D3D12 (still modern, full PBR support)

### Windows 10 (Agility SDK Installed)
1. User installed Agility SDK separately
2. D3D12Core.dll in app directory or system path
3. UE5-style request: `[12_2, 12_1, 12_0]`
4. `D3D12CreateDevice(..., 12_2, ...)` succeeds
5. Device uses SDK features

## Build Configuration

In your Zig build.zig or CMake:

```zig
// build.zig
const exe = b.addExecutable("uzengine", ...);

// Define Agility SDK support (compile-time gate)
exe.root_module.addCMacro("UZENGINE_SUPPORT_AGILITY_SDK", "1");

// Or conditionally based on target/config
if (target.os.tag == .windows and (cpu_model != null and cpu_model.isAmdA64())) {
    exe.root_module.addCMacro("UZENGINE_SUPPORT_AGILITY_SDK", "1");
}
```

## Testing Checklist

- [ ] Windows 11 native: Device creates with Feature Level 12.2
- [ ] Windows 10 + DLL hosting (VSCode): Device creates with Feature Level 12.1 (fallback)
- [ ] Windows 10 + Agility SDK installed: Device creates with Feature Level 12.2
- [ ] Device query returns correct supported feature level
- [ ] Render pipeline initializes without black screen
- [ ] Shader compilation doesn't fail due to requested features
- [ ] Log shows correct feature level and SDK detection

## Code Files to Modify

1. **`runtime/engine/rhi/dx12/dx12_device.h`**
   - Add `supported_feature_level` field to device info struct
   - Add `supports_agility_sdk` flag

2. **`runtime/engine/rhi/dx12/dx12_device.cpp`**
   - Implement feature level fallback in device creation
   - Add Agility SDK detection check
   - Query device for supported feature level

3. **`runtime/engine/rhi/dx12/dx12_context.cpp`** (or shader compile code)
   - Gate Agility SDK features (mesh shaders, work graphs, etc.)
   - Adjust shader model targets based on device support

4. **Build configuration** (Zig or CMake)
   - Add `UZENGINE_SUPPORT_AGILITY_SDK` macro

## Expected Outcome

**Before Fix**: Windows 10 + DLL hosting → black screen (device creation fails)

**After Fix**: Windows 10 + DLL hosting → rendered scene (device creates with fallback feature level)

The engine will gracefully degrade to system D3D12 capabilities instead of hard-failing on Agility SDK-specific requests.

## Non-Solutions (Why Other Approaches Don't Work)

### ❌ Static D3D12SDKVersion/D3D12SDKPath Exports

These are for **Windows D3D12 consumers** to tell their own provider (like a game engine) what SDK version to use. UzEngine is a **consumer** of D3D12, not a provider.

### ❌ ID3D12DeviceFactory Runtime API

This API is for applications that want to **dynamically select and load different SDK versions**. UzEngine doesn't need this; it adapts to whatever SDK is available.

### ❌ LoadLibrary("D3D12Core.dll")

Explicitly loading the SDK adds complexity, requires SDK distribution, and breaks the natural module loading order. The OS handles this automatically.

## Summary

UE5's approach is proven, simple, and correct:
1. Request multiple feature levels in priority order
2. Retry with lower level if higher fails
3. Query device to confirm capabilities
4. Gate features based on actual support

This makes UzEngine work on:
- Windows 11 (Agility SDK always available)
- Windows 10 + DLL hosting (fall back to system D3D12)
- Windows 10 + Agility SDK (use SDK if available)
- All configurations without code changes
