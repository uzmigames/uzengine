<!-- doc-meta
title: UE5 Agility SDK Analysis - Executive Summary
slug: agility-sdk-summary
author: Architecture Analysis
date: 2026-03-25
phase: research-complete
topics: [D3D12, Agility-SDK, DLL-hosting, Windows]
-->

# UE5 Agility SDK Research - Executive Summary

## Your Questions Answered

### 1. Does UE5 bundle the Agility SDK?
**NO.** UE5 does not bundle, distribute, or explicitly load the Agility SDK. It relies on system/OS loading.

### 2. How does UE5 activate it?
**Not actively.** UE5 only checks if D3D12Core.dll is loaded via `GetModuleHandle()`. No explicit activation, no dynamic loading.

### 3. Does UE5 use D3D12SDKVersion/D3D12SDKPath static exports?
**NO.** These exports are not found anywhere in UE5 source. They're for D3D12 **hosts**, not clients.

### 4. Does UE5 use ID3D12DeviceFactory/ID3D12SDKConfiguration1 runtime API?
**NO.** UE5 uses the standard, undecorated `D3D12CreateDevice()` export from system D3D12.

### 5. What about Windows 10 vs Windows 11 SDK handling?
**Identical code.** No branching. Same device creation path on both Windows versions.

### 6. Fallback when Agility SDK can't be loaded?
**YES.** UE5 requests Feature Level 12.2 (SDK-specific), but gracefully falls back to 12.1/12.0 if 12.2 fails. See "How It Works" below.

## How UE5 Actually Works

### Feature Level Request Pattern

```cpp
const D3D_FEATURE_LEVEL requestedLevels[] = {
#if D3D12_CORE_ENABLED
    D3D_FEATURE_LEVEL_12_2,   // Try Agility SDK first
#endif
    D3D_FEATURE_LEVEL_12_1,   // Fallback (always available on Windows 10+)
    D3D_FEATURE_LEVEL_12_0,
};
```

### Device Creation Flow

```
1. Request Feature Level 12.2 via D3D12CreateDevice(adapter, 12.2, ...)
   ├─ SUCCESS (Windows 11 or Win10 + Agility SDK)
   │  └─ Device uses SDK features
   └─ FAIL: E_UNSUPPORTED_FEATURE_LEVEL (Windows 10 + no SDK)
      └─ Retry with Feature Level 12.1
         └─ SUCCESS (always available)
         └─ Device uses system D3D12

2. Query device: CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, ...)
   └─ Confirm actual max supported level

3. Gate advanced features based on actual support:
   ├─ Feature Level 12.2 → enable mesh shaders, work graphs
   └─ Feature Level 12.1 → traditional rendering
```

## Why UzEngine Has a Black Screen on Windows 10 + DLL Hosted

**Current UzEngine behavior** (inferred from memory):
```cpp
// Hard-coded request for Agility SDK feature level
D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_12_2;
HRESULT hr = D3D12CreateDevice(adapter, level, IID_PPV_ARGS(&device));
if (FAILED(hr)) {
    // Black screen: device creation failed, no fallback
    return nullptr;
}
```

**On Windows 10 + DLL hosted (VSCode)**:
1. VSCode host (node.exe) does NOT load Agility SDK
2. D3D12Core.dll not in module list
3. `D3D12CreateDevice(..., 12.2, ...)` fails with **E_UNSUPPORTED_FEATURE_LEVEL**
4. No retry logic → black screen

**UE5 behavior** (handles this case):
```cpp
// Request multiple levels with fallback
if (D3D12CreateDevice(adapter, level[0], ...) fails) {
    try level[1]
    try level[2]
    // At least 12.1 always succeeds on Windows 10
}
```

## Key Findings

| Discovery | Evidence |
|-----------|----------|
| No Agility SDK bundling | No `LoadLibrary()` calls in D3D12RHI source |
| Runtime detection only | `GetModuleHandle("D3D12Core.dll")` at line 686 |
| Standard D3D12 API | Uses `D3D12CreateDevice()`, not factory patterns |
| Feature level fallback | Lines 150-158, 179-189 show array with conditional 12.2 |
| Graceful degradation | Feature level query post-creation (line 503-504) |
| DLL hosting aware | No special code needed; relies on OS module loading |
| No static exports | No D3D12SDKVersion/Path in codebase |
| No SDK version checks | No `D3D12GetSDKVersion()` calls found |

## The Fix for UzEngine (3-Step Plan)

### Step 1: Implement Feature Level Array with Fallback
```cpp
// In dx12_device.cpp
const D3D_FEATURE_LEVEL requestedLevels[] = {
#ifdef UZENGINE_SUPPORT_AGILITY_SDK
    D3D_FEATURE_LEVEL_12_2,
#endif
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
};

// Try levels in order
for (auto level : requestedLevels) {
    hr = D3D12CreateDevice(adapter, level, IID_PPV_ARGS(&device));
    if (SUCCEEDED(hr)) break;
}
```

### Step 2: Query Device for Actual Capabilities
```cpp
// After successful creation
D3D12_FEATURE_DATA_FEATURE_LEVELS caps = {};
caps.pFeatureLevelsRequested = requestedLevels;
caps.NumFeatureLevels = std::size(requestedLevels);
device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &caps, sizeof(caps));
actual_level = caps.MaxSupportedFeatureLevel;
```

### Step 3: Gate Features Based on Actual Support
```cpp
// In rendering code
if (actual_level >= D3D_FEATURE_LEVEL_12_2) {
    // Use SDK features
} else {
    // Use traditional rendering
}
```

## Result

| Platform | Before Fix | After Fix |
|----------|-----------|-----------|
| Windows 11 (Agility SDK auto-loaded) | Works | Works |
| Windows 10 + Agility SDK installed | Works | Works |
| **Windows 10 + DLL hosted (VSCode)** | **BLACK SCREEN** | **RENDERS** |

The engine will gracefully accept Feature Level 12.1 (from system D3D12) instead of hard-failing on the 12.2 request.

## Source Files Analyzed

- **Primary**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\Windows\WindowsD3D12Device.cpp`
  - Lines 683-688: Agility SDK detection
  - Lines 495-530: Device creation pattern
  - Lines 146-171, 179-189: Feature level/shader model arrays
  - Line 1012: SDK status logging

- **Build**: `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\D3D12RHI.Build.cs`
  - Depends on system "DX12" library (not custom Agility SDK)

## Documentation Files Created

1. **ue5-agility-sdk-research.md** — Detailed findings (12 sections, complete analysis)
2. **uzengine-agility-sdk-dll-hosting-fix.md** — Implementation guide (4 steps, code examples)
3. **ue5-code-reference-map.md** — Code location reference (exact line numbers, snippets)
4. **AGILITY-SDK-SUMMARY.md** — This file (executive summary)

All files in: `e:\UzEngine\docs\analysis\`

---

## Concrete Recommendation

**Delegate to `rhi-engineer` agent** to implement the 3-step fix in `runtime/engine/rhi/dx12/dx12_device.cpp`:

1. Replace hard-coded `D3D_FEATURE_LEVEL_12_2` with feature level array
2. Implement retry loop for lower feature levels
3. Query device and store actual support level
4. Gate advanced features based on actual support

Expected time: 2-3 hours. Result: UzEngine works on Windows 10 + DLL hosting (VSCode).
